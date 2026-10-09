"""Host test for qspi_placement.py: fed nm lines, it must refuse a hot
symbol in QSPI and accept a cold one. Plain script, exit code is the verdict."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import qspi_placement as q

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

cold = "90110010 00000130 T daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)\n"
hot_in_qspi = "90100100 00000708 T spky::ModLane::follow(long, float, float)\n"
hot_in_sram = "2400a55c 00000708 T spky::ModLane::follow(long, float, float)\n"
check("cold symbol in QSPI passes", q.violations(cold) == [])
check("hot symbol in SRAM passes", q.violations(hot_in_sram) == [])
check("hot symbol in QSPI is refused",
      q.violations(hot_in_qspi) == ["spky::ModLane::follow(long, float, float)"])
check("engine switch path is hot",
      q.violations("90100200 00000010 T spky::FeedEngine::init(float)\n") != [])
check("the control law is hot",
      q.violations("90100300 00000010 T control::ControlLawT<spky::Instrument, control::NoHooks>::tick(float const*, control::Options const&, spky::Instrument&)\n") != [])
# Boot code is not all cold: whatever runs before the board's own QSPI init
# has returned (clocks, MPU, the QSPI driver itself) must stay in SRAM.
check("the system init is refused",
      q.violations("90110400 000001a4 T daisy::System::Init(daisy::System::Config const&)\n") != [])
check("the QSPI driver is refused",
      q.violations("90110500 000001b4 T daisy::QSPIHandle::Init(daisy::QSPIHandle::Config const&)\n") != [])
check("the board init is refused",
      q.violations("90110600 00000518 T daisy::patch_sm::DaisyPatchSM::Init()\n") != [])
check("a static constructor is refused",
      q.violations("90110700 00000050 t _GLOBAL__sub_I__ZN5shell6fx_memEv\n") != [])

# Exception handlers and the tick live from HAL_Init on: SysTick fires during
# the QSPI re-init, so none of them may sit in QSPI. Then the shell's per-block
# code: the scan and the control table it drives every block.
for name in ["SysTick_Handler", "HardFault_Handler", "Default_Handler",
             "NMI_Handler", "HAL_IncTick", "HAL_GetTick",
             "shell::apply_control(shell::ControlEntry const&, float, spky::Instrument&)",
             "shell::find_control(shell::ControlTable const&, int, int)",
             "shell::chain_word(shell::ChainProfile const&, shell::StepPattern, unsigned long)",
             "shell::channel_at(shell::ChainProfile const&, int, int)",
             "shell::group_at(shell::ChainProfile const&, int, int)",
             "shell::group_of_step(shell::ChainProfile const&, int)",
             "shell::mux_channel(shell::ChainProfile const&, int, int)",
             "shell::scan_steps(shell::ChainProfile const&)",
             "shell::sense_live(shell::ChainProfile const&, int, int)",
             "shell::step_of(shell::ChainProfile const&, int, int)",
             "shell::step_pattern(shell::ChainProfile const&, int)",
             "shell::coupon_span(unsigned short const*, int)",
             "shell::panel_span(unsigned short, unsigned short)",
             "shell::run_panel_scan_report(daisy::patch_sm::DaisyPatchSM&)"]:
    check("refused in QSPI: " + name.split("(")[0],
          q.violations("90110800 00000010 T " + name + "\n") == [name])
check("an nm line without a size is read",
      q.violations("90110900 T spky::ModLane::follow(long, float, float)\n")
      == ["spky::ModLane::follow(long, float, float)"])

# The call graph: names alone do not say who CALLS the cold code. A canned
# image: a hot callback, main, the system init, one linker veneer into QSPI,
# two cold functions. Calls from SRAM reach QSPI through the veneer, whose
# literal word is its branch target.
NM = """\
24000100 00000020 T AudioCallback(float const* const*, float**, unsigned int)
24000200 00000040 T main
24000300 00000010 T daisy::System::Init(daisy::System::Config const&)
24000400 00000010 T helper()
24000500 00000010 T spky::Part::init(float)
24000600 00000010 T spky::Part::retune(float)
2400f000 00000008 t ___ZN4spky13AmbientReverb4initEf_veneer
90110000 00000040 T spky::AmbientReverb::init(float)
90110040 00000020 T daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)
"""
VENEER_WORD = [(0x2400f004, 0x90110001)]
BOOT_CALL = "24000210:\tbl\t2400f000 <___ZN4spky13AmbientReverb4initEf_veneer>\n"
def reached(dis, words=()):
    return sorted(v[0] for v in q.graph_violations(NM, dis, list(VENEER_WORD) + list(words)))
check("graph: a boot call into QSPI passes", reached(BOOT_CALL) == [])
check("graph: a hot call into QSPI is refused",
      reached(BOOT_CALL + "24000104:\tbl\t2400f000 <___ZN4spky13AmbientReverb4initEf_veneer>\n")
      == ["spky::AmbientReverb::init(float)"])
check("graph: a hot call through a helper is refused",
      reached(BOOT_CALL + "24000108:\tb.w\t24000400 <helper()>\n"
              "24000404:\tbl\t90110040 <daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)>\n")
      == ["daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)"])
check("graph: a call from the pre-QSPI window is refused",
      reached(BOOT_CALL + "24000304:\tbl\t90110040 <daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)>\n")
      == ["daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)"])
# spky::Part::* is a family pattern: it keeps Part out of QSPI, but a Part
# function only the boot path reaches is judged by the graph, while one the
# boot path never reaches is called some way the graph cannot see.
check("graph: a family function only boot reaches may call cold code",
      reached(BOOT_CALL + "24000220:\tbl\t24000500 <spky::Part::init(float)>\n"
              "24000504:\tbl\t90110040 <daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)>\n")
      == [])
check("graph: a family function boot never reaches may not",
      reached(BOOT_CALL + "24000604:\tbl\t90110040 <daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)>\n")
      == ["daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)"])
check("graph: a family function boot reaches is still hot if a root does",
      reached(BOOT_CALL + "24000220:\tbl\t24000500 <spky::Part::init(float)>\n"
              "24000504:\tbl\t90110040 <daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)>\n"
              "2400010c:\tbl\t24000500 <spky::Part::init(float)>\n")
      == ["daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)"])
check("graph: a pointer to cold code is refused",
      reached(BOOT_CALL, [(0x24010000, 0x90110041)])
      == ["daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)"])

# The gate must not fail open: a graph it cannot trust is a refusal, not a
# pass. The canned image gets its vector table (Reset_Handler, SysTick_Handler)
# and the literal that registers the audio callback.
NM2 = NM + "24000010 00000004 T Reset_Handler\n24000020 00000004 T SysTick_Handler\n"
TRUSTED = [(0x24000004, 0x24000011), (0x2400003c, 0x24000021), (0x24000234, 0x24000101)]
def problems(dis=BOOT_CALL, words=None, nm=NM2):
    w = list(VENEER_WORD) + TRUSTED if words is None else words
    return q.graph_problems(nm, dis, w)
check("sane: the canned image is trusted", problems() == [])
check("sane: an empty disassembly is refused", problems(dis="") != [])
check("sane: no vector table is refused",
      problems(words=list(VENEER_WORD) + TRUSTED[2:]) != [])
check("sane: an unregistered audio callback is refused",
      problems(words=list(VENEER_WORD) + TRUSTED[:2]) != [])
check("sane: a veneer that resolves to nothing is refused", problems(words=TRUSTED) != [])
check("sane: a branch into QSPI outside any function is refused",
      problems(dis=BOOT_CALL + "24000220:\tbl\t90120000 <somewhere>\n") != [])
check("sane: a stored code address in QSPI outside any function is refused",
      problems(words=list(VENEER_WORD) + TRUSTED + [(0x24010000, 0x90120001)]) != [])
# Two kinds of stored words that are not code pointers: a switch's jump table
# inside its own function (GPIO::Init has one), and the wavetable bank's
# samples, QSPI data that may happen to look like a QSPI code address.
check("sane: a jump table inside a cold function is not a pointer",
      problems(words=list(VENEER_WORD) + TRUSTED + [(0x90110030, 0x90110011)]) == []
      and reached(BOOT_CALL, [(0x90110030, 0x90110011)]) == [])
check("sane: bank samples are not code pointers",
      problems(words=list(VENEER_WORD) + TRUSTED + [(0x90100698, 0x904e8ff5)]) == [])
check("sane: a branch into QSPI from code no symbol owns is refused",
      problems(dis=BOOT_CALL + "24020000:\tbl\t90110000 <spky::AmbientReverb::init(float)>\n") != [])
check("sane: a code symbol without size in QSPI is refused",
      problems(nm=NM2 + "90120000 T cold_asm\n") != [])
check("sane: a linker boundary symbol in QSPI is not",
      problems(nm=NM2 + "90100000 T _sqspiflash_text\n90100000 T __qspiflash_text_start\n") == [])
# A code symbol without a size (memchr, __aeabi_uldivmod) owns the bytes up to
# the next symbol, so its calls are edges, not dropped.
check("graph: a size-less function's calls are followed",
      sorted(v[0] for v in q.graph_violations(
          NM2 + "24000700 T memchr_like\n",
          BOOT_CALL + "24000704:\tbl\t90110040 <daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)>\n",
          list(VENEER_WORD) + TRUSTED + [(0x24010010, 0x24000701)]))
      == ["daisy::I2CHandle::Init(daisy::I2CHandle::Config const&)"])

# elf_words on a hand-built ELF: every word of an allocated data section, only
# the $d stretch of a code section, nothing of a NOBITS section.
def tiny_elf():
    import struct
    names = b"\0$t\0$d\0"
    isr = struct.pack("<II", 0x24000011, 0x24000021)
    text = struct.pack("<IIII", 0xdeadbeef, 0x24000101, 0x24000101, 0x90110001)
    sym = (b"\0" * 16
           + struct.pack("<IIIBBH", 1, 0x24000100, 0, 0, 0, 2)    # $t
           + struct.pack("<IIIBBH", 4, 0x24000108, 0, 0, 0, 2))   # $d
    body = isr + text + sym + names
    off_isr, off_text = 52, 52 + len(isr)
    off_sym = off_text + len(text)
    off_str = off_sym + len(sym)
    shoff = 52 + len(body)
    sh = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
          (0, 1, 3, 0x24000000, off_isr, len(isr), 0, 0, 4, 0),      # .isr_vector WA
          (0, 1, 6, 0x24000100, off_text, len(text), 0, 0, 4, 0),    # .text AX
          (0, 8, 3, 0x24001000, 0, 16, 0, 0, 4, 0),                  # .bss NOBITS
          (0, 2, 0, 0, off_sym, len(sym), 5, 1, 4, 16),              # .symtab
          (0, 3, 0, 0, off_str, len(names), 0, 0, 1, 0)]             # .strtab
    hdr = bytearray(52)
    hdr[0:4] = b"\x7fELF"
    struct.pack_into("<I", hdr, 0x20, shoff)
    struct.pack_into("<HHH", hdr, 0x2E, 40, len(sh), 0)
    return bytes(hdr) + body + b"".join(struct.pack("<IIIIIIIIII", *s) for s in sh)
check("elf_words: data sections whole, code sections by $d, no NOBITS",
      q.elf_words(tiny_elf()) == [(0x24000000, 0x24000011), (0x24000004, 0x24000021),
                                  (0x24000108, 0x24000101), (0x2400010c, 0x90110001)])

# The command line needs all three inputs: a name-only run would skip the graph.
import subprocess, tempfile
with tempfile.NamedTemporaryFile("w", suffix=".nm", delete=False) as f:
    f.write(NM2)
rc = subprocess.run([sys.executable, os.path.join(here, "qspi_placement.py"), f.name],
                    capture_output=True).returncode
os.unlink(f.name)
check("cli: the nm file alone is refused", rc == 1)
sys.exit(1 if FAILS else 0)
