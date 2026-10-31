#pragma once
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
    bool   is_empty() const { return _size == 0; }
    bool   valid() const { return _buffer != nullptr && _buffer_size > 0; }
    size_t capacity() const { return _buffer_size; }
    Frame* raw() const { return _buffer; }
    // Where the next write() lands. The slice detector (SamplerEngine::
    // process_in) snapshots this before write() to know which frame the
    // written value landed on.
    size_t write_head() const { return _write_head; }

#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SAMPLER_I16
    // CPU round 2, candidate G4 (class 2): the content as int16 stereo frames
    // (4 B), not float ones (8 B), so a grain's read moves half the bytes. The
    // frames sit at the front of the same injected arena -- the host still
    // hands over Frame* and the length in frames, and the length stays what
    // it was; the arena's back half is unused. Stored as round(x * 32 768),
    // saturated to the int16 range (non-finite stores 0); read as q / 32 768.
    // With SHELL_LEAN_SAMPLER_I16=1 (SPKY_LEAN_SAMPLER_I16) this is the only
    // storage; in the measurement image toggle bit 21 picks it per block
    // (set_probe_word). raw() and sample_data() are then the arena, not the
    // content: read the content through frame_at().
    struct Frame16 { int16_t l; int16_t r; };

    static int16_t to_i16(float x) {
        float s = x * 32768.f;
        // One test catches the range and NaN (every NaN comparison is false).
        if (!(s > -32768.f && s < 32767.f)) s = s > 0.f ? 32767.f : (s < 0.f ? -32768.f : 0.f);
        return static_cast<int16_t>(static_cast<int32_t>(s + (s < 0.f ? -0.5f : 0.5f)));
    }

    // Content frame i through the storage, and the store the load path uses
    // (no state change: set_rec_size() declares the content afterwards).
    void frame_at(size_t i, float& l, float& r) const {
        if (_is_i16()) {
            const Frame16 q = _q()[i];
            l = static_cast<float>(q.l) * (1.f / 32768.f);
            r = static_cast<float>(q.r) * (1.f / 32768.f);
        } else {
            l = _buffer[i].l;
            r = _buffer[i].r;
        }
    }
    void set_frame(size_t i, float l, float r) {
        if (_is_i16()) _q()[i] = Frame16{ to_i16(l), to_i16(r) };
        else           _buffer[i] = Frame{ l, r };
    }
#endif
#if defined(SPKY_PROBE_TOGGLES)
    // Measurement image only (util/probe_toggles.h, bit 21 LEAN_SAMPLER_I16):
    // SamplerEngine::set_probe_word hands the block's word down, once per
    // block. When the bit differs from the storage, the whole arena is
    // converted in place, at the block boundary; a slow block, once per flip.
    void set_probe_word(uint32_t w);
    bool storage_i16() const { return _i16; }
#endif

private:
    enum class State : uint8_t { idle, fadein, sustain, fadeout };

#if defined(SPKY_PROBE_TOGGLES)
    bool _is_i16() const { return _i16; }
#elif SPKY_LEAN_SAMPLER_I16
    static constexpr bool _is_i16() { return true; }
#endif
#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SAMPLER_I16
    Frame16* _q() const { return reinterpret_cast<Frame16*>(_buffer); }
#endif

    SoftSwitch _cut;
    Frame*  _buffer      = nullptr;
    size_t  _buffer_size = 0;
    float   _feedback    = 0.f;   // linear factor; set by set_feedback()
    size_t  _size        = 0;     // recorded content length, in frames
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
#if defined(SPKY_PROBE_TOGGLES)
    bool    _i16 = false;   // bit 21: the arena holds Frame16 (G4)
#endif
};

}  // namespace spky
