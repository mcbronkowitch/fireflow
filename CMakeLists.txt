cmake_minimum_required(VERSION 3.16)
project(spky LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Portable engine: header-only for now; carries the include dir.
add_library(spky_engine INTERFACE)
target_include_directories(spky_engine INTERFACE engine third_party)

# --- third-party (vendored single headers under third_party/, desktop only) ---
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE third_party)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# --- DaisySP (vendored submodule; only the modules the FX chain needs) ---
add_library(daisysp_min STATIC
    lib/DaisySP/Source/Effects/overdrive.cpp
    lib/DaisySP/Source/Effects/decimator.cpp
    lib/DaisySP/Source/Effects/sampleratereducer.cpp
    lib/DaisySP/Source/Control/phasor.cpp
    lib/DaisySP/Source/Filters/svf.cpp
    # MIT, not LGPL. engine/body/ks_string.* is a control-rate port of this
    # file; tests/test_ks_string.cpp compares the two, so the reference has to
    # be linkable on the desktop as well as in the bench.
    lib/DaisySP/Source/PhysicalModeling/KarplusString.cpp
    lib/DaisySP/Source/Utility/dcblock.cpp
    lib/DaisySP/Source/Dynamics/crossfade.cpp
)
target_include_directories(daisysp_min PUBLIC
    lib/DaisySP/Source
    PRIVATE
    lib/DaisySP/Source/Utility
    lib/DaisySP/Source/Control
    lib/DaisySP/Source/Effects
)

