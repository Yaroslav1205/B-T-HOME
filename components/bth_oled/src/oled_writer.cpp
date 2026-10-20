#include "bth/oled/oled.hpp"
#include <algorithm>

namespace bth {
Status OledWriter::send(const OledUpdate& update) noexcept {
    const bool refresh = !attempted_ || generation_ != update.generation;
    if (status_ != Status::ok && !refresh) return status_;
    // Failed frames must wait for the next page, including already queued frames.
    generation_ = update.generation;
    attempted_ = true;
    if (status_ == Status::hardware_error) {
        status_ = io_.recover_bus();
        if (status_ != Status::ok) return status_;
    } else if (status_ != Status::ok) {
        return status_;
    }
    if (refresh) {
        // Display off, full configuration, all pixels, then on. Restore addressing,
        // contrast, orientation, charge pump and normal (non-scrolling) display.
        const std::uint8_t setup[] = {
            0x00, 0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
            0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x02,
            0x81, update.contrast, 0xd9, 0xf1, 0xdb, 0x20, 0xa4, 0xa6,
            0x2e, 0x23, 0x00, 0xd6, 0x00, 0x21, 0x00, 0x7f, 0x22, 0x00, 0x03};
        status_ = io_.write(setup, sizeof(setup));
    } else {
        const std::uint8_t window[] = {0x00, 0x21, 0x00, 0x7f, 0x22, 0x00, 0x03};
        status_ = io_.write(window, sizeof(window));
    }
    if (status_ != Status::ok) return status_;
    std::array<std::uint8_t, 513> data{};
    data[0] = 0x40;
    std::copy(update.frame.begin(), update.frame.end(), data.begin()+1);
    status_ = io_.write(data.data(), data.size());
    if (status_ == Status::ok && refresh) {
        const std::uint8_t on[] = {0x00, 0xaf};
        status_ = io_.write(on, sizeof(on));
    }
    return status_;
}
void OledWriter::off() noexcept {
    // Do not attempt recovery during shutdown.
    if (attempted_ && status_ == Status::ok) {
        const std::uint8_t off[] = {0x00, 0xae};
        (void)io_.write(off, sizeof(off));
    }
}
}  // namespace bth
