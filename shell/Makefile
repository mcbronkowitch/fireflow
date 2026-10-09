# Die erste FireFlow-Firmware, die engine/ enthaelt. Auslieferbar gedacht,
# anders als bench/ -- deshalb KEIN USE_DAISYSP_LGPL hier (siehe unten).
#
# Modelled on bench/Makefile. (The repo root used to carry the upstream
# Spotykach firmware, which compiled no engine/ at all; it was removed on
# 2026-09-29, see docs/upstream-firmware.md.)

TARGET = shell

CPP_STANDARD = -std=c++17

LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR  = ../lib/DaisySP

# NICHT gesetzt, anders als in bench/Makefile: die Bench misst die
# LGPL-Module in Familie 2 und darf sie deshalb hereinziehen, weil sie nie
# ausgeliefert wird. Diese Firmware wird ausgeliefert. Geprueft, dass es
# reicht: engine/ inkludiert aus DaisySP nur Effects/{overdrive,decimator,
# sampleratereducer}.h, Filters/onepole.h, Utility/{dcblock,delayline,dsp}.h
# und Dynamics/crossfade.h -- alles im MIT-Teil. Taucht hier je ein
# undefined reference auf einen LGPL-Typ auf, ist das eine Lizenzfrage und
# keine Build-Frage: dann gehoert der Aufrufer in engine/ geaendert, nicht
# diese Zeile eingeschaltet.
# USE_DAISYSP_LGPL = 1

# Same linkage as the bench, on purpose: that is what makes the bench's
# numbers say something about this firmware.
APP_TYPE = BOOT_SRAM
LDSCRIPT = ../alt_sram.lds

# Die Engine inkludiert DaisySP-Header relativ zu Source/ ("Effects/
# overdrive.h", "Utility/dsp.h"), also muss das Verzeichnis auf den Pfad.
# ../src/ ist fuer genau einen Header drauf: hw/board.h -- derselbe, den die
# Bench einbindet, und der einzige geteilte Code zwischen beiden Baeumen.
C_INCLUDES = -I. -I../ -I../src/ -I../engine/ -I../third_party/ -I../lib/DaisySP/Source/
C_USR_FLAGS = -ffast-math -funroll-loops

# Dieses Board und nur dieses. Ein blankes -D ist hier zulaessig, obwohl die
# Bench fuer denselben Schalter einen generierten Header baut: dort ist
# BENCH_BOARD eine Make-Variable, die zwischen zwei Werten umgestellt wird,
# und ein bestehendes build/ wuerde ein veraltetes Objekt mit der Init des
# ANDEREN Boards relinken. Hier gibt es nichts umzustellen -- der Wert ist
# konstant, also kann er nicht veralten. Kommt jemals ein zweites Board in
# diesen Shell, wird daraus dieselbe Header-Konstruktion wie in bench/.
#
# Der Name sagt BENCH_, obwohl das hier Firmware ist. Das ist eine Altlast
# aus src/hw/board.h und stoert; umbenannt wird sie NICHT nebenbei, denn sie
# haengt an bench/main.cpp, anchor.cpp, Makefile, write_bench_board.py und
# run.py -- also an der Messkette, die gerade erst als bit-identisch
# nachgewiesen wurde. Eigener Commit, eigener Beweis.
C_DEFS += -DBENCH_BOARD_PATCH_SM

# The firmware drops the parameter-name strings (engine/param_table.h): nothing
# in shell/ reads ParamInfo::name, and the strings cost 752 B of SRAM_EXEC
# (measured) in a region the play images fill to the byte (spec 2026-10-02-rev-a-p6a-panel-scan-design.md
# section 6).
C_DEFS += -DSPKY_NO_PARAM_NAMES

