#include "bth/oled/oled.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bth {
namespace {
// Fit the 23 mm raster into a 19 mm enclosure window offset 3 mm from the left.
// Merge columns rather than dropping thin strokes; preserve all 32 rows.
OledFrame fit_window(const OledFrame& source) noexcept {
    constexpr int visible_width = 128 * 19 / 23;
    constexpr int offset_x = (128 * 3 + 23 / 2) / 23;  // Round to 17 pixels.
    static_assert(offset_x + visible_width <= 128);
    int left = 128;
    for (int x = 0; x < 128; ++x)
        for (int page = 0; page < 4; ++page)
            if (source[page * 128 + x] != 0) left = std::min(left, x);
    OledFrame result{};
    for (int x = left; x < 128; ++x)
        for (int page = 0; page < 4; ++page)
            result[page * 128 + offset_x + (x - left) * visible_width / 128] |= source[page * 128 + x];
    return result;
}

void pixel(OledFrame& frame, int x, int y) noexcept {
    if (x >= 0 && x < 128 && y >= 0 && y < 32) {
        frame[static_cast<std::size_t>(y / 8 * 128 + x)] |=
            static_cast<std::uint8_t>(1U << (y % 8));
    }
}

void bar(OledFrame& frame, int x, int y, int width, int height) noexcept {
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) { pixel(frame, x + col, y + row); }
    }
}

void digit(OledFrame& frame, int x, int y, unsigned value) noexcept {
    // Bits a..g: top, upper right, lower right, bottom, lower left, upper left, middle.
    constexpr std::uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                      0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    const auto mask = masks[value];
    // 18 x 32 pixels: reserve horizontal space for the voltage-kind prefix.
    if (mask & 0x01) { bar(frame, x + 4, y, 10, 4); }
    if (mask & 0x02) { bar(frame, x + 14, y + 4, 4, 10); }
    if (mask & 0x04) { bar(frame, x + 14, y + 18, 4, 10); }
    if (mask & 0x08) { bar(frame, x + 4, y + 28, 10, 4); }
    if (mask & 0x10) { bar(frame, x, y + 18, 4, 10); }
    if (mask & 0x20) { bar(frame, x, y + 4, 4, 10); }
    if (mask & 0x40) { bar(frame, x + 4, y + 14, 10, 4); }
}
}  // namespace

OledFrame render_mains_period(const MainsPeriod& period, bool previous) noexcept {
    OledFrame frame{};
    // Ukrainian 5x7 labels: ЗАРАЗ / БУЛО, units хв / год.
    constexpr std::uint8_t glyphs[][7] = {
        {14,17,1,6,1,17,14}, {14,17,17,31,17,17,17}, // З А
        {30,17,17,30,16,16,16}, {31,16,16,30,17,17,30}, // Р Б
        {17,17,17,15,1,17,14}, {7,9,9,9,9,9,17}, // У Л
        {14,17,17,17,17,17,14}, {0,0,17,10,4,10,17}, // О х
        {0,0,30,17,30,17,30}, {0,0,31,16,16,16,16}, // в г
        {0,0,14,17,17,17,14}, {0,0,6,10,10,31,17}, // о д
    };
    const auto glyph = [&](int index, int x, int y) {
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyphs[index][row] & (1U << (4-col))) pixel(frame, x+col, y+row);
    };
    const int current_label[] = {0,1,2,1,0};
    const int previous_label[] = {3,4,5,6};
    for (int i = 0; i < (previous ? 4 : 5); ++i)
        glyph(previous ? previous_label[i] : current_label[i], i*6, 0);
    // Plus = mains present, minus = absent. Unknown has no state indicator.
    if (period.known) {
        bar(frame, 40, 3, 7, 1);
        if (period.present) bar(frame, 43, 0, 1, 7);
    }
    const auto render_duration = [&](const char* text) {
        int x = 0;
        for (const char* c = text; *c; ++c) {
            if (*c == ':') {
                bar(frame, x + 2, 17, 3, 3);
                bar(frame, x + 2, 25, 3, 3);
                x += 6;
                continue;
            }
            if (*c == '-') {
                bar(frame, x + 1, 20, 10, 3);
                x += 15;
                continue;
            }
            OledFrame source{};
            digit(source, 0, 0, static_cast<unsigned>(*c - '0'));
            for (int y = 0; y < 21; ++y)
                for (int col = 0; col < 12; ++col) {
                    const int sy = y * 32 / 21, sx = col * 18 / 12;
                    if (source[sy / 8 * 128 + sx] & (1U << (sy % 8))) pixel(frame, x + col, y + 11);
                }
            x += 15;
        }
    };
    if (!period.known) {
        render_duration("---:--:--");
        return fit_window(frame);
    }
    constexpr std::uint64_t max_minutes = 999ULL * 24 * 60 + 23 * 60 + 59;
    const auto total_minutes = std::min(period.duration_ms / 60000, max_minutes);
    const auto days = total_minutes / (24 * 60);
    const auto hours = total_minutes / 60 % 24;
    const auto minutes = total_minutes % 60;
    char text[16]{};
    std::snprintf(text, sizeof(text), "%03llu:%02llu:%02llu",
                  static_cast<unsigned long long>(days),
                  static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes));
    render_duration(text);
    return fit_window(frame);
}

