#pragma once
#include <cstddef>
#include <cstdint>
#include "fx/bbd.h"
#include "parts/bbd_music.h"
#include "parts/engine_iface.h"
#include "util/fast_tanh.h"

namespace spky {

// The bucket-brigade delay as a part engine. Two BbdEcho, one per channel: a
// part engine has no dry path -- it IS the signal path -- and its input is
// stereo at every boundary.
//
// MIX is on LANE_LEVEL, not on a knob. Instrument::process composes
// l = al*ga + bl*gb, so the audio input reaches the output nowhere else: on a
// wet/dry engine the mix IS the level, and putting it there lets the plane open
// and close the echo rhythmically.
class BbdEngine : public IPartEngine {
public:
    // Cells per line. Same sizing as Flux's, which is kMaxStages/2 -- a
    // two-phase BBD stores one sample per TWO stages.
    static constexpr size_t kCells = bbd_tuning::kMaxStages / 2;

    void init(float sample_rate) override;
    // The host owns the memory, as it does for Flux. Two lines per deck at
    // kCells floats = 64 KB, in SDRAM on the Seed. nullptr -> the deck is
    // silent, which is what a host that forgot to allocate deserves.
    void init_buffers(float* l, float* r, size_t cells);

    void set_targets(const float* targets, float tune) override;
    void trigger(float /*pitch_norm*/) override {}
    void process(float& outL, float& outR) override;
    void process_in(float inL, float inR) override;
    bool consumes_input() const override { return true; }
    void set_cycle(float seconds) override;

    // Clears both lines, both companders and both feedback states. Part has no
    // swap-away notification, so this is called on activation instead.
    void reset();

    // Observers (spec 9): a clamp that is invisible reads as a broken knob.
    float clock_hz() const { return _f_clk; }
    int   stages() const { return _stages; }
    int   div_index() const { return _ladder.index(); }
    bool  time_clamped() const { return _win.time_clamped; }
    bool  scale_truncated() const { return _win.scale_truncated; }

private:
    void _recompute();

    BbdEcho _l, _r;
    bbd_music::DivLadder _ladder;
    bbd_music::Window _win;
    float _sr = 48000.f;
    float _cycle = 1.f;
    float _in_l = 0.f, _in_r = 0.f;
    float _mix = 0.f;
    float _pitch = 0.5f;
    float _f_clk = 4000.f;
    int   _stages = 8192;
    bool  _buf_ok = false;
};

}  // namespace spky
