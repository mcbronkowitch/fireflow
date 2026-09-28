// The scan-check image's run block, as data: which arm each block belongs
// to, which step it selects and which it reads. Spec: docs/superpowers/
// specs/2026-09-28-coupon-panel-scan-design.md section 4.
#include <doctest/doctest.h>
#include <initializer_list>
#include "../shell/scan_check_plan.h"
#include "../shell/mux_plan.h"

using shell::ScanArm;

TEST_CASE("scan check plan: the numbers the spec prints") {
    CHECK(shell::kCheckSteps == shell::scan_steps(shell::kCouponChain));
    CHECK(shell::kArmSBlocks == 1537);
    CHECK(shell::kArmPBlocks == 1656);
    CHECK(shell::kArmZeroBlocks == 1536);
    CHECK(shell::kRunBlocks == 4729);
}

TEST_CASE("scan check plan: every arm reads every step exactly 64 times") {
    int reads[shell::kArmCount][shell::kCheckSteps] = {};
    for(int b = 0; b < shell::kRunBlocks; ++b)
    {
        const shell::CheckSlot s = shell::check_slot(b);
        if(s.read_step < 0) continue;
        REQUIRE(s.read_step < shell::kCheckSteps);
        ++reads[static_cast<int>(s.arm)][s.read_step];
    }
    for(int a = 0; a < shell::kArmCount; ++a)
        for(int st = 0; st < shell::kCheckSteps; ++st)
            CHECK(reads[a][st] == shell::kCheckReads);
}

TEST_CASE("scan check plan: arm S reads the step it selected one block earlier") {
    const shell::CheckSlot first = shell::check_slot(0);
    CHECK(first.arm == ScanArm::S);
    CHECK(first.select_step == 0);
    CHECK(first.read_step == -1);
    int selected = first.select_step;
    for(int b = 1; b < shell::kArmSBlocks; ++b)
    {
        const shell::CheckSlot s = shell::check_slot(b);
        CHECK(s.arm == ScanArm::S);
        CHECK_FALSE(s.read_after_select);
        CHECK(s.read_step == selected);
        selected = s.select_step;
    }
    CHECK(shell::check_slot(shell::kArmSBlocks - 1).select_step == -1);
}

TEST_CASE("scan check plan: arm P parks each step and reads it from block 5") {
    for(int st = 0; st < shell::kCheckSteps; ++st)
    {
        const int base = shell::kArmSBlocks + st * shell::kParkHold;
        const shell::CheckSlot sel = shell::check_slot(base);
        CHECK(sel.arm == ScanArm::P);
        CHECK(sel.select_step == st);
        CHECK(sel.read_step == -1);
        for(int k = 1; k < shell::kParkHold; ++k)
        {
            const shell::CheckSlot s = shell::check_slot(base + k);
            CHECK(s.arm == ScanArm::P);
            CHECK(s.select_step == -1);
            CHECK(s.read_step == (k >= shell::kParkFirstRead ? st : -1));
        }
    }
}

TEST_CASE("scan check plan: arm 0 reads in the same block it selects") {
    const int base = shell::kArmSBlocks + shell::kArmPBlocks;
    for(int b = base; b < shell::kRunBlocks; ++b)
    {
        const shell::CheckSlot s = shell::check_slot(b);
        CHECK(s.arm == ScanArm::Zero);
        CHECK(s.read_after_select);
        CHECK(s.select_step == (b - base) % shell::kCheckSteps);
        CHECK(s.read_step == s.select_step);
    }
}

TEST_CASE("scan check plan: out of range does nothing") {
    for(int b : {-1, shell::kRunBlocks, shell::kRunBlocks + 100})
    {
        const shell::CheckSlot s = shell::check_slot(b);
        CHECK(s.select_step == -1);
        CHECK(s.read_step == -1);
    }
}

TEST_CASE("scan check plan: arm letters") {
    CHECK(shell::arm_letter(ScanArm::S) == 'S');
    CHECK(shell::arm_letter(ScanArm::P) == 'P');
    CHECK(shell::arm_letter(ScanArm::Zero) == '0');
}