OledFrame render_relay(bool on, bool valid) noexcept {
    // Ukrainian labels: relay, enabled / disabled. Two centered rows.
    constexpr std::uint8_t glyphs[][7] = {
        {30,17,17,30,16,16,16}, // Cyrillic R
        {31,16,16,30,16,16,31}, // E
        {7,9,9,9,9,9,17},      // L
        {17,17,17,15,1,17,14}, // U
        {30,17,17,30,17,17,30}, // V
        {14,4,4,4,4,4,14},     // I
        {17,27,21,21,17,17,17}, // M
        {17,18,20,24,20,18,17}, // K
        {17,17,19,21,25,17,17}, // Y
    };
    OledFrame frame{};
    const auto glyph = [&](int index, int x, int y) {
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyphs[index][row] & (1U << (4-col)))
                    bar(frame, x+col*2, y+row*2, 2, 2);
    };
    const int label[] = {0,1,2,1};
    for (int i = 0; i < 4; ++i) glyph(label[i], 41+i*12, 0);
    if (!valid) {
        for (int x : {47,59,71}) bar(frame, x, 24, 10, 2);
    } else {
        const int enabled[] = {3,4,5,6,7};
        const int disabled[] = {4,8,6,7};
        for (int i = 0; i < (on ? 5 : 4); ++i)
            glyph(on ? enabled[i] : disabled[i], (on ? 35 : 41)+i*12, 18);
    }
    return fit_window(frame);
}

OledFrame render_ipv4(const std::array<std::uint8_t, 4>& address) noexcept {
    OledFrame frame{};
    // IP heading, centered above a single full address (at most 15 characters).
    constexpr std::uint8_t heading[][7] = {
        {14,4,4,4,4,4,14}, {30,17,17,30,16,16,16}
    };
    for (int i = 0; i < 2; ++i)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (heading[i][y] & (1U << (4-x))) pixel(frame, 58+i*6+x, y);
    char text[16] = "---";
    if (address != std::array<std::uint8_t, 4>{})
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u",
            static_cast<unsigned>(address[0]), static_cast<unsigned>(address[1]),
            static_cast<unsigned>(address[2]), static_cast<unsigned>(address[3]));
    int width = 0;
    for (const char* c = text; *c; ++c) width += *c == '.' ? 4 : 9;
    int x = (128-width)/2;
    for (const char* c = text; *c; ++c) {
        if (*c == '.') {
            bar(frame, x, 27, 2, 2);
        } else if (*c == '-') {
            bar(frame, x, 20, 7, 2);
        } else {
            OledFrame source{};
            digit(source, 0, 0, static_cast<unsigned>(*c-'0'));
            for (int y = 0; y < 18; ++y)
                for (int col = 0; col < 7; ++col) {
                    const int sy = y*32/18, sx = col*18/7;
                    if (source[sy/8*128+sx] & (1U << (sy%8))) pixel(frame, x+col, y+11);
                }
        }
        x += *c == '.' ? 4 : 9;
    }
    return fit_window(frame);
}

