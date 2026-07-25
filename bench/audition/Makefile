TARGET = audition

CPP_STANDARD = -std=c++17

LIBDAISY_DIR = ../../lib/libDaisy
DAISYSP_DIR = ../../lib/DaisySP

APP_TYPE = BOOT_SRAM
LDSCRIPT = ../../alt_sram.lds

C_INCLUDES = \
	-I. \
	-I.. \
	-Ibuild \
	-I../../engine \
	-I../../third_party \
	-I../../lib/DaisySP/Source
C_USR_FLAGS = -ffast-math -funroll-loops

CPP_SOURCES = \
	main.cpp \
	memory.cpp \
	init_patch.cpp \
	protocol.cpp \
	../rand_shim.cpp \
	../../engine/mod/lane.cpp \
	../../engine/sampler/sampler_engine.cpp \
	../../engine/sampler/sample_buffer.cpp \
	../../engine/sampler/slice_map.cpp \
	../../engine/mod/super_modulator.cpp \
	../../engine/parts/part.cpp \
	../../engine/instrument.cpp \
	../../engine/center/center.cpp \
	../../engine/fx/grit.cpp \
	../../engine/fx/flux.cpp \
	../../engine/fx/taps.cpp \
	../../engine/fx/reverb.cpp \
	../../engine/fx/part_fx.cpp \
	../../engine/fx/comp.cpp \
	../../engine/synth/voice.cpp \
	../../engine/synth/synth_engine.cpp \
	../../engine/synth/wt_bank.cpp

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

FACTORY_WAV = ../../host/vcv/res/factory.wav
FACTORY_BIN = $(BUILD_DIR)/factory-planar-f32.bin
FACTORY_META = $(BUILD_DIR)/factory_meta.h

$(FACTORY_META): prepare_factory.py $(FACTORY_WAV) | $(BUILD_DIR)
	python prepare_factory.py --output $(FACTORY_BIN) --header $(FACTORY_META)

$(BUILD_DIR)/main.o $(BUILD_DIR)/memory.o: $(FACTORY_META)
