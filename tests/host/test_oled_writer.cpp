#include "bth/oled/oled.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK_W(c) do { if (!(c)) { std::cerr << "oled writer:" << __LINE__ << ": " #c "\n"; std::exit(1); } } while(false)
namespace {
struct Io final : bth::OledIo {
    std::vector<std::vector<std::uint8_t>> writes;
    unsigned recoveries = 0;
    int fail_at = -1;
    bool offline = false, fail_recovery = false;
    bth::Status write(const std::uint8_t* data, std::size_t size) noexcept override {
        writes.emplace_back(data,data+size);
        return offline || static_cast<int>(writes.size()) == fail_at ? bth::Status::hardware_error : bth::Status::ok;
    }
    bth::Status recover_bus() noexcept override {
        ++recoveries;
        return fail_recovery ? bth::Status::hardware_error : bth::Status::ok;
    }
};
}
void test_oled_writer() {
    using bth::Status;
    bth::OledUpdate frame{bth::render_voltage(230.5,true),0,0x8f};
    Io io;
    bth::OledWriter writer(io);
    CHECK_W(writer.send(frame) == Status::ok && io.writes.size()==3 && io.recoveries==0);
    const auto setup=io.writes[0];
    CHECK_W(setup[0]==0 && setup[1]==0xae);
    for (const auto pair : {std::vector<std::uint8_t>{0xa8,0x1f}, {0xda,0x02}, {0x81,0x8f}, {0x8d,0x14}, {0x20,0x00}})
        CHECK_W(std::search(setup.begin(),setup.end(),pair.begin(),pair.end())!=setup.end());
    CHECK_W(io.writes[1].size()==513 && io.writes[1][0]==0x40);
    CHECK_W(std::equal(frame.frame.begin(),frame.frame.end(),io.writes[1].begin()+1));
    CHECK_W(io.writes[2]==(std::vector<std::uint8_t>{0,0xaf}));
    frame.frame=bth::render_voltage(231,true);
    CHECK_W(writer.send(frame)==Status::ok && io.writes.size()==5);
    CHECK_W(io.writes[3]==(std::vector<std::uint8_t>{0,0x21,0,0x7f,0x22,0,3}));
    ++frame.generation;
    CHECK_W(writer.send(frame)==Status::ok && io.writes.size()==8);
    CHECK_W(io.writes[5]==setup && io.writes[7]==(std::vector<std::uint8_t>{0,0xaf}));
    writer.off(); CHECK_W(io.writes.back()==(std::vector<std::uint8_t>{0,0xae}));

    // A failure in any startup/page transfer is contained; queued frames from
    // that page cannot create a tight retry loop. The next page fully restores it.
    for (int stage=1;stage<=3;++stage) {
        Io broken; broken.fail_at=stage;
        bth::OledWriter retry(broken);
        frame.generation=0;
        CHECK_W(retry.send(frame)==Status::hardware_error);
        CHECK_W(broken.writes.size()==static_cast<std::size_t>(stage));
        for (int i=0;i<100;++i) CHECK_W(retry.send(frame)==Status::hardware_error);
        CHECK_W(broken.writes.size()==static_cast<std::size_t>(stage) && broken.recoveries==0);
        retry.off(); CHECK_W(broken.writes.size()==static_cast<std::size_t>(stage));
        broken.fail_at=-1; frame.generation=1;
        CHECK_W(retry.send(frame)==Status::ok && broken.recoveries==1);
        CHECK_W(broken.writes[stage]==setup && broken.writes.back()==(std::vector<std::uint8_t>{0,0xaf}));
    }
    // Disconnect after a successful frame, then a bus that remains stuck.
    Io disconnected; bth::OledWriter retry(disconnected);
    frame.generation=0; CHECK_W(retry.send(frame)==Status::ok);
    disconnected.offline=true;
    CHECK_W(retry.send(frame)==Status::hardware_error);
    disconnected.fail_recovery=true;
    const auto count=disconnected.writes.size();
    for (std::uint64_t page=1;page<=10;++page) {
        frame.generation=page;
        CHECK_W(retry.send(frame)==Status::hardware_error);
        CHECK_W(disconnected.recoveries==page && disconnected.writes.size()==count);
        CHECK_W(retry.send(frame)==Status::hardware_error && disconnected.recoveries==page);
    }
    disconnected.fail_recovery=disconnected.offline=false; frame.generation=11;
    CHECK_W(retry.send(frame)==Status::ok && disconnected.writes.size()==count+3);
    // The latest overwritten frame still carries the new generation and contrast.
    frame.generation=99; frame.contrast=0x45;
    CHECK_W(retry.send(frame)==Status::ok);
    const auto& changed=disconnected.writes[disconnected.writes.size()-3];
    const std::uint8_t contrast[]={0x81,0x45};
    CHECK_W(std::search(changed.begin(),changed.end(),contrast,contrast+2)!=changed.end());
}
