#include "application.hpp"
#include "bth/platform/esp_buzzer.hpp"
#include "bth/platform/buzzer_settings.hpp"
#include "bth/platform/esp_mains.hpp"
#include "bth/platform/battery_store.hpp"

#include "bth/board/config.hpp"
#include "bth/core/runtime.hpp"
#include "bth/dc/dc.hpp"
#include "bth/heartbeat/heartbeat.hpp"
#include "bth/platform/esp_heartbeat_sink.hpp"
#include "bth/platform/esp_rgb_led.hpp"
#include "bth/platform/esp_gpio_led.hpp"
#include "bth/platform/esp_oled_i2c.hpp"
#include "bth/startup_blink/startup_blink.hpp"
#include "bth/platform/nvs_storage.hpp"
#include "bth/platform/esp_relay.hpp"
#include "bth/platform/relay_service.hpp"
#include "bth/platform/wifi_ap.hpp"
#include "bth/web/relay_web.hpp"
#include "bth/platform/access_store.hpp"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_BTH_MATTER_ENABLED
#include "bth/matter/matter.hpp"
#include "bth/platform/boot_button.hpp"
#endif
#if CONFIG_BTH_ZIGBEE_ENABLED
#include "bth/zigbee/zigbee.hpp"
#include "bth/platform/boot_button.hpp"
#include "bth/radio/radio_mode.hpp"
#endif

namespace bth::app {
namespace {
constexpr const char* tag = "app";
// Keep ADC diagnostics available for sensor calibration and wiring checks.
constexpr bool voltage_console_enabled = false;
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "B-T HOME requires at least 8192 bytes of main task stack. "
              "Update CONFIG_ESP_MAIN_TASK_STACK_SIZE in the existing sdkconfig; defaults do not override it.");
// One tick drains at most 256 ADC results: 64 ms at 4 kHz for AC + DC.
// Leave scheduling headroom and keep BOOT debounce/startup indication responsive.
static_assert((board::mains_adc_gpio < 0 && board::boot_button_gpio < 0 && board::led_gpio < 0) ||
              CONFIG_BTH_LOOP_INTERVAL_MS <= 50,
              "Boards with ADC, BOOT or startup LED require BTH_LOOP_INTERVAL_MS <= 50. "
              "Update the existing sdkconfig through menuconfig.");

class MainsOutputs final : public MainsSink {
public:
    MainsOutputs(MainsSink& log, MainsSink& display, MainsSink* web, MainsSink* matter,
                 MainsSink* battery, MainsSink* buzzer)
        : log_(log), display_(display), web_(web), matter_(matter), battery_(battery), buzzer_(buzzer) {}
    void report(const MainsReading& reading) noexcept override {
        if (voltage_console_enabled) log_.report(reading);
        display_.report(reading);
        if (web_) web_->report(reading);
        if (matter_) matter_->report(reading);
        if (battery_) battery_->report(reading);
        if (buzzer_) buzzer_->report(reading);
    }
private:
    MainsSink& log_;
    MainsSink& display_;
    MainsSink* web_;
    MainsSink* matter_;
    MainsSink* battery_;
    MainsSink* buzzer_;
};

class DcOutput final : public DcSink {
public:
    DcOutput(OledModule& display, DcSink* web, DcSink* matter, DcSink* battery)
        : display_(display), web_(web), matter_(matter), battery_(battery) {}
    void report(const DcReading& reading) noexcept override {
        (void)display_.report_value(1, reading.volts, reading.valid);
        if (web_) web_->report(reading);
        if (matter_) matter_->report(reading);
        if (battery_) battery_->report(reading);
        if (voltage_console_enabled) {
            if (reading.valid) ESP_LOGI("dc", "U=%.1f V DC | n=%u", reading.volts, reading.samples);
            else ESP_LOGW("dc", "INVALID | n=%u (gap, range or ADC calibration)", reading.samples);
        }
    }
private:
    OledModule& display_;
    DcSink* web_;
    DcSink* matter_;
    DcSink* battery_;
};

#if CONFIG_BTH_MATTER_ENABLED
class MatterVoltageOutputs final : public MainsSink, public DcSink {
public:
    explicit MatterVoltageOutputs(MatterModule& matter) : matter_(matter) {}
    void report(const MainsReading& reading) noexcept override {
        matter_.report_mains_voltage(reading.volts, reading.valid && reading.calibrated);
    }
    void report(const DcReading& reading) noexcept override {
        matter_.report_backup_voltage(reading.volts, reading.valid);
    }
private:
    MatterModule& matter_;
};
#endif

void log_failure(const Runtime& runtime) {
    const auto& failure = runtime.failure();
    ESP_LOGE(tag, "module=%s phase=%s error=%s dependency=%s",
             failure.module ? failure.module : "-", phase_name(failure.phase),
             status_name(failure.status), failure.dependency ? failure.dependency : "-");
}
}  // namespace

