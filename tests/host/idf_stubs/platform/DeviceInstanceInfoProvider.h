#pragma once
#include <cstddef>
#include <cstdint>
using CHIP_ERROR = int;
inline constexpr CHIP_ERROR CHIP_NO_ERROR = 0;
inline constexpr CHIP_ERROR CHIP_ERROR_BUFFER_TOO_SMALL = 1;
inline constexpr CHIP_ERROR CHIP_ERROR_NOT_IMPLEMENTED = 2;
namespace chip {
struct MutableByteSpan {};
namespace app::Clusters::BasicInformation {
enum class ProductFinishEnum { unknown };
enum class ColorEnum { unknown };
}
namespace DeviceLayer {
// Minimal host double for the SDK presentation-provider contract.
class DeviceInstanceInfoProvider {
public:
    virtual ~DeviceInstanceInfoProvider() = default;
    virtual CHIP_ERROR GetVendorName(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetVendorId(uint16_t&) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetProductName(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetProductId(uint16_t&) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetPartNumber(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetProductURL(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetProductLabel(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetSerialNumber(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetManufacturingDate(uint16_t&, uint8_t&, uint8_t&) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetHardwareVersion(uint16_t&) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetHardwareVersionString(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetSoftwareVersionString(char*, size_t) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetRotatingDeviceIdUniqueId(MutableByteSpan&) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetProductFinish(app::Clusters::BasicInformation::ProductFinishEnum*) { return CHIP_ERROR_NOT_IMPLEMENTED; }
    virtual CHIP_ERROR GetProductPrimaryColor(app::Clusters::BasicInformation::ColorEnum*) { return CHIP_ERROR_NOT_IMPLEMENTED; }
};
}
}
