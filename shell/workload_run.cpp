// The remote CPU hunt's workloads on the board, pure half: see workload_run.h.
// This translation unit also holds the generated tables (SHELL_WORKLOADS_DEFINE),
// which on the board land in .qspi_rodata (generated_workloads.h).
#define SHELL_WORKLOADS_DEFINE
#include "workload_run.h"

#include "boot_patch.h"
#include "control/params.hpp"

namespace shell {

bool OverrunGuard::block(uint32_t cb)
{
    if(cb > limit_)
    {
        if(streak_ < n_) ++streak_;
        if(streak_ >= n_) tripped_ = true;
    }
    else
    {
        streak_ = 0;
    }
    return tripped_;
}

void OverrunGuard::clear()
{
    streak_  = 0;
    tripped_ = false;
}

void WorkloadRun::start(int id, uint32_t now_block, uint32_t settle_blocks)
{
    active_ = true;
    void_   = false;
    id_     = id;
    start_  = now_block;
    settle_ = settle_blocks;
    gen_    = gen_ + 1u;
}

void WorkloadRun::stop()
{
    active_ = false;
    void_   = false;
    id_     = -1;
}

void WorkloadRun::on_pot_take()
{
    if(active_) void_ = true;
}

// Unsigned differences, so a run across the block counter's wrap still counts.
bool WorkloadRun::counting(uint32_t now_block) const
{
    return active_ && now_block - start_ >= settle_;
}

bool WorkloadRun::settle_ends(uint32_t now_block) const
{
    return active_ && now_block - start_ == settle_;
}

uint32_t WorkloadRun::counted(uint32_t now_block) const
{
    return counting(now_block) ? now_block - start_ - settle_ : 0u;
}

bool workload_knobs(const WorkloadDef& w, const ControlTable& t, float* knobs)
{
    if(t.count != kWorkloadRowCount) return false;
    // apply_boot_patch_rows() takes int; the table stores int16_t to stay small.
    int rows[kWorkloadRowCount];
    for(int r = 0; r < kWorkloadRowCount; ++r) rows[r] = w.rows_x1000[r];
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
    if(apply_boot_patch_rows(t, rows, kWorkloadRowCount, knobs) < 0) return false;
    knobs[ffctl::REC_A] = w.rec[0] ? 1.0f : 0.0f;
    knobs[ffctl::REC_B] = w.rec[1] ? 1.0f : 0.0f;
    return true;
}

namespace {

// The KeyState bit of the first key with function fn, 0 when the map has none.
uint8_t key_bit(const KeyMap& map, KeyFn fn)
{
    for(int i = 0; i < map.count && i < kMaxKeys; ++i)
        if(map.fn[i] == fn) return static_cast<uint8_t>(1u << i);
    return 0;
}

} // namespace

bool apply_gesture_step(const GestureStep& s, const ControlTable& t, const KeyMap& map,
                        float* knobs)
{
    if(s.row == kGestureRowMod || s.row == kGestureRowShiftMod)
    {
        const uint8_t mod   = key_bit(map, KeyFn::Mod);
        const uint8_t shift = s.row == kGestureRowShiftMod ? key_bit(map, KeyFn::Shift) : 0;
        return apply_key_edges(mod, static_cast<uint8_t>(mod | shift), map, knobs).mod_cleared;
    }
    if(s.row < 0 || s.row >= t.count) return false;
    const ControlEntry& e      = t.entries[s.row];
    const int           target = pot_target(e, knobs);
    if(target >= 0) knobs[target] = knob_from_pot(target, static_cast<float>(s.value_x1000) * 0.001f);
    return false;
}

} // namespace shell