OledFrame render_startup_splash() noexcept {
    // B-T HOME, 5x7 glyphs scaled 2x3, centered within 128x32.
    constexpr std::uint8_t glyphs[][7] = {
        {30, 17, 17, 30, 17, 17, 30}, // B
        {0, 0, 0, 31, 0, 0, 0},      // -
        {31, 4, 4, 4, 4, 4, 4},      // T
        {0, 0, 0, 0, 0, 0, 0},       // space
        {17, 17, 17, 31, 17, 17, 17}, // H
        {14, 17, 17, 17, 17, 17, 14}, // O
        {17, 27, 21, 21, 17, 17, 17}, // M
        {31, 16, 16, 30, 16, 16, 31}, // E
    };
    OledFrame frame{};
    for (int glyph = 0; glyph < 8; ++glyph)
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyphs[glyph][row] & (1U << (4 - col)))
                    bar(frame, 17 + glyph * 12 + col * 2, 5 + row * 3, 2, 3);
    return fit_window(frame);
}

OledFrame render_voltage(double volts, bool valid, VoltageKind kind) noexcept {
    OledFrame frame{};
    // Fill 128 x 32 without clipping, keeping gaps between every glyph.
    valid = valid && std::isfinite(volts) && volts >= 0 && volts < 999.95;
    int prefix_x = 0;
    if (valid) {
        const auto tenths = static_cast<unsigned>(std::floor(volts * 10 + 0.5));
        prefix_x = tenths >= 1000 ? 0 : (tenths >= 100 ? 22 : 44);
        if (tenths >= 1000) digit(frame, 16, 0, tenths / 1000);
        if (tenths >= 100) digit(frame, 38, 0, tenths / 100 % 10);
        digit(frame, 60, 0, tenths / 10 % 10);
        digit(frame, 90, 0, tenths % 10);
    } else {
        for (int x : {16, 38, 60, 90}) bar(frame, x + 4, 14, 10, 4);
    }
    if (kind == VoltageKind::dc) {
        bar(frame, prefix_x, 15, 12, 3);
        bar(frame, prefix_x + 5, 10, 3, 13);
    } else if (kind == VoltageKind::average) {
        bar(frame, prefix_x, 11, 12, 3);
        bar(frame, prefix_x, 19, 12, 3);
    } else {
        constexpr int wave[] = {0, -1, -2, -2, -1, 0, 1, 2, 2, 1, 0, -1};
        for (int x = 0; x < 12; ++x) bar(frame, prefix_x + x, 14 + wave[x], 1, 3);
    }
    bar(frame, 82, 28, 4, 4);
    // V cannot be represented unambiguously with seven segments; draw its diagonals.
    for (int y = 0; y < 32; ++y) {
        const int inset = y * 6 / 31;
        bar(frame, 112 + inset, y, 3, 1);
        bar(frame, 125 - inset, y, 3, 1);
    }
    return fit_window(frame);
}

std::array<char, 24> format_battery_line(double volts, bool valid, const BatteryEstimate& estimate) noexcept {
    std::array<char, 24> text{};
    char voltage[8] = "--.-";
    if (valid && std::isfinite(volts) && volts >= 0 && volts < 30.05) {
        const auto tenths = static_cast<unsigned>(std::floor(volts * 10 + 0.5));
        std::snprintf(voltage, sizeof(voltage), "%u.%u", tenths / 10 % 100, tenths % 10);
    } else {
        valid = false;
    }
    char time[8] = "--:--";
    if (valid && estimate.valid && estimate.mains_known && !estimate.mains_present) {
        if (estimate.minutes > 5999) {
            std::snprintf(time, sizeof(time), ">99:59");
        } else {
            std::snprintf(time, sizeof(time), "%02u:%02u",
                          static_cast<unsigned>(estimate.minutes / 60 % 100),
                          static_cast<unsigned>(estimate.minutes % 60));
        }
    }
    std::snprintf(text.data(), text.size(), "%c%sV=%s",
                  estimate.mains_known ? (estimate.mains_present ? '+' : '-') : '?', voltage, time);
    return text;
}

