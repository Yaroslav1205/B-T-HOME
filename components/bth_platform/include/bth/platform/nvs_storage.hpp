#pragma once
#include "bth/core/module.hpp"
#include "bth/relay/relay.hpp"
#include "nvs.h"
namespace bth {
class NvsStorage final : public Module {
public:
    const char* id() const noexcept override { return "nvs"; }
    Status init() noexcept override;
    Status start() noexcept override { return initialized_ ? Status::ok : Status::invalid_state; }
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    void stop() noexcept override {}
    void deinit() noexcept override;
private:
    bool initialized_ = false;
};
class NvsRelayStore final : public RelayStore {
public:
    Status init() noexcept override;
    Status load(bool& on) noexcept override;
    Status save(bool on) noexcept override;
    Status load_mode(RelayMode& mode) noexcept override;
    Status save_mode(RelayMode mode) noexcept override;
    void deinit() noexcept override;
private:
    nvs_handle_t handle_ = 0;
};
}  // namespace bth
