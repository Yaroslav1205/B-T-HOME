#pragma once
#include "bth/core/module.hpp"

namespace bth {
// Own the dedicated NVS partition before the SDK opens its dataset namespace.
class ZigbeeStorage {
public:
    Status init() noexcept;
    void deinit() noexcept;
private:
    bool initialized_ = false;
};
}  // namespace bth
