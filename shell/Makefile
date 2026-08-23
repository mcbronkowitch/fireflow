# Die erste FireFlow-Firmware, die engine/ enthaelt. Auslieferbar gedacht,
# anders als bench/ -- deshalb KEIN USE_DAISYSP_LGPL hier (siehe unten).
#
# Vorlage ist bench/Makefile und nicht das Root-Makefile: die Root-Firmware
# ist die alte Upstream-Spotykach-Firmware auf src/core und kompiliert
# engine/** ueberhaupt nicht. Wer die drei verwechselt, baut das falsche
# Instrument.

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

# Gleiche Linkage wie Bench und Root-Firmware. Beides ist Absicht: gegen die
# Root-Firmware, damit der Speicherplan der ausgelieferte bleibt, und gegen
# die Bench, damit deren Zahlen etwas ueber diese Firmware aussagen.
APP_TYPE = BOOT_SRAM
LDSCRIPT = ../alt_sram.lds

# Die Engine inkludiert DaisySP-Header relativ zu Source/ ("Effects/
# overdrive.h", "Utility/dsp.h"), also muss das Verzeichnis auf den Pfad.
# ../src/ ist fuer genau einen Header drauf: hw/board.h -- derselbe, den die
# Bench einbindet, und der einzige geteilte Code zwischen beiden Baeumen.
C_INCLUDES = -I. -I../src/ -I../engine/ -I../third_party/ -I../lib/DaisySP/Source/
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

CPP_SOURCES = \
	main.cpp \
	sdram_mem.cpp \
	mux_plan.cpp \
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

# --- generierte Schalter-Header ------------------------------------------
# Als echte Header und mit echter Abhaengigkeitskante, nicht als blankes
# -D. Die Begruendung steht in write_shell_selftest.py; es ist dieselbe
# stale-Objekt-Falle, an der die Bench schon einmal haengengeblieben ist.
#
# UND DIE KANTE ALLEIN REICHT NICHT -- warum, steht im naechsten Block. Wer
# hier ein Objekt vergisst, das einen Schalter liest, bekommt genau diesen
# Fehler zurueck, und er sieht wie eine Messung aus.
C_INCLUDES += -I$(BUILD_DIR)

SWITCH_OBJECTS = $(BUILD_DIR)/main.o

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
  $(shell python write_shell_idle_fill.py $(BUILD_DIR)/shell_idle_fill.h $(SHELL_IDLE_FILL) $(SWITCH_OBJECTS))

$(BUILD_DIR)/main.o: $(BUILD_DIR)/shell_selftest.h $(BUILD_DIR)/shell_cpu_probe.h $(BUILD_DIR)/shell_mux_probe.h $(BUILD_DIR)/shell_idle_fill.h

.PHONY: FORCE
FORCE:

# --- die zwei physischen Images -----------------------------------------
# libDaisys `all` wuerde ein flaches shell.bin ueber SRAM (0x24000000) UND
# QSPI (0x90100000) bauen -- 17 MB, fast alles Fuellbytes, an die falsche
# Adresse gezielt. Stattdessen zwei Artefakte aus derselben gelinkten ELF,
# genau wie bench/qspi_tools.py es fuer die Bench macht:
#
#   shell-sram.bin  -- alles ausser der Wavetable-Bank, geht per DFU nach
#                      0x90040000 und wird vom Daisy-Bootloader nach SRAM
#                      kopiert.
#   shell-qspi.bin  -- nur die Bank. Liegt seit dem 7. August auf dem Board
#                      und muss normalerweise NICHT neu geschrieben werden;
#                      das Ziel existiert, damit man es vergleichen kann.
QSPI_SECTION = .qspiflash_data

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

$(BUILD_DIR)/$(TARGET)-sram.elf: $(BUILD_DIR)/$(TARGET).elf FORCE
	$(CP) --remove-section=$(QSPI_SECTION) $< $@

$(BUILD_DIR)/$(TARGET)-sram.bin: $(BUILD_DIR)/$(TARGET)-sram.elf FORCE
	$(CP) -O binary -S $< $@

$(BUILD_DIR)/$(TARGET)-qspi.bin: $(BUILD_DIR)/$(TARGET).elf FORCE
	$(CP) -O binary --only-section=$(QSPI_SECTION) $< $@
