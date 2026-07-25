# Seed Init-Patch Audition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the approved VCV init patch continuously on the connected Daisy
Seed from SRAM, with the factory sample uploaded into SDRAM and no persistent
device writes.

**Architecture:** A small standalone Daisy host under `bench/audition/` links
the portable engine and reuses the generated VCV parameter IDs and init values.
A Python launcher prepares planar float sample data, builds and splits the
SRAM/QSPI image, resolves upload symbols, then drives a two-stage OpenOCD
session: run until SDRAM is ready, byte-verify the existing WAVE bank, upload
the sample, and resume continuous audio.

**Tech Stack:** C++17, libDaisy, the portable spotymod engine, Python 3 standard
library, GNU Arm Embedded tools, OpenOCD, ST-Link V3.

## Global Constraints

- Load executable code only into AXI SRAM through ST-Link.
- Do not erase or program internal flash, the Daisy bootloader, application
  QSPI, or the WAVE-bank QSPI region.
- Use 48 kHz stereo audio with 96-sample blocks.
- Reproduce `spkyvcv::kInitParamDefaults` and load the bundled factory sample
  into sampler part B.
- Start automatically and play continuously until reset or power-off.
- Use fixed engine seeds and a fixed external output trim of `0.25f`.
- Any failed upload, sample validation, QSPI comparison, or runtime state check
  must leave the audio outputs silent.

---

### Task 1: Prepare the factory sample as deterministic SDRAM input

**Files:**
- Create: `bench/audition/prepare_factory.py`
- Create: `bench/audition/test_prepare_factory.py`
- Read: `host/vcv/res/factory.wav`
- Produce: `bench/audition/build/factory-planar-f32.bin`
- Produce: `bench/audition/build/factory_meta.h`

**Interfaces:**
- Consumes: a RIFF/WAVE file containing 48 kHz mono or stereo
  16/24/32-bit PCM or 32-bit IEEE float samples.
- Produces: `prepare(source: Path, output: Path, header: Path) -> Metadata`.
- Produces: planar little-endian float data (`all L`, then `all R`) and C++
  constants `kFactoryFrames`, `kFactoryBytes`, `kFactoryFnv1a`,
  `kFactoryFirstL`, `kFactoryFirstR`, `kFactoryLastL`, and
  `kFactoryLastR`.

- [ ] **Step 1: Write failing converter tests**

Create `test_prepare_factory.py` with standard-library `unittest`. Build tiny
RIFF fixtures in a temporary directory and assert:

```python
class PrepareFactoryTests(unittest.TestCase):
    def test_converts_stereo_24_bit_pcm_to_planar_float(self):
        wav = self.make_pcm24([(0, 8388607), (-8388608, 4194304)])
        meta = prepare(wav, self.out, self.header)
        self.assertEqual(meta.frames, 2)
        self.assertEqual(
            struct.unpack("<4f", self.out.read_bytes()),
            (0.0, -1.0, 8388607 / 8388608.0, 0.5),
        )

    def test_rejects_non_48khz_source(self):
        wav = self.make_pcm24([(0, 0)], sample_rate=44100)
        with self.assertRaisesRegex(ValueError, "48000 Hz"):
            prepare(wav, self.out, self.header)

    def test_real_factory_file_has_finite_stereo_output(self):
        meta = prepare(FACTORY_WAV, self.out, self.header)
        self.assertGreater(meta.frames, 0)
        self.assertEqual(self.out.stat().st_size, meta.frames * 2 * 4)
        self.assertTrue(all(math.isfinite(v) for v in meta.sentinels))
```

- [ ] **Step 2: Run the converter tests and verify RED**

Run:

```powershell
python -m unittest bench.audition.test_prepare_factory -v
```

Expected: import failure because `bench.audition.prepare_factory` does not
exist.

- [ ] **Step 3: Implement the minimal converter**

Implement chunk-safe RIFF parsing with `struct`, explicit signed 24-bit
conversion, mono duplication, planar float packing, and FNV-1a over the exact
output bytes:

```python
@dataclass(frozen=True)
class Metadata:
    frames: int
    byte_count: int
    fnv1a: int
    sentinels: tuple[float, float, float, float]


def fnv1a32(data: bytes) -> int:
    value = 0x811C9DC5
    for byte in data:
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def prepare(source: Path, output: Path, header: Path) -> Metadata:
    rate, left, right = decode_wave(source)
    if rate != 48000:
        raise ValueError(f"factory sample must be 48000 Hz, got {rate} Hz")
    planar = struct.pack(f"<{len(left) * 2}f", *(left + right))
    metadata = Metadata(
        len(left), len(planar), fnv1a32(planar),
        (left[0], right[0], left[-1], right[-1]),
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(planar)
    write_header(header, metadata)
    return metadata
```

