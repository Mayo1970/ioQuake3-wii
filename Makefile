# ioquake3-wii Makefile
#
# Convenience targets (all accept a trailing "dol" to also convert to .dol):
#   make dol              - Q3A release          → build/boot.dol
#   make oa dol           - Open Arena release   → build_oa/boot.dol
#   make debug dol        - Q3A debug            → build/boot.dol
#   make oa-debug dol     - OA debug             → build_oa/boot.dol
#   make 240p dol         - Q3A 240p NTSC        → build/boot.dol
#   make 240p-pal dol     - Q3A 264p PAL         → build/boot.dol
#   make oa-240p dol      - OA 240p NTSC         → build_oa/boot.dol
#   make oa-240p-pal dol  - OA 264p PAL          → build_oa/boot.dol
#   make all-flavors dol      - Q3A + OA release
#   make all-flavors-240p dol - Q3A + OA 240p NTSC
#   make all-flavors-240p-pal dol - Q3A + OA 264p PAL
#   make clean            - Clean Q3A build dir
#   make oa clean         - Clean OA build dir
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
.PHONY: oa debug oa-debug 240p 240p-pal oa-240p oa-240p-pal \
        all-flavors all-flavors-240p all-flavors-240p-pal

.DEFAULT_GOAL := all

oa:
	@$(MAKE) _OA=1

debug:
	@$(MAKE) _DEBUG=1

oa-debug:
	@$(MAKE) _OA=1 _DEBUG=1

240p:
	@$(MAKE) _240P=1

240p-pal:
	@$(MAKE) _240P=1 _PAL=1

oa-240p:
	@$(MAKE) _OA=1 _240P=1

oa-240p-pal:
	@$(MAKE) _OA=1 _240P=1 _PAL=1

all-flavors:
	@$(MAKE) dol
	@$(MAKE) _OA=1 dol

all-flavors-240p:
	@$(MAKE) _240P=1 dol
	@$(MAKE) _OA=1 _240P=1 dol

all-flavors-240p-pal:
	@$(MAKE) _240P=1 _PAL=1 dol
	@$(MAKE) _OA=1 _240P=1 _PAL=1 dol

#---------------------------------------------------------------------------------
# Internal build configuration (set by the phony targets above)
#---------------------------------------------------------------------------------
_OA    ?= 0
_DEBUG ?= 0
_240P  ?= 0
_PAL   ?= 0

ifeq ($(_OA),1)
  BUILD          := build_oa
  GAMEMODE_FLAGS := -DSTANDALONEOA -DWII_BASEGAME=\"baseoa\"
  DOL_DEST       := /apps/openarena/boot.dol
  DOL_NOTE       := OA data: sd:/quake3/baseoa/pak*.pk3
else
  BUILD          := build
  GAMEMODE_FLAGS := -DWII_BASEGAME=\"baseq3\"
  DOL_DEST       := /apps/ioquake3/boot.dol
  DOL_NOTE       :=
endif

ifeq ($(_DEBUG),1)
  WII_DEBUG_FLAG := -DWII_DEBUG
else
  WII_DEBUG_FLAG :=
endif

ifeq ($(_240P),1)
  ifeq ($(_PAL),1)
    WII_240P_FLAG := -DWII_240P=1 -DWII_PAL=1
  else
    WII_240P_FLAG := -DWII_240P=1
  endif
else
  WII_240P_FLAG :=
endif

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

WII_INPUT_SRC := code/input/wii_input.c
INCLUDES      := code

# OpenGX — prebuilt library + headers vendored under libs/opengx.
OPENGX_INC  := libs/opengx/include
OPENGX_LIB  := libs/opengx/lib

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
          $(WII_DEBUG_FLAG) \
          $(GAMEMODE_FLAGS) \
          $(WII_INPUT_FLAGS) \
          $(WII_240P_FLAG) \
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
          -DOPENGX_AVAILABLE -I$(OPENGX_INC)

CXXFLAGS = $(CFLAGS)

LDFLAGS = $(MACHDEP) -Wl,-Map,$(BUILD)/boot.elf.map -Wl,--wrap,CL_GenerateQKey -Wl,--wrap,VM_Call -Wl,--wrap,calloc -Wl,--wrap,__malloc_lock -Wl,--wrap,__malloc_unlock -G 0 -T rvl.ld

ifeq ($(INPUT_BACKEND),gamecube)
  LIBS  = -L$(LIBOGC_LIB) -L$(OPENGX_LIB) -lopengx -Wl,--start-group -lasnd -logc -ldi -lfat -lm -Wl,--end-group $(ZLIB_LIBS) -L$(JPEG_LIBDIR) -ljpeg
else
  LIBS  = -L$(LIBOGC_LIB) -L$(OPENGX_LIB) -lopengx -lwiiuse -lbte -lwiikeyboard -Wl,--start-group -lasnd -logc -ldi -lfat -lm -Wl,--end-group $(ZLIB_LIBS) -L$(JPEG_LIBDIR) -ljpeg
endif

# Source collection
SOURCES_NO_INPUT := $(filter-out code/input,$(SOURCES))
WII_C_SRCS   := $(foreach dir,$(SOURCES_NO_INPUT),$(wildcard $(dir)/*.c)) \
                $(WII_INPUT_SRC)
WII_CPP_SRCS := $(foreach dir,$(SOURCES_NO_INPUT),$(wildcard $(dir)/*.cpp))
ALL_SRCS     := $(WII_C_SRCS) $(WII_CPP_SRCS) $(IOQ3_SRCS) $(IOQ3_ZLIB_SRCS)

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(ALL_SRCS))) \
        $(patsubst %.cpp,$(BUILD)/%.o,$(filter %.cpp,$(ALL_SRCS)))

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

# net_ip.c and wii_main.c both inline wii_net.h; rebuild both when the shim changes.
WII_NET_H := code/sys/wii_net.h
$(BUILD)/code/sys/wii_main.o: code/sys/wii_main.c $(WII_NET_H)
$(BUILD)/code/qcommon/net_ip.o: code/qcommon/net_ip.c $(WII_NET_H)
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
	@rm -rf $(BUILD)
	@rm -f $(ZLIB_H_COPY) $(ZCONF_H_COPY)
	@echo "Cleaned."