void run() {
    ESP_LOGI(tag, "B-T HOME | board=%s | target=%s", board::id, board::target);
    ESP_LOGI(tag, "Main task stack: configured=%u bytes",
             static_cast<unsigned>(CONFIG_ESP_MAIN_TASK_STACK_SIZE));

    // Composition root: dependencies first, then consumers. No hidden registration.
    // Declare modules BEFORE Runtime: reverse destruction keeps references valid.
#if CONFIG_BTH_HEARTBEAT_ENABLED
    EspHeartbeatSink heartbeat_sink;
    Heartbeat heartbeat(heartbeat_sink, CONFIG_BTH_HEARTBEAT_INTERVAL_MS);
#endif
#if BTH_LED_VARIANT == BTH_LED_VARIANT_GPIO
    EspGpioLed startup_led(board::led_gpio, board::led_active_high);
#else
    EspRgbLed startup_led(board::led_gpio);
#endif
    StartupBlinkConfig blink_config{
        CONFIG_BTH_STARTUP_BLINK_COUNT,
        CONFIG_BTH_STARTUP_BLINK_ON_MS,
        CONFIG_BTH_STARTUP_BLINK_OFF_MS};
#ifdef CONFIG_BTH_STARTUP_BLINK_PULSE_DARK
    blink_config.pulse_on = false;
#endif
    blink_config.repeat_pause_ms = CONFIG_BTH_STARTUP_BLINK_REPEAT_PAUSE_MS;
#if CONFIG_BTH_WEB_RELAY_ENABLED
#if CONFIG_BTH_ZIGBEE_ENABLED
    blink_config.count = 1;
#else
    blink_config.count = 1;
#endif
    blink_config.pulse_on = true;
    blink_config.repeat_pause_ms = 2000;
#endif
    StartupBlinkModule startup_blink(startup_led, blink_config);
#if CONFIG_BTH_WEB_RELAY_ENABLED
    NvsStorage storage;
    NvsBuzzerSettings buzzer_settings;
    BuzzerSettings* web_buzzer = board::buzzer_gpio >= 0 ? &buzzer_settings : nullptr;
    NvsRelayStore relay_store;
    EspRelay relay_output(board::relay_gpio, board::relay_active_high);
    RelayController relay_controller(relay_output, relay_store);
    RelayService relay_service(relay_controller, board::relay_gpio >= 0);
    std::array<unsigned char, 6> ap_mac{};
    if (esp_read_mac(ap_mac.data(), ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        ESP_LOGE(tag, "Cannot read AP MAC address");
        return;
    }
    NvsAccessStore access_store;
    EspPasswordCodec password_codec;
    AccessSettings access(access_store, password_codec, ap_mac);
#if CONFIG_BTH_MATTER_ENABLED
    static_assert(board::relay_gpio >= 0 && board::boot_button_gpio >= 0, "Matter requires relay and BOOT GPIOs");
    MatterModule matter(relay_service);
    MatterVoltageOutputs matter_voltages(matter);
    RelayWeb web(relay_service, access, web_buzzer);
    BootButton boot_button(board::boot_button_gpio, board::boot_button_active_low);
#elif CONFIG_BTH_ZIGBEE_ENABLED
    WifiAp wifi(access);
    static_assert(board::relay_gpio >= 0, "Zigbee requires a configured relay in the board profile");
    static_assert(board::boot_button_gpio >= 0, "Zigbee/Wi-Fi mode selection requires a confirmed BOOT GPIO");
    ZigbeeModule zigbee(relay_service);
    // The configuration AP never shares the radio with Zigbee.
    RelayWeb web(relay_service, access, wifi, nullptr, web_buzzer);
    RadioMode radio(zigbee, wifi, web, wifi, access_store);
    BootButton boot_button(board::boot_button_gpio, board::boot_button_active_low);
#else
    WifiAp wifi(access);
    RelayWeb web(relay_service, access, wifi, nullptr, web_buzzer);
#endif
    MainsSink* web_mains = &web;
    DcSink* web_dc = &web;
#else
    MainsSink* web_mains = nullptr;
    DcSink* web_dc = nullptr;
#endif
#if CONFIG_BTH_MATTER_ENABLED
    MainsSink* matter_mains = &matter_voltages;
    DcSink* matter_dc = &matter_voltages;
#else
    MainsSink* matter_mains = nullptr;
    DcSink* matter_dc = nullptr;
#endif
    EspOledI2c oled_bus({board::oled_port, board::oled_sda, board::oled_scl,
                       board::oled_speed_hz, board::oled_address, board::oled_internal_pullups});
#if CONFIG_BTH_WEB_RELAY_ENABLED
    constexpr bool show_relay = board::relay_gpio >= 0;
#else
    constexpr bool show_relay = false;
#endif
    OledModule oled(oled_bus, board::oled_contrast, board::dc_adc_gpio >= 0 ? 2 : 1,
                    blink_config.startup_ms, board::mains_adc_gpio >= 0,
                    board::mains_adc_gpio >= 0, show_relay,
#if CONFIG_BTH_MATTER_ENABLED || CONFIG_BTH_ZIGBEE_ENABLED
                    true
#else
                    false
#endif
                    );
#if CONFIG_BTH_WEB_RELAY_ENABLED
    NvsBatteryStore battery_store;
    static constexpr const char* battery_providers[] = {"nvs", "mains", "dc"};
    BatteryModule battery(battery_store, oled, {12000, 58000, 1500, 10.5}, {battery_providers, 3});
    constexpr bool battery_enabled = board::mains_adc_gpio >= 0 && board::dc_adc_gpio >= 0;
    MainsSink* battery_mains = battery_enabled ? &battery : nullptr;
    DcSink* battery_dc = battery_enabled ? &battery : nullptr;
#else
    MainsSink* battery_mains = nullptr;
    DcSink* battery_dc = nullptr;
#endif
    EspMainsInput mains_input(board::mains_adc_gpio, board::dc_adc_gpio);
    EspMainsSink mains_sink;
    EspBuzzer buzzer_output(board::buzzer_gpio, board::buzzer_active_high);
    BuzzerModule buzzer(buzzer_output);
    MainsOutputs mains_outputs(mains_sink, oled, web_mains, matter_mains, battery_mains,
                               board::buzzer_gpio >= 0 ? &buzzer : nullptr);
    MainsModule mains(mains_input, mains_outputs, CONFIG_BTH_MAINS_MILLIVOLTS_PER_COUNT / 1000.0);
    DcOutput dc_output(oled, web_dc, matter_dc, battery_dc);
    static constexpr const char* adc_provider[] = {"mains"};
    DcModule dc(mains_input, dc_output,
                board::dc_divider_ratio * CONFIG_BTH_DC_CALIBRATION_PERMILLE / 1000.0,
                {adc_provider, 1});
    Runtime runtime;
#if CONFIG_BTH_WEB_RELAY_ENABLED
    if (runtime.add(storage) != Status::ok) { log_failure(runtime); return; }
    if constexpr (board::buzzer_gpio >= 0) {
        if (runtime.add(buzzer_settings) != Status::ok) { log_failure(runtime); return; }
    }
#endif
    if constexpr (board::mains_adc_gpio >= 0) {
        if (runtime.add(mains) != Status::ok) {
            log_failure(runtime);
            return;
        }
    }
    if constexpr (board::dc_adc_gpio >= 0) {
        if (runtime.add(dc) != Status::ok) {
            log_failure(runtime);
            return;
        }
    }
    if constexpr (board::buzzer_gpio >= 0) {
        if (runtime.add(buzzer) != Status::ok) { log_failure(runtime); return; }
    }
#if CONFIG_BTH_WEB_RELAY_ENABLED
    if constexpr (battery_enabled) {
        if (runtime.add(battery) != Status::ok) { log_failure(runtime); return; }
    }
#endif
    if constexpr (board::oled_enabled) {
        if (runtime.add(oled) != Status::ok) {
            log_failure(runtime);
            return;
        }
    }
#if CONFIG_BTH_WEB_RELAY_ENABLED
    // Explicit lifecycle order: web joins before relay worker, Wi-Fi and NVS stop.
    Module* control_modules[] = {&access, &relay_service,
#if CONFIG_BTH_MATTER_ENABLED
                                &matter, &web, &boot_button
#elif CONFIG_BTH_ZIGBEE_ENABLED
                                &radio, &boot_button
#else
                                &wifi, &web
#endif
                                };
    for (auto* module : control_modules) {
        if (runtime.add(*module) != Status::ok) {
            log_failure(runtime);
            return;
        }
    }
#endif
    if constexpr (board::led_gpio >= 0) {
        if (runtime.add(startup_blink) != Status::ok) {
            log_failure(runtime);
            return;
        }
    }
#if CONFIG_BTH_HEARTBEAT_ENABLED
    if (runtime.add(heartbeat) != Status::ok) {
        log_failure(runtime);
        return;
    }
#endif
    if (runtime.start() != Status::ok) {
        log_failure(runtime);
        return;
    }
    ESP_LOGI(tag, "Started %u module(s)", static_cast<unsigned>(runtime.size()));
#if CONFIG_BTH_MATTER_ENABLED
    ESP_LOGI(tag, "Mode: Matter Wi-Fi | BOOT: short=pair, hold 3s and release=reset Matter and Wi-Fi, reboot and pair");
#elif CONFIG_BTH_ZIGBEE_ENABLED
    ESP_LOGI(tag, "BOOT: short=AP/STA/Zigbee, hold 3s and release=Zigbee pairing");
#endif

    const TickType_t delay_ticks = pdMS_TO_TICKS(CONFIG_BTH_LOOP_INTERVAL_MS);
#if CONFIG_BTH_MATTER_ENABLED
    std::uint64_t next_ip_ms = 0;
#endif
    for (;;) {
        const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
#if CONFIG_BTH_WEB_RELAY_ENABLED
        if constexpr (board::buzzer_gpio >= 0) {
            if (buzzer.set_interval_seconds(buzzer_settings.interval_seconds()) != Status::ok) {
                ESP_LOGE(tag, "Invalid buzzer interval");
                runtime.stop();
                return;
            }
        }
        const auto relay_state = relay_service.snapshot();
        oled.report_relay(relay_state.on, relay_state.ready && relay_state.output_known);
#endif
#if CONFIG_BTH_MATTER_ENABLED
        if (now_ms >= next_ip_ms) {
            oled.report_ipv4(matter.ipv4_address());
            next_ip_ms = now_ms + 1000;
        }
        startup_blink.set_count(matter.pairing() ? 3 : 1);
#elif CONFIG_BTH_ZIGBEE_ENABLED
        oled.report_ipv4(radio.configuration_mode() ? wifi.ipv4_address() : std::array<std::uint8_t, 4>{});
        startup_blink.set_count(radio.selection() == RadioSelection::ap ? 1 :
            radio.selection() == RadioSelection::station ? 2 :
            zigbee.snapshot().state == ZigbeeState::joining ? 8 : 3);
#endif
        if (runtime.tick(now_ms) != Status::ok) {
            log_failure(runtime);
            // Runtime has already stopped and deinitialized all modules.
            return;
        }
#if CONFIG_BTH_WEB_RELAY_ENABLED
        if constexpr (battery_enabled) {
            const auto saved = battery.service();
            if (saved != Status::ok) {
                ESP_LOGE(tag, "Battery checkpoint failed: %s", status_name(saved));
                runtime.stop();
                return;
            }
        }
#endif
#if CONFIG_BTH_MATTER_ENABLED
        const auto button_action = boot_button.take_action();
        if (button_action != ButtonAction::none) {
            const bool reset_matter = button_action == ButtonAction::toggle_mode;
            ESP_LOGI(tag, "BOOT: %s requested", reset_matter ? "Matter factory reset" : "Matter pairing");
            matter.open_pairing(reset_matter);
        }
        matter.service_pairing();
#elif CONFIG_BTH_ZIGBEE_ENABLED
        const auto action = boot_button.take_action();
        if (action == ButtonAction::pair) {
            // Joins radio workers outside tick; providers and measurements stay alive.
            const auto result = radio.toggle();
            if (result != Status::ok) {
                ESP_LOGE(tag, "Radio mode switch failed: %s", status_name(result));
                runtime.stop();
                return;
            }
            ESP_LOGI(tag, "Mode: %s", radio.selection() == RadioSelection::ap ? "Wi-Fi AP | http://192.168.4.1" :
                radio.selection() == RadioSelection::station ? "Wi-Fi client" : "Zigbee");
        } else if (action == ButtonAction::toggle_mode && !radio.configuration_mode()) {
            ESP_LOGI(tag, "BOOT pairing: %s", zigbee.request(ZigbeeCommand::join) ? "queued" : "busy");
        }
#endif
        vTaskDelay(delay_ticks > 0 ? delay_ticks : 1);
    }
}

}  // namespace bth::app
