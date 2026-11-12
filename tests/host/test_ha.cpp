// Host tests for the MQTT topics, Home Assistant discovery and commands.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "doser/alarms.h"
#include "doser/ha_core.h"
#include "lgal/json_reader.h"

using namespace doser;
using namespace doser::ha;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static std::string str(const lgal::JsonValue& v) {
    char b[256] = "";
    return v.valid() && v.type() == lgal::JsonType::String && v.get(b, sizeof(b)) ? b : "<none>";
}

static DeviceInfo device() {
    DeviceInfo d;
    d.id = "lgal_doser_eeff";
    d.name = "LGAL Doser eeff";
    d.firmware = "1.2.3";
    d.url = "http://lgal-doser-eeff.local/";
    d.base = "lgal-doser-eeff";
    d.pump_names[1] = "Kalium";
    return d;
}

struct Doc {
    std::string text;
    lgal::JsonReader r;
    std::vector<lgal::JsonToken> tok = std::vector<lgal::JsonToken>(4096);
    bool parse() { return r.parse(text.data(), text.size(), tok.data(), int(tok.size())); }
};

static void test_discovery() {
    std::printf("HA: discovery\n");
    CHECK(discovery_topic("homeassistant", "lgal_doser_eeff") == "homeassistant/device/lgal_doser_eeff/config");
    DeviceInfo d = device();
    Doc doc;
    {
        lgal::Json j;
        write_discovery(j, d);
        doc.text = j.c_str();
    }
    CHECK(doc.parse());
    const auto root = doc.r.root();
    CHECK(str(root["device"]["identifiers"][0]) == "lgal_doser_eeff");
    CHECK(str(root["device"]["name"]) == "LGAL Doser eeff");
    CHECK(str(root["device"]["sw_version"]) == "1.2.3");
    CHECK(str(root["device"]["configuration_url"]) == "http://lgal-doser-eeff.local/");
    CHECK(str(root["origin"]["name"]) == "LGAL Doser");
    CHECK(str(root["availability_topic"]) == "lgal-doser-eeff/status");
    const auto c = root["components"];
    CHECK(c.type() == lgal::JsonType::Object);
    CHECK(c.size() == 5 * 10 + 4);
    // every full component has a unique id equal to its key
    int full = 0;
    for (int i = 0; i < c.size(); ++i) {
        const std::string key = str(c.key_at(i));
        const auto v = c.value_at(i);
        CHECK(key.rfind("lgal_doser_eeff_", 0) == 0);
        if (v.size() > 1) {
            ++full;
            CHECK(str(v["unique_id"]) == key);
            CHECK(str(v["name"]) != "<none>");
        } else {
            CHECK(v.size() == 1 && str(v["platform"]) != "<none>");  // removal stub
        }
    }
    // control off: schedule/amount/dose per pump and stop are stubs;
    // no update source: the firmware entity is a stub
    CHECK(full == 5 * 7 + 2);
    const auto total = c["lgal_doser_eeff_p1_total"];
    CHECK(str(total["platform"]) == "sensor");
    CHECK(str(total["state_topic"]) == "lgal-doser-eeff/pump/1/state");
    CHECK(str(total["state_class"]) == "total_increasing");
    CHECK(str(total["name"]) == "Pump 1 Total dosed");
    CHECK(str(c["lgal_doser_eeff_p2_total"]["name"]) == "Kalium Total dosed");
    CHECK(str(c["lgal_doser_eeff_p4_tube"]["unit_of_measurement"]) == "h");
    CHECK(str(c["lgal_doser_eeff_p4_tube"]["entity_category"]) == "diagnostic");
    CHECK(str(c["lgal_doser_eeff_p4_tube"]["name"]) == "Pump 4 Tube hours");
    CHECK(c["lgal_doser_eeff_p1_dose"].size() == 1 && str(c["lgal_doser_eeff_p1_dose"]["platform"]) == "button");
    CHECK(str(c["lgal_doser_eeff_critical"]["json_attributes_topic"]) == "lgal-doser-eeff/state");
    CHECK(c["lgal_doser_eeff_firmware"].size() == 1 && str(c["lgal_doser_eeff_firmware"]["platform"]) == "update");

    // update source without control: the entity reports, but cannot install
    d.update_source = true;
    Doc up;
    {
        lgal::Json j;
        write_discovery(j, d);
        up.text = j.c_str();
    }
    CHECK(up.parse());
    const auto fw = up.r.root()["components"]["lgal_doser_eeff_firmware"];
    CHECK(str(fw["platform"]) == "update" && str(fw["device_class"]) == "firmware");
    CHECK(str(fw["state_topic"]) == "lgal-doser-eeff/update" && str(fw["name"]) == "Firmware");
    CHECK(!fw["command_topic"].valid());

    // control on, German
    d.control = true;
    d.german = true;
    d.url.clear();
    Doc de;
    {
        lgal::Json j;
        write_discovery(j, d);
        de.text = j.c_str();
    }
    CHECK(de.parse());
    const auto r2 = de.r.root();
    CHECK(!r2["device"]["configuration_url"].valid());
    const auto c2 = r2["components"];
    CHECK(str(c2["lgal_doser_eeff_p5_dose"]["command_topic"]) == "lgal-doser-eeff/pump/5/dose/set");
    CHECK(str(c2["lgal_doser_eeff_p5_dose"]["payload_press"]) == "PRESS");
    CHECK(str(c2["lgal_doser_eeff_p4_schedule"]["command_topic"]) == "lgal-doser-eeff/pump/4/schedule/set");
    CHECK(str(c2["lgal_doser_eeff_p4_amount"]["command_topic"]) == "lgal-doser-eeff/pump/4/amount/set");
    CHECK(str(c2["lgal_doser_eeff_stop"]["command_topic"]) == "lgal-doser-eeff/stop/set");
    CHECK(str(c2["lgal_doser_eeff_p1_next"]["name"]) == "Pumpe 1 Nächste Dosierung");
    CHECK(str(c2["lgal_doser_eeff_stop"]["name"]) == "Alle stoppen");
    CHECK(str(c2["lgal_doser_eeff_firmware"]["command_topic"]) == "lgal-doser-eeff/update/set");
    CHECK(str(c2["lgal_doser_eeff_firmware"]["payload_install"]) == "install");
    int stubs = 0;
    for (int i = 0; i < c2.size(); ++i) stubs += c2.value_at(i).size() == 1;
    CHECK(stubs == 0);

    // pump 3 not fitted: all its entities become removal stubs
    d.unfitted[2] = true;
    Doc nf;
    {
        lgal::Json j;
        write_discovery(j, d);
        nf.text = j.c_str();
    }
    CHECK(nf.parse());
    const auto c3 = nf.r.root()["components"];
    CHECK(c3.size() == 5 * 10 + 4);
    CHECK(c3["lgal_doser_eeff_p3_total"].size() == 1 && str(c3["lgal_doser_eeff_p3_total"]["platform"]) == "sensor");
    CHECK(c3["lgal_doser_eeff_p3_tube"].size() == 1 && c3["lgal_doser_eeff_p3_dose"].size() == 1);
    CHECK(c3["lgal_doser_eeff_p4_total"].size() > 1);
    stubs = 0;
    for (int i = 0; i < c3.size(); ++i) stubs += c3.value_at(i).size() == 1;
    CHECK(stubs == 10);  // pump 3: all 10
}

