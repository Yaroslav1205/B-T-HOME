#include "bth/web/request_body.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK_REQUEST(c) do { if (!(c)) { std::cerr << "web request check failed at line " << __LINE__ << '\n'; std::exit(EXIT_FAILURE); } } while (false)

void test_web_request() {
    using namespace std::string_view_literals;
    CHECK_REQUEST(bth::valid_flat_json_body(R"({"username":"admin","password":"admin"})"));
    CHECK_REQUEST(bth::valid_flat_json_body(R"({"ssid":"{}[]","password":"quote\"slash\\end"})"));
    CHECK_REQUEST(bth::valid_flat_json_body(R"({"password":"literal\\u0000","ssid":"\u0410"})"));
    CHECK_REQUEST(bth::valid_flat_json_body(" \r\n{}\t"));
    for (auto body : {R"({"password":"admin\u0000suffix"})"sv,
                      R"({"user\u0000name":"admin"})"sv,
                      R"({"unused":{"nested":true}})"sv,
                      R"({"unused":[true]})"sv, "[]"sv, "}"sv, "{"sv,
                      "{\"password\":\"abc"sv, "{\"password\":\"abc\\"sv,
                      "{\"password\":\"abc\0def\"}"sv}) {
        CHECK_REQUEST(!bth::valid_flat_json_body(body));
    }
    const std::string nested = "{\"unused\":" + std::string(200, '[') + "0" + std::string(200, ']') + "}";
    CHECK_REQUEST(nested.size() < 512 && !bth::valid_flat_json_body(nested));
    bool on = false;
    CHECK_REQUEST(bth::parse_relay_command("true", on) && on);
    CHECK_REQUEST(bth::parse_relay_command("false", on) && !on);
    for (auto body : {"true\0"sv, "false\0"sv, "truex"sv, "True"sv, " true"sv, "null"sv, ""sv}) {
        on = true;
        CHECK_REQUEST(!bth::parse_relay_command(body, on) && on);
    }
}
