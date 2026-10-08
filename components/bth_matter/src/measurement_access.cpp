#include "bth/matter/measurement_access.hpp"
#include "bth/matter/voltage_value.hpp"

namespace bth {
CHIP_ERROR MeasurementAccess::Read(const chip::app::ConcreteReadAttributePath& path,
                                   chip::app::AttributeValueEncoder& encoder) {
    using namespace chip;
    using namespace chip::app;
    using namespace chip::app::Clusters;
    using namespace ElectricalPowerMeasurement;
    if (path.mEndpointId != mains_ && path.mEndpointId != backup_ &&
        path.mEndpointId != mains_sensor_) return CHIP_NO_ERROR;
    const bool ac = path.mEndpointId == mains_ || path.mEndpointId == mains_sensor_;
    switch (path.mAttributeId) {
    case Attributes::PowerMode::Id:
        return encoder.Encode(ac ? PowerModeEnum::kAc : PowerModeEnum::kDc);
    case Attributes::NumberOfMeasurementTypes::Id:
        return encoder.Encode(static_cast<uint8_t>(ac ? 3 : 2));
    case Attributes::ActivePower::Id:
        // This hardware measures voltage, not current or power.
        return encoder.Encode(DataModel::Nullable<int64_t>());
    case Attributes::Accuracy::Id:
        return encoder.EncodeList([ac](const AttributeValueEncoder::ListEncodeHelper& list) {
            for (const auto type : {MeasurementTypeEnum::kActivePower,
                                    MeasurementTypeEnum::kVoltage,
                                    MeasurementTypeEnum::kRMSVoltage}) {
                if (!ac && type == MeasurementTypeEnum::kRMSVoltage) continue;
                ElectricalPowerMeasurement::Structs::MeasurementAccuracyRangeStruct::Type range;
                ElectricalPowerMeasurement::Structs::MeasurementAccuracyStruct::Type accuracy;
                accuracy.measurementType = type;
                const bool voltage = type != MeasurementTypeEnum::kActivePower;
                accuracy.measured = voltage;
                // These are software bounds, not a claim about sensor accuracy.
                // No percent/fixed accuracy is advertised without calibration data.
                range.rangeMin = 0;
                range.rangeMax = voltage ? voltage_max_millivolts : 0;
                accuracy.minMeasuredValue = range.rangeMin;
                accuracy.maxMeasuredValue = range.rangeMax;
                accuracy.accuracyRanges = DataModel::List<const
                    ElectricalPowerMeasurement::Structs::MeasurementAccuracyRangeStruct::Type>(&range, 1);
                ReturnErrorOnFailure(list.Encode(accuracy));
            }
            return CHIP_NO_ERROR;
        });
    default:
        // Voltage/RMSVoltage and global attributes use ESP-Matter storage.
        return CHIP_NO_ERROR;
    }
}
} // namespace bth