static void test_states() {
    std::printf("HA: states\n");
    char iso[26];
    fmt_iso_utc(0, iso);
    CHECK(std::strcmp(iso, "1970-01-01T00:00:00+00:00") == 0);
    fmt_iso_utc(1794034800, iso);  // 2026-11-07 07:00 UTC
    CHECK(std::strcmp(iso, "2026-11-07T07:00:00+00:00") == 0);

    PumpState s;
    s.activity = "dosing";
    s.total_ul = 123456;
    s.planned_ul = 5000;
    s.tracked = true;
    s.remaining_ul = 480260;
    s.days = 12.34f;
    s.has_next = true;
    s.next_utc = 1794034800;
    s.automatic = true;
    s.amount_ul = 2500;
    s.tube_h = 12.34f;
    s.last = "success";
    lgal::Json j;
    write_pump_state(j, s);
    CHECK(std::string(j.c_str()) ==
          R"({"activity":"dosing","total_ml":123.456,"planned_ml":5.000,"remaining_ml":480.3,"days":12.3,)"
          R"("next":"2026-11-07T07:00:00+00:00","schedule":"ON","amount_ml":2.500,"tube_h":12.3,"last":"success"})");
    PumpState e;
    lgal::Json j2;
    write_pump_state(j2, e);
    CHECK(std::string(j2.c_str()) ==
          R"({"activity":"idle","total_ml":0.000,"planned_ml":0.000,"remaining_ml":null,"days":null,"next":null,)"
          R"("schedule":"OFF","amount_ml":0.000,"tube_h":0.0,"last":null})");

    DeviceState ds;
    ds.critical = true;
    ds.conditions = uint16_t((1u << alarms::kRtcBit) | alarms::cond_bit(0, alarms::Cond::ContainerLow) |
                             alarms::cond_bit(4, alarms::Cond::TubeWorn));
    lgal::Json j3;
    write_device_state(j3, ds);
    CHECK(std::string(j3.c_str()) ==
          R"({"critical":true,"warning":false,"conditions":["rtc_fault","pump1_container_low","pump5_tube_worn"]})");

    UpdateState u;
    u.installed = "0.1.0";
    u.latest = "0.0.9";  // older release: not offered
    lgal::Json j4;
    write_update_state(j4, u);
    CHECK(std::string(j4.c_str()) ==
          R"({"installed_version":"0.1.0","latest_version":"0.1.0","in_progress":false,"update_percentage":null})");
    u.latest = "0.2.0";
    u.available = true;
    u.notes = "https://example.com/v0.2.0";
    u.in_progress = true;
    u.percent = 42;
    lgal::Json j5;
    write_update_state(j5, u);
    CHECK(std::string(j5.c_str()) == R"({"installed_version":"0.1.0","latest_version":"0.2.0",)"
                                     R"("release_url":"https://example.com/v0.2.0","in_progress":true,"update_percentage":42})");
}

static bool cmd(const char* topic, const char* payload, Command* c) {
    return parse_command("aq/doser", topic, int(std::strlen(topic)), payload, int(std::strlen(payload)), c);
}

static void test_commands() {
    std::printf("HA: commands\n");
    uint32_t ul = 0;
    CHECK(parse_ml("2.5", 3, &ul) && ul == 2500);
    CHECK(parse_ml("2,5", 3, &ul) && ul == 2500);
    CHECK(parse_ml(" 10 ", 4, &ul) && ul == 10000);
    CHECK(parse_ml("0.001", 5, &ul) && ul == 1);
    CHECK(parse_ml("1000", 4, &ul) && ul == 1000000);
    CHECK(parse_ml(".5", 2, &ul) && ul == 500);
    const char* bad[] = {"", "0", "0.000", "-1", "1.0001", "1.2.3", "abc", "1e3", "1000.001", "99999999999999999", "."};
    for (const char* b : bad) {
        ul = 77;
        CHECK(!parse_ml(b, int(std::strlen(b)), &ul) && ul == 77);
    }

    Command c;
    CHECK(cmd("aq/doser/pump/1/dose/set", "PRESS", &c) && c.cmd == Cmd::Dose && c.pump == 0 && c.press);
    CHECK(cmd("aq/doser/pump/5/dose/set", "1.25", &c) && c.cmd == Cmd::Dose && c.pump == 4 && !c.press &&
          c.ul == 1250);
    CHECK(cmd("aq/doser/pump/2/amount/set", "3", &c) && c.cmd == Cmd::Amount && c.pump == 1 && c.ul == 3000);
    CHECK(cmd("aq/doser/pump/3/schedule/set", "ON", &c) && c.cmd == Cmd::Schedule && c.pump == 2 && c.on);
    CHECK(cmd("aq/doser/pump/3/schedule/set", "OFF", &c) && c.cmd == Cmd::Schedule && !c.on);
    CHECK(cmd("aq/doser/stop/set", "", &c) && c.cmd == Cmd::Stop);
    CHECK(cmd("aq/doser/stop/set", "PRESS", &c) && c.cmd == Cmd::Stop);
    CHECK(cmd("aq/doser/update/set", "install", &c) && c.cmd == Cmd::Install);
    CHECK(!cmd("aq/doser/update/set", "https://example.com/x.bin", &c));
    CHECK(!cmd("aq/doser/update/set", "", &c));
    const char* bad_topics[] = {"aq/doser",
                                "aq/doser/",
                                "aq/doserx/stop/set",
                                "aq/dose/stop/set",
                                "other/stop/set",
                                "aq/doser/pump/0/dose/set",
                                "aq/doser/pump/6/dose/set",
                                "aq/doser/pump/12/dose/set",
                                "aq/doser/pump/1/dose",
                                "aq/doser/pump/1/dose/set/x",
                                "aq/doser/pump/1/state",
                                "aq/doser/pump/1",
                                "aq/doser/stop/setx"};
    for (const char* t : bad_topics) CHECK(!cmd(t, "PRESS", &c));
    CHECK(!cmd("aq/doser/pump/1/dose/set", "press", &c));
    CHECK(!cmd("aq/doser/pump/1/dose/set", "0", &c));
    CHECK(!cmd("aq/doser/pump/1/amount/set", "PRESS", &c));
    CHECK(!cmd("aq/doser/pump/1/schedule/set", "on", &c));
    CHECK(!parse_command("", "x/stop/set", 10, "", 0, &c));
    // payload not terminated
    const char buf[] = "2.5XYZ";
    CHECK(parse_command("aq/doser", "aq/doser/pump/1/dose/setXX", 24, buf, 3, &c) && c.ul == 2500);
}

int main() {
    test_discovery();
    test_states();
    test_commands();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