The CLI accepts optional source/output/header paths and defaults to the
repository factory asset and `bench/audition/build/`.

- [ ] **Step 4: Run the converter tests and verify GREEN**

Run:

```powershell
python -m unittest bench.audition.test_prepare_factory -v
python bench\audition\prepare_factory.py
```

Expected: all tests pass; the generated binary size is exactly
`frames * 8`.

- [ ] **Step 5: Commit the converter**

```powershell
git add bench/audition/prepare_factory.py bench/audition/test_prepare_factory.py
git commit -m "feat(audition): prepare factory sample for Seed SDRAM"
```

---

### Task 2: Apply the exact VCV init patch through the portable engine API

**Files:**
- Create: `bench/audition/init_patch.h`
- Create: `bench/audition/init_patch.cpp`
- Create: `tests/test_seed_audition_init.cpp`
- Modify: `CMakeLists.txt`
- Read: `host/vcv/src/generated_panel.hpp`
- Read: `host/vcv/src/init_patch.hpp`

**Interfaces:**
- Consumes: `spkyvcv::ParamId`, `spkyvcv::PART_STRIDE`, and
  `spkyvcv::initParamDefault(int)`.
- Produces: `void audition::apply_init_patch(spky::Instrument& inst)`.
- Does not load sample data; Task 3 calls `Instrument::load_sample` after the
  uploaded source has passed validation.

- [ ] **Step 1: Write the failing real-engine contract test**

Add the audition adapter source to `spky_tests`, then create:

```cpp
TEST_CASE("Seed audition applies the VCV init engine and arranger state") {
    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);

    float in[96] = {}, out_l[96] = {}, out_r[96] = {};
    for (int i = 0; i < 8; ++i)
        inst.process(in, in, out_l, out_r, 96);

    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_SYNTH);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_SAMPLER);
    CHECK(inst.form(spky::PART_A) == 2);
    CHECK(inst.form(spky::PART_B) == 2);
    CHECK(inst.song(spky::PART_A) == 0);
    CHECK(inst.song(spky::PART_B) == 0);
}

TEST_CASE("Seed audition shares the complete generated VCV parameter snapshot") {
    CHECK(spkyvcv::NUM_PARAMS == 82);
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_A) == doctest::Approx(0.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_B) == doctest::Approx(1.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::TEMPO) == doctest::Approx(0.5f));
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
cmake --build build
.\build\spky_tests.exe --test-case="Seed audition*"
```

Expected: compile failure because `audition::apply_init_patch` is absent.

- [ ] **Step 3: Implement the complete mapping**

`init_patch.h` declares the function. `init_patch.cpp` includes the two pure
VCV headers and applies the same routing as `Spotymod::pushParams()`:

