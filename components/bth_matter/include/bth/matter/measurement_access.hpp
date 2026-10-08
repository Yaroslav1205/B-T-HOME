#pragma once
#include <app/AttributeAccessInterface.h>
#include <app-common/zap-generated/cluster-objects.h>

namespace bth {
// Supplies metadata for application-owned voltage attributes. Register only
// while the Matter server is running; access is serialized by the CHIP lock.
class MeasurementAccess final : public chip::app::AttributeAccessInterface {
public:
    MeasurementAccess() : AttributeAccessInterface(chip::NullOptional,
        chip::app::Clusters::ElectricalPowerMeasurement::Id) {}
    void configure(chip::EndpointId mains, chip::EndpointId backup,
                   chip::EndpointId mains_sensor) noexcept {
        mains_ = mains; backup_ = backup; mains_sensor_ = mains_sensor;
    }
    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath& path,
                    chip::app::AttributeValueEncoder& encoder) override;
private:
    chip::EndpointId mains_ = chip::kInvalidEndpointId;
    chip::EndpointId backup_ = chip::kInvalidEndpointId;
    chip::EndpointId mains_sensor_ = chip::kInvalidEndpointId;
};
} // namespace bth