# --- tests ---
enable_testing()
add_executable(spky_tests
    tests/test_main.cpp
    tests/smoke.cpp
    tests/test_quantizer.cpp
    tests/test_chord.cpp
    tests/test_pull.cpp
    tests/test_rng.cpp
    tests/test_divisions.cpp
    tests/test_lane_len.cpp
    tests/test_transport.cpp
    tests/test_waveforms.cpp
    tests/test_range.cpp
    engine/mod/lane.cpp
    tests/test_lane.cpp
    tests/test_lane_sh.cpp
    tests/test_lane_step_cache.cpp
    tests/test_lane_step_rescale.cpp
    tests/test_lane_tick.cpp
    tests/test_engine_map.cpp
    tests/test_melody_reachable.cpp
    tests/test_flow_melody.cpp
    tests/test_song_offset.cpp
    tests/test_pitch_range_law.cpp
    tests/test_rhythm_ring.cpp
    tests/test_gate_density.cpp
    tests/test_step_accent.cpp
    tests/test_led_law.cpp
    tests/test_led_observers.cpp
    tests/test_mod_layer.cpp
    tests/test_control_params.cpp
    tests/test_control_law.cpp
    tests/test_clock_law.cpp
    tests/test_restart.cpp
    tests/test_law_cache.cpp
    tests/test_step.cpp
    tests/test_shuffle_grid.cpp
    tests/test_step_clock.cpp
    tests/test_lane_follow.cpp
    tests/test_evolve.cpp
    tests/test_variation.cpp
    tests/test_new_phrase.cpp
    engine/mod/super_modulator.cpp
    tests/test_super_modulator.cpp
    tests/test_step_grid_lock.cpp
    engine/parts/part.cpp
    tests/test_part.cpp
    tests/test_flow_melody_wiring.cpp
    engine/parts/bbd_engine.cpp
    tests/test_bbd_engine.cpp
    engine/feed/feed_engine.cpp
    tests/test_feed_pair.cpp
    tests/test_feed_engine.cpp
    tests/test_deck_bus.cpp
    engine/instrument.cpp
    bench/audition/init_patch.cpp
    bench/audition/protocol.cpp
    tests/test_instrument.cpp
    tests/test_block_size_invariance.cpp
    shell/controls.cpp
    tests/test_controls_map.cpp
    shell/mux_plan.cpp
    tests/test_mux_plan.cpp
    shell/coupon_expect.cpp
    tests/test_coupon_expect.cpp
    shell/settle_plan.cpp
    tests/test_settle_plan.cpp
    shell/xtalk_plan.cpp
    tests/test_xtalk_plan.cpp
    shell/tone_plan.cpp
    tests/test_tone_plan.cpp
    shell/wait_plan.cpp
    tests/test_wait_plan.cpp
    tests/test_pot_plan.cpp
    shell/scan_value.cpp
    tests/test_scan_value.cpp
    shell/pot_owner.cpp
    tests/test_pot_owner.cpp
    shell/sd_schedule.cpp
    tests/test_sd_schedule.cpp
    shell/panel_step.cpp
    tests/test_panel_step.cpp
    shell/boot_take.cpp
    tests/test_boot_take.cpp
    shell/keys.cpp
    tests/test_keys.cpp
    shell/led_out.cpp
    tests/test_led_out.cpp
    shell/scan_check_plan.cpp
    tests/test_scan_check_plan.cpp
    tests/test_seed_audition_init.cpp
    tests/test_seed_audition_protocol.cpp
    tests/test_bbd_edge_state.cpp
    tests/test_drift_settle_state.cpp
    engine/center/center.cpp
    tests/test_center.cpp
    tests/test_wav.cpp
    host/render/scenario.cpp
    tests/test_scenario.cpp
    tests/test_fx_deps.cpp
    tests/test_fx_util.cpp
    engine/fx/grit.cpp
    tests/test_grit.cpp
    engine/fx/flux.cpp
    tests/test_flux.cpp
    tests/test_cpu_hunt_equiv.cpp
    tests/test_cpu_round2_equiv.cpp
    tests/test_tape_echo.cpp
    engine/fx/bbd.cpp
    tests/test_bbd.cpp
    tests/test_bbd_music.cpp
    engine/fx/reverb.cpp
    tests/test_reverb.cpp
    tests/test_oliverb.cpp
    engine/fx/part_fx.cpp
    tests/test_part_fx.cpp
    engine/fx/comp.cpp
    tests/test_comp.cpp
    tests/test_limiter.cpp
    tests/test_svf_lp.cpp
    tests/test_svf_bp.cpp
    tests/test_fast_sin.cpp
    tests/test_fast_tanh.cpp
    tests/test_morph_osc.cpp
    engine/synth/wt_bank.cpp
    tests/test_wt_bank.cpp
    bench/qspi_digest.cpp
    tests/test_bench_qspi_digest.cpp
    tests/test_qspi_programmer.cpp
    bench/rand_shim.cpp
    tests/test_bench_rand_shim.cpp
    tests/test_bench_serial_arena.cpp
    tests/test_wt_osc.cpp
    tests/test_wt_bank_swap.cpp
    tests/test_env.cpp
    engine/synth/voice.cpp
    tests/test_voice.cpp
    engine/synth/synth_engine.cpp
    tests/test_synth_engine.cpp
    tests/test_wave_engine.cpp
    tests/test_synth_engine_voice_count.cpp
    tests/test_phrase_gen.cpp
    tests/test_song_form.cpp
    tests/test_song_ladder.cpp
    tests/test_song_rung_state.cpp
    tests/test_song_lane.cpp
    tests/test_vcv_form_song_migration.cpp
    tests/test_vcv_link_migration.cpp
    tests/test_choke.cpp
    tests/test_pan.cpp
    tests/test_filt.cpp
    tests/test_mod_tide.cpp
    engine/sampler/sample_buffer.cpp
    tests/test_sample_buffer.cpp
    tests/test_grain.cpp
    engine/sampler/sampler_engine.cpp
    engine/sampler/slice_map.cpp
    tests/test_sampler_engine.cpp
    tests/test_sampler_part.cpp
    tests/test_review_register.cpp
    tests/test_slice_map.cpp
    engine/body/mode_bank.cpp
    tests/test_mode_bank.cpp
    engine/body/ks_string.cpp
    tests/test_ks_string.cpp
    engine/body/body_voice.cpp
    tests/test_body_voice.cpp
    tests/test_body_engine.cpp
    # engine/body/material.h is header-only on purpose: it is one stateless
    # free function, and a .cpp would have to be added to spky_tests, render,
    # bench/Makefile, bench/audition/Makefile AND host/vcv/Makefile.
    tests/test_material.cpp
    tests/test_part_engine_contract.cpp
    tests/test_param_table.cpp
    tests/test_param_impact.cpp
    tests/test_smooth_law.cpp
    tests/test_skip_unchanged.cpp
    shell/boot_patch.cpp
    shell/remote_cmd.cpp
    tests/test_remote_cmd.cpp
    shell/persist_record.cpp
    tests/test_persist_record.cpp
    shell/persist_policy.cpp
    tests/test_persist_policy.cpp
    shell/take_file.cpp
    tests/test_take_file.cpp
    tests/test_boot_patch.cpp
    shell/workload_run.cpp
    tests/test_workload_run.cpp
)
target_link_libraries(spky_tests PRIVATE spky_engine doctest nlohmann_json daisysp_min)
target_include_directories(spky_tests PRIVATE host ${CMAKE_SOURCE_DIR})
target_compile_definitions(spky_tests PRIVATE SPKY_TESTING=1)
add_test(NAME spky_tests COMMAND spky_tests)

# The switch headers are generated by shell/Makefile into shell/build/, which
# the host build does not have and must not depend on. tone_plan.h reads
# SHELL_TONE_DC -- Task 1's measured bench answer -- so the host gets its own
# one-line header. Its value is DERIVED from shell/Makefile's own
# SHELL_TONE_DC default line rather than carried as a second hand-copied
# literal here: two copies of one bit is the exact failure class the
# codec-tone brief's step 6 warned about (the host and the firmware silently
# disagreeing about kToneRowCount while each build's own suite stays green),
# and deriving it removes the second copy instead of just flagging it.
#
# The match is ANCHORED to the start of a line (the pattern is prefixed with
# a literal newline, so it can never match inside a '#'-prefixed comment)
# and REQUIRED TO BE UNIQUE: configure fails loudly if zero lines match and
# equally loudly, with a different message, if two or more do -- an
# ambiguous match is never resolved by picking the first or the last one.
# Only when exactly one line matches is its digit trusted.
#
# NOTE FOR A FUTURE MAINTAINER, not a description of what exists: NO
# SHELL_TONE_DC=0 host test is registered anywhere, so the AC-coupled branch
# of tone_plan.h is unbuilt on the host. To exercise it, flip the generated
# line -- do not flip it here, and do not hand-edit the generated header; the
# firmware's value always comes from the Makefile switch, never from this
# file.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shell/Makefile SHELL_MAKEFILE_CONTENTS)
string(REGEX MATCHALL "\nSHELL_TONE_DC[ \t]*\\?=[ \t]*([01])"
       SHELL_TONE_DC_MATCHES "${SHELL_MAKEFILE_CONTENTS}")
