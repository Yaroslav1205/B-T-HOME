#pragma once
#include <cstddef>
#include <cstdint>
namespace bth::ota {
inline constexpr const char* layout = "c6-4m-ota-v1";
inline constexpr std::size_t slot_size = 0x1e0000;
struct Release {
    char version[32]{};
    char file[96]{};
    char sha256[65]{};
    std::size_t size = 0;
};
bool newer(const char* candidate, const char* current) noexcept;
bool valid(const Release& release) noexcept;
enum class Result { ok, cancelled, download, image, write, checksum, activate };
const char* name(Result result) noexcept;
class Source {
public:
    virtual ~Source() = default;
    virtual int read(void* buffer, std::size_t capacity) noexcept = 0;
};
class Target {
public:
    virtual ~Target() = default;
    virtual bool begin(std::size_t size) noexcept = 0;
    virtual bool write(const void* bytes, std::size_t size) noexcept = 0;
    virtual bool checksum(const char* expected) noexcept = 0;
    virtual bool finish() noexcept = 0;
    virtual bool activate() noexcept = 0;
    virtual void abort() noexcept = 0;
};
class Observer {
public:
    virtual ~Observer() = default;
    virtual bool cancelled() const noexcept = 0;
    virtual void progress(std::size_t bytes) noexcept = 0;
};
// Blocking transaction for a dedicated worker, never Runtime.tick().
Result install(const Release&, Source&, Target&, Observer&) noexcept;
}  // namespace bth::ota
