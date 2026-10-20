#include "bth/ota/update.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::cerr << "ota:" << __LINE__ << ": " #c "\n"; std::exit(1); } } while(false)
using namespace bth::ota;
struct Input final : Source {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(5000);
    std::size_t offset = 0, chunk = 13, fail_at = 10000;
    Input() {
        bytes[0] = 0xe9; bytes[12] = 13;
        bytes[32] = 0x32; bytes[33] = 0x54; bytes[34] = 0xcd; bytes[35] = 0xab;
        std::memcpy(bytes.data() + 48, "0.3.0", 6);
        std::memcpy(bytes.data() + 80, "bt_home", 8);
    }
    int read(void* to, std::size_t capacity) noexcept override {
        if (offset >= fail_at) return -1;
        const auto count = std::min({capacity, chunk, bytes.size() - offset});
        std::memcpy(to, bytes.data() + offset, count); offset += count;
        return static_cast<int>(count);
    }
};
struct Output final : Target {
    std::vector<unsigned char> bytes;
    bool began = false, ended = false, activated = false, aborted = false;
    int failure = 0;
    bool begin(std::size_t) noexcept override { began = true; return failure != 1; }
    bool write(const void* data, std::size_t size) noexcept override {
        CHECK(began && !ended); const auto* p = static_cast<const unsigned char*>(data);
        bytes.insert(bytes.end(), p, p + size); return failure != 2;
    }
    bool checksum(const char*) noexcept override { CHECK(began && !ended); return failure != 3; }
    bool finish() noexcept override { ended = true; return failure != 4; }
    bool activate() noexcept override { CHECK(ended); activated = failure != 5; return activated; }
    void abort() noexcept override { aborted = true; }
};
struct Watch final : Observer {
    std::size_t received = 0, cancel_at = 10000;
    bool cancelled() const noexcept override { return received >= cancel_at; }
    void progress(std::size_t n) noexcept override { CHECK(n >= received); received = n; }
};
Release release() {
    Release r{}; std::strcpy(r.version, "0.3.0");
    std::strcpy(r.file, "bt-home-esp32c6_rgb8-0.3.0.bin");
    std::memset(r.sha256, 'a', 64); r.size = 5000; return r;
}
int main() {
    CHECK(newer("0.10.0", "0.9.0")); CHECK(newer("1.0.0", "0.99.99"));
    for (const auto* bad : {"0.2.0", "0.3.0", "0.03.1", "0.4.0-rc1", "1", "1.0", "1.0.0/", "9999999.0.0"}) CHECK(!newer(bad, "0.3.0"));
    CHECK(valid(release()));
    for (const auto n : {0U, 287U, 0x1e0001U}) { auto r = release(); r.size = n; CHECK(!valid(r)); }
    { auto r = release(); r.file[0] = '/'; CHECK(!valid(r)); }
    for (const auto chunk : {1U, 13U, 2048U}) {
        Input input; input.chunk = chunk; Output output; Watch watch;
        CHECK(install(release(), input, output, watch) == Result::ok);
        CHECK(output.activated && !output.aborted && output.bytes == input.bytes && watch.received == 5000);
    }
    for (int failure = 1; failure <= 5; ++failure) {
        Input input; Output output; output.failure = failure; Watch watch;
        CHECK(install(release(), input, output, watch) != Result::ok);
        CHECK(output.aborted && !output.activated);
    }
    for (const auto offset : {0U, 12U, 32U, 48U, 80U}) {
        Input input; input.bytes[offset] = 0; Output output; Watch watch;
        CHECK(install(release(), input, output, watch) == Result::image);
        CHECK(!output.began && !output.activated);
    }
    for (const auto size : {100U, 4999U, 5001U}) {
        Input input; input.bytes.resize(size); Output output; Watch watch;
        CHECK(install(release(), input, output, watch) == Result::download);
        CHECK(!output.activated);
    }
    for (const auto cancel : {0U, 288U, 1000U, 5000U}) {
        Input input; Output output; Watch watch; watch.cancel_at = cancel;
        CHECK(install(release(), input, output, watch) == Result::cancelled);
        CHECK(!output.activated && (!output.began || output.aborted));
    }
    { Input input; input.fail_at = 1000; Output output; Watch watch;
      CHECK(install(release(), input, output, watch) == Result::download); CHECK(output.aborted); }
    std::cout << "OTA transaction tests passed\n";
}
