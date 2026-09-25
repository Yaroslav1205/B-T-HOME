#pragma once

#include <array>
#include "bth/core/module.hpp"

namespace bth {

enum class Phase { none, registration, validation, init, start, tick };
enum class RuntimeState { configuring, stopped, running, failed };

const char* phase_name(Phase phase) noexcept;

struct Failure {
    Status status = Status::ok;
    Phase phase = Phase::none;
    const char* module = nullptr;
    const char* dependency = nullptr;
};

// Non-owning, fixed capacity, single-task lifecycle coordinator. No heap allocation.
// Modules must outlive this object. Calls must not re-enter the Runtime.
class Runtime final {
public:
    static constexpr std::size_t max_modules = 16;

    Runtime() = default;
    ~Runtime() { stop(); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    Status add(Module& module) noexcept;
    Status start() noexcept;
    Status tick(std::uint64_t now_ms) noexcept;
    void stop() noexcept;

    RuntimeState state() const noexcept { return state_; }
    std::size_t size() const noexcept { return count_; }
    const Failure& failure() const noexcept { return failure_; }

private:
    Status validate() noexcept;
    Status fail(Status status, Phase phase, const char* module,
                const char* dependency = nullptr) noexcept;
    void rollback() noexcept;

    std::array<Module*, max_modules> modules_{};
    std::size_t count_ = 0;
    std::size_t initialized_ = 0;
    std::size_t started_ = 0;
    RuntimeState state_ = RuntimeState::configuring;
    Failure failure_{};
};

}  // namespace bth