# --- Selbsttest ----------------------------------------------------------
# Das Submodule hat keine Klinkenbuchse. "Macht es Ton" laesst sich deshalb
# ohne Verdrahtung nicht hoeren -- aber messen: der Shell laeuft seinen
# eigenen Ausgang eine Sekunde lang ueber einen Peak-Detektor und legt das
# Urteil auf die User-LED. Siehe main.cpp.
#
# Default AUS, und das ist der wichtige Teil: der Detektor sitzt IM
# Audio-Callback und kostet dort Zyklen. Task 6 misst genau an dieser Stelle
# den Shell-Aufschlag gegen 2,17 Punkte Reserve; ein mitgeschleppter
# Selbsttest wuerde in diese Zahl hineinlaufen. Wer misst, misst mit
# SHELL_SELFTEST=0 -- also mit dem Default.
SHELL_SELFTEST ?= 0

ifneq ($(filter $(SHELL_SELFTEST),0 1),$(SHELL_SELFTEST))
$(error SHELL_SELFTEST must be 0 or 1)
endif

# --- CPU-Sonde -----------------------------------------------------------
# Legt einen CpuLoadMeter um process() und meldet die Last ueber USB-CDC.
# Das ist das Verfahren, das Task 6 Schritt 7 ohnehin verlangt, hier schon
# gebaut, weil ein Stoerton auf der Blockrate ohne diese Zahl nicht zu
# beurteilen ist (shell/README.md, "Offener Befund").
#
# Default AUS, aus zwei Gruenden: der Meter kostet im Callback Zyklen, und
# USB-CDC kostet auf diesem Board zusaetzlich 6370 Zyklen pro Block (0,66 %,
# docs/bench/2026-08-07-transport-semihost-vs-usb.md) -- eine Firmware, die
# beides mitschleppt, misst sich selbst.
SHELL_CPU_PROBE ?= 0

ifneq ($(filter $(SHELL_CPU_PROBE),0 1),$(SHELL_CPU_PROBE))
$(error SHELL_CPU_PROBE must be 0 or 1)
endif

# --- Mux-Sonde -----------------------------------------------------------
# Drei Stellungen, nicht zwei: 0 aus, 1 der Scan IM Audio-Callback, 2 der
# Scan im Vordergrund, getaktet auf einen Schritt pro Block. Die Frage
# dieser Runde ist genau der Unterschied zwischen 1 und 2 -- deshalb muss
# derselbe Arbeitsumfang an beiden Stellen laufen und nicht ungefaehr
# derselbe. Plan:
# ../docs/superpowers/plans/2026-08-23-mux-scan-placement-probe.md
SHELL_MUX_PROBE ?= 0

ifneq ($(filter $(SHELL_MUX_PROBE),0 1 2),$(SHELL_MUX_PROBE))
$(error SHELL_MUX_PROBE must be 0, 1 or 2)
endif

# --- Leerlauf-Fuellung ---------------------------------------------------
# 0 `while(1) {}` -- Sprung auf sich selbst, kein Speicherzugriff. Die Form,
#   die das Audio-Bild bisher hatte.
# 1 eine Schleife, die pro Iteration ein volatile aus dem Speicher laedt --
#   die Form, die das CPU-Sonden-Bild hat.
# 2 dieselbe Schleife plus echte ALU-Arbeit.
#
# Der Zweck ist eine Ablation, KEIN Betriebsmodus: das 500-Hz-Artefakt liegt
# zwischen Audio-Bild und Sonden-Bild 15,5 dB auseinander, obwohl beide
# dieselbe Engine im selben Betriebspunkt fahren. Der Leerlauf ist der
# groesste unbeachtete Unterschied zwischen ihnen, und der 8. August hat
# gemessen, dass Leerlauf-Inhalt den Ton um 7,5 dB bewegt. Capture:
# ../docs/bench/2026-08-23-978cbaf-artifact-triage.md
SHELL_IDLE_FILL ?= 0

ifneq ($(filter $(SHELL_IDLE_FILL),0 1 2),$(SHELL_IDLE_FILL))
$(error SHELL_IDLE_FILL must be 0, 1 or 2)
endif

