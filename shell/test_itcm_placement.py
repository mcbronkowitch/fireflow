"""Host test for itcm_placement.py: fed canned nm, disassembly and linker
fragment text, it must accept a sane image and refuse each way an ITCM image
can leave the board dark. Plain script, exit code is the verdict."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import itcm_placement as p

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

# A canned image. Mangled names (nm -S, not -C): the fragment lists them that
# way. Reset_Handler calls main; main calls board_init, then the loader, then
# Instrument::init; Instrument::init calls into ITCM through a veneer, as the
# AudioCallback does. Two functions sit in ITCM behind the reserved 256 bytes.
NM = """\
00000100 A __itcm_hot_start__
00000200 A __itcm_hot_end__
90118000 A __itcm_hot_load__
00000100 00000080 T _ZN4spky10Instrument7processEPKfS2_PfS3_j
00000180 00000080 T _ZN4spky4Flux7processERfS1_
24000010 00000010 T Reset_Handler
24000020 00000010 T SysTick_Handler
24000100 00000020 T _Z13AudioCallbackPKPKfPPfj
24000200 00000080 T main
24000300 00000010 T _ZN5bench10board_initERN5daisy8patch_sm12DaisyPatchSME
24000400 00000010 T _ZN5shell9itcm_loadEv
24000500 00000010 T _ZN4spky10Instrument4initEfRKNS_5FxMemE
24000600 00000010 T _Z6helperv
2400f000 00000008 t ___ZN4spky4Flux7processERfS1__veneer
2400f010 00000008 t ___ZN4spky10Instrument7processEPKfS2_PfS3_j_veneer
"""
LD = """\
    *(.text._ZN4spky10Instrument7processEPKfS2_PfS3_j)
    *(.text._ZN4spky4Flux7processERfS1_)
"""
BOOT = (
    "24000010:\tbl\t24000200 <main>\n"
    "24000204:\tbl\t24000300 <_ZN5bench10board_initERN5daisy8patch_sm12DaisyPatchSME>\n"
    "24000208:\tbl\t24000400 <_ZN5shell9itcm_loadEv>\n"
    "2400020c:\tbl\t24000500 <_ZN4spky10Instrument4initEfRKNS_5FxMemE>\n"
    "24000504:\tbl\t2400f000 <___ZN4spky4Flux7processERfS1__veneer>\n"
    "24000104:\tbl\t2400f010 <___ZN4spky10Instrument7processEPKfS2_PfS3_j_veneer>\n"
    "00000110:\tbl\t00000180 <_ZN4spky4Flux7processERfS1_>\n"
)

def refusals(nm=NM, dis=BOOT, ld=LD):
    return p.refusals(nm, dis, ld)

check("a sane image passes", refusals() == [])

# (a) The section itself.
check("an empty section is refused",
      refusals(nm=NM.replace("00000200 A __itcm_hot_end__", "00000100 A __itcm_hot_end__")) != [])
check("a section in the reserved first 256 bytes is refused",
      refusals(nm=NM.replace("00000100 A __itcm_hot_start__", "00000000 A __itcm_hot_start__")) != [])
check("a section past 64 KB is refused",
      refusals(nm=NM.replace("00000200 A __itcm_hot_end__", "00010004 A __itcm_hot_end__")) != [])
check("a load address outside QSPI_COLD is refused",
      refusals(nm=NM.replace("90118000 A __itcm_hot_load__", "24030000 A __itcm_hot_load__")) != [])
check("missing section symbols are refused",
      refusals(nm="\n".join(l for l in NM.splitlines() if "__itcm_hot_" not in l) + "\n") != [])

# (b) Nothing that can run before the copy reaches ITCM.
check("the reset path reaching ITCM is refused",
      refusals(dis=BOOT + "24000014:\tbl\t24000600 <_Z6helperv>\n"
                          "24000604:\tbl\t2400f000 <___ZN4spky4Flux7processERfS1__veneer>\n") != [])
check("an interrupt handler reaching ITCM is refused",
      refusals(dis=BOOT + "24000024:\tbl\t2400f000 <___ZN4spky4Flux7processERfS1__veneer>\n") != [])
check("a static constructor reaching ITCM is refused",
      refusals(nm=NM + "24000700 00000010 t _GLOBAL__sub_I_main\n",
               dis=BOOT + "24000704:\tbl\t2400f000 <___ZN4spky4Flux7processERfS1__veneer>\n") != [])
check("the audio callback may reach ITCM", refusals() == [])

# (c) main copies before it calls anything that reaches ITCM.
check("main calling into ITCM before the copy is refused",
      refusals(dis=BOOT.replace("24000208:\tbl\t24000400", "24000210:\tbl\t24000400")) != [])
check("main without the copy is refused",
      refusals(dis="".join(l + "\n" for l in BOOT.splitlines() if "itcm_load" not in l)) != [])

# (d) Every name the fragment lists is in the image, in ITCM.
check("a listed name missing from the image is refused",
      refusals(ld=LD + "    *(.text._ZN4spky7Missing7processEv)\n") != [])
check("a listed name outside ITCM is refused",
      refusals(ld=LD + "    *(.text._Z6helperv)\n") != [])
check("an empty fragment is refused", refusals(ld="") != [])

sys.exit(1 if FAILS else 0)
