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
    tests/test_rhythm_ring.cpp
    tests/test_gate_density.cpp
    tests/test_step.cpp
    tests/test_shuffle_grid.cpp
    tests/test_step_clock.cpp
    tests/test_lane_grid.cpp
    tests/test_evolve.cpp
    tests/test_variation.cpp
    tests/test_new_phrase.cpp
    engine/mod/super_modulator.cpp
    tests/test_super_modulator.cpp
    engine/parts/part.cpp
    tests/test_part.cpp
    engine/instrument.cpp
    bench/audition/init_patch.cpp
    bench/audition/protocol.cpp
    tests/test_instrument.cpp
    tests/test_seed_audition_init.cpp
    tests/test_seed_audition_protocol.cpp
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
    engine/fx/taps.cpp
    tests/test_taps.cpp
    engine/fx/reverb.cpp
    tests/test_reverb.cpp
    tests/test_oliverb.cpp
    engine/fx/part_fx.cpp
    tests/test_part_fx.cpp
    engine/fx/comp.cpp
    tests/test_comp.cpp
    tests/test_limiter.cpp
    tests/test_svf_lp.cpp
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
    tests/test_phrase_gen.cpp
    tests/test_song_form.cpp
    tests/test_song_lane.cpp
    tests/test_vcv_form_song_migration.cpp
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
    engine/instrument.cpp
    engine/center/center.cpp
    engine/fx/grit.cpp
    engine/fx/flux.cpp
    engine/fx/taps.cpp
    engine/fx/reverb.cpp
    engine/fx/part_fx.cpp
    engine/fx/comp.cpp
    engine/synth/wt_bank.cpp
    engine/synth/voice.cpp
    engine/synth/synth_engine.cpp
    engine/sampler/sample_buffer.cpp
    engine/sampler/sampler_engine.cpp
    engine/sampler/slice_map.cpp
)
target_include_directories(render PRIVATE host engine third_party)
target_link_libraries(render PRIVATE nlohmann_json daisysp_min)

add_test(
    NAME ctrl_identity
    COMMAND ${CMAKE_COMMAND}
        -DRENDER=$<TARGET_FILE:render>
        -DSCENARIO=${CMAKE_SOURCE_DIR}/host/render/scenarios/ctrl_identity.json
        -DEXPECTED=4296cf27d94aef392cfe5fdc9c48a9df2ceaae85c15c5993c42966bed16e59a7
        -DOUT_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/check_render_hash.cmake
)

add_test(
    NAME wave_formant_sweep
    COMMAND ${CMAKE_COMMAND}
        -DRENDER=$<TARGET_FILE:render>
        -DSCENARIO=${CMAKE_SOURCE_DIR}/host/render/scenarios/wave_formant_sweep.json
        -DEXPECTED=82aac4d6d818d205711b90a68a6b562e5d2fc435cf5f2f88e6df2ba3694706b5
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
