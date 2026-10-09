/* Boot-only code executes in place from QSPI (spec 2026-10-09-rev-a-p6b1
   section 5), to make room in SRAM_EXEC. shell/qspi_placement.py reads the
   linked image and refuses it if anything on the audio path, the engine-switch
   path or the control tick landed here, or, by name, anything of rule 2 below.

   WHAT MAY GO HERE. A function qualifies only if all three hold, judged on the
   call graph of the linked image (arm-none-eabi-objdump -d), not on its name:

   1. Nothing hot reaches it: no interrupt handler, no address-taken function
      (callbacks, vtables, the audio callback), and nothing they call.
   2. It never runs before the board's own QSPI init has returned. libDaisy's
      DaisyPatchSM::Init() calls QSPIHandle::Init(), which takes the QSPI out
      of memory-mapped mode and puts it back; until that call has returned, a
      fetch from here has nothing behind it. So everything reachable from the
      startup code, the static constructors, System::Init (clocks, MPU,
      caches), the SDRAM init and the QSPI driver stays in SRAM, and so do
      DaisyPatchSM::Init() and main() themselves, which are on the stack across
      that call.
   3. It runs once, in main() before StartAudio(): no foreground loop.

   Each name is exact (one input section per function: libDaisy and the shell
   build with -ffunction-sections). A name that stops existing matches nothing
   and only costs room; a name that starts matching something hot is what the
   checker is for. Functions under ~16 B are left out on purpose: the long-call
   veneer an SRAM caller needs to reach QSPI costs about as much as they save.

   WHERE. Not in front of the wavetable bank but behind it, in a region of its
   own, so the bank keeps its address 0x90100000 and the bench, audition and
   qspi_programmer images (which all assume it) keep working. The bank is
   65 024 B; if it ever outgrows 0x90110000 the link fails with an overlap.

   HOW IT IS LINKED. shell/Makefile passes this file as -Wl,-T,qspi_cold.ld.
   gcc hands -Wl options to ld ahead of the -T../alt_sram.lds that libDaisy's
   core Makefile adds, so this script is read first and its exact names win
   over the main script's *(.text*). Passed as a plain -T after the main
   script, ld fails with ".text not found for insert". */

MEMORY
{
  QSPI_COLD (RX) : ORIGIN = 0x90110000, LENGTH = 0x90800000 - 0x90110000
}

SECTIONS
{
  .qspi_cold :
  {
    . = ALIGN(4);

    /* The instrument's boot: Instrument::init and the FX inits only it and
       Part::init call. Part::init, ModLane::init, Center::init,
       SuperModulator::init and every engine's init stay in SRAM: the
       checker's patterns cover them whole. */
    *(.text._ZN4spky10Instrument4initEfRKNS_5FxMemE)
    *(.text._ZN4spky13AmbientReverb4initEf)
    *(.text._ZN4spky6PartFx4initEfPfS1_)
    *(.text._ZN4spky4Grit4initEf)
    *(.text._ZN4spky4Flux4initEfPfS1_)
    *(.text._ZN4spky4Comp4initEf)
    *(.text._ZN7daisysp9Overdrive4InitEv)
    *(.text._ZN7daisysp9Decimator4InitEv)
    *(.text._ZN7daisysp17SampleRateReducer7SetFreqEf)
    *(.text._ZN7daisysp17SampleRateReducer4InitEv)

    /* The shell's boot. adc_use_measured_sampling_time is a GCC clone
       (.constprop.0), hence the trailing glob. */
    *(.text._ZL30adc_use_measured_sampling_time*)
    *(.text._ZN5shell6fx_memEv)
    *(.text._ZN5shell15panel_scan_initEv)

    /* The board's peripherals as DaisyPatchSM::Init() brings them up AFTER
       its QSPI init: ADC, DAC, codec over I2C, gate inputs, analog controls. */
    *(.text._ZN5daisy9AdcHandle4InitEPNS_16AdcChannelConfigEjNS0_12OverSamplingE)
    *(.text._ZN5daisy9AdcHandle5StartEv)
    *(.text._ZN5daisy16AdcChannelConfig10InitSingleENS_3PinENS0_15ConversionSpeedE)
    *(.text._ZNK5daisy9AdcHandle6GetPtrEh)
    *(.text.HAL_ADC_Init)
    *(.text.HAL_ADC_MspInit)
    *(.text.HAL_ADC_ConfigChannel)
    *(.text.HAL_ADC_Stop_DMA)
    *(.text.HAL_ADCEx_MultiModeConfigChannel)
    *(.text.HAL_ADCEx_Calibration_Start)
    *(.text.ADC_ConfigureBoostMode)
    *(.text.ADC_ConversionStop)
    *(.text.ADC_Disable)
    *(.text._ZN5daisy9DacHandle4InitERKNS0_6ConfigE)
    *(.text._ZN5daisy9DacHandle5StartEPtS1_jPFvPS1_jE)
    *(.text._ZN5daisy9DacHandle4StopEv)
    *(.text.HAL_DAC_Init)
    *(.text.HAL_DAC_MspInit)
    *(.text.HAL_DAC_ConfigChannel)
    *(.text.HAL_DAC_Start_DMA)
    *(.text.HAL_DAC_Stop_DMA)
    *(.text._ZN5daisy7Pcm30604InitENS_9I2CHandleE)
    *(.text._ZN5daisy9I2CHandle4InitERKNS0_6ConfigE)
    *(.text._ZN5daisy9I2CHandle4Impl8InitPinsEv)
    *(.text._ZN5daisy9I2CHandle18WriteDataAtAddressEtttPhtm)
    *(.text._ZN5daisy9I2CHandle17ReadDataAtAddressEtttPhtm)
    *(.text.HAL_I2C_Init)
    *(.text.HAL_I2C_MspInit)
    *(.text.HAL_I2C_Mem_Read)
    *(.text.HAL_I2C_Mem_Write)
    *(.text.HAL_I2CEx_ConfigAnalogFilter)
    *(.text.HAL_I2CEx_ConfigDigitalFilter)
    *(.text.I2C_WaitOnTXISFlagUntilTimeout)
    *(.text.I2C_WaitOnSTOPFlagUntilTimeout)
    *(.text._ZN5daisy4GPIO4InitEv)
    *(.text._ZN5daisy4GPIO4InitENS_3PinENS0_4ModeENS0_4PullENS0_5SpeedEPFvS1_E)
    *(.text._ZN5daisy6GateIn4InitENS_3PinEb)
    *(.text._ZN5daisy13AnalogControl4InitEPtfbbf)
    *(.text._ZN5daisy13AnalogControl13InitBipolarCvEPtfbf)
    *(.text._ZN5daisy13AnalogControl13SetSampleRateEf)
    *(.text._ZN5daisy8patch_sm12DaisyPatchSM18SetAudioSampleRateENS_9SaiHandle6Config10SampleRateE)
    *(.text._ZN5daisy8patch_sm12DaisyPatchSM17SetAudioBlockSizeEj)

    . = ALIGN(4);
  } > QSPI_COLD
}
INSERT AFTER .qspiflash_data;
