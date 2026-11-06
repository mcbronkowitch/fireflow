#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "fx/fx_util.h"   // SoftSwitch, hann_value_at

namespace spky {

// The record buffer of the M5 texture deck: a port of the original firmware's
// src/core/buffer.* (proven on hardware) with the libDaisy-adjacent pieces
// stripped and six deliberate changes, all listed in the M5a plan, Task 1.
//
// Memory is INJECTED (engine "no heap" contract): the host owns the frames,
// this class owns only the state machine. An un-init'ed buffer is inert.
//
// The read is const and holds no cursor -- up to kGrains readers call
// read_linear() concurrently within one sample, so a shared read head (which
// the original has) would be shared mutable state between them.
class SampleBuffer {
public:
    struct Frame { float l; float r; };

    SampleBuffer() = default;
    SampleBuffer(const SampleBuffer&) = delete;
    SampleBuffer& operator=(const SampleBuffer&) = delete;

    // buf may be nullptr: the buffer then reports !valid() and every
    // operation is a no-op. Hosts that cannot spare the memory rely on this.
    // sample_rate is forwarded to the overdub SoftSwitch -- without it the
    // switch's fade coefficient stays 1.0 and its Hann lookup runs off the
    // end of the table on the first overdub.
    void init(Frame* buf, size_t length, float sample_rate);

    // Linear-interpolated stereo read at a fractional frame index. Folds
    // out-of-range positions back into [0, rec_size). Silent when empty.
    // Defined below, in this header (G3, round 2), so the grain loop reads
    // inline instead of through a call. always_inline, as WtOsc::process
    // (synth/wt_osc.h): the one call site must not be left to the heuristics.
    __attribute__((always_inline)) inline
    void read_linear(float frame, float& out0, float& out1) const;

    // --- transport ---
    void set_recording(bool on);
    bool is_recording() const { return _state != State::idle; }
    bool is_overdubbing() const { return _cut.is_on() && is_recording(); }
    void set_feedback(float knob);    // 0..1 knob -> -60..0 dB linear factor
    void write(float in0, float in1); // call once per sample, always
    void cut();                       // lock the loop length at the fill
    void clear();
    void set_rec_size(size_t frames); // load path + tests: declare content

    // --- queries ---
    // The linear feedback coefficient. Exposed for tests: above unity it is
    // the thing the bloom's boundedness is asserted against.
    float  feedback() const { return _feedback; }
    float  fill() const { return _buffer_size ? float(_size) / float(_buffer_size) : 0.f; }
    size_t rec_size() const { return _size; }
    // The written-frame watermark (spec 2026-11-06-sampler-takes-on-sd §4.7):
    // frames at or past it count as silence, whatever the memory holds. The
    // overdub read in write() never sees past it, and the reads never see
    // past rec_size() <= written(). So the host's memory need not be zeroed
    // (Daisy SDRAM is not), and clear() needs no memset. Follows the write
    // head up; clear() sets it to 0, set_rec_size() to the declared length.
    size_t written() const { return _written; }
    // Counts the state machine's idle edges: +1 when a take or overdub
    // leaves idle, +1 when its fade-out reaches idle. Between two equal
    // readings with is_recording() false the content did not change. clear()
    // does not count (init() calls it); SamplerEngine adds its own bumps.
    uint32_t gen() const { return _gen; }
    bool   is_empty() const { return _size == 0; }
    bool   valid() const { return _buffer != nullptr && _buffer_size > 0; }
    size_t capacity() const { return _buffer_size; }
    Frame* raw() const { return _buffer; }
    // Where the next write() lands. The slice detector (SamplerEngine::
    // process_in) snapshots this before write() to know which frame the
    // written value landed on.
    size_t write_head() const { return _write_head; }

private:
    enum class State : uint8_t { idle, fadein, sustain, fadeout };

