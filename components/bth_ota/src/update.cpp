#include "bth/ota/update.hpp"
#include <cstdio>
#include <cstring>
namespace bth::ota {
namespace {
bool version(const char* text, unsigned (&parts)[3]) noexcept {
    if (!text) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (*text < '0' || *text > '9') return false;
        const bool zero = *text == '0';
        unsigned digits = 0;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 6 || (zero && digits > 1)) return false;
            parts[i] = parts[i] * 10 + static_cast<unsigned>(*text++ - '0');
        }
        if (i < 2 && *text++ != '.') return false;
    }
    return *text == 0;
}
}
bool newer(const char* candidate, const char* current) noexcept {
    unsigned a[3]{}, b[3]{};
    if (!version(candidate, a) || !version(current, b)) return false;
    for (unsigned i = 0; i < 3; ++i) if (a[i] != b[i]) return a[i] > b[i];
    return false;
}
bool valid(const Release& r) noexcept {
    if (!std::memchr(r.version, 0, sizeof(r.version)) ||
        !std::memchr(r.file, 0, sizeof(r.file)) || r.sha256[64] != 0) return false;
    unsigned parts[3]{};
    if (!version(r.version, parts) || r.size < 288 || r.size > slot_size) return false;
    char expected[96]{};
    std::snprintf(expected, sizeof(expected), "bt-home-esp32c6_rgb8-%s.bin", r.version);
    if (std::strcmp(r.file, expected)) return false;
    for (unsigned i = 0; i < 64; ++i)
        if (!((r.sha256[i] >= '0' && r.sha256[i] <= '9') || (r.sha256[i] >= 'a' && r.sha256[i] <= 'f'))) return false;
    return true;
}
const char* name(Result r) noexcept {
    switch (r) {
        case Result::ok: return "ok";
        case Result::cancelled: return "cancelled";
        case Result::download: return "download_failed";
        case Result::image: return "invalid_image";
        case Result::write: return "flash_failed";
        case Result::checksum: return "checksum_failed";
        case Result::activate: return "activation_failed";
    }
    return "unknown";
}
Result install(const Release& r, Source& source, Target& target, Observer& observer) noexcept {
    if (!valid(r)) return Result::image;
    unsigned char buffer[2048]{};
    std::size_t received = 0;
    while (received < 288) {
        if (observer.cancelled()) return Result::cancelled;
        const int n = source.read(buffer + received, 288 - received);
        if (n <= 0 || static_cast<std::size_t>(n) > 288 - received) return Result::download;
        received += static_cast<std::size_t>(n);
    }
    if (buffer[0] != 0xe9 || buffer[12] != 13 || buffer[13] != 0 ||
        buffer[32] != 0x32 || buffer[33] != 0x54 || buffer[34] != 0xcd || buffer[35] != 0xab ||
        !std::memchr(buffer + 48, 0, 32) || !std::memchr(buffer + 80, 0, 32) ||
        std::strcmp(reinterpret_cast<char*>(buffer + 48), r.version) ||
        std::strcmp(reinterpret_cast<char*>(buffer + 80), "bt_home")) return Result::image;
    if (observer.cancelled()) return Result::cancelled;
    if (!target.begin(r.size)) { target.abort(); return Result::write; }
    auto fail = [&target](Result result) { target.abort(); return result; };
    if (!target.write(buffer, received)) return fail(Result::write);
    observer.progress(received);
    while (received < r.size) {
        if (observer.cancelled()) return fail(Result::cancelled);
        const auto remaining = r.size - received;
        const auto capacity = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const int n = source.read(buffer, capacity);
        if (n <= 0 || static_cast<std::size_t>(n) > capacity) return fail(Result::download);
        if (!target.write(buffer, static_cast<std::size_t>(n))) return fail(Result::write);
        received += static_cast<std::size_t>(n);
        observer.progress(received);
    }
    if (observer.cancelled()) return fail(Result::cancelled);
    if (source.read(buffer, 1) != 0) return fail(Result::download);
    if (!target.checksum(r.sha256)) return fail(Result::checksum);
    if (!target.finish()) return fail(Result::image);
    if (observer.cancelled()) return fail(Result::cancelled);
    if (!target.activate()) return fail(Result::activate);
    return Result::ok;
}
}  // namespace bth::ota
