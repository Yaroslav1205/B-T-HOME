#include "bth/outages/outages.hpp"
#include <cstdio>

namespace bth {
namespace {
bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\r' || s.front() == '\n' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}
bool time_value(std::string_view s, unsigned& value) noexcept {
    if (s.size() < 5 || !digit(s[0]) || !digit(s[1]) || s[2] != ':' || !digit(s[3]) || !digit(s[4])) return false;
    const unsigned h = (s[0]-'0')*10 + s[1]-'0', m = (s[3]-'0')*10 + s[4]-'0';
    if (h > 24 || m > 59 || (h == 24 && m != 0)) return false;
    value = h*60+m;
    return true;
}
unsigned message_date(std::string_view text) noexcept {
    text = text.substr(0, text.find("Група "));
    for (std::size_t i = 0; i + 10 <= text.size(); ++i) {
        const auto s = text.substr(i, 10);
        if (s[2] != '.' || s[5] != '.') continue;
        bool valid = true;
        for (unsigned j : {0U,1U,3U,4U,6U,7U,8U,9U}) valid = valid && digit(s[j]);
        if (!valid) continue;
        const unsigned d=(s[0]-'0')*10+s[1]-'0', m=(s[3]-'0')*10+s[4]-'0';
        const unsigned y=(s[6]-'0')*1000+(s[7]-'0')*100+(s[8]-'0')*10+s[9]-'0';
        if (d >= 1 && d <= 31 && m >= 1 && m <= 12) return y*10000+m*100+d;
    }
    return 0;
}
bool schedule_message(std::string_view text) noexcept {
    return text.find("графік заживлення") != text.npos ||
        (text.find("Електропостачання без обмежень") != text.npos && text.find("всіх груп") != text.npos);
}
}
OutageSchedule parse_outage_message(std::string_view text, unsigned group, unsigned date) noexcept {
    OutageSchedule result{};
    result.group = group; result.date = date;
    if (!group || group > outage_max_group || !date || message_date(text) != date || !schedule_message(text)) return result;
    if (text.find("Група ") == text.npos && text.find("всіх груп") != text.npos && text.find("Електропостачання без обмежень") != text.npos) {
        result.valid = true; return result;
    }
    char marker[32]{};
    std::snprintf(marker, sizeof(marker), "Група %u", group);
    auto pos = text.find(marker);
    if (pos == text.npos) return result;
    pos += std::char_traits<char>::length(marker);
    if (pos < text.size() && digit(text[pos])) return result;
    text.remove_prefix(pos);
    auto end = text.find("Група ");
    for (const auto stop : {"☝", "❗", "Дізнатись"}) {
        const auto p = text.find(stop);
        if (p < end) end = p;
    }
    text = trim(text.substr(0, end));
    const auto bulb = text.find("💡");
    if (bulb != text.npos) text = trim(text.substr(0, bulb));
    if (text == "Електропостачання без обмежень") { result.valid = true; return result; }
    unsigned previous = 0, supply_count = 0;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = trim(text.substr(0, newline));
        if (newline == text.npos) text = {}; else text.remove_prefix(newline+1);
        if (line.empty()) continue;
        unsigned begin = 0, finish = 1440;
        if (line.substr(0, 3) == "з ") {
            line = trim(line.substr(3));
            if (line.size() != 5 || !time_value(line, begin)) return result;
        } else {
            if (!time_value(line, begin)) return result;
            line = trim(line.substr(5));
            if (line.substr(0, 1) == "-") line.remove_prefix(1);
            else if (line.substr(0, 3) == "–" || line.substr(0, 3) == "—") line.remove_prefix(3);
            else return result;
            line = trim(line);
            if (line.size() != 5 || !time_value(line, finish)) return result;
        }
        if (begin < previous || begin >= finish || ++supply_count > 15) return result;
        if (begin > previous) result.intervals[result.count++] = {previous, begin};
        previous = finish;
    }
    if (!supply_count) return result;
    if (previous < 1440) result.intervals[result.count++] = {previous, 1440};
    result.valid = true;
    return result;
}
void OutageFeed::append(char c) noexcept {
    if (used_ < message_.size()) message_[used_++] = c; else overflow_ = true;
}
void OutageFeed::tag() noexcept {
    const std::string_view t(tag_.data(), tag_used_);
    if (!capturing_ && t.substr(0, 4) == "<div" && t.find("class=\"tgme_widget_message_text ") != t.npos) {
        capturing_ = true; used_ = 0; overflow_ = entity_ = false;
    } else if (capturing_ && t == "</div>") {
        const std::string_view message(message_.data(), used_);
        if (message_date(message) == date_ && schedule_message(message)) {
            result_ = overflow_ ? OutageSchedule{} : parse_outage_message(message, group_, date_);
        }
        capturing_ = false;
    } else if (capturing_ && t.substr(0, 3) == "<br") append('\n');
}
bool OutageFeed::feed(const char* bytes, std::size_t size) noexcept {
    if (failed_ || size > 262144 - total_) { failed_ = true; return false; }
    total_ += size;
    for (std::size_t i = 0; i < size; ++i) {
        const char c = bytes[i];
        if (c == '<') { in_tag_ = true; tag_used_ = 0; }
        if (in_tag_) {
            if (tag_used_ < tag_.size()) tag_[tag_used_++] = c;
            else { failed_ = true; return false; }
            if (c == '>') { in_tag_ = false; tag(); }
        } else if (capturing_) {
            if (c == '&') { entity_ = true; append(' '); }
            else if (entity_) { if (c == ';') entity_ = false; }
            else append(c);
        }
    }
    return true;
}
OutageDisplay outage_display(const OutageSchedule& s, unsigned group, unsigned date,
                            unsigned minute, bool connected, std::uint64_t age) noexcept {
    if (!group || group > outage_max_group) return {};
    OutageDisplay d{OutageDisplayState::unavailable, group, 0, 0};
    if (!s.valid || s.group != group || s.date != date || minute >= 1440) return d;
    if (!connected || age >= outage_expiry_ms) { d.state = OutageDisplayState::stale; return d; }
    d.state = OutageDisplayState::clear;
    for (unsigned i = 0; i < s.count && i < s.intervals.size(); ++i) {
        if (minute < s.intervals[i].end) {
            d.state = OutageDisplayState::interval;
            d.begin = s.intervals[i].begin; d.end = s.intervals[i].end;
            break;
        }
    }
    return d;
}
bool OutagePoll::due(unsigned group, bool connected, std::uint64_t now) noexcept {
    const bool changed = group_ != group || connected_ != connected;
    group_ = group; connected_ = connected;
    if (!group || group > outage_max_group || !connected) { attempted_ = false; return false; }
    if (!attempted_ || changed || now < last_ || now-last_ >= outage_poll_ms) {
        attempted_ = true; last_ = now; return true;
    }
    return false;
}
}  // namespace bth
