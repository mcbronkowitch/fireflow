"""Host test for hotpath_libm.py (plan 2026-10-29-cpu-hunt-remote Task 4).

Hermetic: hand-written `objdump -d` text, no toolchain. Plain script (pytest
is not installed here), exit code is the verdict.
"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import hotpath_libm as hl
sys.stdout.reconfigure(encoding="utf-8")   # the report's arrows on a cp1252 console

FAILS = []
def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name + (("  -- " + detail) if not cond and detail else ""))
    if not cond: FAILS.append(name)


# ---- the brief's fixture: two hot functions, one calling powf twice, one
# cold function calling expf. Expected: `hot_a → powf × 2` only. ------------
FIXTURE = """
build/shell.elf:     file format elf32-littlearm


Disassembly of section .text:

24000100 <cold_c>:
24000100:	b510      	push	{r4, lr}
24000102:	f02d fc94 	bl	2402e540 <expf>
24000106:	bd10      	pop	{r4, pc}

2402e540 <powf>:
2402e540:	b510      	push	{r4, lr}
2402e542:	f000 f801 	bl	2402e548 <__math_oflowf>
2402e546:	bd10      	pop	{r4, pc}

2402e548 <expf>:
2402e548:	4770      	bx	lr

Disassembly of section .itcm_hot:

00000100 <hot_a>:
     100:	b510      	push	{r4, lr}
     102:	f02d fc94 	bl	2402e540 <powf>
     106:	f02d fc94 	bl	2402e540 <powf>
     10a:	f000 b801 	b.w	110 <hot_a+0x10>
     110:	bd10      	pop	{r4, pc}

00000120 <hot_b>:
     120:	b510      	push	{r4, lr}
     122:	f7ff ffed 	bl	100 <hot_a>
     126:	bd10      	pop	{r4, pc}
"""

out = hl.report(hl.scan(FIXTURE))
check("brief fixture: exactly `hot_a → powf × 2`", out == ["hot_a → powf × 2"], repr(out))


# ---- reachability from AudioCallback, through a veneer and a tail call ----
REACH = """
Disassembly of section .text:

24000b8c <_ZL13AudioCallbackPKPKfPPfj>:
24000b8c:	b510      	push	{r4, lr}
24000b8e:	f000 f801 	bl	24000c00 <helper>
24000b92:	bd10      	pop	{r4, pc}

24000c00 <helper>:
24000c00:	f000 b801 	b.w	24000d00 <tail>

24000d00 <tail>:
24000d00:	f02d fc94 	bl	24033fd8 <__sinf_veneer>
24000d04:	f02d fc94 	bl	24033fe0 <__cosf_veneer>
24000d08:	4770      	bx	lr

24000e00 <only_called_from_cold>:
24000e00:	f02d fc94 	bl	2402e540 <logf>
24000e04:	4770      	bx	lr

24000f00 <cold_main>:
24000f00:	f7ff fffe 	bl	24000e00 <only_called_from_cold>
24000f04:	f7ff fffe 	bl	24000b8c <_ZL13AudioCallbackPKPKfPPfj>
24000f08:	4770      	bx	lr

24033fd8 <__sinf_veneer>:
24033fd8:	f85f f000 	ldr.w	pc, [pc]	; 24033fdc <__sinf_veneer+0x4>
24033fdc:	00008bfd 	.word	0x00008bfd

24033fe0 <__cosf_veneer>:
24033fe0:	f85f f000 	ldr.w	pc, [pc]	; 24033fe4 <__cosf_veneer+0x4>
24033fe4:	00008bfd 	.word	0x00008bfd
"""

out = hl.report(hl.scan(REACH))
check("reach: tail-called callee of AudioCallback lists its veneered sinf",
      "tail → sinf × 1" in out, repr(out))
check("reach: ... and its veneered cosf", "tail → cosf × 1" in out, repr(out))
check("reach: a function only a cold caller reaches stays cold",
      not any(l.startswith("only_called_from_cold") for l in out), repr(out))
check("reach: a caller OF the callback is not hot",
      not any(l.startswith("cold_main") for l in out), repr(out))
check("reach: nothing else", len(out) == 2, repr(out))

# libm's own insides are not hot callers: log10f -> __ieee754_log10f -> logf
INTERNAL = """
Disassembly of section .itcm_hot:

00000100 <hot_x>:
     100:	f02d fc94 	bl	2402e540 <__log10f_veneer>
     104:	4770      	bx	lr

Disassembly of section .text:

2402e540 <log10f>:
2402e540:	f000 f801 	bl	2402e600 <__ieee754_log10f>

2402e600 <__ieee754_log10f>:
2402e600:	f000 f801 	bl	2402e700 <logf>
"""
out = hl.report(hl.scan(INTERNAL))
check("libm internals: no `__ieee754_log10f → logf`", out == [], repr(out))
out = hl.report(hl.scan(INTERNAL, hl.LIBM_OTHER))
check("other entry points: `hot_x → log10f × 1` through its veneer", out == ["hot_x → log10f × 1"], repr(out))

# no root at all: no AudioCallback and no .itcm_hot -> a refusal, not silence
try:
    hl.scan("Disassembly of section .text:\n\n24000100 <f>:\n24000100:\tf02d fc94 \tbl\t2402e540 <powf>\n")
    check("no root is refused", False, "no exception")
except ValueError:
    check("no root is refused", True)

if FAILS:
    print(f"\n{len(FAILS)} FAILED"); sys.exit(1)
print("\nall passed")