```cpp
void apply_init_patch(spky::Instrument& inst) {
    using namespace spkyvcv;
    auto p = [](int id) { return initParamDefault(id); };
    auto part = [&](int base, int deck) {
        return p(base + deck * PART_STRIDE);
    };

    inst.set_shuffle(p(SHUFFLE));
    for (int deck = 0; deck < spky::PART_COUNT; ++deck) {
        inst.set_rate(deck, part(RATE_A, deck));
        inst.set_shape(deck, part(SHAPE_A, deck));
        inst.set_density(deck, part(DENSITY_A, deck));
        inst.set_smooth(deck, part(SMOOTH_A, deck));
        inst.set_range(deck, part(RANGE_A, deck));
        inst.set_variation(deck, part(MELODY_A, deck));
        inst.set_depth(deck, part(MOD_A, deck));
        inst.set_tune(deck, part(TUNE_A, deck));

        inst.set_voice_attack(deck, part(ATTACK_A, deck));
        inst.set_voice_decay(deck, part(DECAY_A, deck));
        inst.set_voice_resonance(deck, part(RES_A, deck));
        inst.set_voice_filt(deck, p(deck ? FILT_B : FILT_A));
        inst.set_color(deck, p(deck ? COLOR_B : COLOR_A));
        inst.set_voice_sub(deck, part(SUB_A, deck));
        inst.set_voice_detune(deck, p(deck ? DETUNE_B : DETUNE_A));

        inst.set_flux_mix(deck, part(FLUX_A, deck));
        inst.set_flux_rate(
            deck, spky::flux_division_index(p(deck ? FLUXRATE_B : FLUXRATE_A)));
        inst.set_fx_target_base(
            deck, spky::FXT_FLUX_FB, p(deck ? FLUXFB_B : FLUXFB_A));
        inst.set_grit_mix(deck, part(GRIT_A, deck));
        inst.set_dust(deck, p(deck ? DUST_B : DUST_A));
        inst.set_rot(deck, p(deck ? ROT_B : ROT_A));
        inst.set_fx_on(
            deck, spky::FxBlock::Flux, part(FLUX_A, deck) > 1e-4f);
        inst.set_fx_on(
            deck, spky::FxBlock::Grit, part(GRIT_A, deck) > 1e-4f);
        inst.set_comp(deck, part(COMP_A, deck));

        const int engine_value =
            static_cast<int>(std::lround(part(ENGINE_A, deck)));
        const spky::EngineId engine =
            engine_value == 0 ? spky::ENGINE_SYNTH :
            engine_value == 2 ? spky::ENGINE_WAVE :
                                spky::ENGINE_SAMPLER;
        inst.set_engine(deck, engine);
        const bool sampler = engine == spky::ENGINE_SAMPLER;

        inst.sampler_speed_mode(deck, true);
        inst.sampler_reverse(deck, false);
        inst.sampler_feedback(deck, 0.95f);
        inst.sampler_overlap(deck, part(DENSITY_A, deck));
        inst.set_target_base(deck, spky::LANE_SOURCE, part(SOURCE_A, deck));
        if (sampler)
            inst.sampler_scan(deck, part(MELODY_A, deck));
        inst.set_target_base(
            deck, spky::LANE_SIZE, sampler ? part(SUB_A, deck) : 0.5f);
        inst.set_target_active(deck, spky::LANE_PITCH, !sampler);

        inst.set_grit_mode(
            deck, part(GRITMODE_A, deck) > 0.5f
                      ? spky::GritMode::Reduce
                      : spky::GritMode::Drive);
        inst.set_step(
            deck, part(STEP_A, deck) > 0.5f,
            static_cast<int>(std::lround(part(STEPS_A, deck))));
        inst.set_form(
            deck, static_cast<int>(std::lround(part(FORM_A, deck))));
        inst.set_song(
            deck, static_cast<int>(std::lround(part(SONG_A, deck))));
    }

    inst.set_morph(p(MORPH));
    inst.set_couple(p(COUPLE));
    inst.set_drift(p(DRIFT));
    inst.set_tide(p(TIDE));
    inst.set_sync(p(SYNC) > 0.5f);
    inst.set_choke(p(CHOKE) * 0.5f);
    inst.set_reverb_size(p(REV_SIZE));
    inst.set_reverb_decay(p(REV_DECAY));
    inst.set_reverb_tone(p(REV_TONE));
    inst.set_reverb_diffusion(p(REV_DIFF));
    inst.set_reverb_mix(spky::PART_A, p(REV_MIX_A));
    inst.set_reverb_mix(spky::PART_B, p(REV_MIX_B));
    inst.set_reverb_smear(p(REV_SMEAR));
    inst.set_reverb_mod(p(REV_MOD));
    inst.set_master_drive(p(MASTER_DRIVE));
    inst.set_scale(static_cast<int>(std::lround(p(SCALE))));
    inst.set_tempo_bpm(40.f + p(TEMPO) * 200.f);
}
```

No runtime trigger is fired for `NEWPHRASE`, `SPOT`, or `SETTLE` because all
three approved defaults are zero. The non-parameter sampler edit state is the
VCV construction default from `SamplerPartState`: Tape mode, forward playback,
and `0.95f` overdub feedback.

- [ ] **Step 4: Run focused and full tests**

Run:

```powershell
cmake --build build
.\build\spky_tests.exe --test-case="Seed audition*"
ctest --test-dir build --output-on-failure
python host\vcv\res\test_panel.py
python host\vcv\res\test_factory_wav.py
```

Expected: all commands pass.

- [ ] **Step 5: Commit the init adapter**

```powershell
git add CMakeLists.txt tests/test_seed_audition_init.cpp bench/audition/init_patch.h bench/audition/init_patch.cpp
git commit -m "feat(audition): map VCV init patch to portable engine"
```

---

### Task 3: Build a fail-silent SRAM audition firmware

**Files:**
- Create: `bench/audition/Makefile`
- Create: `bench/audition/memory.h`
- Create: `bench/audition/memory.cpp`
- Create: `bench/audition/main.cpp`
- Create: `bench/audition/test_source_contract.py`
- Consume: `bench/audition/build/factory_meta.h`