# --- Coupon-Sonde --------------------------------------------------------
# 0 aus, 1 der Bring-up-Scan fuer den Testcoupon (hardware/coupon/): laeuft
# im Vordergrund, haelt jeden Kanal 5 ms, liest den frei laufenden ADC und
# meldet pro Kanal ein Urteil ueber USB-CDC. KEIN Zeitmass -- die 1,6-us-
# Frage ist die Settle-Sonde, nicht diese.
#
# Stellung 1 baut ausserdem das Kettenprofil des Coupons statt des Panels;
# die beiden Platinen haben nicht eine Zahl gemeinsam. Plan:
# ../docs/superpowers/plans/2026-09-17-coupon-bring-up.md
SHELL_COUPON_PROBE ?= 0

ifneq ($(filter $(SHELL_COUPON_PROBE),0 1),$(SHELL_COUPON_PROBE))
$(error SHELL_COUPON_PROBE must be 0 or 1)
endif

# Position 1 replaces the instrument with the settle probe: the coupon's mux
# channels are stepped one pair at a time and timed through the ADC. It needs
# the coupon chain profile, so it implies SHELL_COUPON_PROBE's board -- but
# not its scan, which is a different measurement.
# Spec:  ../docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-17-coupon-settle-probe.md
SHELL_SETTLE_PROBE ?= 0

