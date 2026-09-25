#pragma once

#include <cstddef>
#include <cstdint>

namespace bth {

enum class Status {
    ok,
    invalid_argument,
    invalid_state,
    capacity_exceeded,
    duplicate_module,
    missing_dependency,
    dependency_order,
    hardware_error,
    no_memory,
    internal_error,
};

const char* status_name(Status status) noexcept;

// The array and its strings must outlive the Runtime. Dependencies are module IDs.
struct Dependencies {
    const char* const* ids = nullptr;
    std::size_t count = 0;
};

class Module {
public:
    virtual ~Module() = default;
    // Nonempty, immutable, stable for the entire lifetime of the module.
    virtual const char* id() const noexcept = 0;
    virtual Dependencies dependencies() const noexcept { return {}; }

    virtual Status init() noexcept = 0;
    virtual Status start() noexcept = 0;
    // Called from one owner task. Must not block or perform long I/O.
    virtual Status tick(std::uint64_t now_ms) noexcept = 0;
    // Must also clean up partially failed start/init; must be idempotent.
    virtual void stop() noexcept = 0;
    virtual void deinit() noexcept = 0;
};

}  // namespace bth