**Interfaces:**
- Exports unmangled symbols `g_audition_state` and `g_factory_upload` for the
  launcher.
- State values:
  `kBooting = 0x41550001`, `kWaiting = 0x41550002`,
  `kUploadReady = 0x41550003`, `kRunning = 0x41550004`,
  `kError = 0x4155FFFF`.
- Produces `build/audition.elf`, whose `.qspiflash_data` is exactly the linked
  65,024-byte WAVE bank.

- [ ] **Step 1: Write failing source and layout contract tests**

Create Python tests that assert the source contains:

```python
def test_firmware_exports_upload_handshake_and_never_programs_flash(self):
    source = MAIN.read_text(encoding="utf-8")
    self.assertIn('extern "C" volatile uint32_t g_audition_state', source)
    self.assertIn('extern "C" float DSY_SDRAM_BSS g_factory_upload', source)
    self.assertNotRegex(source, r"QSPIHandle::|Erase|Write")

def test_callback_uses_real_instrument_with_fixed_trim(self):
    source = MAIN.read_text(encoding="utf-8")
    self.assertIn("g_instrument.process", source)
    self.assertIn("constexpr float kOutputTrim = 0.25f", source)
```

Add a post-link test helper that parses `arm-none-eabi-objdump -h` and rejects
any loadable section outside SRAM/DTCM except `.qspiflash_data` at
`0x90040000` with size `65024`.

- [ ] **Step 2: Run the source contract and verify RED**

Run:

```powershell
python -m unittest bench.audition.test_source_contract -v
```

Expected: failure because the firmware files do not exist.

- [ ] **Step 3: Implement memory ownership**

Use generated `kFactoryFrames` as the sampler capacity and reserve:

```cpp
extern "C" float DSY_SDRAM_BSS
    g_factory_upload[2][audition::kFactoryFrames];

spky::SampleBuffer::Frame DSY_SDRAM_BSS
    g_sampler[spky::PART_COUNT][audition::kFactoryFrames];
float DSY_SDRAM_BSS
    g_echo[spky::PART_COUNT][2][spky::Flux::kMaxSamples];
```

Construct `AmbientReverb` in aligned raw SDRAM storage only after
`DaisySeed::Init(true)`, following `bench/mem.cpp`, and return a complete
`spky::FxMem`. Add compile-time assertions that the upload, sampler, echo, and
reverb allocations remain below 64 MiB.

- [ ] **Step 4: Implement the upload handshake and audio loop**

`main.cpp` performs:

```cpp
extern "C" volatile uint32_t g_audition_state = kBooting;
constexpr float kOutputTrim = 0.25f;

void AudioCallback(daisy::AudioHandle::InputBuffer,
                   daisy::AudioHandle::OutputBuffer out, size_t size) {
    float zero[96] = {};
    float left[96], right[96];
    if (g_audition_state != kRunning) {
        std::fill_n(out[0], size, 0.f);
        std::fill_n(out[1], size, 0.f);
        return;
    }
    g_instrument.process(zero, zero, left, right, size);
    for (size_t i = 0; i < size; ++i) {
        out[0][i] = left[i] * kOutputTrim;
        out[1][i] = right[i] * kOutputTrim;
    }
}
```

After hardware initialization, set `kWaiting` and execute `bkpt 0`. On resume,
require `kUploadReady`, verify byte count, FNV-1a and all four sentinels, then
initialize `Instrument`, load `g_factory_upload[0/1]` into part B, call
`apply_init_patch`, set `kRunning`, start audio, and remain in an infinite
foreground loop. Every failure sets `kError` and starts no audio.

- [ ] **Step 5: Implement the standalone Makefile**

Mirror the engine source list and flags from `bench/Makefile`, set
`TARGET = audition`, `APP_TYPE = BOOT_SRAM`, and
`LDSCRIPT = ../../alt_sram.lds`. Make `build/factory_meta.h` depend on
`prepare_factory.py` plus the factory WAV.

- [ ] **Step 6: Build and verify GREEN**

Run:

```powershell
python -m unittest bench.audition.test_source_contract -v
make -C bench\audition -j8 build/audition.elf
python bench\audition\test_source_contract.py --elf bench\audition\build\audition.elf
```

Expected: tests pass; the ELF contains SRAM code, NOLOAD SDRAM buffers, and one
65,024-byte `.qspiflash_data` payload at `0x90040000`.

