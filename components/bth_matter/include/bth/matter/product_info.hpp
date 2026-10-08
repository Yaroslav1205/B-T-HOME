#pragma once
#include <platform/DeviceInstanceInfoProvider.h>
#include <cstring>

namespace bth {
// Override presentation names while retaining the SDK's identity and credentials.
class ProductInfo final : public chip::DeviceLayer::DeviceInstanceInfoProvider {
public:
    void attach(DeviceInstanceInfoProvider& source) noexcept { source_ = &source; }
    CHIP_ERROR GetProductName(char* buf, size_t size) override {
        constexpr char name[] = "B-T HOME";
        if (size < sizeof(name)) return CHIP_ERROR_BUFFER_TOO_SMALL;
        std::memcpy(buf, name, sizeof(name));
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR GetProductLabel(char* buf, size_t size) override { return GetProductName(buf, size); }
    CHIP_ERROR GetVendorName(char* b, size_t s) override { return source_->GetVendorName(b, s); }
    CHIP_ERROR GetVendorId(uint16_t& v) override { return source_->GetVendorId(v); }
    CHIP_ERROR GetProductId(uint16_t& v) override { return source_->GetProductId(v); }
    CHIP_ERROR GetPartNumber(char* b, size_t s) override { return source_->GetPartNumber(b, s); }
    CHIP_ERROR GetProductURL(char* b, size_t s) override { return source_->GetProductURL(b, s); }
    CHIP_ERROR GetSerialNumber(char* b, size_t s) override { return source_->GetSerialNumber(b, s); }
    CHIP_ERROR GetManufacturingDate(uint16_t& y, uint8_t& m, uint8_t& d) override {
        return source_->GetManufacturingDate(y, m, d);
    }
    CHIP_ERROR GetHardwareVersion(uint16_t& v) override { return source_->GetHardwareVersion(v); }
    CHIP_ERROR GetHardwareVersionString(char* b, size_t s) override { return source_->GetHardwareVersionString(b, s); }
    CHIP_ERROR GetSoftwareVersionString(char* b, size_t s) override { return source_->GetSoftwareVersionString(b, s); }
    CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan& v) override { return source_->GetRotatingDeviceIdUniqueId(v); }
    CHIP_ERROR GetProductFinish(chip::app::Clusters::BasicInformation::ProductFinishEnum* v) override {
        return source_->GetProductFinish(v);
    }
    CHIP_ERROR GetProductPrimaryColor(chip::app::Clusters::BasicInformation::ColorEnum* v) override {
        return source_->GetProductPrimaryColor(v);
    }
private:
    DeviceInstanceInfoProvider* source_ = nullptr;
};
} // namespace bth
