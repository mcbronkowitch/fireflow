/* The per-sample audio path executes from ITCM (spec
   docs/superpowers/specs/2026-10-18-cpu-headroom-design.md section 4.2):
   zero-wait-state, no instruction cache in the way. The July bench measured
   about 6 points (~58 k cycles a block) for the same idea on the Seed
   (docs/bench/2026-07-30-d570e47-system-itcm-hot.md); this board's figure
   comes from the coupon session, not from there.

   HOW IT GETS THERE. Nothing loads ITCM on this board: no SWD, and the Daisy
   bootloader copies one block into AXI SRAM. So the section's load address
   is in QSPI, behind .qspi_cold, and travels in shell-qspi.bin;
   shell::itcm_load() (itcm_load.cpp) copies it to ITCM at the top of main(),
   right after board_init() has memory-mapped the QSPI. shell/itcm_placement.py
   refuses any link in which something could run an ITCM function before that
   copy, or in which a name below is missing.

   WHAT GOES IN. Functions that run on every sample of a playing deck (tier
   1), then the control-rate work of the playing path (tier 2, once per 96
   samples: the block's most expensive sample, ~30 k cycles). Measured
   2026-10-18 on the coupon probe build (sizes in bytes, -O3). Out on
   purpose: libm and libc (log10f, sqrtf, lroundf, memmove -- archive
   members), the probe's probe_block (probe builds only), boot and event code
   (inits, triggers, phrase generation, engine switches).

   The first 256 bytes of ITCM stay empty: a function at address 0 made the
   bench's instrument silent (2026-07-30, bench/itcm_hot.lds).

   Read by ld as -Wl,-T,itcm_hot.ld after qspi_cold.ld (shell/Makefile), so
   QSPI_COLD is declared by the time this script uses it, and its exact names
   win over the main script's *(.text*). */

MEMORY
{
  ITCM_HOT (RX) : ORIGIN = 0x00000100, LENGTH = 0x10000 - 0x100
}