- [ ] **Step 7: Commit the firmware**

```powershell
git add bench/audition/Makefile bench/audition/memory.h bench/audition/memory.cpp bench/audition/main.cpp bench/audition/test_source_contract.py
git commit -m "feat(audition): add fail-silent Seed SRAM host"
```

---

### Task 4: Launch safely through ST-Link and perform the listening run

**Files:**
- Create: `bench/audition/launch.py`
- Create: `bench/audition/audition.cfg`
- Create: `bench/audition/test_launch.py`
- Produce: `bench/audition/build/audition-sram.elf`
- Produce: `bench/audition/build/audition-qspi.bin`

**Interfaces:**
- Consumes the full audition ELF, planar sample binary, and symbol table.
- Produces a running SRAM audition or exits nonzero before audio starts.
- OpenOCD only calls `load_image` for the SRAM ELF and SDRAM sample; it uses
  `verify_image` for QSPI and never calls a flash programming command.

- [ ] **Step 1: Write failing pure launcher tests**

Test symbol parsing, section validation, and command construction:

```python
def test_parse_symbols_requires_state_and_upload():
    symbols = parse_symbols(
        "24070000 B g_audition_state\nc1200000 B g_factory_upload\n"
    )
    self.assertEqual(symbols["g_audition_state"], 0x24070000)
    self.assertEqual(symbols["g_factory_upload"], 0xC1200000)

def test_openocd_command_contains_no_flash_programming(self):
    command = openocd_command(self.paths, self.symbols)
    joined = " ".join(command)
    self.assertIn("audition.cfg", joined)
    self.assertNotRegex(joined, r"program|flash write|erase")
```

- [ ] **Step 2: Run launcher tests and verify RED**

Run:

```powershell
python -m unittest bench.audition.test_launch -v
```

Expected: import failure because `launch.py` does not exist.

- [ ] **Step 3: Implement artifact preparation and guards**

Build the ELF and factory data, then use `arm-none-eabi-objcopy` to:

```powershell
arm-none-eabi-objcopy --only-section=.qspiflash_data -O binary audition.elf audition-qspi.bin
arm-none-eabi-objcopy --remove-section=.qspiflash_data audition.elf audition-sram.elf
```

Require QSPI payload size `65024`, factory binary size
`kFactoryFrames * 8`, the two required unmangled symbols, and a clean
`dfu-util -l`/ST-Link device discovery result before launching.

- [ ] **Step 4: Implement the non-writing OpenOCD sequence**

`audition.cfg` must:

1. `reset halt` and `load_image $SRAM_ELF`;
2. enable the FPU, set VTOR/MSP/PC as in `bench/openocd/spotykach-sram.cfg`;
3. resume and `wait_halt 30000` for the firmware's upload breakpoint;
4. assert `g_audition_state == kWaiting`;
5. `verify_image $QSPI_PAYLOAD 0x90040000 bin`;
6. `load_image $FACTORY_BIN $UPLOAD_ADDRESS bin`;
7. write `kUploadReady` to `g_audition_state` and resume;
8. sleep two seconds, halt, assert `kRunning`, resume, and `shutdown`.

No command in the file may contain `flash`, `program`, `erase`, or a write to
the QSPI address range.

- [ ] **Step 5: Run launcher tests and a build-only dry run**

Run:

```powershell
python -m unittest bench.audition.test_launch -v
python bench\audition\launch.py --build-only
```

Expected: tests and all artifact/layout guards pass without contacting the
Seed.

- [ ] **Step 6: Run complete verification before hardware**

Run:

```powershell
python -m unittest discover -s bench\audition -p "test_*.py" -v
cmake --build build
ctest --test-dir build --output-on-failure
git status --short
```

Expected: all tests pass and the worktree contains only the intended audition
changes and ignored build output.

- [ ] **Step 7: Launch the audition on the connected Seed**

Confirm monitors are connected at low volume, then run:

```powershell
python bench\audition\launch.py
```

Expected terminal result:

```text
QSPI verify: exact
Factory upload: verified
Audition state: RUNNING
Application flash/QSPI writes: none
```

The ST-Link releases the core before OpenOCD exits. The patch continues until
the Seed is reset or powered off.

- [ ] **Step 8: Commit the launcher and record the handoff**

```powershell
git add bench/audition/launch.py bench/audition/audition.cfg bench/audition/test_launch.py
git commit -m "feat(audition): launch init patch without persistent writes"
```

Report the exact hardware command, QSPI comparison result, runtime state, and
that reset/power-off stops the audition.