list(LENGTH SHELL_TONE_DC_MATCHES SHELL_TONE_DC_MATCH_COUNT)
if(SHELL_TONE_DC_MATCH_COUNT EQUAL 0)
    message(FATAL_ERROR
        "CMakeLists.txt found no 'SHELL_TONE_DC ?= 0' or 'SHELL_TONE_DC ?= "
        "1' line anchored at the start of a line in shell/Makefile. The "
        "host test suite derives its SHELL_TONE_DC default from that line "
        "(see the comment above this message in CMakeLists.txt); if "
        "shell/Makefile's SHELL_TONE_DC default line was reformatted or "
        "removed, restore it or update the regex here to match its new "
        "shape.")
elseif(NOT SHELL_TONE_DC_MATCH_COUNT EQUAL 1)
    message(FATAL_ERROR
        "CMakeLists.txt found ${SHELL_TONE_DC_MATCH_COUNT} lines in "
        "shell/Makefile anchored at the start of a line and shaped like "
        "'SHELL_TONE_DC ?= 0' or 'SHELL_TONE_DC ?= 1', and refuses to guess "
        "which is the real default. The host test suite requires exactly "
        "one such line (see the comment above this message in "
        "CMakeLists.txt); remove or reword the extra line(s) so only "
        "shell/Makefile's actual default assignment matches.")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/host_switches/shell_tone_probe.h
     "#define SHELL_TONE_PROBE 0\n#define SHELL_TONE_DC ${CMAKE_MATCH_1}\n")
target_include_directories(spky_tests PRIVATE
     ${CMAKE_CURRENT_BINARY_DIR}/host_switches)

# --- the half-rate reverb, both compile shapes (spec 2026-10-29-cpu-hunt-remote
# section 4: a firmware-only switch needs the desktop suite to exercise it) ---
# SHELL_REVERB_HALF=1 compiles Oliverb with SPKY_REVERB_HALF_ONLY (only the
# half-rate tank, switched on in init()); the desktop tests run the dispatch
# variant (SPKY_TESTING). These two tiny programs render the same room in each
# and `reverb_half_variants` requires bit-identical output, so the tests check
# the code the firmware ships.
add_executable(reverb_dump_dispatch tests/reverb_dump.cpp engine/fx/reverb.cpp)
target_compile_definitions(reverb_dump_dispatch PRIVATE SPKY_TESTING=1 REVERB_DUMP_SET_HALF=1)
target_link_libraries(reverb_dump_dispatch PRIVATE spky_engine daisysp_min)
add_executable(reverb_dump_only tests/reverb_dump.cpp engine/fx/reverb.cpp)
target_compile_definitions(reverb_dump_only PRIVATE SPKY_REVERB_HALF_ONLY=1)
target_link_libraries(reverb_dump_only PRIVATE spky_engine daisysp_min)
add_test(NAME reverb_half_variants
         COMMAND ${CMAKE_COMMAND}
                 -DDUMP_A=$<TARGET_FILE:reverb_dump_dispatch>
                 -DDUMP_B=$<TARGET_FILE:reverb_dump_only>
                 -DOUT_DIR=${CMAKE_BINARY_DIR}/reverb_half_variants
                 "-DWHAT=the SHELL_REVERB_HALF=1 variant (SPKY_REVERB_HALF_ONLY) renders differently from the tested dispatch variant"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_reverb_dumps.cmake)

# The other direction: VCV, the render host and every firmware image with
# SHELL_REVERB_HALF=0 build the reverb with none of the three defines
# (OLIVERB_HALF_PATH 0, the full-rate code alone); the doctests run the
# dispatch variant (SPKY_TESTING) with the half-rate switch off. This renders
# the same room in the plain build and in the dispatch build at full rate and
# requires bit-identical output, so an edit inside `#if !OLIVERB_HALF_PATH`
# (or a dispatch change that alters the full-rate tank) goes red here and not
# only in a render hash.
add_executable(reverb_dump_plain tests/reverb_dump.cpp engine/fx/reverb.cpp)
target_link_libraries(reverb_dump_plain PRIVATE spky_engine daisysp_min)
add_executable(reverb_dump_dispatch_full tests/reverb_dump.cpp engine/fx/reverb.cpp)
target_compile_definitions(reverb_dump_dispatch_full PRIVATE SPKY_TESTING=1)
target_link_libraries(reverb_dump_dispatch_full PRIVATE spky_engine daisysp_min)
add_test(NAME reverb_full_rate_variants
         COMMAND ${CMAKE_COMMAND}
                 -DDUMP_A=$<TARGET_FILE:reverb_dump_dispatch_full>
                 -DDUMP_B=$<TARGET_FILE:reverb_dump_plain>
                 -DOUT_DIR=${CMAKE_BINARY_DIR}/reverb_full_rate_variants
                 "-DWHAT=the plain build (VCV, render, SHELL_REVERB_HALF=0) renders differently from the tested dispatch variant at full rate"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_reverb_dumps.cmake)

