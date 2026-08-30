# Vendored KiCad libraries

Third-party KiCad assets, copied in **unmodified** and pinned by checksum. If
one of these files ever needs changing, it is forked into a FireFlow-owned
library instead — never edited in place, or the provenance below becomes a lie.

## DaisyKiCad

Electrosmith's official symbols and footprints for the Daisy boards. Only the
Patch SM parts are vendored; the Seed footprints in the upstream archive are not
used here and are left out.

| | |
|---|---|
| Source | `https://daisy.nyc3.cdn.digitaloceanspaces.com/libraries/DaisyKiCad-main.zip` |
| Linked from | [docs.daisy.audio/hardware](https://docs.daisy.audio/hardware/) — "Get the KiCad footprints for all the Daisy boards" |
| Retrieved | 2026-08-30 |
| Licence | MIT, Copyright (c) 2025 Electrosmith — `DaisyKiCad/LICENSE` |
| Checksums | `DaisyKiCad/checksums.sha256`, including the archive as downloaded |

Contents:

- `Daisy-Boards.kicad_sym` — symbols. `Daisy_Patch_SM` is the one used, 40 pins.
- `Daisy-Boards.pretty/DAISY_PATCH_SM.kicad_mod` — through-hole landing pattern,
  40 pads, **61.35 × 36.16 mm** (x −32.08…29.27, y ±18.08).
- `Daisy-Boards.pretty/DAISY_PATCH_SM_SMT.kicad_mod` — the SMT variant, kept for
  reference; the coupon uses the through-hole one so the module stays removable.

### Two things worth knowing before using the symbol

**The pins are named after their default peripheral, not after what we use them
for.** The four raw ADC inputs of
[`docs/hardware/io-budget.md`](../../docs/hardware/io-budget.md) §3 appear as
`UART_RX` (A2 = `ADC_9`), `UART_TX` (A3 = `ADC_10`), `SPI_MISO` (D8 = `ADC_11`)
and `SPI_MOSI` (D9 = `ADC_12`); the four chain GPIOs appear as `I2C1_SCL` (B7),
`I2C1_SDA` (B8), `SPI_NSS` (D1) and `SPI_SCK` (D10). Anything built on this
symbol should carry a net name that says the actual function, or the next reader
will go looking for a UART that is not there.

**The landing pattern is wider than the coupon was assumed to be.** The hardware
roadmap
([`2026-08-07-fireflow-hardware-roadmap-design.md`](../../docs/superpowers/specs/2026-08-07-fireflow-hardware-roadmap-design.md))
sizes the test coupon at "~5×5 cm". The submodule's own footprint is 61.35 mm
wide, so a board carrying it cannot be 50 mm. That number predates anyone
checking the footprint.

### What was checked, and what was not

The symbol's power and I/O assignment was compared pin by pin against the Patch
SM datasheet v1.0.5 Table 2 and against io-budget §3, and agrees: `A1` = −12 V,
`A4`/`A7` = GND, `A5` = +12 V, `A6` = +5 V, `A10` = +3V3, `B1`/`B2` = audio out
R/L. **The footprint's dimensions have not been checked against real hardware** —
they are Electrosmith's numbers, taken on trust until a board is on the desk and
a module actually seats in it.
