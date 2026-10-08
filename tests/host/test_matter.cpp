#include "bth/matter/relay_gate.hpp"
#include "bth/matter/pairing_retry.hpp"
#include "bth/matter/voltage_value.hpp"
#include "bth/matter/product_info.hpp"
#include <limits>
#include <cstdlib>
#include <iostream>

void test_matter() {
    using namespace bth;
    auto check = [](bool ok) { if (!ok) { std::cerr << "Matter relay lifecycle gate failed\n"; std::exit(EXIT_FAILURE); } };
    struct IdentitySource final : chip::DeviceLayer::DeviceInstanceInfoProvider {
        uint16_t product = 0x8000;
        CHIP_ERROR GetVendorId(uint16_t& value) override { value = 0xFFF1; return CHIP_NO_ERROR; }
        CHIP_ERROR GetProductId(uint16_t& value) override { value = product; return CHIP_NO_ERROR; }
        CHIP_ERROR GetSerialNumber(char*, size_t) override { return 73; }
    } source;
    ProductInfo product_info;
    product_info.attach(source);
    char name[9]{};
    check(product_info.GetProductName(name, sizeof(name)) == CHIP_NO_ERROR);
    check(std::strcmp(name, "B-T HOME") == 0);
    char short_name[8] = {'x'};
    check(product_info.GetProductName(short_name, sizeof(short_name)) == CHIP_ERROR_BUFFER_TOO_SMALL);
    check(short_name[0] == 'x'); // No truncated or unterminated product name.
    check(product_info.GetProductLabel(name, sizeof(name)) == CHIP_NO_ERROR);
    check(std::strcmp(name, "B-T HOME") == 0);
    uint16_t identity = 0;
    check(product_info.GetVendorId(identity) == CHIP_NO_ERROR && identity == 0xFFF1);
    check(product_info.GetProductId(identity) == CHIP_NO_ERROR && identity == 0x8000);
    check(product_info.GetSerialNumber(name, sizeof(name)) == 73); // Preserve upstream errors.
    IdentitySource replacement;
    replacement.product = 0x8001;
    product_info.attach(replacement);
    check(product_info.GetProductId(identity) == CHIP_NO_ERROR && identity == 0x8001);
    check(usable_voltage(230.0, true));
    check(usable_voltage(0.0, true));
    check(!usable_voltage(230.0, false));
    check(!usable_voltage(-1.0, true));
    check(!usable_voltage(std::numeric_limits<double>::quiet_NaN(), true));
    check(!usable_voltage(std::numeric_limits<double>::infinity(), true));
    check(!usable_voltage(std::numeric_limits<double>::max(), true));
    check(!usable_voltage(static_cast<double>(voltage_max_millivolts) / 1000.0, true));
    const auto compatible = compatibility_temperature_value(230.45, true);
    check(compatible.valid && compatible.centi_degrees == 23045);
    check(compatibility_temperature_value(0.0, true).valid);
    check(!compatibility_temperature_value(300.01, true).valid);
    check(!compatibility_temperature_value(230.0, false).valid);
    check(!compatibility_temperature_value(std::numeric_limits<double>::quiet_NaN(), true).valid);
    RollingMainsVoltageAverage average;
    check(average.add(200.0, true).centi_degrees == 20000);
    check(average.add(230.0, true).centi_degrees == 21500);
    for (int i = 0; i < 58; ++i) check(average.add(200.0, true).valid);
    const auto full_average = average.add(230.0, true);
    check(full_average.valid && full_average.centi_degrees == 20100);
    const auto invalid_average = average.add(230.0, false);
    check(!invalid_average.valid);
    const auto restarted_average = average.add(12.0, true);
    check(restarted_average.valid && restarted_average.centi_degrees == 1200);
    // Both presentations follow changing ADC reports and reject invalid readings.
    for (const double volts : {0.0, 12.5, 13.8, 221.5, 237.25}) {
        const auto electrical = matter_voltage_value(volts, true);
        const auto panel = compatibility_temperature_value(volts, true);
        check(electrical.valid && panel.valid);
        check(electrical.millivolts == static_cast<std::int64_t>(volts * 1000));
        check(panel.centi_degrees == static_cast<std::int16_t>(volts * 100));
        check(!matter_voltage_value(volts, false).valid);
        check(!compatibility_temperature_value(volts, false).valid);
    }
    check(matter_voltage_value(230.0, true).millivolts == 230000);
    check(matter_voltage_value(12.3456, true).millivolts == 12346);
    check(!matter_voltage_value(230.0, false).valid);
    check(matter_voltage_value(12.5, true).millivolts == 12500); // Backup remains physical.
    MatterRelayGate gate;
    // SDK restores its attributes before startup completes: the relay keeps its own saved state.
    check(gate.classify(false) == MatterWrite::ignore);
    gate.start();
    check(gate.classify(false) == MatterWrite::apply);
    // Reporting a web-originated state must not enqueue a second relay command.
    check(gate.classify(true) == MatterWrite::ignore);
    check(gate.classify(false) == MatterWrite::apply);
    gate.stop(); gate.stop();
    check(gate.classify(false) == MatterWrite::reject);
    check(gate.classify(true) == MatterWrite::reject);
    MatterRelayGate partial;
    partial.stop();
    check(partial.classify(false) == MatterWrite::reject);

    PairingRetry retry;
    check(retry.advance(false) == PairingAction::none); // No unsolicited rollback.
    retry.request();
    check(retry.advance(true) == PairingAction::open);
    check(retry.advance(true) == PairingAction::none);
    retry.request();
    check(retry.advance(false) == PairingAction::rollback);
    for (int i = 0; i != 100; ++i) {
        check(retry.advance(false) == PairingAction::none); // Wait without losing BOOT.
    }
    retry.request(); // Repeated BOOT must not restart an ongoing rollback.
    check(retry.advance(false) == PairingAction::none);
    check(retry.advance(true) == PairingAction::open);
    check(retry.advance(true) == PairingAction::none);
    retry.request();
    check(retry.advance(false) == PairingAction::rollback); // Next session can retry too.
    retry.request(true);
    retry.request(); // A short press cannot cancel the explicit reset.
    check(retry.advance(false) == PairingAction::factory_reset); // Do not wait for stuck rollback.
    check(!retry.pending());
    check(retry.advance(true) == PairingAction::none);
    retry.request();
    retry.request(true);
    check(!retry.pending()); // SDK reboot is already scheduled: dispatch only once.
    check(retry.advance(false) == PairingAction::none);

    PairingRetry fresh;
    check(fresh.advance(true) == PairingAction::none); // Boot never resets automatically.
    fresh.request(true);
    fresh.request(true);
    check(fresh.advance(true) == PairingAction::factory_reset);
    check(!fresh.pending());
    check(fresh.advance(true) == PairingAction::none);
}
