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
    tests/test_rng.cpp
    tests/test_divisions.cpp
    tests/test_lane_len.cpp
    tests/test_transport.cpp
    tests/test_waveforms.cpp
    tests/test_range.cpp
    engine/mod/lane.cpp
    tests/test_lane.cpp
    tests/test_lane_tick.cpp
    tests/test_engine_map.cpp
    tests/test_melody_reachable.cpp
    tests/test_flow_melody.cpp
    tests/test_rhythm_ring.cpp
    tests/test_gate_density.cpp
    tests/test_step_accent.cpp
    tests/test_led_law.cpp
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
    tests/test_deck_bus.cpp
    engine/instrument.cpp
    bench/audition/init_patch.cpp
    bench/audition/protocol.cpp
    tests/test_instrument.cpp
    tests/test_block_size_invariance.cpp
    shell/controls.cpp
    tests/test_controls_map.cpp
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
)
target_link_libraries(spky_tests PRIVATE spky_engine doctest nlohmann_json daisysp_min)
target_include_directories(spky_tests PRIVATE host)
target_compile_definitions(spky_tests PRIVATE SPKY_TESTING=1)
add_test(NAME spky_tests COMMAND spky_tests)

# --- render host CLI ---
add_executable(render
    host/render/main.cpp
    host/render/scenario.cpp
    engine/mod/lane.cpp
    engine/mod/super_modulator.cpp
    engine/parts/part.cpp
    engine/parts/bbd_engine.cpp
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
        -DEXPECTED=2b836457ff2d20d9f0e7b1c4141b548b8a6a5bd846ff08454521254d6dd4655f
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
