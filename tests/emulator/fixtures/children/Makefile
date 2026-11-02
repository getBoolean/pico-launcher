# Build twice: make CHILD=A, then make CHILD=B. Each uses a separate object tree.
BLOCKSDS ?= /opt/blocksds/core
CHILD ?= A

ifeq ($(CHILD),A)
CHILD_ID := 1
else ifeq ($(CHILD),B)
CHILD_ID := 2
else
$(error CHILD must be A or B)
endif

NAME := child_$(CHILD)
GAME_TITLE := Pico Child $(CHILD)
GAME_SUBTITLE := Emulator fixture
GAME_AUTHOR := Pico Launcher
DEFINES := -DFIXTURE_CHILD=$(CHILD_ID)

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