# --- the class-2 lean switches (spec 2026-10-30-cpu-engine-kernels section 4;
# shell/Makefile, SHELL_LEAN_*) ---
# SHELL_LEAN_<NAME>=1 compiles the engine with SPKY_LEAN_<NAME>=1, in the
# firmware only: VCV, the render host and spky_tests build the old code, so the
# render hashes do not move. This target compiles each candidate's property
# tests with its lean define on, as a SHELL_LEAN_*=1 image does, against the
# old code as the reference (V6: tests/test_lean_svf1.cpp; G2:
# tests/test_lean_grain_win.cpp, Grain is header-only and sample_buffer.cpp is
# the one sampler file it links, for the buffer's init and set_rec_size; V5:
# tests/test_lean_exp2.cpp, MorphOsc and WtOsc are header-only and a voice
# comes from voice.cpp. The three defines share this target: each reaches only
# its own class, and the V5 test builds its oscillators and voices itself.)
add_executable(spky_lean_tests
    tests/test_main.cpp
    tests/test_lean_svf1.cpp
    tests/test_lean_grain_win.cpp
    tests/test_lean_exp2.cpp
    engine/synth/voice.cpp
    engine/synth/wt_bank.cpp
    engine/sampler/sample_buffer.cpp
)
target_compile_definitions(spky_lean_tests PRIVATE SPKY_LEAN_SVF1=1 SPKY_LEAN_GRAIN_WIN=1
                                                    SPKY_LEAN_EXP2=1)
target_link_libraries(spky_lean_tests PRIVATE spky_engine doctest)
add_test(NAME spky_lean_tests COMMAND spky_lean_tests)

# --- render host CLI ---
add_executable(render
    host/render/main.cpp
    host/render/scenario.cpp
    engine/mod/lane.cpp
    engine/mod/super_modulator.cpp
    engine/parts/part.cpp
    engine/parts/bbd_engine.cpp
    engine/feed/feed_engine.cpp
    engine/instrument.cpp
    engine/center/center.cpp
    engine/fx/grit.cpp
    engine/fx/flux.cpp
    engine/fx/bbd.cpp
    engine/fx/reverb.cpp
    engine/fx/part_fx.cpp
    engine/fx/comp.cpp
    engine/synth/wt_bank.cpp
    engine/synth/voice.cpp
    engine/synth/synth_engine.cpp
    engine/sampler/sample_buffer.cpp
    engine/sampler/sampler_engine.cpp
    engine/sampler/slice_map.cpp
    engine/body/mode_bank.cpp
    engine/body/ks_string.cpp
    engine/body/body_voice.cpp
)
target_include_directories(render PRIVATE host engine third_party)
target_link_libraries(render PRIVATE nlohmann_json daisysp_min)