SECTIONS
{
  .itcm_hot :
  {
    . = ALIGN(32);
    __itcm_hot_start__ = .;

    /* Tier 1: every sample. */
    *(.text._ZN4spky10Instrument7processEPKfS2_PfS3_j)                /* 7560 */
    *(.text._ZN6clouds7Oliverb7ProcessEPfS1_)                          /* 2798 */
    *(.text._ZN4spky6VoiceTINS_8MorphOscEE7processERfS3_)              /* 2008 */
    *(.text._ZN4spky9BbdEngine7processERfS1_)                          /* 1980 */
    *(.text._ZN4spky10FeedEngine7processERfS1_)                        /* 1792 */
    *(.text._ZN4spky6VoiceTINS_5WtOscEE7processERfS3_)                 /* 1668 */
    *(.text._ZN4spky4Flux7processERfS1_)                               /* 1652 */
    *(.text._ZN4spky7BbdLine7ProcessEf)                                /* 1498 */
    *(.text._ZN4spky9BodyVoice7processERfS1_)                          /* 1222 */
    *(.text._ZN4spky12SampleBuffer5writeEff)                           /* 1188 */
    *(.text._ZN4spky6PartFx7processERfS1_S1_S1_PKf)                    /* 1142 */
    *(.text._ZN4spky13SamplerEngine7processERfS1_)                     /* 1100 */
    *(.text._ZN4spky7ModLane7processEv)                                /* 1056 */
    *(.text._ZN4spky13AmbientReverb7processEffRfS1_)                   /*  920 */
    *(.text._ZN4spky8KsString7processEf)                               /*  848 */
    *(.text._ZN4spky4Grit7processERfS1_)                               /*  624 */
    *(.text._ZN4spky14SuperModulator7processEv)                        /*  604 */
    *(.text._ZNK4spky7ModLane12_compute_rawEv)                         /*  588 */
    *(.text._ZN4spky12SynthEngineTINS_6VoiceTINS_5WtOscEEEE7processERfS5_)    /* 580 */
    *(.text._ZN4spky12SynthEngineTINS_6VoiceTINS_8MorphOscEEEE7processERfS5_) /* 560 */
    *(.text._ZN4spky12SynthEngineTINS_9BodyVoiceEE7processERfS3_)      /*  552 */
    *(.text._ZN4spky4Comp12compute_gainEv)                             /*  504 */
    *(.text._ZN4spky7ModLane12_on_boundaryEv)                          /*  400 */
    *(.text._ZNK4spky4Part13max_voice_envEv)                           /*  372 */
    *(.text._ZN4spky8SliceMap7_detectEjff)                             /*  368 */
    *(.text._ZN4spky4Comp7processERfS1_)                               /*  344 */
    *(.text._ZN4spky8ModeBank7processEf)                               /*  336 */
    *(.text._ZN4spky7ModLane11_start_noteEi)                           /*  332 */
    *(.text._ZN4spky8SliceMap8on_writeEjff)                            /*  214 */
    *(.text._ZN7daisysp9CrossFade7ProcessERfS1_)                       /*  212 */
    *(.text._ZNK4spky12SampleBuffer11read_linearEfRfS1_)               /*  204 */
    *(.text._ZN7daisysp9Decimator7ProcessEf)                           /*  156 */
    *(.text._ZN7daisysp17SampleRateReducer7ProcessEf)                  /*  148 */
    *(.text._ZN7daisysp9Overdrive7ProcessEf)                           /*  106 */
    *(.text._ZN4spky14TestToneEngine7processERfS1_)                    /*   96 */
    *(.text._ZN4spky13SamplerEngine10process_inEff)                    /*   74 */
    *(.text._ZN7daisysp7DcBlock7ProcessEf)                             /*   30 */
    *(.text._ZNK4spky12SynthEngineTINS_6VoiceTINS_8MorphOscEEEE9voice_envEi)  /* 28 */
    *(.text._ZNK4spky12SynthEngineTINS_6VoiceTINS_5WtOscEEEE9voice_envEi)     /* 28 */
    *(.text._ZNK4spky12SynthEngineTINS_9BodyVoiceEE9voice_envEi)       /*   24 */
    *(.text._ZN4spky9BbdEngine10process_inEff)                         /*   22 */
    *(.text._ZN4spky11IPartEngine10process_inEff)                      /*    2 */

    /* Tier 2: once per control tick (96 samples) on the playing path. */
    *(.text._ZN4spky4Part13_control_tickEv)                            /* 4904 */
    *(.text._ZN4spky6Center6updateERNS_14SuperModulatorES2_RNS_4PartES4_)     /* 2348 */
    *(.text._ZN4spky7ModLane6followElff)                               /* 1800 */
    *(.text._ZN4spky6VoiceTINS_5WtOscEE11_apply_freqEv)                /* 1794 */
    *(.text._ZN4spky12SynthEngineTINS_6VoiceTINS_5WtOscEEEE15_update_controlEv) /* 1416 */
    *(.text._ZN4spky10FeedEngine13_control_tickEv)                     /* 1272 */

    /* The link stamp (itcm_load.cpp): this build's copy, carried to ITCM in
       shell-qspi.bin and compared after the copy with the one in SRAM. */
    . = ALIGN(4);
    KEEP(*(.itcm_hot_stamp))

    . = ALIGN(32);
    __itcm_hot_end__ = .;
  } > ITCM_HOT AT > QSPI_COLD
  __itcm_hot_load__ = LOADADDR(.itcm_hot);
}
INSERT AFTER .qspi_cold;

ASSERT(__itcm_hot_end__ > __itcm_hot_start__, "ITCM hot set is empty");
ASSERT(__itcm_hot_end__ <= 0x10000, "ITCM hot set exceeds the 64 KiB ITCM");
