#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>
#include "instrument.h"
#include "render/scenario.h"
#include "shared/wav_reader.h"
#include "shared/wav_writer.h"

using namespace spky;

// FX memory, injected per the engine's no-heap contract (FxMem pattern).
static float s_echo[spky::PART_COUNT][2][spky::Flux::kMaxSamples];
// The BBD part engine's two lines per deck, same static idiom as the echo
// buffer above: 32 KB per line, 128 KB in total.
static float s_bbd[spky::PART_COUNT][2][spky::BbdEngine::kCells];
static spky::AmbientReverb s_reverb;

// M5 texture deck: one 42 s stereo record buffer per part (spec 2026-07-18
// sampler-texture-deck-design.md: "Sizing follows the original: 42 s stereo
// per part... Desktop: heap"). Left unallocated, FxMem::sampler_buf stays
// nullptr and every part's sampler runs silent -- the render host would
// "succeed" while recording nothing, which is exactly the silent-failure
// shape this task is supposed to avoid. Heap (not the echo idiom's static
// arrays): two of these are ~32 MB, too large to want living in the binary.
static constexpr double kSamplerBufferSeconds = 42.0;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: render <scenario.json> [out.wav] [mods.csv]\n");
        return 1;
    }
    std::string scen_path = argv[1];
    std::string wav_path  = argc > 2 ? argv[2] : "out.wav";
    std::string csv_path  = argc > 3 ? argv[3] : "mods.csv";

    Scenario scen;
    std::string err;
    if (!load_scenario(scen_path, scen, err)) {
        std::printf("scenario error: %s\n", err.c_str());
        return 2;
    }

    Instrument inst;
    FxMem fx_mem;
    for (int p = 0; p < PART_COUNT; ++p) {
        fx_mem.echo[p][0] = s_echo[p][0];
        fx_mem.echo[p][1] = s_echo[p][1];
        fx_mem.bbd[p][0] = s_bbd[p][0];
        fx_mem.bbd[p][1] = s_bbd[p][1];
    }
    fx_mem.reverb = &s_reverb;

    const size_t sampler_frames =
        static_cast<size_t>(kSamplerBufferSeconds * scen.sample_rate);
    std::vector<SampleBuffer::Frame> sampler_mem[PART_COUNT];
    for (int p = 0; p < PART_COUNT; ++p) {
        sampler_mem[p].assign(sampler_frames, SampleBuffer::Frame{ 0.f, 0.f });
        fx_mem.sampler_buf[p] = sampler_mem[p].data();
    }
    fx_mem.sampler_frames = sampler_frames;

    inst.init(static_cast<float>(scen.sample_rate), fx_mem);
    inst.set_tempo_bpm(scen.bpm);

    for (const auto& e : scen.init_events) apply_event(inst, e);

    WavData in_wav;
    if (!scen.input_wav.empty()) {
        std::string in_err;
        if (!read_wav(scen.input_wav, in_wav, in_err)) {
            std::printf("input_wav: %s\n", in_err.c_str());
            return 4;
        }
        // Fix 5: samples are fed in per-sample below with no resampling, so
        // a rate mismatch silently plays the input at the wrong pitch.
        // Resampling is out of scope here -- just make the mismatch loud.
        if (in_wav.sample_rate != scen.sample_rate) {
            std::fprintf(stderr,
                "warning: input_wav %s is %d Hz but the scenario runs at %d Hz; "
                "no resampling is performed, so the input will play at the wrong pitch\n",
                scen.input_wav.c_str(), in_wav.sample_rate, scen.sample_rate);
        }
    }

    WavWriter wav(scen.sample_rate);
    FILE* csv = std::fopen(csv_path.c_str(), "wb");
    if (csv) {
        std::fprintf(csv, "t,"
            "a_src,a_size,a_pitch,a_motion,a_level,a_pcv,a_gate,"
            "a_fx0,a_fx1,a_fx2,a_fx3,a_fx4,a_voices,a_v0,a_v1,a_v2,a_v3,a_pgate,a_fill,a_grains,a_slices,a_exc,a_matl,"
            "a_fclk,a_stages,a_div,a_frz,a_tclamp,a_strunc,"
            "b_src,b_size,b_pitch,b_motion,b_level,b_pcv,b_gate,"
            "b_fx0,b_fx1,b_fx2,b_fx3,b_fx4,b_voices,b_v0,b_v1,b_v2,b_v3,b_pgate,b_fill,b_grains,b_slices,b_exc,b_matl,"
            "b_fclk,b_stages,b_div,b_frz,b_tclamp,b_strunc,"
            "morph,couple,drift,weather,phase_err\n");
    }

    const size_t total = static_cast<size_t>(scen.duration_s * scen.sample_rate);
    const int    csv_decim = 64;
    size_t next_event = 0;

    for (size_t i = 0; i < total; ++i) {
        double t = static_cast<double>(i) / scen.sample_rate;
        while (next_event < scen.events.size() && scen.events[next_event].time_s <= t) {
            apply_event(inst, scen.events[next_event]);
            ++next_event;
        }

        const float in_l = i < in_wav.l.size() ? in_wav.l[i] : 0.f;
        const float in_r = i < in_wav.r.size() ? in_wav.r[i] : 0.f;
        float l = 0.f, r = 0.f;
        inst.process(&in_l, &in_r, &l, &r, 1);
        wav.push(l, r);

        if (csv && (i % csv_decim == 0)) {
            std::fprintf(csv, "%.5f", t);
            for (int p = 0; p < 2; ++p) {
                for (int s = 0; s < LANE_COUNT; ++s)
                    std::fprintf(csv, ",%.4f", inst.lane_output(p, s));
                std::fprintf(csv, ",%.4f,%d", inst.pitch_cv(p), inst.gate(p) ? 1 : 0);
                for (int s = 0; s < FXT_COUNT; ++s)
                    std::fprintf(csv, ",%.4f", inst.fx_target_value(p, s));
                std::fprintf(csv, ",%d", inst.active_voices(p));
                for (int v = 0; v < 4; ++v)
                    std::fprintf(csv, ",%.4f", inst.voice_env(p, v));
                std::fprintf(csv, ",%d", inst.pitch_gate(p) ? 1 : 0);
                std::fprintf(csv, ",%.4f,%d,%d", inst.sampler_fill(p),
                             inst.sampler_grains(p), inst.sampler_slices(p));
                // Excitation bus (spec §6, Task 10 observer): the post-sum,
                // post-clip value actually pushed to the engine this control
                // block. Zero on every engine but BODY (IPartEngine::
                // set_excitation defaults to a no-op) and zero on BODY too
                // whenever the bus is disabled -- a column a demo scenario
                // can point at to prove the bus was actually hot, not merely
                // that a checkbox was set (task-12-brief-addendum.md §E).
                std::fprintf(csv, ",%.4f", inst.excitation_bus(p));
                // Effective SOURCE-lane value actually fed to the engine
                // (base + active-mod*depth, clamped -- Part::target_value,
                // NOT lane_output()/a_src above, which is the raw bipolar
                // modulation source and stays nonzero even when depth = 0
                // pins the effective value at its base). On BODY this IS
                // MATL (spec §5), so this is the column body_strum.json's
                // sweep shows up in.
                std::fprintf(csv, ",%.4f", inst.target_value(p, LANE_SOURCE));
                // BBD observers (spec 2026-07-31 9): f_clk (the clock the line
                // is actually running at -- see Instrument::bbd_clock_hz's own
                // comment for why clock_now() and not clock_hz()), the derived
                // stage count, the active div rung, the freeze state and the
                // two clamp flags. Zero (or -1 for the div rung, which has no
                // natural zero) on every engine but BBD, so a demo scenario on
                // a BBD deck cannot pass vacuously the way a_voices/a_v0..3
                // would let it.
                std::fprintf(csv, ",%.4f,%d,%d,%d,%d,%d",
                             inst.bbd_clock_hz(p), inst.bbd_stages(p),
                             inst.bbd_div(p), inst.bbd_frozen(p) ? 1 : 0,
                             inst.bbd_time_clamped(p) ? 1 : 0,
                             inst.bbd_scale_truncated(p) ? 1 : 0);
            }
            std::fprintf(csv, ",%.4f,%.4f,%.4f,%.4f,%.4f",
                         inst.morph(), inst.couple(), inst.drift(),
                         inst.weather(), inst.phase_err());
            std::fprintf(csv, "\n");
        }
    }

    if (csv) std::fclose(csv);
    if (!wav.write(wav_path)) {
        std::printf("failed to write %s\n", wav_path.c_str());
        return 3;
    }
    std::printf("wrote %s (%zu frames) and %s\n", wav_path.c_str(), total, csv_path.c_str());
    return 0;
}
