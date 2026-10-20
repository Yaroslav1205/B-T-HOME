#include "bth/outages/outages.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <iterator>
#define CHECK(c) do { if (!(c)) { std::cerr << "outages:" << __LINE__ << ": " #c "\n"; std::exit(1); } } while(false)
int main(int argc, char** argv) {
    using namespace bth;
    const std::string header = "Орієнтовний графік заживлення для груп споживачів • 18.10.2026\n";
    const std::string supply = header + "Група 1\nЕлектропостачання без обмежень\n💡 Група 2\n00:00 - 08:00\n10:30 - 17:00\nз 19:30\n☝Дізнатись номер групи\n09:00 - 11:00";
    auto schedule = parse_outage_message(supply,2,20261018);
    CHECK(schedule.valid && schedule.count==2);
    CHECK(schedule.intervals[0].begin==480 && schedule.intervals[0].end==630);
    CHECK(schedule.intervals[1].begin==1020 && schedule.intervals[1].end==1170);
    CHECK(!parse_outage_message(supply,2,20261019).valid);
    CHECK(!parse_outage_message(supply,0,20261018).valid);
    CHECK(!parse_outage_message(supply,13,20261018).valid);
    CHECK(parse_outage_message(supply,1,20261018).valid);
    CHECK(parse_outage_message(supply,1,20261018).count==0);
    CHECK(!parse_outage_message(header+"Група 12\nз 10:00",1,20261018).valid);
    for (const auto bad : {"08:00 - 07:00", "00:00 - 24:30", "00:00 - 25:00", "з 24:00", "невідомо", "00:00 - 12:00\n11:00 - 18:00", "00:00 - 12:00\nнеповний графік"})
        CHECK(!parse_outage_message(header+"Група 2\n"+bad,2,20261018).valid);
    auto late=parse_outage_message(header+"Група 2\n08:00 – 12:00",2,20261018);
    CHECK(late.valid && late.count==2 && late.intervals[0].begin==0 && late.intervals[1].end==1440);
    const std::string clear="На 18.10.2026\nЕлектропостачання без обмежень для всіх груп споживачів!";
    CHECK(parse_outage_message(clear,12,20261018).valid);
    const auto html=[](const std::string& s) { return "<div class=\"tgme_widget_message_text js-message_text\">"+s+"</div>"; };
    const std::string page=html(header+"Група 2<br/>00:00 - 12:00<br/>з 14:00")+html(clear);
    for (std::size_t chunk : {1U,7U,1024U}) {
        OutageFeed parser(2,20261018);
        for (std::size_t i=0;i<page.size();i+=chunk) CHECK(parser.feed(page.data()+i,std::min(chunk,page.size()-i)));
        CHECK(parser.complete() && parser.result().valid && parser.result().count==0);
    }
    OutageFeed broken(2,20261018);
    auto revised=page+html(header+"Група 2<br/>невідомо");
    CHECK(broken.feed(revised.data(),revised.size()) && !broken.result().valid);
    OutageFeed truncated(2,20261018);
    CHECK(truncated.feed(page.data(),page.size()-2) && !truncated.complete());
    OutageFeed bounded(2,20261018);
    std::string huge(262145,' ');
    CHECK(!bounded.feed(huge.data(),huge.size()) && !bounded.complete());
    CHECK(outage_display(schedule,0,20261018,0,true,0).state==OutageDisplayState::hidden);
    CHECK(outage_display(schedule,3,20261018,0,true,0).state==OutageDisplayState::unavailable);
    CHECK(outage_display(schedule,2,20261019,0,true,0).state==OutageDisplayState::unavailable);
    CHECK(outage_display(schedule,2,20261018,0,false,0).state==OutageDisplayState::stale);
    CHECK(outage_display(schedule,2,20261018,0,true,outage_expiry_ms).state==OutageDisplayState::stale);
    CHECK(outage_display(schedule,2,20261018,480,true,0).end==630);
    CHECK(outage_display(schedule,2,20261018,630,true,0).begin==1020);
    CHECK(outage_display(schedule,2,20261018,1170,true,0).state==OutageDisplayState::clear);
    OutagePoll poll;
    CHECK(!poll.due(2,false,0)); CHECK(poll.due(2,true,1));
    CHECK(!poll.due(2,true,300000)); CHECK(poll.due(2,true,300001));
    CHECK(poll.due(3,true,300002)); CHECK(!poll.due(0,true,300003));
    CHECK(poll.due(3,true,300004)); CHECK(!poll.due(3,false,300005));
    CHECK(poll.due(3,true,300006));
    // Optional integration check against a downloaded public page, never needed by CI.
    if (argc>1) {
        std::ifstream f(argv[1],std::ios::binary);
        const std::string live((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
        CHECK(!live.empty()); OutageFeed parser(2,20261018);
        for(std::size_t i=0;i<live.size();i+=511) CHECK(parser.feed(live.data()+i,std::min(std::size_t{511},live.size()-i)));
        CHECK(parser.complete() && parser.result().valid);
        std::cout << "PASS: downloaded official feed, group 2\n";
    }
    std::cout << "PASS: outage parsing, bounds, revisions, dates, freshness and polling\n";
}
