// Host tests for the device name and the host name derived from it.
#include <cstdio>
#include <string>

#include "lgal/device_name.h"

using namespace lgal::devname;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static void test_names() {
    CHECK(valid_name(""));
    CHECK(valid_name("Riffbecken"));
    CHECK(valid_name("Becken \xC3\xBC" "ber 2 m"));
    CHECK(valid_name(std::string(kNameMax, 'a').c_str()));
    CHECK(!valid_name(std::string(kNameMax + 1, 'a').c_str()));
    CHECK(!valid_name("   "));
    CHECK(!valid_name("a\tb"));
    CHECK(!valid_name("a\nb"));
    CHECK(!valid_name(nullptr));
}

static void test_hosts() {
    CHECK(valid_host("riffbecken"));
    CHECK(valid_host("a"));
    CHECK(valid_host("doser-2"));
    CHECK(valid_host("2doser"));
    CHECK(valid_host(std::string(kHostMax, 'a').c_str()));
    CHECK(!valid_host(std::string(kHostMax + 1, 'a').c_str()));
    CHECK(!valid_host(""));
    CHECK(!valid_host("-a"));
    CHECK(!valid_host("a-"));
    CHECK(!valid_host("Riff"));
    CHECK(!valid_host("riff.becken"));
    CHECK(!valid_host("riff_becken"));
    CHECK(!valid_host(nullptr));
}

static void test_slug() {
    CHECK(slug("Riffbecken") == "riffbecken");
    CHECK(slug("Riffbecken Wohnzimmer") == "riffbecken-wohnzimmer");
    CHECK(slug("  Doser  #2  ") == "doser-2");
    CHECK(slug("Gr\xC3\xBC" "nes Becken") == "gruenes-becken");
    CHECK(slug("\xC3\x96" "lfilter") == "oelfilter");
    CHECK(slug("Stra\xC3\x9F" "e") == "strasse");
    CHECK(slug("Caf\xC3\xA9 Cr\xC3\xA8me") == "cafe-creme");
    CHECK(slug("\xC3\x84rger") == "aerger");
    CHECK(slug("Reef \xF0\x9F\x90\xA0 Tank") == "reef-tank");  // emoji -> separator
    CHECK(slug("\xF0\x9F\x90\xA0") == "");
    CHECK(slug("---") == "");
    CHECK(slug("") == "");
    CHECK(slug(nullptr) == "");
    // cut to one DNS label, no "-" at the end
    const std::string s = slug("Das ist ein sehr langer Name fuer ein Becken");
    CHECK(s.size() <= kHostMax);
    CHECK(valid_host(s.c_str()));
    CHECK(s == "das-ist-ein-sehr-langer-name-fue");
    CHECK(slug("abcdefghijklmnopqrstuvwxyz12345 x") == "abcdefghijklmnopqrstuvwxyz12345");
    // every slug of a valid name is a valid host name or empty
    for (const char* n : {"A", "a b", "\xC3\x9F", "x\xC3\xA7y", "1", "Z-", "-Z"}) {
        const std::string h = slug(n);
        CHECK(h.empty() || valid_host(h.c_str()));
    }
}

static void test_effective() {
    const std::string def = "lgal-doser-e4f5";
    CHECK(effective("", "", def) == def);
    CHECK(effective(nullptr, nullptr, def) == def);
    CHECK(effective("Riff", "", def) == "riff");
    CHECK(effective("Riff", "becken-1", def) == "becken-1");
    CHECK(effective("\xF0\x9F\x90\xA0", "", def) == def);
}

int main() {
    test_names();
    test_hosts();
    test_slug();
    test_effective();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
