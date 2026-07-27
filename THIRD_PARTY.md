# Third-Party Licenses

This project bundles or depends on the components listed below. Each retains its
own copyright and license; the notices in the respective source files are
authoritative. All linked components are permissively licensed (MIT or Boost
Software License 1.0 — see the ported-code section below). Between M1.6 and M4.5
the reverb linked DaisySP's separately-licensed LGPL `ReverbSc` module; as of
M4.5 the reverb is a vendored MIT Oliverb port and **no LGPL code is compiled
or linked** — the note below is retained for history.

The Spotykach firmware itself is distributed under the MIT License — see
[`LICENSE`](LICENSE) (Copyright © 2026 Synthux Academy). This repository is a
fork of [Synthux-Academy/Spotykach](https://github.com/Synthux-Academy/Spotykach).

## Vendored (source included in this repository)

Located under `third_party/`.

| Component | Version / Source | License | Copyright |
|-----------|------------------|---------|-----------|
| **doctest** | `third_party/doctest/doctest.h` | MIT | © 2016–2023 Viktor Kirilov |
| **nlohmann/json** | `third_party/nlohmann/json.hpp` | MIT | © 2013–2023 Niels Lohmann |
| **Oliverb** (Clouds Parasite reverb) | `third_party/oliverb/` — from [mqtthiqs/parasites](https://github.com/mqtthiqs/parasites) `clouds/dsp/fx/` + [pichenettes/stmlib](https://github.com/pichenettes/stmlib) utilities | MIT | © 2014 Emilie Gillet, © 2015 Matthias Puech |

- **doctest** — single-header C++ test framework. MIT License; see the header
  comment at the top of `third_party/doctest/doctest.h`
  (<https://opensource.org/licenses/MIT>). Portions are influenced by
  [Catch2](https://github.com/catchorg/Catch2) (Boost Software License 1.0).
- **nlohmann/json** — single-header JSON library. `SPDX-License-Identifier: MIT`.
  Embeds MIT-licensed sub-components: Grisu2/`dtoa` © 2009 Florian Loitsch, and a
  UTF-8 decoder © 2008–2009 Björn Höhrmann — both under permissive terms
  documented inline in `third_party/nlohmann/json.hpp`.
- **Oliverb** — the shared ambient reverb core (M4.5): `oliverb.h`,
  `fx_engine.h` (Emilie Gillet), `random_oscillator.h` (Matthias Puech), and
  `stmlib_shim.h` (trimmed stmlib utilities). Vendored **with modifications**,
  each listed in a comment block under the original MIT notice in the
  respective file (float32 buffer, 48 kHz constants, pitch shifter removed,
  per-sample processing, deterministic injected RNG).
- **stmlib Limiter recipe** — `engine/fx/limiter.h` reimplements the
  gain-riding recipe of stmlib's `Limiter` (© Emilie Gillet, MIT) with a
  stereo-linked peak follower, an exactly-transparent sub-knee path, and a
  built-in master drive. No stmlib code is copied verbatim; the recipe
  credit is retained here out of courtesy.
- **DaisySP ResonatorSvf recurrence** — `engine/util/svf_bp.h` ports the
  two-integrator recurrence, topology, and coefficient formulation from
  `daisysp::ResonatorSvf<N>::Process` (© 2020 Electrosmith / Emilie Gillet,
  MIT). The class wraps it to accept pre-computed coefficients so the per-sample
  path contains arithmetic only, keeping transcendentals and divisions out of
  the audio-rate loop.

## Ported (rewritten from a licensed reference, not vendored verbatim)

Located under `engine/fx/`. Unlike the `third_party/` entries above, no
upstream source file is copied into this repository — the reference project's
own source is not included here at all — but the model, structure and several
formulas are derived closely enough from it that the license and full
attribution belong here regardless.

| Component | Reference | License | Copyright |
|-----------|-----------|---------|-----------|
| **`engine/fx/bbd.h`, `engine/fx/bbd.cpp`** (FLUX's bucket-brigade delay) | [jpcima/bbd-delay-experimental](https://github.com/jpcima/bbd-delay-experimental) — `bbd_line.cc`, `bbd_filter.cc` | Boost Software License 1.0 | © jpcima |

- **What was taken.** The combined bucket-brigade-device-and-filters model of
  Martin Holters & Julian Parker, *A Combined Model for a Bucket Brigade
  Device and its Input and Output Filters* (DAFx-18), as implemented by
  `jpcima/bbd-delay-experimental`'s `bbd_line.cc` (the clocked line, its
  analytic Butterworth filter chain and the parallel-branch discretisation)
  and `bbd_filter.cc` (the per-branch pole/residue and `G`-table
  construction). `engine/fx/bbd.h`/`engine/fx/bbd.cpp` are FLUX's port of that
  model; the BSL-1.0 notice already reproduced at the top of both files is
  authoritative for those files and is unchanged by this entry.
- **How the port differs from the reference**, all deliberate: `float`
  throughout instead of `double`; a local six-line `Cf` struct instead of
  `std::complex<float>` (no `<complex>` dependency, no toolchain-dependent
  slow-path multiply); injected memory (a caller-owned buffer, as every
  `FxMem` consumer already expects) instead of heap allocation
  (`std::vector`/`unique_ptr`); a single filter build performed once at
  init time instead of a mutex-guarded lazy cache; FLUX's own filter chain —
  two 3rd-order Butterworth sections at ~3.6 kHz (the Deluxe Memory Man's
  corner), where the reference's vendored table is the Juno-60 chorus BBD's
  ~7–10 kHz chain; and an added charge-transfer loss pole (one one-pole
  inside the clocked domain, corner fixed at `f_clk/4`) that the reference
  does not model at all. None of the reference's SIMD paths (`SSEComplex`
  and similar, found only in the GPLv3 ChowDSP/Surge variants of this model —
  reference reading only, never a dependency of this port) are present.
- **Where it lives:** `engine/fx/bbd.{h,cpp}`, consumed by
  `engine/fx/flux.h`/`flux.cpp` (the `Flux` class). Design spec:
  `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md`.
- **License — Boost Software License - Version 1.0 - August 17th, 2003**,
  reproduced in full (also present verbatim at the top of `engine/fx/bbd.h`):

  ```
  Boost Software License - Version 1.0 - August 17th, 2003

  Permission is hereby granted, free of charge, to any person or
  organization obtaining a copy of the software and accompanying
  documentation covered by this license (the "Software") to use, reproduce,
  display, distribute, execute, and transmit the Software, and to prepare
  derivative works of the Software, and to permit third-parties to whom the
  Software is furnished to do so, all subject to the following:

  The copyright notices in the Software and this entire statement,
  including the above license grant, this restriction and the following
  disclaimer, must be included in all copies of the Software, in whole or
  in part, and all derivative works of the Software, unless such copies or
  derivative works are solely in the form of machine-executable object code
  generated by a source language processor.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
  NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR ANYONE
  DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY,
  WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```

## Dependencies (referenced as git submodules — source NOT included here)

Located under `lib/`, pinned to specific upstream commits. Their code is fetched
separately (`git submodule update --init --recursive`) and remains under its own
license.

| Component | Source | License |
|-----------|--------|---------|
| **libDaisy** | [electro-smith/libDaisy](https://github.com/electro-smith/libDaisy) | MIT (© Electrosmith) |
| **DaisySP** | [electro-smith/DaisySP](https://github.com/electro-smith/DaisySP) | MIT (© Electrosmith) |

### Note on DaisySP-LGPL

DaisySP ships an optional module set under `DaisySP/DaisySP-LGPL/` that is
licensed under the **LGPL**, separate from the MIT-licensed DaisySP core. If a
compiled firmware binary is distributed that links any DaisySP-LGPL module, the
LGPL's relinking/attribution obligations apply to that binary. Distributing this
**source** repository imposes no such obligation.

As of M4.5 nothing in this repository compiles or links DaisySP-LGPL code:
the reverb moved to the vendored MIT Oliverb port under `third_party/oliverb/`,
and `ReverbSc`/`PitchShifter` were removed. The `DaisySP-LGPL/` directory
still exists inside the `lib/DaisySP` submodule checkout but is not part of
any build target.
