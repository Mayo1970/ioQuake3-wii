# ioquake3-wii Makefile
#
# Convenience targets:
#   make dol              - Q3A release          → build/boot.dol
#   make oa               - Open Arena release   → build_oa/boot.dol
#   make ta               - Team Arena release   → build_ta/boot.dol
#   make modsel           - Mod-select release   → build_modsel/boot.dol
#   make debug            - Q3A debug            → build/boot.dol
#   make oa-debug         - OA debug             → build_oa/boot.dol
#   make ta-debug         - TA debug             → build_ta/boot.dol
#   make all-flavors      - Q3A + OA + TA release
#   make clean            - Clean all build dirs
#
# Video mode (480p-ish default / 240p NTSC / 264p PAL) is chosen at BOOT via a
# d-pad prompt (UP/LEFT/RIGHT, 10s timeout → default) - all three modes are
# built into every DOL, see Wii_VideoModeBootPrompt() in code/sys/wii_main.c.
#
# Renderer backend: native GX is the default. Legacy OpenGX escape hatch:
#   make WII_OPENGX=1 dol  (run `make clean` first when switching backends —
#   enforced by a stamp file in the build dir).
#
# Build from devkitPro MSYS2 shell only (sets DEVKITPRO / DEVKITPPC / PATH).
ifeq ($(strip $(DEVKITPRO)),)
  $(error "Set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif
ifeq ($(strip $(DEVKITPPC)),)
  $(error "Set DEVKITPPC in your environment. export DEVKITPPC=/opt/devkitpro/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
# Convenience phony targets — recurse with the right internal flags
#---------------------------------------------------------------------------------
.PHONY: oa ta classic modsel debug oa-debug ta-debug classic-debug modsel-debug all-flavors

.DEFAULT_GOAL := all

oa:
	@$(MAKE) _OA=1 dol

ta:
	@$(MAKE) _TA=1 dol

classic:
	@$(MAKE) _CLASSIC=1 dol

modsel:
	@$(MAKE) _MODSEL=1 dol

debug:
	@$(MAKE) _DEBUG=1 dol

classic-debug:
	@$(MAKE) _CLASSIC=1 _DEBUG=1 dol

modsel-debug:
	@$(MAKE) _MODSEL=1 _DEBUG=1 dol

oa-debug:
	@$(MAKE) _OA=1 _DEBUG=1 dol

ta-debug:
	@$(MAKE) _TA=1 _DEBUG=1 dol

all-flavors:
	@$(MAKE) dol
	@$(MAKE) _OA=1 dol
	@$(MAKE) _TA=1 dol
	@$(MAKE) _CLASSIC=1 dol

#---------------------------------------------------------------------------------
# Internal build configuration (set by the phony targets above)
#---------------------------------------------------------------------------------
_OA      ?= 0
_TA      ?= 0
_CLASSIC ?= 0
_MODSEL  ?= 0
_DEBUG   ?= 0
# Optional: boot directly into a mod. e.g. make WII_FSGAME=missionpack dol
WII_FSGAME ?=
# QVM execution: native PPC JIT, DEFAULT since 2026-06-11 (all release and
# test builds use it). Defines HAVE_VM_COMPILED in wii_platform.h AND sets
# vm_*=2. Escape hatch: make WII_VM_NATIVE=0 dol builds the bytecode
# interpreter (needs more hunk — big maps like q3dm11 may OOM there).
WII_VM_NATIVE ?= 1
# In-game framerate cap, DEFAULT 60 since 2026-06-11. Set 30 if 60 proves
# unstable on a target machine: make WII_MAXFPS=30 dol. (Menus/loading always
# run at 60 regardless — see CL_InMenu in common.c.) NOTE: com_maxfps is
# CVAR_ARCHIVE — a saved q3config.cfg overrides this on boot.
WII_MAXFPS ?= 60
# Renderer backend. Native GX (direct GX calls at the GL1 choke points) is the
# DEFAULT as of Phase 7 (2026-06-10). Escape hatch for one release:
# make WII_OPENGX=1 dol builds the legacy OpenGX (GL->GX translation) path;
# WII_NATIVE_GX=0 is an equivalent override. Both paths share the build dirs —
# `make clean` is required when switching (enforced by the backend stamp below).
WII_OPENGX ?= 0
ifeq ($(WII_OPENGX),1)
  WII_NATIVE_GX := 0
else
  WII_NATIVE_GX ?= 1
endif
# Optional: Phase-0 GP-bottleneck profiler on the OpenGX path. Logs GP
# performance counters (xf_wait_out, fifo_req, etc.) to diag.txt to decide
# whether native GX can raise FPS before any GX code is written. Requires a
# debug build for wii_diag output: make WII_GX_PROFILE=1 debug. Default 0.
WII_GX_PROFILE ?= 0

ifeq ($(_TA),1)
  BUILD          := build_ta
  GAMEMODE_FLAGS := -DSTANDALONETA -DWII_BASEGAME=\"baseq3\"
  DOL_DEST       := /apps/teamarena/boot.dol
  DOL_NOTE       := TA data: sd:/quake3/baseq3/pak*.pk3 + sd:/quake3/missionpack/pak*.pk3
else ifeq ($(_OA),1)
  BUILD          := build_oa
  GAMEMODE_FLAGS := -DSTANDALONEOA -DWII_BASEGAME=\"baseoa\"
  DOL_DEST       := /apps/openarena/boot.dol
  DOL_NOTE       := OA data: sd:/quake3/baseoa/pak*.pk3
else ifeq ($(_CLASSIC),1)
  BUILD          := build_classic
  GAMEMODE_FLAGS := -DWII_BASEGAME=\"baseq3\" -DCLASSIC -DLEGACY_PROTOCOL -DPROTOCOL_VERSION=43
  DOL_DEST       := /apps/ioquake3-classic/boot.dol
  DOL_NOTE       := Classic data: sd:/quake3/baseq3/pak0-pak2.pk3 + zpack-classic.pk3
else ifeq ($(_MODSEL),1)
  BUILD          := build_modsel
  GAMEMODE_FLAGS := -DWII_BASEGAME=\"baseq3\" -DWII_MODSELECT=1
  DOL_DEST       := /apps/ioquake3-modselect/boot.dol
  DOL_NOTE       := Modsel data: sd:/quake3/baseq3/pak0-pak8.pk3 (default Q3A build) + any extra sd:/quake3/<mod>/ folder with at least one *.pk3 (any naming, e.g. Rocket Arena) to pick from
else
  BUILD          := build
  GAMEMODE_FLAGS := -DWII_BASEGAME=\"baseq3\"
  DOL_DEST       := /apps/ioquake3/boot.dol
  DOL_NOTE       :=
endif

ifeq ($(_DEBUG),1)
  WII_DEBUG_FLAG := -DWII_DEBUG
else
  # Release: define NDEBUG so the standard C convention compiles OUT assert()
  # and HUNK_DEBUG (q_shared.h gates both on !defined(NDEBUG)). Without this a
  # "release" make dol still ships every assert() as a live abort() trap and a
  # 24-byte hunkblock_t header on every hunk allocation. Matches upstream ioq3.
  WII_DEBUG_FLAG := -DNDEBUG
endif

ifneq ($(WII_FSGAME),)
  WII_FSGAME_FLAG := -DWII_FSGAME=\"$(WII_FSGAME)\"
else
  WII_FSGAME_FLAG :=
endif

ifeq ($(WII_VM_NATIVE),1)
  WII_VM_NATIVE_FLAG := -DWII_VM_NATIVE=1
else
  WII_VM_NATIVE_FLAG :=
endif

ifeq ($(WII_NATIVE_GX),1)
  WII_NATIVE_GX_FLAG := -DWII_NATIVE_GX=1
else
  WII_NATIVE_GX_FLAG :=
endif

# Backend stamp guard: the .o files do not depend on the Makefile, so switching
# renderer backend without `make clean` would silently link a mixed-flag binary
# (half native GX, half OpenGX). A stamp file in the build dir records which
# backend it was compiled with; a mismatch aborts with a clean instruction.
# Only checked for goals that actually compile into $(BUILD).
GX_BACKEND := $(if $(filter 1,$(WII_NATIVE_GX)),nativegx,opengx)
ifneq ($(filter all dol,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
  GX_BACKEND_STALE := $(filter-out $(BUILD)/.backend-$(GX_BACKEND),$(wildcard $(BUILD)/.backend-*))
  ifneq ($(GX_BACKEND_STALE),)
    $(error $(BUILD)/ was compiled with the other renderer backend ($(patsubst .backend-%,%,$(notdir $(GX_BACKEND_STALE)))). Run 'make clean' before switching between native GX and OpenGX)
  endif
  $(shell mkdir -p $(BUILD) && touch $(BUILD)/.backend-$(GX_BACKEND))
endif

ifeq ($(WII_GX_PROFILE),1)
  WII_GX_PROFILE_FLAG := -DWII_GX_PROFILE=1
else
  WII_GX_PROFILE_FLAG :=
endif

# In-game framerate cap, stringized for the boot cmdline. Always defined.
WII_MAXFPS_FLAG := -DWII_MAXFPS_STR=\"$(WII_MAXFPS)\"

# Input backend
INPUT_BACKEND ?= wiimote
ifeq ($(INPUT_BACKEND),wiimote)
  WII_INPUT_FLAGS := -DWPAD_ENABLED=1
else
  WII_INPUT_FLAGS := -DWPAD_ENABLED=0
endif

# Project identity
TARGET      := boot
SOURCES     := code \
               code/renderer \
               code/audio \
               code/sys
PORTDIR     := $(CURDIR)

WII_INPUT_SRC := code/input/wii_input.c code/input/wii_usb_hid.c
INCLUDES      := code

# OpenGX — prebuilt library + headers vendored under libs/opengx.
OPENGX_INC  := libs/opengx/include
OPENGX_LIB  := libs/opengx/lib

# libwiidrc — Wii U GamePad (DRC) support, vendored under libs/wiidrc.
# Only linked in the Wiimote backend (DRC is vWii-only; shares the WPAD build).
WIIDRC_INC  := libs/wiidrc/include
WIIDRC_LIB  := libs/wiidrc/lib

# ioQuake3 sources are vendored under code/ — see legacy/apply_patches.sh for
# the historical patch set if you need to regenerate the vendored tree from
# a fresh upstream ioQ3 clone.
IOQ3_SRCS   := \
  code/qcommon/cmd.c \
  code/qcommon/cm_load.c \
  code/qcommon/cm_patch.c \
  code/qcommon/cm_polylib.c \
  code/qcommon/cm_test.c \
  code/qcommon/cm_trace.c \
  code/qcommon/common.c \
  code/qcommon/cvar.c \
  code/qcommon/files.c \
  code/qcommon/huffman.c \
  code/qcommon/md4.c \
  code/qcommon/md5.c \
  code/qcommon/msg.c \
  code/qcommon/net_chan.c \
  code/qcommon/net_ip.c \
  code/qcommon/q_math.c \
  code/qcommon/q_shared.c \
  code/qcommon/unzip.c \
  code/qcommon/vm.c \
  code/qcommon/vm_interpreted.c \
  code/qcommon/vm_powerpc.c \
  code/client/cl_cgame.c \
  code/client/cl_cin.c \
  code/client/cl_console.c \
  code/client/cl_input.c \
  code/client/cl_keys.c \
  code/client/cl_main.c \
  code/client/cl_net_chan.c \
  code/client/cl_parse.c \
  code/client/cl_scrn.c \
  code/client/cl_ui.c \
  code/client/snd_dma.c \
  code/client/snd_mem.c \
  code/client/snd_mix.c \
  code/client/snd_codec.c \
  code/client/snd_codec_wav.c \
  code/client/snd_adpcm.c \
  code/client/snd_wavelet.c \
  code/server/sv_bot.c \
  code/server/sv_ccmds.c \
  code/server/sv_client.c \
  code/server/sv_game.c \
  code/server/sv_init.c \
  code/server/sv_main.c \
  code/server/sv_net_chan.c \
  code/server/sv_snapshot.c \
  code/server/sv_world.c \
  code/botlib/be_aas_bspq3.c \
  code/botlib/be_aas_cluster.c \
  code/botlib/be_aas_debug.c \
  code/botlib/be_aas_entity.c \
  code/botlib/be_aas_file.c \
  code/botlib/be_aas_main.c \
  code/botlib/be_aas_move.c \
  code/botlib/be_aas_optimize.c \
  code/botlib/be_aas_reach.c \
  code/botlib/be_aas_route.c \
  code/botlib/be_aas_routealt.c \
  code/botlib/be_aas_sample.c \
  code/botlib/be_ai_char.c \
  code/botlib/be_ai_chat.c \
  code/botlib/be_ai_gen.c \
  code/botlib/be_ai_goal.c \
  code/botlib/be_ai_move.c \
  code/botlib/be_ai_weap.c \
  code/botlib/be_ai_weight.c \
  code/botlib/be_ea.c \
  code/botlib/be_interface.c \
  code/botlib/l_crc.c \
  code/botlib/l_libvar.c \
  code/botlib/l_log.c \
  code/botlib/l_memory.c \
  code/botlib/l_precomp.c \
  code/botlib/l_script.c \
  code/botlib/l_struct.c \
  code/renderergl1/tr_animation.c \
  code/renderergl1/tr_bsp.c \
  code/renderergl1/tr_curve.c \
  code/renderergl1/tr_init.c \
  code/renderergl1/tr_light.c \
  code/renderergl1/tr_main.c \
  code/renderergl1/tr_marks.c \
  code/renderergl1/tr_mesh.c \
  code/renderergl1/tr_model.c \
  code/renderergl1/tr_model_iqm.c \
  code/renderergl1/tr_scene.c \
  code/renderergl1/tr_shade_calc.c \
  code/renderergl1/tr_shader.c \
  code/renderergl1/tr_backend.c \
  code/renderergl1/tr_cmds.c \
  code/renderergl1/tr_flares.c \
  code/renderergl1/tr_image.c \
  code/renderergl1/tr_shade.c \
  code/renderergl1/tr_shadows.c \
  code/renderergl1/tr_sky.c \
  code/renderergl1/tr_surface.c \
  code/renderergl1/tr_world.c \
  code/renderercommon/puff.c \
  code/renderercommon/tr_font.c \
  code/renderercommon/tr_image_bmp.c \
  code/renderercommon/tr_image_jpg.c \
  code/renderercommon/tr_image_pcx.c \
  code/renderercommon/tr_image_png.c \
  code/renderercommon/tr_image_pvr.c \
  code/renderercommon/tr_image_tga.c \
  code/renderercommon/tr_noise.c

# zlib: auto-detect ioQ3 internal zlib, else fall back to devkitPro portlibs (ppc-zlib).
IOQ3_ZLIB_A := code/libs/zlib/zlib.h
IOQ3_ZLIB_B := code/zlib/zlib.h

ifneq ($(wildcard $(IOQ3_ZLIB_A)),)
  ZLIB_DIR      := code/libs/zlib
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else ifneq ($(wildcard $(IOQ3_ZLIB_B)),)
  ZLIB_DIR      := code/zlib
  ZLIB_CFLAGS   := -DUSE_INTERNAL_ZLIB -I$(ZLIB_DIR) \
                   -DZLIB_H_PATH=\"$(ZLIB_DIR)/zlib.h\"
  IOQ3_ZLIB_SRCS := $(wildcard $(ZLIB_DIR)/*.c)
  ZLIB_LIBS     :=
else
  PORTLIBS_WII  := $(DEVKITPRO)/portlibs/wii
  PORTLIBS_PPC  := $(DEVKITPRO)/portlibs/ppc
  ifneq ($(wildcard $(PORTLIBS_WII)/include/zlib.h),)
    PORTLIBS    := $(PORTLIBS_WII)
  else ifneq ($(wildcard $(PORTLIBS_PPC)/include/zlib.h),)
    PORTLIBS    := $(PORTLIBS_PPC)
  else
    $(error zlib.h not found. Run: pacman -S ppc-zlib)
  endif
  ZLIB_DIR      := $(PORTLIBS)/include
  ZLIB_CFLAGS   := -I$(PORTLIBS)/include
  IOQ3_ZLIB_SRCS :=
  ZLIB_LIBS     := -L$(PORTLIBS)/lib -lz
endif

# libjpeg: always in portlibs (independent of zlib source)
PORTLIBS_WII_DIR := $(DEVKITPRO)/portlibs/wii
PORTLIBS_PPC_DIR := $(DEVKITPRO)/portlibs/ppc
ifneq ($(wildcard $(PORTLIBS_WII_DIR)/lib/libjpeg.a),)
  JPEG_LIBDIR := $(PORTLIBS_WII_DIR)/lib
else
  JPEG_LIBDIR := $(PORTLIBS_PPC_DIR)/lib
endif

# Copy zlib headers next to unzip.h so #include "zlib.h" resolves correctly.
ZLIB_H_COPY  := code/qcommon/zlib.h
ZCONF_H_COPY := code/qcommon/zconf.h

CFLAGS  = $(MACHDEP) \
          -pipe -O2 -Wall -Wno-unused-variable -Wno-missing-braces -Wno-cpp \
          -MMD -MP \
          $(WII_DEBUG_FLAG) \
          $(GAMEMODE_FLAGS) \
          $(WII_INPUT_FLAGS) \
          $(WII_FSGAME_FLAG) \
          $(WII_VM_NATIVE_FLAG) \
          $(WII_NATIVE_GX_FLAG) \
          $(WII_GX_PROFILE_FLAG) \
          $(WII_MAXFPS_FLAG) \
          -msdata=none -G 0 \
          -DGEKKO -DWII \
          -DMAX_CLIENTS=8 \
          -DBOTLIB -DUSE_CODEC_VORBIS=0 -DUSE_CODEC_OPUS=0 -DUSE_OPENAL=0 \
          -DUSE_LOCAL_HEADERS \
          $(ZLIB_CFLAGS) \
          -include $(PORTDIR)/code/sys/wii_platform.h \
          -I$(PORTDIR)/code/sys/include \
          $(foreach dir,$(INCLUDES),-I$(dir)) \
          -Icode \
          -Icode/sys \
          -Icode/qcommon \
          -Icode/client \
          -Icode/renderercommon \
          -Icode/renderergl1 \
          -Icode/botlib \
          -I$(LIBOGC_INC) \
          -DOPENGX_AVAILABLE -I$(OPENGX_INC) \
          -I$(WIIDRC_INC) \
          -I$(BUILD)

CXXFLAGS = $(CFLAGS)

LDFLAGS = $(MACHDEP) -Wl,-Map,$(BUILD)/boot.elf.map -Wl,--wrap,CL_GenerateQKey -Wl,--wrap,VM_Call -Wl,--wrap,calloc -Wl,--wrap,__malloc_lock -Wl,--wrap,__malloc_unlock -G 0 -T rvl.ld

ifeq ($(INPUT_BACKEND),gamecube)
  LIBS  = -L$(LIBOGC_LIB) -L$(OPENGX_LIB) -lopengx -Wl,--start-group -lasnd -logc -ldi -lfat -lm -Wl,--end-group $(ZLIB_LIBS) -L$(JPEG_LIBDIR) -ljpeg
else
  LIBS  = -L$(LIBOGC_LIB) -L$(OPENGX_LIB) -L$(WIIDRC_LIB) -lopengx -lwiidrc -lwiiuse -lbte -lwiikeyboard -Wl,--start-group -lasnd -logc -ldi -lfat -lm -Wl,--end-group $(ZLIB_LIBS) -L$(JPEG_LIBDIR) -ljpeg
endif

# Source collection
SOURCES_NO_INPUT := $(filter-out code/input,$(SOURCES))
WII_C_SRCS   := $(foreach dir,$(SOURCES_NO_INPUT),$(wildcard $(dir)/*.c)) \
                $(WII_INPUT_SRC)
WII_CPP_SRCS := $(foreach dir,$(SOURCES_NO_INPUT),$(wildcard $(dir)/*.cpp))
ALL_SRCS     := $(WII_C_SRCS) $(WII_CPP_SRCS) $(IOQ3_SRCS) $(IOQ3_ZLIB_SRCS)

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(ALL_SRCS))) \
        $(patsubst %.cpp,$(BUILD)/%.o,$(filter %.cpp,$(ALL_SRCS)))

# Header dependency tracking: -MMD in CFLAGS emits a .d next to each .o, so
# editing any header (wii_platform.h is force-included everywhere) rebuilds
# exactly the objects that use it. Without this, header edits silently
# produced mixed binaries unless you remembered `make clean`.
DEPS := $(OBJS:.o=.d)
-include $(DEPS)

# Flag-change tracking: the stamp file holds the current CFLAGS; the recipe
# runs every invocation (phony prereq) but only rewrites the file — bumping
# its mtime and invalidating every .o — when the flags actually differ.
# Closes the other half of the "make clean is mandatory" trap.
CFLAGS_STAMP := $(BUILD)/.cflags-stamp
.PHONY: cflags-stamp-force
$(CFLAGS_STAMP): cflags-stamp-force
	@mkdir -p $(BUILD)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@
$(OBJS): $(CFLAGS_STAMP)

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
.PHONY: all dol prebuild clean

all: $(BUILD)/$(TARGET).elf

prebuild:
	@cp $(ZLIB_DIR)/zlib.h $(ZLIB_H_COPY)
	@test -f $(ZLIB_DIR)/zconf.h && cp $(ZLIB_DIR)/zconf.h $(ZCONF_H_COPY) || true

$(BUILD)/$(TARGET).elf: prebuild $(OBJS)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $(filter %.o,$^) $(LIBS) -o $@

# Default rule — vendored ioQ3 source. Does NOT pull in the Wii network shim
# (huffman.c's static send() collides with BSD socket send() if it does).
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Wii port layer — needs the network shim (via -DWII_INCLUDE_NET pulling in
# code/sys/wii_net.h from wii_platform.h). One rule per port-layer dir so
# vendored ioQ3 doesn't accidentally inherit it.
$(BUILD)/code/audio/%.o: code/audio/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -DWII_INCLUDE_NET -c $< -o $@

$(BUILD)/code/input/%.o: code/input/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -DWII_INCLUDE_NET -c $< -o $@

$(BUILD)/code/renderer/%.o: code/renderer/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -DWII_INCLUDE_NET -c $< -o $@

$(BUILD)/code/sys/%.o: code/sys/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -DWII_INCLUDE_NET -c $< -o $@

$(BUILD)/code/client/cl_ui.o: code/client/cl_ui.c
	@mkdir -p $(dir $@)
	@echo "CC $< [wii-patched]"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/code/client/cl_main.o: code/client/cl_main.c
	@mkdir -p $(dir $@)
	@echo "CC $< [wii-patched]"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/code/qcommon/common.o: code/qcommon/common.c
	@mkdir -p $(dir $@)
	@echo "CC $< [wii-patched]"
	$(CC) $(CFLAGS) -c $< -o $@

# Bundled pk3 header for CLASSIC build — embedded verbatim as a C array.
# Generated into $(BUILD)/ so each flavor gets its own copy path; the
# -I$(BUILD) in CFLAGS makes the #include resolve without an absolute path.
# Depends on the Makefile too: the generator recipe lives here, and a stale
# header (e.g. old checksum symbol) otherwise survives generator changes.
$(BUILD)/zpack_classic_embedded.h: fixes/baseq3/zpack-classic.pk3 Makefile
	@mkdir -p $(dir $@)
	@echo "GEN $@"
	@python3 -c "import sys, zlib; d=open(sys.argv[1],'rb').read(); n=sys.argv[2]; print('static const unsigned char '+n+'[] = {'+','.join(str(b) for b in d)+'};'); print('static const unsigned int '+n+'_len = '+str(len(d))+';'); print('static const unsigned int '+n+'_crc = '+str(zlib.crc32(d) & 0xffffffff)+'u;')" fixes/baseq3/zpack-classic.pk3 zpack_classic_data > $@

ifeq ($(_CLASSIC),1)
WII_MAIN_EXTRA_DEPS := $(BUILD)/zpack_classic_embedded.h
else
WII_MAIN_EXTRA_DEPS :=
endif

# net_ip.c and wii_main.c both inline wii_net.h; rebuild both when the shim changes.
# Header deps now come from -MMD; the embedded-pk3 header stays an explicit
# prerequisite because it must be *generated* before wii_main.c's first compile.
$(BUILD)/code/sys/wii_main.o: code/sys/wii_main.c $(WII_MAIN_EXTRA_DEPS)
$(BUILD)/code/qcommon/net_ip.o: code/qcommon/net_ip.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	$(CC) $(CFLAGS) -DWII_INCLUDE_NET -c $< -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

dol: $(BUILD)/$(TARGET).elf
	@echo "Converting to .dol"
	elf2dol $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).dol
	@echo "Done! Copy $(BUILD)/$(TARGET).dol to your SD card as $(DOL_DEST)"
ifneq ($(DOL_NOTE),)
	@echo "$(DOL_NOTE)"
endif

clean:
	@rm -rf build build_oa build_ta build_classic build_modsel
	@rm -f $(ZLIB_H_COPY) $(ZCONF_H_COPY)
	@echo "Cleaned."