# --- the law-driven sweep (plan 2026-10-29-cpu-hunt-remote Task 4) ---
# Rev A knob vectors through the firmware's own control path -- the panel
# table, apply_boot_patch_rows(), control::ControlLaw and the panel_step glue
# -- with process(..., 96) timed per block. A ranking tool, not a test.
add_executable(sweep
    host/sweep/main.cpp
    shell/controls.cpp
    shell/boot_patch.cpp
    shell/panel_step.cpp
    shell/pot_owner.cpp
    shell/scan_value.cpp
    shell/mux_plan.cpp
    engine/mod/lane.cpp
    engine/mod/super_modulator.cpp
    engine/parts/part.cpp
    engine/parts/bbd_engine.cpp
    engine/feed/feed_engine.cpp
    engine/instrument.cpp
    engine/center/center.cpp
    engine/fx/grit.cpp
    engine/fx/flux.cpp
    engine/fx/bbd.cpp
    engine/fx/reverb.cpp
    engine/fx/part_fx.cpp
    engine/fx/comp.cpp
    engine/synth/wt_bank.cpp
    engine/synth/voice.cpp
    engine/synth/synth_engine.cpp
    engine/sampler/sample_buffer.cpp
    engine/sampler/sampler_engine.cpp
    engine/sampler/slice_map.cpp
    engine/body/mode_bank.cpp
    engine/body/ks_string.cpp
    engine/body/body_voice.cpp
)
target_include_directories(sweep PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(sweep PRIVATE spky_engine daisysp_min)

# --- the callback profile's stamps, on the desktop ---
# The one desktop target built with SPKY_PROBE_MARKS=1 (engine/util/
# probe_mark.h): the engine compiled as the firmware's probe image compiles
# it, so the stamp sites cannot rot between board sessions, and the stamp
# ORDER per sample is checked against a running clock. Not the cost: that is
# the board's (shell/cb_profile.cpp). Its own executable because the define
# changes Part::process's signature. Also built with SPKY_PROBE_LAW=1
# (engine/util/probe_law.h), the control law's TIDE split, for the same reason.
# And with SPKY_PROBE_TOGGLES=1 (engine/util/probe_toggles.h), as the
# measurement image is, so the CPU round's toggle-word plumbing is compiled
# here (tests/test_cpu_round2_probes.cpp).
add_executable(spky_probe_tests
    tests/test_probe_marks.cpp
    tests/test_cpu_round2_probes.cpp
    engine/mod/lane.cpp
    engine/mod/super_modulator.cpp
    engine/parts/part.cpp
    engine/parts/bbd_engine.cpp
    engine/feed/feed_engine.cpp
    engine/instrument.cpp
    engine/center/center.cpp
    engine/fx/grit.cpp
    engine/fx/flux.cpp
    engine/fx/bbd.cpp
    engine/fx/reverb.cpp
    engine/fx/part_fx.cpp
    engine/fx/comp.cpp
    engine/synth/wt_bank.cpp
    engine/synth/voice.cpp
    engine/synth/synth_engine.cpp
    engine/sampler/sample_buffer.cpp
    engine/sampler/sampler_engine.cpp
    engine/sampler/slice_map.cpp
    engine/body/mode_bank.cpp
    engine/body/ks_string.cpp
    engine/body/body_voice.cpp
)
target_include_directories(spky_probe_tests PRIVATE engine third_party)
target_compile_definitions(spky_probe_tests PRIVATE SPKY_PROBE_MARKS=1 SPKY_PROBE_LAW=1 SPKY_PROBE_TOGGLES=1)
target_link_libraries(spky_probe_tests PRIVATE doctest daisysp_min)
add_test(NAME spky_probe_tests COMMAND spky_probe_tests)

add_test(
    NAME ctrl_identity
    COMMAND ${CMAKE_COMMAND}
        -DRENDER=$<TARGET_FILE:render>
        -DSCENARIO=${CMAKE_SOURCE_DIR}/host/render/scenarios/ctrl_identity.json
        # Reference moved (spec 2026-08-12 modulation-pace task 3):
        # ModLane::_phase and its increment moved from float to double, so
        # every modulated sample in this scenario changed in its low bits.
        # No bit-exactness gates -- this is a sanity render, not a checksum
        # (CLAUDE.md); the move is the unavoidable consequence of accumulating
        # the lane phase in double.
        # Moved again (spec 2026-08-13 flow-melody-engine task 8): the
        # scenario sets no engine and no set_step, i.e. two SYNTH decks in
        # FLOW, and Part now pushes set_flow_melody(true) into both PITCH
        # lanes (Part::init, the boot default engine is ENGINE_SYNTH) -- both
        # decks step through phrase slots instead of running the old
        # continuous LFO, which is the entire point of this task.
        # Moved again (spec 2026-10-26 pitch-range-law §4.2): the scenario
        # never sets RANGE, so the centred law leaves it bit-identical, and the
        # reflection mirrors the pitch sums that leave the span instead of
        # clamping them.
        -DEXPECTED=d64d9a28c2feda30a2e041884235065b880f2bf45c012f0aa2552fb0ad8a0eb7
        -DOUT_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/check_render_hash.cmake
)

add_test(
    NAME wave_formant_sweep
    COMMAND ${CMAKE_COMMAND}
        -DRENDER=$<TARGET_FILE:render>
        -DSCENARIO=${CMAKE_SOURCE_DIR}/host/render/scenarios/wave_formant_sweep.json
        # Reference moved (spec 2026-08-09 hw-control-reduction task 10):
        # the scenario pushes set_voice_detune(part=0, value=0.22) onto the
        # WAVE engine, and WaveEngine shares SynthEngineT::kDetuneCeilCt
        # (35 -> 105 ct) with SynthEngine/BodyEngine, so the same 0.22 knob
        # now reaches 23.1 ct instead of 7.7 ct. No bit-exactness gates --
        # this is a sanity render, not a checksum (CLAUDE.md); the move is
        # the expected, unavoidable consequence of the ceiling change.
        # Moved again (spec 2026-08-12 modulation-pace task 3): the lane phase
        # accumulator is double, and this scenario's formant sweep is
        # modulated. Same reasoning as above -- sanity render, not a checksum.
        # Moved again (spec 2026-08-13 flow-melody-engine task 8): WAVE is a
        # note engine, and this scenario runs it on part 0 with set_step's
        # flag false, i.e. FLOW -- Part now pushes set_flow_melody(true) into
        # its PITCH lane (Part::init/_engine_swap), so it steps through phrase
        # slots instead of the old continuous LFO.
        # Moved again (spec 2026-08-13 shape-smooth-rework task 5): the
        # scenario sets set_smooth 0.65, and SMOOTH is now a fraction of the
        # lane interval instead of an absolute time, so its modulation lands
        # differently. ctrl_identity sets SMOOTH 0.0 twice -- passthrough under
        # both laws -- and is confirmed green, not re-cut. The owner heard both
        # renders (wave_formant_sweep_ALT/NEU.wav) and accepted the new one.
        # Moved once more in the same branch (review fix): the tick slew's
        # coefficient is now derived in double, because in float it quantised
        # and eventually underflowed to zero at the slow end of the panel. That
        # reaches every render, this one included -- but as ROUNDING, not as a
        # sound change. Measured against the render the owner accepted: RMS
        # identical to 0.01 dB, difference signal at -88.86 dBFS (59.2 dB below
        # the render, worst single sample 17 LSB of 32768). The accepted
        # ALT->NEU change, for scale, sat 4.1 dB below the render. So the
        # owner's decision about the SOUND still stands; only the checksum moved.
        -DEXPECTED=4b9173d5ebbfd4df86e6b104699b0235b5568d52e2577ee4fb39109b18b4fd05
        -DOUT_DIR=${CMAKE_BINARY_DIR}
        -DGATE_STEM=wave_formant_sweep
        -DREFERENCE=WAVE
        -P ${CMAKE_SOURCE_DIR}/tests/check_render_hash.cmake
)

# The halfband coefficients in engine/fx/halfband.h against their generator
# (tools/gen_halfband.py): bit for bit in float32, and the check goes red on a
# changed constant.
add_test(NAME halfband_generator
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_gen_halfband.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

add_test(
    NAME wavetable_bank_fresh
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/bake_wavetables.py
        --check
)

# --- panel guards (host/vcv/res/test_*.py) ---------------------------------
# res/test_panel.py (97 tests -- the authority on the whole generated-artifact
# contract) and res/test_hw_panel.py were manual-only: nothing in CI or ctest
# ran them, and during the 2026-08-09 hw-control-reduction plan one of them
# briefly had no runner at all while task reports cited it as a passing gate
# (review finding IMPORTANT 4). Each script resolves its own paths relative to
# host/vcv/, hence WORKING_DIRECTORY below -- running them from the repo root
# fails to find generated_panel.hpp.
#
# find_package(Python3) above is REQUIRED, so a machine without Python 3 fails
# to configure at all rather than silently skipping these -- consistent with
# wavetable_bank_fresh just above, which already depends on the same
# ${Python3_EXECUTABLE} unconditionally.
add_test(
    NAME panel_guard
    COMMAND ${Python3_EXECUTABLE} res/test_panel.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)

add_test(
    NAME hw_panel_guard
    COMMAND ${Python3_EXECUTABLE} res/test_hw_panel.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)

# The group fields of the 60 HP plate are rectilinear unions of cell rects
# (spec docs/superpowers/specs/2026-10-07-panel-nine-mm-raster-design.md §7).
add_test(
    NAME hw_fields_guard
    COMMAND ${Python3_EXECUTABLE} res/test_hw_fields.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)

# Rev A P1: the acrylic cut file, the print sheet and the hole list the board
# will be placed from (spec docs/superpowers/specs/2026-09-29-rev-a-p1-
# panel-parts-design.md §4). Same runner shape as the two guards above.
add_test(
    NAME hw_cut_guard
    COMMAND ${Python3_EXECUTABLE} res/test_hw_cut.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)

# Rev A P5: the aluminium plate's Front Panel Designer files (hole DXF, print
# PDFs) from res/gen_hw_alu.py. Needs fontTools and PyMuPDF, like
# hardware/gen/check.py; without them it fails, never skips.
add_test(
    NAME hw_alu_guard
    COMMAND ${Python3_EXECUTABLE} res/test_hw_alu.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)

# Rev A P3 (docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md):
# the generator tools shared under hardware/gen/. Plain scripts; the exit code
# is the verdict. Guards that call kicad-cli fail -- never skip -- without it.
add_test(NAME hw_gen_coupon_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_coupon_netlist.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME hw_gen_assembly_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_assembly.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME hw_gen_writer_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_sch_writer.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME hw_gen_check_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_check.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME reva_assign_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_assign.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME reva_build_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_build.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# The Rev A schematic at the check tool's full level: every rule, the
# netlist against the intent, ERC against hardware/reva/erc-waivers.txt.
add_test(NAME reva_check_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/check.py
                 --project ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/build.py
                 --full --out ${CMAKE_BINARY_DIR}/reva-check
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME hw_gen_bom_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_bom.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4a: the board-side proof tools shared by coupon and Rev A. The
# script re-runs itself under KiCad's Python (it needs pcbnew).
add_test(NAME hw_gen_pcb_proof_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_pcb_proof.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4a: the pcbnew-free grid router core (A*, clearance classes,
# negotiated congestion); plain system Python.
add_test(NAME hw_gen_route_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_route.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4-2: the stitch search's optional keep-off from plane-net through-hole
# pads; re-runs itself under KiCad's Python.
add_test(NAME hw_gen_stitch_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_stitch.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4-1: shared placement geometry (boxes, hole point, first-fit search);
# re-runs itself under KiCad's Python.
add_test(NAME hw_gen_place_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_place.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4-1: the generated J_SD footprint equals a fresh generation and KiCad
# loads it with the sourced pads; re-runs itself under KiCad's Python.
add_test(NAME reva_sd_footprint_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_sd_footprint.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4-1: two builds byte-identical, the committed board equals a fresh
# one, every check green, every sabotage turns its step red; re-runs itself
# under KiCad's Python.
add_test(NAME reva_place_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_place.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4-2: the shared check machinery (DRC parser, judge, step runner).
add_test(NAME reva_check_kit_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_check_kit.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Rev A P4-2: the routed board -- two runs byte-identical, committed board
# fresh, every check green at baseline and red under its own sabotage.
add_test(NAME reva_route_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_route.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Measured 2026-10-08 (Task 7c, 60 router rounds): 2651 s and 2957 s on
# full Release runs, against the old 3600; 5400 leaves room so a placement
# that stops converging fails on its own report, not on the timeout.
set_tests_properties(reva_route_guard PROPERTIES TIMEOUT 5400)

# The Rev A order package (P4-3): Gerbers, drill, CPL, BOM, assembly sheets,
# every check proven red, two exports byte-identical.
add_test(NAME reva_fab_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_fab.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(reva_fab_guard PROPERTIES TIMEOUT 3600)

# The Python guards need a runner or they rot unnoticed -- tools/test_*.py in
# this repo is wired to nothing, and the VCV panel guard stood red for 23
# days behind exactly that gap. pytest is not installed here, so it runs as
# a plain script and its exit code is the verdict.
add_test(NAME read_coupon_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_coupon.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The SHELL_REMOTE listener's wiring (plan 2026-10-29-cpu-hunt-remote.md Task
# 2): BOOT from the USB interrupt without a delay, the fault vectors first in
# main(), the ITCM failure into the bootloader, the listener after StartLog().
add_test(NAME remote_wiring_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_remote_wiring.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

add_test(NAME read_settle_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_settle.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

add_test(NAME read_xtalk_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_xtalk.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# Reads shell/testdata/tone-block-86070d9.txt as well as its own generated
# fixture -- one real block the board printed, vendored because the capture
# it came from lives under .superpowers/ and is gitignored.
add_test(NAME read_tone_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_tone.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# Generated fixtures only until the wait-sweep probe's first board block
# exists; that block should be vendored under shell/testdata/ and parsed by
# this guard, as read_tone_guard does with its own.
add_test(NAME read_wait_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_wait.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# Round four's shared host-side module: the two position gates, the derived
# impedance, and the pot table held against shell/pot_plan.h.
add_test(NAME pot_round_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_pot_round.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

add_test(NAME read_pots_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_pots.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The panel scan's measurement image: gates G1-G3, the criterion, and the
# hysteresis rule. Generated fixtures; the committed capture joins after the
# first board session (spec 2026-09-28-coupon-panel-scan-design.md).
add_test(NAME read_scan_check_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_scan_check.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The Rev A play image's reader (docs/hardware/reva-bring-up.md 0.2.2): frame
# parsing against panel_scan.cpp's line format, row names from the generated
# header, the stops / rest / span verdicts and their exit codes. Generated
# fixtures; a committed capture joins after the first Rev A board session.
add_test(NAME read_play_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_play.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The SD bring-up image's reader (spec 2026-10-31-sd-bring-up-design.md,
# reva-bring-up.md section 11): the card verdict and the streaming / idle
# split of the raw windows, on generated captures.
add_test(NAME read_sd_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_sd.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The reverb grain layer's reader (SHELL_RGRAIN, spec
# 2026-10-31-sd-use-cases-tests-design.md section 5, B1 and B2): the grain's
# slope and its halves, the cap verdict and the layer check, on generated
# captures.
add_test(NAME read_rgrain_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_rgrain.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The Rev A panel as the firmware sees it: shell/generated_panel_map.h is
# regenerated and compared, and every check the spec names proves its RED
# (spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 4; a few purely
# defensive generator checks have no sabotage).
add_test(NAME shell_panel_map_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_gen_panel_map.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The all-noon boot patch of fault R4 (docs/hardware/reva-bring-up.md 10.1):
# shell/generated_boot_patch_noon.h is regenerated from the committed capture
# and compared, and every refusal of the generator proves its RED.
add_test(NAME shell_boot_patch_noon_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_gen_boot_patch_noon.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The remote CPU hunt's board workloads (plan 2026-10-29-cpu-hunt-remote.md
# Task 5): shell/generated_workloads.h is regenerated from shell/workloads.json
# and compared, round-tripped through its own parser, and every refusal of the
# generator (a vector whose length is not the table's row count first) proves
# its RED.
add_test(NAME shell_workloads_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_gen_workloads.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The callback profile's window reader (CBP_WIN / CBP_WIN2 lines of a play
# capture, shell/cb_profile.cpp): a hand-written fixture plus the committed
# R4 capture.
add_test(NAME read_cbp_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_cbp.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The firmware's CLOCK and RESET wiring (spec 2026-10-19-clock-and-reset-jacks
# sections 6 and 8.5): the gate latch stays out of the coupon image, the
# pending bits are cleared right before StartAudio(), the block runs in order,
# and control::ClockLaw is the only clock logic in shell/.
add_test(NAME shell_clock_wiring_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_clock_wiring.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

add_test(NAME shell_qspi_placement_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_qspi_placement.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The ITCM placement guard (spec 2026-10-18-cpu-headroom-design.md section
# 4.2): nothing may run ITCM code before main() has copied it there.
add_test(NAME shell_itcm_placement_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_itcm_placement.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The math guard (shell/math_guards.py): every image build checks, on the linked
# ELF, that the engine's isfinite guards are still compiled in (SHELL_FAST_MATH
# defaults to 1 and must never fold them). This is its host test; the image
# builds below run the real thing.
add_test(NAME shell_math_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_math_guards.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The build-switch writers (reverb_half, fast_math, remote, remote_measure,
# rgrain, engine_lean, sd, sd_probe) run while the Makefile is parsed and the Makefile ignores their
# exit status: a refusal or crash that left the old header / stamp / linker
# script behind would build on a stale value. This holds that each deletes its
# output on any failure.
add_test(NAME shell_writer_refusals
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_writer_refusals.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)

# The shell images actually build: default, Rev A play (with and without
# SHELL_PERSIST, and its stress image), coupon play, the remote CPU hunt's
# listener and measurement images (which must not link the save path:
# arm-none-eabi-nm on the ELF, with the play image as the canary), the listener
# with both class-2 savings switched back, the measurement image with both
# reverb tanks, the listener with every lean switch (SHELL_LEAN_*, which
# default to 1) at 0, a full -ffast-math build that must be REFUSED by the
# math guard, and the SD bring-up image (it must link FatFs and the card test,
# the others must not) with its refusal without the listener, and the SD
# burst probe image with its refusal without the SD image, and the reverb
# grain layer's image (it must link the layer, the plain listener must not)
# with its refusal beside the SD image, and the SD bank, buffer, patch and
# duplex probe images (the list is IMAGES in
# shell/check_images_build.py, twenty-five entries at the time of writing). Each is a clean ARM build in its own directory under the
# build tree (shell/build is not touched), with SRAM_EXEC use in the output.
# Nothing else in ctest compiles shell/, which is how the Rev A play image
# stayed broken behind a green suite on 2026-10-09 (450eed36). Skipped, not
# failed, without the DaisyToolchain or the built libDaisy/DaisySP. About 80 s
# serial for ten images (2026-10-30).
add_test(NAME shell_images_build_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/check_images_build.py
                 ${CMAKE_BINARY_DIR}/shell_images
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
set_tests_properties(shell_images_build_guard PROPERTIES
                     SKIP_RETURN_CODE 77 TIMEOUT 900)

# The two budget calculations under tools/. scan_budget imports settle_budget,
# so a constant revised there reaches both guards; scan_budget also reads the
# pot count from host/vcv/res/gen_hw_panel.py and goes red when the panel moves.
add_test(NAME settle_budget_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_settle_budget.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

add_test(NAME scan_budget_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_scan_budget.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# The click detector the CPU headroom round judges board recordings with
# (spec 2026-10-18-cpu-headroom-design.md section 2.6): a dropped block is a
# click, a clean tone and a quiet passage are not.
add_test(NAME click_detect_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_click_detect.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# The remote CPU hunt's host runner (plan 2026-10-29-cpu-hunt-remote.md Task 3):
# port reconnection, DFU pair order, capture command, and the run statistics.
# Hermetic -- no serial port, dfu-util or ffmpeg is touched.
add_test(NAME remote_lib_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_remote_lib.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# The CPU round 2 section-means tool (plan 2026-10-30-cpu-engine-kernels
# Task 1): CBP_WIN* serial lines to per-section window means. Pure Python.
add_test(NAME section_means_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_section_means.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# The hot-path libm scan (plan 2026-10-29-cpu-hunt-remote.md Task 4): which
# functions count as hot (.itcm_hot plus the direct-call closure from
# AudioCallback, veneers and tail calls followed), on hand-written objdump text.
add_test(NAME hotpath_libm_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_hotpath_libm.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# The sweep's deck split of the Rev A rows (corner vectors): every row's deck
# from its name's _A / _B suffix, 29 / 29 / 15, so a param pair appended to
# control/params.hpp without host/sweep/main.cpp's deck_of() list goes red.
add_test(NAME sweep_rows_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_sweep_rows.py $<TARGET_FILE:sweep>
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)
