# Standalone benchmark app. Never shipped -> full DaisySP (LGPL) is fine here.
# The shipping firmware is built by the ROOT Makefile and is untouched.

TARGET = bench

CPP_STANDARD = -std=c++17

LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR  = ../lib/DaisySP

# Full DaisySP, including the LGPL modules -- family 2 measures them.
USE_DAISYSP_LGPL = 1

# Same linkage as the shipping firmware, so the memory map the SRAM/SDRAM
# workloads observe is the map the firmware will actually have.
APP_TYPE = BOOT_SRAM
LDSCRIPT = ../alt_sram.lds

# The engine includes DaisySP headers relative to Source/ ("Effects/overdrive.h",
# "Filters/svf.h", "Utility/dsp.h"), so that directory has to be on the path.
C_INCLUDES = -I. -I../engine/ -I../third_party/ -I../lib/DaisySP/Source/
C_USR_FLAGS = -ffast-math -funroll-loops

GIT_HASH = $(shell git -C .. rev-parse --short HEAD)

# --- workload family selection ---------------------------------------
# Which families this image contains. Override on the command line, or let
# run.py pass it from a profile:
#
#   make BENCH_FAMILIES="system voice" build/bench.elf
#
# Selecting a subset drops the family's workload translation unit AND its
# BENCH_FAMILY_* define, so families.cpp's registry entry disappears too; the
# engine code only that family referenced then goes with --gc-sections. That
# is where the SRAM comes back.
BENCH_FAMILIES ?= system voice mem mod abl taps sampler

FAMILY_SOURCE_system  = workloads_system.cpp
FAMILY_SOURCE_voice   = workloads_daisysp.cpp
FAMILY_SOURCE_mem     = workloads_memory.cpp
FAMILY_SOURCE_mod     = workloads_mod.cpp
FAMILY_SOURCE_abl     = workloads_abl.cpp
FAMILY_SOURCE_taps    = workloads_taps.cpp
FAMILY_SOURCE_sampler = workloads_sampler.cpp

FAMILY_DEFINE_system  = BENCH_FAMILY_SYSTEM
FAMILY_DEFINE_voice   = BENCH_FAMILY_VOICE
FAMILY_DEFINE_mem     = BENCH_FAMILY_MEM
FAMILY_DEFINE_mod     = BENCH_FAMILY_MOD
FAMILY_DEFINE_abl     = BENCH_FAMILY_ABL
FAMILY_DEFINE_taps    = BENCH_FAMILY_TAPS
FAMILY_DEFINE_sampler = BENCH_FAMILY_SAMPLER

# Fail loudly on a typo rather than silently building a smaller image.
$(foreach f,$(BENCH_FAMILIES),\
  $(if $(FAMILY_SOURCE_$(f)),,$(error unknown bench family '$(f)')))

FAMILY_SOURCES = $(foreach f,$(BENCH_FAMILIES),$(FAMILY_SOURCE_$(f)))
FAMILY_DEFINES = $(foreach f,$(BENCH_FAMILIES),$(FAMILY_DEFINE_$(f)))

CPP_SOURCES = \
	main.cpp \
	report.cpp \
	qspi_digest.cpp \
	rand_shim.cpp \
	runner.cpp \
	families.cpp \
	anchor.cpp \
	mem.cpp \
	$(FAMILY_SOURCES) \
	../engine/mod/lane.cpp \
	../engine/sampler/sampler_engine.cpp \
	../engine/sampler/sample_buffer.cpp \
	../engine/sampler/slice_map.cpp \
	../engine/mod/super_modulator.cpp \
	../engine/parts/part.cpp \
	../engine/instrument.cpp \
	../engine/center/center.cpp \
	../engine/fx/grit.cpp \
	../engine/fx/flux.cpp \
	../engine/fx/taps.cpp \
	../engine/fx/reverb.cpp \
	../engine/fx/part_fx.cpp \
	../engine/fx/comp.cpp \
	../engine/synth/voice.cpp \
	../engine/synth/synth_engine.cpp \
	../engine/synth/wt_bank.cpp

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# --- git hash header --------------------------------------------------
# `-DBENCH_GIT_HASH=...` on the compile line is invisible to Make's
# dependency graph, so a stale build/main.o survives a HEAD move and keeps
# reporting the old hash (this bit once: a run labelled itself 5555422
# while HEAD was 0e6eeb5). Emit the hash as a real header instead, and make
# it a real prerequisite of the one object that uses it.
#
# Written by a tiny cross-platform helper only when content differs, so the
# header's mtime -- and therefore whether main.o rebuilds -- reflects whether
# the hash actually changed, not merely whether `make` ran.
C_INCLUDES += -I$(BUILD_DIR)

.PHONY: FORCE
FORCE:

$(BUILD_DIR)/git_hash.h: FORCE | $(BUILD_DIR)
	@python write_git_hash.py $@ $(GIT_HASH)

$(BUILD_DIR)/main.o: $(BUILD_DIR)/git_hash.h

# --- bench family selection header -------------------------------------
# `-DBENCH_FAMILY_*` on the compile line has the exact same hazard as
# `-DBENCH_GIT_HASH` above, and it is not hypothetical: switching
# BENCH_FAMILIES leaves a stale build/families.o believing it still holds
# the old family set. Narrow-after-wide dies at link on an undefined
# reference to a family the narrower rebuild dropped (families.o still
# references it); wide-after-narrow links *silently*, producing an image
# that reports the wide build's size while only containing the narrow
# family's workloads. Same fix, same shape as the git hash: emit the
# selection into a real header, written only when its content differs, and
# declare it a real prerequisite of the one object that reads it.
$(BUILD_DIR)/bench_families.h: FORCE | $(BUILD_DIR)
	@python write_bench_families.py $@ $(FAMILY_DEFINES)

$(BUILD_DIR)/families.o: $(BUILD_DIR)/bench_families.h