    SoftSwitch _cut;
    Frame*  _buffer      = nullptr;
    size_t  _buffer_size = 0;
    float   _feedback    = 0.f;   // linear factor; set by set_feedback()
    size_t  _size        = 0;     // recorded content length, in frames
    size_t  _written     = 0;     // watermark: frames [0, _written) hold content
    uint32_t _gen        = 0;     // idle edges, see gen()
    size_t  _write_head  = 0;
    size_t  _fade_ctr    = 0;
    State   _state       = State::idle;
    // Stashed _write_head, taken the instant a take enters fadeout -- BEFORE
    // the free-run branch below (set_recording) rewinds _write_head to 0 for
    // the crossfade into the loop point. A punch-in that interrupts the
    // fade-out restores this instead of resuming at the rewound 0, so the
    // second take continues where the first stopped rather than splicing
    // over its own beginning (F-08 follow-up, review 2026-07-22). When the
    // rewind does not happen (already looping, _cut.is_on()) this simply
    // equals _write_head at the moment of the stop, so restoring it is a
    // no-op there.
    size_t  _fadeout_resume_head = 0;
};

inline void SampleBuffer::read_linear(float frame, float& out0, float& out1) const {
    // Empty or un-init'ed: silence. The original divides by zero on the
    // first branch and spins forever on the second (plan Task 1, change 5).
    if (_size == 0 || _buffer == nullptr) { out0 = 0.f; out1 = 0.f; return; }

    // NaN or an absurd magnitude: silence, same contract. Two compares, and
    // NaN fails both (every NaN comparison is false), so this closes the
    // static_cast<size_t>(NaN) undefined behaviour below AND bounds the fold
    // loops for any finite input. Deliberately NOT fmodf: eight grains x two
    // parts x 48 kHz is ~768k fmodf/s, which is real CPU on the M6 hardware
    // for a case that cannot currently occur.
    if (!(frame > -1e9f && frame < 1e9f)) { out0 = 0.f; out1 = 0.f; return; }

    const float fsz = static_cast<float>(_size);
    // O(1) fold, replacing the original's `frame %= _size` without an
    // integer division. NOT a subtract loop: that would be O(frame/_size),
    // and a grain holding a position latched against a longer previous
    // content length (set_rec_size shrinks it, or a load lands mid-cloud)
    // would then spin hundreds of thousands of times on the audio thread.
    // std::floor is a single instruction on both targets -- roundsd on x86,
    // VRINTM on the Daisy's Cortex-M7 FPv5 -- not a libm call, and cheaper
    // than the loop even in the common case.
    //
    // A frame inside (0, 2 fsz) skips the divide and the floor (G1, round 2,
    // bit-identical). Below fsz, frame / fsz is correctly rounded and
    // monotone, and even the float below fsz divides to at most 1 - 2^-24, so
    // floor() is 0 and the fold would hand back the same frame. From fsz up to
    // 2 fsz it is 1 -- the float below 2 fsz divides to at most 2 - 2^-23 --
    // and the fold's `frame - fsz * 1.f` is frame - fsz, exact there. Past the
    // end of the recording is where a long grain or a high pitch lands, so it
    // has its own arm, and the choice is a select on the sign of frame - fsz,
    // not a branch: the pattern can alternate from read to read, and a branch
    // on frame >= fsz (written as a ?: on the subtract or on its operand)
    // came out as a conditional branch on the Daisy toolchain, this form as a
    // conditional move (vcmpe, it pl, vmovpl).
    //
    // Zero is left to the fold on purpose: -0.f comes out of it as +0.f, and
    // a bare >= 0 test would pass -0.f on with its sign, which flips the sign
    // of a zero in frac.
    const bool inside = frame > 0.f && frame < 2.f * fsz;
    if (inside) {
        const float d = frame - fsz;
        frame = std::signbit(d) ? frame : d;
    } else {
        frame -= fsz * std::floor(frame / fsz);
        // BEIDE Kanten hier, vor i0 und frac, und nicht nur i0 spaeter: bei
        // grossem fsz rundet fsz - epsilon in float32 auf exakt fsz, was ein
        // knapp negatives frame genau hierher bringt. Wurde nur i0 korrigiert,
        // blieb frac = fsz stehen und die Interpolation lief mit dem Faktor der
        // Puffergroesse -- an einem Puffer mit nur +-0.3 Inhalt gemessene
        // Ausgaenge von 14 400 (24 000 Frames) bis 1 209 600 (2 016 000 Frames).
        // Erreichbar ueber REVERSE-Grains, sobald _start + _off unter 0 laeuft
        // (grain.h:111). frame und i0 muessen dieselbe Zahl beschreiben.
        if (!(frame >= 0.f) || frame >= fsz) frame = 0.f;
    }

    size_t i0 = static_cast<size_t>(frame);
    if (i0 >= _size) i0 = 0;                     // Guertel zum Hosentraeger
    size_t i1 = i0 + 1;
    if (i1 >= _size) i1 = 0;

    const float frac = frame - static_cast<float>(i0);
    const Frame a = _buffer[i0];
    const Frame b = _buffer[i1];
    out0 = a.l + frac * (b.l - a.l);
    out1 = a.r + frac * (b.r - a.r);
}

}  // namespace spky
