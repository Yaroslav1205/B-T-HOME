#pragma once
#include "bth/access/access.hpp"
#include "nvs.h"
#include "bth/radio/radio_mode.hpp"
namespace bth {
class NvsAccessStore final : public AccessStore, public RadioModeStore {
public:
    Status load_mode(RadioSelection& mode) noexcept override;
    Status save_mode(RadioSelection mode) noexcept override;
    Status init() noexcept override;
    Status load(AccessConfig& config, bool& found) noexcept override;
    Status save(const AccessConfig& config) noexcept override;
    void deinit() noexcept override;
private:
    nvs_handle_t handle_ = 0;
};
class EspPasswordCodec final : public PasswordCodec {
public:
    Status create(const char* password, WebCredentials& credentials) noexcept override;
    bool verify(const char* password, const WebCredentials& credentials) noexcept override;
};
}  // namespace bth
