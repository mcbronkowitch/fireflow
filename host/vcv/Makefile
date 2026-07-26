# spotymod -- VCV Rack plugin (third host over the shared engine/ core).
#
#   RACK_DIR=/path/to/Rack-SDK make        # build
#   RACK_DIR=/path/to/Rack-SDK make install # build + copy to Rack user dir
#
# No engine sources are duplicated: this Makefile compiles the same
# engine/*.cpp the desktop render host (CMakeLists.txt) uses, two levels up.

RACK_DIR ?= ../../../Rack-SDK
# Absolute repo root: keeps object files for the shared engine sources inside
# host/vcv/build/ (a relative ../.. would make plugin.mk emit .o files outside
# the plugin dir). Run make from this directory.
REPO     := $(realpath ../..)

FLAGS += -I$(REPO)/engine -I$(REPO)/third_party -I$(REPO)/host
# DaisySP fans its private headers (dsp.h, ...) across these subdirs; the engine's
# FX chain pulls them in transitively, so all four must be on the include path.
FLAGS += -I$(REPO)/lib/DaisySP/Source \
         -I$(REPO)/lib/DaisySP/Source/Utility \
         -I$(REPO)/lib/DaisySP/Source/Control \
         -I$(REPO)/lib/DaisySP/Source/Effects \
         -I$(REPO)/lib/DaisySP/Source/Filters
CFLAGS +=
# The shared engine is C++17 (std::clamp in fx_util.h/limiter.h, ...), but the
# Rack SDK defaults CXXFLAGS to -std=c++11. EXTRA_CXXFLAGS is appended by the SDK
# *after* its own -std flag (see compile.mk), so this reliably wins.
EXTRA_CXXFLAGS += -std=c++17

# plugin glue
SOURCES += $(wildcard src/*.cpp)

# Portable engine core (identical list to the render target in CMakeLists.txt).
#
# Keep it identical. synth_engine.cpp instantiates SynthEngineT for every
# engine alias in synth_engine.h -- SYNTH, WAVE and now BODY -- and Part holds
# one of each by value, so a source this host omits is not a feature it lacks,
# it is a link error. That is what BODY's body/*.cpp entries below are: nothing
# in src/ names BodyEngine, and the plugin still cannot link without them.
SOURCES += \
	$(REPO)/engine/mod/lane.cpp \
	$(REPO)/engine/mod/super_modulator.cpp \
	$(REPO)/engine/parts/part.cpp \
	$(REPO)/engine/instrument.cpp \
	$(REPO)/engine/center/center.cpp \
	$(REPO)/engine/fx/grit.cpp \
	$(REPO)/engine/fx/flux.cpp \
	$(REPO)/engine/fx/taps.cpp \
	$(REPO)/engine/fx/reverb.cpp \
	$(REPO)/engine/fx/part_fx.cpp \
	$(REPO)/engine/fx/comp.cpp \
	$(REPO)/engine/synth/wt_bank.cpp \
	$(REPO)/engine/synth/voice.cpp \
	$(REPO)/engine/synth/synth_engine.cpp \
	$(REPO)/engine/sampler/sample_buffer.cpp \
	$(REPO)/engine/sampler/sampler_engine.cpp \
	$(REPO)/engine/sampler/slice_map.cpp \
	$(REPO)/engine/body/ks_string.cpp \
	$(REPO)/engine/body/mode_bank.cpp \
	$(REPO)/engine/body/body_voice.cpp

# Minimal DaisySP slice the FX chain and BODY need. Mirrors the daisysp_min
# target in CMakeLists.txt (minus KarplusString.cpp, which only the desktop
# test suite links, to compare against engine/body/ks_string.cpp). dcblock and
# crossfade are BODY's: KsString holds a DcBlock and a CrossFade by value, and
# both are out-of-line. The bench never needed them listed because it links
# -ldaisysp; this host compiles its slice from source.
SOURCES += \
	$(REPO)/lib/DaisySP/Source/Effects/overdrive.cpp \
	$(REPO)/lib/DaisySP/Source/Effects/decimator.cpp \
	$(REPO)/lib/DaisySP/Source/Effects/sampleratereducer.cpp \
	$(REPO)/lib/DaisySP/Source/Control/phasor.cpp \
	$(REPO)/lib/DaisySP/Source/Filters/svf.cpp \
	$(REPO)/lib/DaisySP/Source/Utility/dcblock.cpp \
	$(REPO)/lib/DaisySP/Source/Dynamics/crossfade.cpp

# The two runtime assets only -- NOT the whole res/ directory. res/ also holds
# gen_panel.py, test_panel.py and their __pycache__, and shipping the panel
# generator (plus stale .pyc files) inside every user's plugin package is both
# noise and a small footgun: the .svg in the package would silently disagree
# with a .py someone edited in place. plugin.mk copies these with
# `cp -r --parents`, so the res/ prefix is preserved in dist/.
#
# plugin.json is deliberately absent here: plugin.mk:15 already adds it, and
# naming it again made `cp` warn about a duplicate source on every dist run.
# That duplication predates this change -- the old `res plugin.json` had it too.
DISTRIBUTABLES += res/factory.wav res/Spotymod.svg

include $(RACK_DIR)/plugin.mk
