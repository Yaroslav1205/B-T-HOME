#pragma once
#include "bth/core/module.hpp"
#include <array>
#include <string_view>

namespace bth {
constexpr unsigned outage_default_group = 2;
constexpr unsigned outage_max_group = 12;
constexpr std::uint64_t outage_poll_ms = 300000;
constexpr std::uint64_t outage_expiry_ms = 900000;
class OutageSettings {
public:
    virtual ~OutageSettings() = default;
    virtual std::uint32_t group() const noexcept = 0;
    virtual Status update_group(std::uint32_t group) noexcept = 0;
};
struct OutageInterval { unsigned begin = 0, end = 0; };
struct OutageSchedule {
    unsigned date = 0, group = 0, count = 0;
    bool valid = false;
    std::array<OutageInterval, 16> intervals{};
};
OutageSchedule parse_outage_message(std::string_view text, unsigned group, unsigned date) noexcept;
// Bounded streaming HTML reader: retains one message, never the whole channel page.
class OutageFeed {
public:
    OutageFeed(unsigned group, unsigned date) noexcept : group_(group), date_(date) {}
    bool feed(const char* bytes, std::size_t size) noexcept;
    bool complete() const noexcept { return !failed_ && !capturing_ && !in_tag_; }
    OutageSchedule result() const noexcept { return result_; }
private:
    void tag() noexcept;
    void append(char c) noexcept;
    unsigned group_, date_;
    OutageSchedule result_{};
    std::array<char, 8192> message_{};
    std::array<char, 1024> tag_{};
    std::size_t used_ = 0, tag_used_ = 0, total_ = 0;
    bool capturing_ = false, in_tag_ = false, entity_ = false, overflow_ = false, failed_ = false;
};
enum class OutageDisplayState { hidden, unavailable, stale, clear, interval };
struct OutageDisplay {
    OutageDisplayState state = OutageDisplayState::hidden;
    unsigned group = 0, begin = 0, end = 0;
};
OutageDisplay outage_display(const OutageSchedule& schedule, unsigned group, unsigned date,
                            unsigned minute, bool connected, std::uint64_t age_ms) noexcept;
class OutagePoll {
public:
    bool due(unsigned group, bool connected, std::uint64_t now) noexcept;
private:
    unsigned group_ = 0;
    bool connected_ = false, attempted_ = false;
    std::uint64_t last_ = 0;
};
}  // namespace bth
