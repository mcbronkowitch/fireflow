/*
 * Supplement to ../alt_sram.lds for the benchmark's pre-registered audio
 * hotset. OpenOCD loads bench-sram.elf directly, including this ITCM segment.
 * Constants stay in their normal sections; only executable input sections
 * from the selected objects move.
 */
SECTIONS
{
    .itcm_audio_hot 0x00000000 :
    {
        . = ALIGN(32);
        __itcm_audio_hot_start__ = .;

        build/instrument.o(.text .text.*)
        build/part.o(.text .text.*)
        build/part_fx.o(.text .text.*)
        build/flux.o(.text .text.*)
        build/bbd.o(.text .text.*)
        build/grit.o(.text .text.*)
        build/reverb.o(.text .text.*)
        build/comp.o(.text .text.*)
        build/synth_engine.o(.text .text.*)
        build/voice.o(.text .text.*)

        . = ALIGN(32);
        __itcm_audio_hot_end__ = .;
    }
}

ASSERT(
    SIZEOF(.itcm_audio_hot) <= 0x10000,
    "ITCM audio hotset exceeds the 64 KiB ITCM region"
);
