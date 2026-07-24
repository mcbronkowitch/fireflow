TARGET = qspi-programmer

CPP_STANDARD = -std=c++17

LIBDAISY_DIR = ../../lib/libDaisy
APP_TYPE = BOOT_SRAM

C_INCLUDES = -I. -I..

CPP_SOURCES = \
	main.cpp \
	../qspi_digest.cpp

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
