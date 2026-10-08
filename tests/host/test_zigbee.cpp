#include "bth/zigbee/control.hpp"
#include <cstdlib>
#include <iostream>

#define CHECK_Z(c) do { if (!(c)) { std::cerr << "zigbee:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while(false)
void test_zigbee() {
    bth::ZigbeeJoinWindow window;
    CHECK_Z(!window.active(0) && !window.due(0) && window.remaining(0) == 0);
    const std::uint64_t base = 1ULL << 32;
    window.open(base);
    CHECK_Z(window.due(base) && window.remaining(base) == 120);
    window.started();
    CHECK_Z(!window.due(base+1000));
    window.finished(base+1000, false);
    CHECK_Z(!window.due(base+5999) && window.due(base+6000));
    window.started();
    window.open(base+7000); // Repeated request must not overlap an active scan.
    CHECK_Z(!window.due(base+7000));
    CHECK_Z(window.remaining(base+126999) == 1 && !window.needs_cancel(base+126999));
    CHECK_Z(window.remaining(base+127000) == 0 && window.needs_cancel(base+127000));
    window.finished(base+127001, false);
    CHECK_Z(!window.needs_cancel(base+127001) && !window.due(base+200000));
    window.open(base+200000);
    window.started();
    window.finished(base+201000, true);
    CHECK_Z(!window.active(base+201000) && !window.needs_cancel(base+201000));
    window.open(base+300000); window.started(); window.close();
    CHECK_Z(window.needs_cancel(base+300001));
    window = {}; // Lifecycle restart clears a pending scan and its deadline.
    CHECK_Z(!window.active(0) && !window.needs_cancel(0));
}