ifneq ($(filter $(SHELL_SETTLE_PROBE),0 1),$(SHELL_SETTLE_PROBE))
$(error SHELL_SETTLE_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_SETTLE_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_SETTLE_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
endif

# Position 1 replaces the instrument with the crosstalk probe: one mux
# channel is held still and one controlled digital event is fired on the same
# board at a chosen time before the ADC's aperture. It needs the coupon chain
# profile, so it implies SHELL_COUPON_PROBE's board -- but not its scan, and
# not the settle probe's sweep either. The two probe switches are mutually
# exclusive: both dispatch before StartAudio and neither returns, so an image
# with both set would run whichever main.cpp reaches first and print the other
# one's name nowhere.
# Spec:  ../docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-18-coupon-crosstalk-probe.md
SHELL_XTALK_PROBE ?= 0

ifneq ($(filter $(SHELL_XTALK_PROBE),0 1),$(SHELL_XTALK_PROBE))
$(error SHELL_XTALK_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_XTALK_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_XTALK_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
ifeq ($(SHELL_SETTLE_PROBE),1)
$(error SHELL_XTALK_PROBE=1 and SHELL_SETTLE_PROBE=1 are mutually exclusive: \
both dispatch before StartAudio and neither returns)
endif
endif

# Whether RV4 is fitted on the coupon. Spec section 4: with the 16:1 enabled,
# toggling MUX_A3 moves it between channel 6 -- RV4's wiper, FLOATING until
# the pot is in -- and channel 14 (AGND). Switching a floating node into
# ADC_9 is a different experiment, so those four cases are reported as
# skipped=1 until this says otherwise. A skipped case is a LINE in the
# output, never a silently shorter table.
#
# NOT a -D, and that is the same trap as the switch above: a bare -D is
# invisible to make's dependency graph and an existing build/ would happily
# reuse an xtalk_probe.o built with the other value -- an image that reports
# four cases as skipped when the pot is in, or measures a floating wiper when
# it is not, and either way looks exactly like a measurement. It rides in the
# generated header beside SHELL_XTALK_PROBE.
SHELL_XTALK_RV4 ?= 0

ifneq ($(filter $(SHELL_XTALK_RV4),0 1),$(SHELL_XTALK_RV4))
$(error SHELL_XTALK_RV4 must be 0 or 1)
endif

# Position 1 replaces the instrument with the codec-tone probe: a tone of
# chosen frequency and level runs on AUDIO_OUT_L/R through the analog domain
# while a mux channel is read. Unlike every other probe switch here it DOES
# call StartAudio -- that is the point, and it is why it cannot share an
# image with the crosstalk probe, whose floor is measured with the codec
# stopped.
# Spec:  ../docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-18-coupon-codec-tone-probe.md
SHELL_TONE_PROBE ?= 0

ifneq ($(filter $(SHELL_TONE_PROBE),0 1),$(SHELL_TONE_PROBE))
$(error SHELL_TONE_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_TONE_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_TONE_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
ifeq ($(SHELL_SETTLE_PROBE),1)
$(error SHELL_TONE_PROBE=1 and SHELL_SETTLE_PROBE=1 are mutually exclusive)
endif
ifeq ($(SHELL_XTALK_PROBE),1)
$(error SHELL_TONE_PROBE=1 and SHELL_XTALK_PROBE=1 are mutually exclusive: \
the crosstalk probe measures its floor with the codec stopped, and an image \
that starts audio cannot produce that floor)
endif
endif

# Whether the submodule's audio output is DC-coupled at B1/B2 -- MEASURED
# 2026-09-18, handheld multimeter, DC volts, black probe on TP_AGND, red
# probe on TP_AUDIO_L, spec section 9 and the codec-tone plan's Task 1: the
# -6 dBFS constant read -4.34 V, steady, tracking the callback (collapses
# toward zero on RESET, returns to exactly -4.34 V when the callback
# restarts) -- no coupling capacitor. 1 adds the static-level row (the codec
# held at a constant, the simplest possible aggressor); 0 would leave it out,
# because a static row measured through a coupling capacitor reads as
# silence and looks like a clean result -- that was the conservative default
# before this reading. The question is answered, not open, so the default is
# now 1.
#
# LOAD-BEARING BEYOND THE FIRMWARE: CMakeLists.txt also reads this file, by
# an anchored regex, to derive the host test suite's SHELL_TONE_DC default,
# so that value is never hand-copied a second time. The anchor requires
# exactly one line shaped like this one; moving, duplicating, or
# reformatting it so the match disappears or stops being unique makes CMake
# configure fail loudly rather than silently keep a stale host default.
SHELL_TONE_DC ?= 1

ifneq ($(filter $(SHELL_TONE_DC),0 1),$(SHELL_TONE_DC))
$(error SHELL_TONE_DC must be 0 or 1)
endif

# Position 1 replaces the instrument with the wait-sweep probe: a settled mux
# channel is converted, the converter idles a commanded W, and the channel is
# converted again, for W from back to back to 50 ms. It runs the codec only
# inside its own bridge arm, so it is exclusive with every other probe
# switch for the same reason they are exclusive with each other: each
# dispatches before StartAudio and none returns.
# Spec:  ../docs/superpowers/specs/2026-09-27-coupon-wait-sweep-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-27-coupon-wait-sweep-probe.md
SHELL_WAIT_PROBE ?= 0

ifneq ($(filter $(SHELL_WAIT_PROBE),0 1),$(SHELL_WAIT_PROBE))
$(error SHELL_WAIT_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_WAIT_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_WAIT_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
ifeq ($(SHELL_SETTLE_PROBE),1)
$(error SHELL_WAIT_PROBE=1 and SHELL_SETTLE_PROBE=1 are mutually exclusive)
endif
ifeq ($(SHELL_XTALK_PROBE),1)
$(error SHELL_WAIT_PROBE=1 and SHELL_XTALK_PROBE=1 are mutually exclusive)
endif
ifeq ($(SHELL_TONE_PROBE),1)
$(error SHELL_WAIT_PROBE=1 and SHELL_TONE_PROBE=1 are mutually exclusive)
endif
endif

# Round four: adds the three pots (RV2, RV4, RV6 at mid travel) to the settle
# probe as six extra pairs, or to the wait probe as three extra victims. It
# selects nothing on its own, so it is an error without exactly one of those
# two probes. Position 0 is today's six-pair / five-victim image, unchanged.
# Spec:  ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
# Plan:  ../docs/superpowers/plans/2026-09-28-coupon-pot-round.md
SHELL_POT_ROUND ?= 0

ifneq ($(filter $(SHELL_POT_ROUND),0 1),$(SHELL_POT_ROUND))
$(error SHELL_POT_ROUND must be 0 or 1)
endif

ifeq ($(SHELL_POT_ROUND),1)
ifeq ($(filter 1,$(SHELL_SETTLE_PROBE) $(SHELL_WAIT_PROBE)),)
$(error SHELL_POT_ROUND=1 needs SHELL_SETTLE_PROBE=1 or SHELL_WAIT_PROBE=1: \
it adds the pots to one of those two probes and selects nothing on its own)
endif
endif

# The panel scan's measurement image (spec 2026-09-28-coupon-panel-scan-
# design.md section 4). The engine plays at the fixed operating point, the
# scan runs at the start of the callback in three arms, and nothing it reads
# reaches the engine. Coupon only: its arms walk the coupon's 24 steps.
SHELL_SCAN_CHECK ?= 0

ifneq ($(filter $(SHELL_SCAN_CHECK),0 1),$(SHELL_SCAN_CHECK))
$(error SHELL_SCAN_CHECK must be 0 or 1)
endif

ifeq ($(SHELL_SCAN_CHECK),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_SCAN_CHECK=1 needs SHELL_COUPON_PROBE=1: its arms walk the \
coupon's 24 steps)
endif
ifneq ($(filter 1,$(SHELL_SETTLE_PROBE) $(SHELL_XTALK_PROBE) $(SHELL_TONE_PROBE) $(SHELL_WAIT_PROBE) $(SHELL_CPU_PROBE)),)
$(error SHELL_SCAN_CHECK=1 excludes the settle, xtalk, tone, wait and CPU probes)
endif
ifneq ($(SHELL_MUX_PROBE),0)
$(error SHELL_SCAN_CHECK=1 excludes SHELL_MUX_PROBE)
endif
endif

# The playing image (spec 2026-09-28-coupon-panel-scan-design.md section 5):
# the scan runs first in the callback and drives the engine through the
# board's control table. With SHELL_COUPON_PROBE=1 it is the coupon's chain
# and table; without it, kRevaChain and the generated 70-row table
# (generated_panel_map.h).
SHELL_PANEL_SCAN ?= 0

ifneq ($(filter $(SHELL_PANEL_SCAN),0 1),$(SHELL_PANEL_SCAN))
$(error SHELL_PANEL_SCAN must be 0 or 1)
endif

ifeq ($(SHELL_PANEL_SCAN),1)
ifneq ($(filter 1,$(SHELL_SETTLE_PROBE) $(SHELL_XTALK_PROBE) $(SHELL_TONE_PROBE) $(SHELL_WAIT_PROBE) $(SHELL_CPU_PROBE) $(SHELL_SCAN_CHECK)),)
$(error SHELL_PANEL_SCAN=1 excludes the settle, xtalk, tone, wait, CPU and \
scan-check images)
endif
ifneq ($(SHELL_MUX_PROBE),0)
$(error SHELL_PANEL_SCAN=1 excludes SHELL_MUX_PROBE)
endif
endif

CPP_SOURCES = \
	main.cpp \
	sdram_mem.cpp \
	mux_plan.cpp \
	coupon_expect.cpp \
	coupon_scan.cpp \
	settle_plan.cpp \
	probe_adc.cpp \
	settle_probe.cpp \
	xtalk_plan.cpp \
	xtalk_probe.cpp \
	tone_probe.cpp \
	tone_plan.cpp \
	wait_probe.cpp \
	wait_plan.cpp \
	pot_report.cpp \
	scan_check_plan.cpp \
	scan_check.cpp \
	scan_value.cpp \
	controls.cpp \
	keys.cpp \
	panel_scan.cpp \
	mux_scan.cpp \
	../engine/body/mode_bank.cpp \
	../engine/body/ks_string.cpp \
	../engine/body/body_voice.cpp \
	../engine/mod/lane.cpp \
	../engine/mod/super_modulator.cpp \
	../engine/sampler/sampler_engine.cpp \
	../engine/sampler/sample_buffer.cpp \
	../engine/sampler/slice_map.cpp \
	../engine/parts/part.cpp \
	../engine/parts/bbd_engine.cpp \
	../engine/feed/feed_engine.cpp \
	../engine/instrument.cpp \
	../engine/center/center.cpp \
	../engine/fx/grit.cpp \
	../engine/fx/flux.cpp \
	../engine/fx/bbd.cpp \
	../engine/fx/reverb.cpp \
	../engine/fx/part_fx.cpp \
	../engine/fx/comp.cpp \
	../engine/synth/voice.cpp \
	../engine/synth/synth_engine.cpp \
	../engine/synth/wt_bank.cpp

# -O3, und das ist nicht optional. libDaisys core/Makefile setzt `OPT ?= -O2`,
# und mit dem Default lief dieser Shell am 8. August hoerbar in den Ueberlauf:
# ein Stoerton mit konstanter Amplitude auf 48000/96 = 500 Hz, dazu Zerren.
# JEDE Zahl in docs/bench/ ist mit -O3 gemessen (bench/Makefile, o3), also ist
# -O2 hier nicht "etwas langsamer", sondern eine Firmware, ueber deren CPU-Last
# nichts bekannt ist. Wer das aendert, macht die gesamte Messhistorie fuer
# diese Firmware ungueltig.
#
# `override`, weil OPT aus dem eingebundenen Makefile mit ?= kommt und eine
# Kommandozeilen-Zuweisung sonst gewinnen wuerde -- dieselbe Form, die
# bench/Makefile fuer seinen Optimierungsschalter benutzt.
override OPT := -O3

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# The one exception to -O3 above: the parallel step model's runtime loops in
# mux_plan.cpp are inlined by -O3 into ~2.6 KB that the play images have no
# room for (SRAM_EXEC, spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 6,
# decided 2026-10-03). The scan path runs once per block, not per sample.
# After the include on purpose: BUILD_DIR is defined there, and above it this
# line would name the target "/mux_plan.o" and silently do nothing.
$(BUILD_DIR)/mux_plan.o: override OPT := -Os

# Boot-only code executes in place from QSPI (spec 2026-10-09-rev-a-p6b1
# section 5); the list and the rules for what may go on it are in
# qspi_cold.ld. As -Wl,-T and not as a second plain -T: gcc puts -Wl options
# ahead of the -T$(LDSCRIPT) in LDFLAGS, and the fragment has to be read first
# for its names to win over the main script's *(.text*). The edge makes an
# edit to the list relink.
LDFLAGS += -Wl,-T,qspi_cold.ld
$(BUILD_DIR)/$(TARGET).elf: qspi_cold.ld
NM := $(CP:objcopy=nm)
OBJDUMP := $(CP:objcopy=objdump)

# --- generierte Schalter-Header ------------------------------------------
# Als echte Header und mit echter Abhaengigkeitskante, nicht als blankes
# -D. Die Begruendung steht in write_shell_selftest.py; es ist dieselbe
# stale-Objekt-Falle, an der die Bench schon einmal haengengeblieben ist.
#
# UND DIE KANTE ALLEIN REICHT NICHT -- warum, steht im naechsten Block. Wer
# hier ein Objekt vergisst, das einen Schalter liest, bekommt genau diesen
# Fehler zurueck, und er sieht wie eine Messung aus.
C_INCLUDES += -I$(BUILD_DIR)

SWITCH_OBJECTS = $(BUILD_DIR)/main.o $(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o $(BUILD_DIR)/settle_probe.o $(BUILD_DIR)/probe_adc.o $(BUILD_DIR)/xtalk_probe.o $(BUILD_DIR)/tone_probe.o $(BUILD_DIR)/wait_probe.o $(BUILD_DIR)/scan_check.o $(BUILD_DIR)/panel_scan.o

# ZUR PARSE-ZEIT, nicht als Regel, und das ist der Kern der Reparatur vom
# 2026-08-23. Als Regel mit FORCE hat es ZWEIMAL nicht gehalten:
#
#   1. Die Zeitstempel-Kante allein reicht nicht. Der geschriebene Header lag
#      0,36 s hinter main.o -- dieselbe Wanduhrsekunde, fuer Make also "nicht
#      neuer", also kein Neuuebersetzen.
#   2. Das Objekt aus dem Rezept heraus zu loeschen reicht auch nicht: Make
#      hat den Verzeichnisinhalt zu diesem Zeitpunkt laengst gelesen und
#      merkt das Verschwinden innerhalb desselben Laufs nicht.
#
# Beide Male kamen Images heraus, die sich byte-identisch verglichen, obwohl
# der Schalter anders stand. $(shell ...) laeuft, waehrend das Makefile
# gelesen wird -- also bevor Make den Graphen baut und bevor es irgendetwas
# gestattet hat. Aendert sich der Wert, ist main.o weg, wenn der Graph
# entsteht, und es gibt nichts zu glauben.
SWITCH_HEADERS := \
  $(shell python write_shell_selftest.py $(BUILD_DIR)/shell_selftest.h $(SHELL_SELFTEST) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_cpu_probe.py $(BUILD_DIR)/shell_cpu_probe.h $(SHELL_CPU_PROBE) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_mux_probe.py $(BUILD_DIR)/shell_mux_probe.h $(SHELL_MUX_PROBE) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_idle_fill.py $(BUILD_DIR)/shell_idle_fill.h $(SHELL_IDLE_FILL) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_coupon_probe.py $(BUILD_DIR)/shell_coupon_probe.h $(SHELL_COUPON_PROBE) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_settle_probe.py $(BUILD_DIR)/shell_settle_probe.h $(SHELL_SETTLE_PROBE) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_xtalk_probe.py $(BUILD_DIR)/shell_xtalk_probe.h $(SHELL_XTALK_PROBE) $(SHELL_XTALK_RV4) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_tone_probe.py $(BUILD_DIR)/shell_tone_probe.h $(SHELL_TONE_PROBE) $(SHELL_TONE_DC) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_wait_probe.py $(BUILD_DIR)/shell_wait_probe.h $(SHELL_WAIT_PROBE) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_pot_round.py $(BUILD_DIR)/shell_pot_round.h $(SHELL_POT_ROUND) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_scan_check.py $(BUILD_DIR)/shell_scan_check.h $(SHELL_SCAN_CHECK) $(SWITCH_OBJECTS)) \
  $(shell python write_shell_panel_scan.py $(BUILD_DIR)/shell_panel_scan.h $(SHELL_PANEL_SCAN) $(SWITCH_OBJECTS)) \
  $(shell python write_git_hash.py $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/xtalk_probe.o $(BUILD_DIR)/tone_probe.o $(BUILD_DIR)/wait_probe.o $(BUILD_DIR)/pot_report.o $(BUILD_DIR)/scan_check.o)

# The git stamp deletes only xtalk_probe.o, tone_probe.o, wait_probe.o, pot_report.o and scan_check.o, because those are the only objects that read it. Listing the whole SWITCH_OBJECTS there would
# force a full rebuild of seven objects on every commit for a string in one
# format argument.

$(BUILD_DIR)/main.o: $(BUILD_DIR)/shell_selftest.h $(BUILD_DIR)/shell_cpu_probe.h $(BUILD_DIR)/shell_mux_probe.h $(BUILD_DIR)/shell_idle_fill.h $(BUILD_DIR)/shell_coupon_probe.h $(BUILD_DIR)/shell_xtalk_probe.h $(BUILD_DIR)/shell_tone_probe.h $(BUILD_DIR)/shell_wait_probe.h $(BUILD_DIR)/shell_scan_check.h $(BUILD_DIR)/shell_panel_scan.h
$(BUILD_DIR)/wait_probe.o: $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/shell_coupon_probe.h
$(BUILD_DIR)/xtalk_probe.o: $(BUILD_DIR)/shell_xtalk_probe.h $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/shell_coupon_probe.h
$(BUILD_DIR)/tone_probe.o: $(BUILD_DIR)/shell_tone_probe.h $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/shell_coupon_probe.h
$(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o: $(BUILD_DIR)/shell_coupon_probe.h $(BUILD_DIR)/shell_panel_scan.h
$(BUILD_DIR)/settle_probe.o $(BUILD_DIR)/wait_probe.o: $(BUILD_DIR)/shell_pot_round.h
$(BUILD_DIR)/pot_report.o: $(BUILD_DIR)/shell_git_hash.h
$(BUILD_DIR)/scan_check.o: $(BUILD_DIR)/shell_scan_check.h $(BUILD_DIR)/shell_coupon_probe.h $(BUILD_DIR)/shell_git_hash.h
$(BUILD_DIR)/panel_scan.o: $(BUILD_DIR)/shell_panel_scan.h $(BUILD_DIR)/shell_coupon_probe.h

.PHONY: FORCE
FORCE:

# --- die zwei physischen Images -----------------------------------------
# libDaisys `all` wuerde ein flaches shell.bin ueber SRAM (0x24000000) UND
# QSPI (0x90100000) bauen -- 17 MB, fast alles Fuellbytes, an die falsche
# Adresse gezielt. Stattdessen zwei Artefakte aus derselben gelinkten ELF,
# genau wie bench/qspi_tools.py es fuer die Bench macht:
#
#   shell-sram.bin  -- everything but QSPI_SECTIONS below; goes over DFU to
#                      0x90040000 and is copied to SRAM by the Daisy
#                      bootloader.
#   shell-qspi.bin  -- the bank and the boot-only code behind it (see below).
#                      Since 2026-10-09 it changes with the code: always flash
#                      both images, QSPI first.
# .qspiflash_text (alt_sram.lds) sits in FRONT of the wavetable bank and is
# empty, and must stay empty: anything placed there shifts the bank off
# 0x90100000, where bench/, bench/audition and qspi_programmer expect it.
# Cold code goes to .qspi_cold (qspi_cold.ld), BEHIND the bank at 0x90110000.
# All three travel as one image, shell-qspi.bin, which pairs with the
# shell-sram.bin of the same build: always flash both, QSPI first.
QSPI_SECTIONS = .qspiflash_text .qspiflash_data .qspi_cold

# FORCE an allen drei Ableitungen, und das ist kein Schluderfix. Make hat auf
# diesem Rechner Sekundenaufloesung und vergleicht mit "echt neuer": werden
# zwei Schalterstellungen kurz hintereinander gebaut, liegt die frisch
# gelinkte shell.elf in DERSELBEN Sekunde wie das shell-sram.bin der
# vorigen Stellung -- und objcopy laeuft nicht. Am 2026-08-23 sind so zwei
# Images herausgekommen, die sich byte-identisch verglichen, obwohl ihre
# ELFs verschieden waren. objcopy kostet Millisekunden, die Kante kostet
# eine Messung.
.PHONY: images
images: $(BUILD_DIR)/$(TARGET)-sram.bin $(BUILD_DIR)/$(TARGET)-qspi.bin

# The QSPI placement guard (qspi_placement.py): refuses an ELF with hot code in
# QSPI, or code that runs before the QSPI is memory-mapped -- by name on the
# nm listing, and by call graph on the disassembly and the ELF's stored words
# (nothing hot, pre-QSPI or address-taken may reach QSPI code). Both images
# depend on it, so a refused link yields no fresh image; the old ones are
# deleted too, so nothing stale is left to flash. One shell line on purpose: a
# failing nm or objdump takes the same deleting branch as a refusal.
$(BUILD_DIR)/$(TARGET).nm: $(BUILD_DIR)/$(TARGET).elf FORCE
	( $(NM) -C -S $< > $@ && $(OBJDUMP) -d -C --no-show-raw-insn $< > $(BUILD_DIR)/$(TARGET).dis && python qspi_placement.py $@ $(BUILD_DIR)/$(TARGET).dis $< ) || (rm -f $@ $(BUILD_DIR)/$(TARGET).dis $(BUILD_DIR)/$(TARGET)-sram.bin $(BUILD_DIR)/$(TARGET)-qspi.bin && exit 1)

$(BUILD_DIR)/$(TARGET)-sram.elf: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).nm FORCE
	$(CP) $(foreach s,$(QSPI_SECTIONS),--remove-section=$(s)) $< $@

$(BUILD_DIR)/$(TARGET)-sram.bin: $(BUILD_DIR)/$(TARGET)-sram.elf FORCE
	$(CP) -O binary -S $< $@

$(BUILD_DIR)/$(TARGET)-qspi.bin: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).nm FORCE
	$(CP) -O binary $(foreach s,$(QSPI_SECTIONS),--only-section=$(s)) $< $@