OledFrame render_battery(double volts, bool valid, const BatteryEstimate& estimate) noexcept {
    if (estimate.mains_known && estimate.mains_present) return render_voltage(volts, valid, VoltageKind::dc);
    const auto text = format_battery_line(volts, valid, estimate);
    const auto width = [](char c) { return c == '.' || c == ':' ? 4 : 10; };
    int length = 0;
    for (const char* c = text.data(); *c; ++c) length += width(*c);
    OledFrame frame{};
    int x = (128 - length) / 2;
    for (const char* c = text.data(); *c; ++c) {
        if (*c >= '0' && *c <= '9') {
            OledFrame source{};
            digit(source, 0, 0, static_cast<unsigned>(*c - '0'));
            for (int y = 0; y < 24; ++y)
                for (int col = 0; col < 8; ++col) {
                    const int sy = y * 32 / 24, sx = col * 18 / 8;
                    if (source[sy / 8 * 128 + sx] & (1U << (sy % 8))) pixel(frame, x + col, y + 4);
                }
        } else if (*c == '.') {
            bar(frame, x, 25, 2, 3);
        } else if (*c == ':') {
            bar(frame, x, 10, 2, 3); bar(frame, x, 21, 2, 3);
        } else if (*c == '-' || *c == '+') {
            bar(frame, x, 15, 8, 2);
            if (*c == '+') bar(frame, x + 3, 10, 2, 12);
        } else if (*c == '=') {
            bar(frame, x, 11, 8, 2); bar(frame, x, 20, 8, 2);
        } else if (*c == 'V') {
            for (int y = 0; y < 24; ++y) {
                pixel(frame, x + y * 3 / 23, y + 4);
                pixel(frame, x + 7 - y * 3 / 23, y + 4);
            }
        } else if (*c == '>') {
            for (int y = 0; y < 18; ++y) pixel(frame, x + (y < 9 ? y : 17 - y) / 2, y + 7);
        } else if (*c == '?') {
            bar(frame, x, 5, 7, 2); bar(frame, x + 6, 7, 2, 7);
            bar(frame, x + 3, 13, 4, 2); bar(frame, x + 3, 15, 2, 5);
            bar(frame, x + 3, 25, 2, 3);
        }
        x += width(*c);
    }
    return fit_window(frame);
}

Status OledModule::init() noexcept {
    if (acquired_ || initialized_) { return Status::invalid_state; }
    if (page_count_ == 0 || page_count_ + (show_average_ ? 1 : 0) > max_pages)
        return Status::invalid_argument;
    acquired_ = true;
    auto result = transport_.init();
    if (result != Status::ok) { return result; }
    result = transport_.probe(present_);
    if (result != Status::ok) { return result; }
    if (!present_) {
        transport_.deinit();
        acquired_ = false;
    }
    initialized_ = result == Status::ok;
    return result;
}

Status OledModule::start() noexcept {
    if (!initialized_ || start_attempted_) { return Status::invalid_state; }
    start_attempted_ = true;
    pages_ = {};
    history_ = {};
    battery_ = {};
    battery_enabled_ = battery_fresh_ = false;
    relay_on_ = relay_valid_ = false;
    ipv4_ = {};
    average_samples_ = {};
    average_sum_ = 0;
    average_count_ = average_next_ = 0;
    active_page_ = 0;
    page_timed_ = false;
    splash_active_ = splash_ms_ != 0;
    splash_timed_ = false;
    if (!present_) {
        running_ = true;
        return Status::ok;
    }
    // Control byte 0x00 introduces commands. Configure 32 COM rows and charge pump.
    const std::uint8_t setup[] = {
        0x00, 0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x02,
        0x81, contrast_, 0xd9, 0xf1, 0xdb, 0x20, 0xa4, 0xa6, 0x2e, 0x23, 0x00, 0xd6, 0x00,
        0x21, 0x00, 0x7f, 0x22, 0x00, 0x03};
    auto result = transport_.write(setup, sizeof(setup));
    if (result != Status::ok) { return result; }
    frame_ = splash_active_ ? render_startup_splash() : render_voltage(0, false);
    std::array<std::uint8_t, 513> data{};
    data[0] = 0x40;  // Data control byte; overwrite all four pages before display-on.
    std::copy(frame_.begin(), frame_.end(), data.begin() + 1);
    result = transport_.write(data.data(), data.size());
    if (result != Status::ok) { return result; }
    const std::uint8_t on[] = {0x00, 0xaf};
    result = transport_.write(on, sizeof(on));
    if (result != Status::ok) { return result; }
    result = transport_.start_updates();
    running_ = result == Status::ok;
    return result;
}

