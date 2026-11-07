// Host tests for the event history store (day files in a temp directory).
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "doser/history_core.h"

using namespace doser::history;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static const uint32_t kDay0 = 20400 * 86400u;  // some day in 2025

static Event ev(uint32_t utc, Kind k, uint8_t pump, uint32_t done = 0) {
    Event e;
    e.utc = utc;
    e.kind = k;
    e.pump = pump;
    e.source = Source::Schedule;
    e.code = code_of("success");
    e.target_ul = done;
    e.done_ul = done;
    return e;
}

static std::vector<Event> all(const Store& s, const Filter& f = {}, size_t limit = 1000) {
    struct Ctx { std::vector<Event> v; size_t limit; } c{{}, limit};
    s.query(f, [](const Event& e, void* p) {
        auto* c = static_cast<Ctx*>(p);
        c->v.push_back(e);
        return c->v.size() < c->limit;
    }, &c);
    return c.v;
}

static std::string mkdir_tmp() {
    const char* base = std::getenv("TMPDIR");
    std::string t = std::string(base && *base ? base : "/tmp") + "/lgal_hist_XXXXXX";
    std::vector<char> buf(t.begin(), t.end());
    buf.push_back(0);
    return mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}

static void rm_dir(const std::string& d) {
    std::string cmd = "rm -rf '" + d + "'";
    if (std::system(cmd.c_str()) != 0) std::printf("  (could not remove %s)\n", d.c_str());
}

static void test_codec() {
    std::printf("Record codec\n");
    Event e = ev(kDay0 + 123, Kind::Calibration, 4, 7000);
    e.source = Source::Api;
    e.code = code_of("dose_too_small");
    e.target_ul = 0xA1B2C3D4u;
    e.value = -123456;
    uint8_t r[kRecordSize];
    encode(e, r);
    Event d;
    CHECK(decode(r, &d));
    CHECK(d.utc == e.utc && d.kind == e.kind && d.pump == 4 && d.source == Source::Api);
    CHECK(d.code == e.code && d.target_ul == 0xA1B2C3D4u && d.done_ul == 7000 && d.value == -123456);
    r[9] ^= 0x10;
    CHECK(!decode(r, &d));
    uint8_t zero[kRecordSize] = {};
    CHECK(!decode(zero, &d));  // padding from a torn write

    CHECK(std::strcmp(code_str(code_of("container_low")), "container_low") == 0);
    CHECK(code_of("nonsense") == 0 && std::strcmp(code_str(0), "other") == 0);
    CHECK(std::strcmp(code_str(250), "other") == 0);
    Kind k;
    CHECK(parse_kind("calib_run", &k) && k == Kind::CalibRun);
    CHECK(!parse_kind("doses", &k) && !parse_kind(nullptr, &k));
    for (uint8_t i = 1; i <= kKindMax; ++i) {
        CHECK(parse_kind(kind_str(static_cast<Kind>(i)), &k) && static_cast<uint8_t>(k) == i);
    }
    CHECK(std::strcmp(source_str(Source::Mqtt), "mqtt") == 0);
}

static void test_store() {
    std::printf("Store\n");
    // the store keeps short paths (device: /storage/hist), so work relative
    const std::string tmp = mkdir_tmp();
    CHECK(!tmp.empty() && chdir(tmp.c_str()) == 0 && mkdir("h", 0755) == 0);
    const std::string dir = "h";
    Store s(dir.c_str());
    CHECK(all(s).empty());

    // three days, pumps 1..3
    for (int d = 0; d < 3; ++d)
        for (int i = 0; i < 4; ++i) CHECK(s.append(ev(kDay0 + d * 86400 + i * 3600, Kind::Dose, uint8_t(1 + i % 3), 1000 + i)));
    CHECK(s.append(ev(kDay0 + 2 * 86400 + 5 * 3600, Kind::Skip, 2)));

    auto v = all(s);
    CHECK(v.size() == 13);
    bool sorted = true;
    for (size_t i = 1; i < v.size(); ++i) sorted = sorted && v[i - 1].utc >= v[i].utc;
    CHECK(sorted);
    CHECK(!v.empty() && v.front().kind == Kind::Skip);

    Filter f;
    f.pump = 2;
    v = all(s, f);
    CHECK(v.size() == 4);
    f.kinds = 1u << uint8_t(Kind::Skip);
    CHECK(all(s, f).size() == 1);

    f = {};
    f.from = kDay0 + 86400;
    f.to = kDay0 + 86400 + 2 * 3600;
    v = all(s, f);
    CHECK(v.size() == 3 && v.front().utc == kDay0 + 86400 + 2 * 3600 && v.back().utc == kDay0 + 86400);

    v = all(s, {}, 2);  // stop early
    CHECK(v.size() == 2);

    int files = 0;
    uint64_t bytes = 0;
    s.usage(&files, &bytes);
    CHECK(files == 3 && bytes == 13 * kRecordSize);

    // torn record: partial bytes at the end are skipped and the next append realigns
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%d.ev", dir.c_str(), int(kDay0 / 86400 + 2));
    FILE* fp = std::fopen(path, "ab");
    CHECK(fp && std::fwrite("\x01\x02\x03\x04\x05", 1, 5, fp) == 5);
    if (fp) std::fclose(fp);
    CHECK(all(s).size() == 13);
    CHECK(s.append(ev(kDay0 + 2 * 86400 + 6 * 3600, Kind::Container, 5)));
    v = all(s);
    CHECK(v.size() == 14 && v.front().kind == Kind::Container && v.front().pump == 5);

    // foreign files are ignored
    std::snprintf(path, sizeof(path), "%s/notes.txt", dir.c_str());
    fp = std::fopen(path, "w");
    if (fp) std::fclose(fp);
    s.usage(&files, &bytes);
    CHECK(files == 3);

    // prune keeps today and keep_days before
    CHECK(s.prune(kDay0 + 3 * 86400, 2) == 1);
    v = all(s);
    CHECK(v.size() == 10 && v.back().utc >= kDay0 + 86400);
    CHECK(s.prune(kDay0 + 3 * 86400, 2) == 0);
    CHECK(s.clear() == 2);
    CHECK(all(s).empty());
    CHECK(chdir("/") == 0);
    rm_dir(tmp);

    Store missing("/nonexistent/lgal_hist");
    CHECK(all(missing).empty());
    CHECK(!missing.append(ev(kDay0, Kind::Dose, 1)));
}

static void test_many() {
    std::printf("Many records per day\n");
    const std::string tmp = mkdir_tmp();
    CHECK(!tmp.empty() && chdir(tmp.c_str()) == 0 && mkdir("h", 0755) == 0);
    Store s("h");
    for (uint32_t i = 0; i < 100; ++i) CHECK(s.append(ev(kDay0 + 100 + i * 60, Kind::Dose, 1, i)));
    auto v = all(s);
    bool ok = v.size() == 100;
    for (size_t i = 0; ok && i < v.size(); ++i) ok = v[i].done_ul == 99 - i;
    CHECK(ok);
    v = all(s, {}, 40);  // stop inside the second chunk
    CHECK(v.size() == 40 && v.back().done_ul == 60);
    CHECK(chdir("/") == 0);
    rm_dir(tmp);
}

int main() {
    test_codec();
    test_store();
    test_many();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
