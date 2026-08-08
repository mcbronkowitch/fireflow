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

CPP_SOURCES = \
	main.cpp \
	sdram_mem.cpp \
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

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# --- generierter Selbsttest-Header ---------------------------------------
# Als echter Header und mit echter Abhaengigkeitskante, nicht als blankes
# -D. Die Begruendung steht in write_shell_selftest.py; es ist dieselbe
# stale-Objekt-Falle, an der die Bench schon einmal haengengeblieben ist.
C_INCLUDES += -I$(BUILD_DIR)

.PHONY: FORCE
FORCE:

$(BUILD_DIR)/shell_selftest.h: FORCE | $(BUILD_DIR)
	@python write_shell_selftest.py $@ $(SHELL_SELFTEST)

$(BUILD_DIR)/main.o: $(BUILD_DIR)/shell_selftest.h

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

.PHONY: images
images: $(BUILD_DIR)/$(TARGET)-sram.bin $(BUILD_DIR)/$(TARGET)-qspi.bin

$(BUILD_DIR)/$(TARGET)-sram.elf: $(BUILD_DIR)/$(TARGET).elf
	$(CP) --remove-section=$(QSPI_SECTION) $< $@

$(BUILD_DIR)/$(TARGET)-sram.bin: $(BUILD_DIR)/$(TARGET)-sram.elf
	$(CP) -O binary -S $< $@

$(BUILD_DIR)/$(TARGET)-qspi.bin: $(BUILD_DIR)/$(TARGET).elf
	$(CP) -O binary --only-section=$(QSPI_SECTION) $< $@