void OledModule::report(const MainsReading& reading) noexcept {
    history_ = reading.history;
    (void)report_value(0, reading.volts, reading.valid && reading.calibrated);
    if (show_average_) {
        auto& average = pages_[page_count_];
        if (reading.valid && reading.calibrated && std::isfinite(reading.volts) &&
            reading.volts >= 0 && reading.volts < 999.95) {
            // Confirmed outage: retain the last mains average without adding
            // low-voltage samples. Fresh outage reports keep it visible.
            if (reading.volts < 100.0) {
                average.fresh = true;
                return;
            }
            if (average_count_ == average_samples_.size()) {
                average_sum_ -= average_samples_[average_next_];
            } else {
                ++average_count_;
            }
            average_samples_[average_next_] = reading.volts;
            average_sum_ += reading.volts;
            average_next_ = (average_next_ + 1) % average_samples_.size();
            average.volts = average_sum_ / average_count_;
            average.valid = true;
        } else {
            average_samples_ = {};
            average_sum_ = 0;
            average_count_ = average_next_ = 0;
            average.valid = false;
        }
        average.fresh = true;
    }
}

Status OledModule::report_value(unsigned page, double volts, bool valid) noexcept {
    if (page >= page_count_ || page >= max_pages) return Status::invalid_argument;
    pages_[page].volts = volts;
    pages_[page].valid = valid;
    pages_[page].fresh = true;
    return Status::ok;
}

Status OledModule::tick(std::uint64_t now_ms) noexcept {
    if (!running_) return Status::invalid_state;
    if (!present_) return Status::ok;
    if (battery_fresh_) { battery_at_ = now_ms; battery_fresh_ = false; }
    if (now_ms < battery_at_ || now_ms - battery_at_ >= 2500) battery_ = {};
    const auto status = transport_.poll();
    if (status != Status::ok) return status;
    const unsigned voltage_pages = page_count_ + (show_average_ ? 1 : 0);
    for (unsigned i = 0; i < voltage_pages; ++i) {
        auto& page = pages_[i];
        if (page.fresh) {
            page.last_reading_ms = now_ms;
            page.received = true;
            page.fresh = false;
        }
    }
    // Share the first Runtime tick as the epoch with the startup LED burst.
    // Continue ingesting readings above while the splash is visible.
    if (splash_active_) {
        if (!splash_timed_) { splash_started_ms_ = now_ms; splash_timed_ = true; }
        if (now_ms - splash_started_ms_ < splash_ms_) return Status::ok;
        splash_active_ = false;
    }
    if (!page_timed_) { page_since_ms_ = now_ms; page_timed_ = true; }
    const auto steps = (now_ms - page_since_ms_) / 3000;
    const unsigned relay_page = voltage_pages + (show_history_ ? 2 : 0);
    const unsigned ip_page = relay_page + (show_relay_ ? 1 : 0);
    const unsigned total_pages = ip_page + (show_ip_ ? 1 : 0);
    active_page_ = static_cast<unsigned>((active_page_ + steps % total_pages) % total_pages);
    page_since_ms_ += steps * 3000;
    const bool average_page = show_average_ && active_page_ == 1;
    const bool voltage_page = active_page_ < voltage_pages;
    const unsigned data_page = average_page ? page_count_ :
        active_page_ > 1 && show_average_ ? active_page_ - 1 : active_page_;
    const auto& page = pages_[voltage_page ? data_page : 0];
    const bool valid = page.received && now_ms - page.last_reading_ms < 2500 && page.valid;
    const bool previous = active_page_ == voltage_pages + 1;
    const auto next = show_ip_ && active_page_ == ip_page
        ? render_ipv4(ipv4_)
        : show_relay_ && active_page_ == relay_page
        ? render_relay(relay_on_, relay_valid_)
        : voltage_page
        ? (data_page == 1 && !average_page && battery_enabled_
            ? render_battery(page.volts, valid, battery_)
            : render_voltage(page.volts, valid, average_page ? VoltageKind::average :
                         data_page == 0 ? VoltageKind::ac : VoltageKind::dc))
        : render_mains_period(valid ? (previous ? history_.previous : history_.current) : MainsPeriod{}, previous);
    if (next == frame_) return Status::ok;
    const auto result = transport_.submit(next);
    if (result == Status::ok) frame_ = next;
    return result;
}

void OledModule::stop() noexcept {
    transport_.stop_updates();
    if (start_attempted_ && present_) {
        const std::uint8_t off[] = {0x00, 0xae};
        (void)transport_.write(off, sizeof(off));
    }
    running_ = false;
    start_attempted_ = false;
}

void OledModule::deinit() noexcept {
    stop();
    if (acquired_) { transport_.deinit(); }
    acquired_ = initialized_ = present_ = false;
}
}  // namespace bth
