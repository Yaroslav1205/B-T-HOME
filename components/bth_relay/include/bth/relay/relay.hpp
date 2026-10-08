#pragma once
#include "bth/core/module.hpp"

namespace bth {
class RelayOutput {
public:
    virtual ~RelayOutput() = default;
    virtual Status init() noexcept = 0;  // Acquire in the OFF state.
    virtual Status set(bool on) noexcept = 0;
    virtual void deinit() noexcept = 0;  // OFF, including partial init failure.
};
class RelayStore {
public:
    virtual ~RelayStore() = default;
    virtual Status init() noexcept = 0;
    virtual Status load(bool& on) noexcept = 0;  // Missing key means OFF.
    virtual Status save(bool on) noexcept = 0;  // Success means durably committed.
    virtual void deinit() noexcept = 0;
};
struct RelayState {
    bool ready = false;
    bool on = false;
    bool saved_on = false;
    bool output_known = false;
    bool storage_known = false;
    Status error = Status::ok;
};

// Single-owner controller. The platform worker serializes commands and persistence.
// The UI confirms the output command, not the mechanical relay contacts.
class RelayController final {
public:
    RelayController(RelayOutput& output, RelayStore& store) noexcept
        : output_(output), store_(store) {}
    RelayController(const RelayController&) = delete;
    RelayController& operator=(const RelayController&) = delete;
    Status init() noexcept;
    Status start() noexcept;
    Status set(bool on) noexcept;
    void stop() noexcept;
    void deinit() noexcept;
    RelayState state() const noexcept { return state_; }
private:
    RelayOutput& output_;
    RelayStore& store_;
    RelayState state_{};
    bool store_acquired_ = false;
    bool output_acquired_ = false;
    bool initialized_ = false;
};
}  // namespace bth
