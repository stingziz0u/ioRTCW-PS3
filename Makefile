#---------------------------------------------------------------------------------
# ioRTWC-PS3 -- iortcw (Return to Castle Wolfenstein, single player) for PS3
#
#   make              full game (stage 2)            -> iortcw-ps3-$(VERSION).pkg
#   make STAGE=1      headless engine test           -> iortcw-ps3-stage1.pkg
#   make MAPWALK=1    game that loads every map, logs memory/fps and quits
#                                                    -> iortcw-ps3-mapwalk.pkg
#   make MAPWALK=1 WALKMAPS="dark xlabs"   only those maps, 20 s each
#   make MAPWALK=1 WALKSECS=90   long test: 90 s per map with god mode,
#                                cutscene maps until their script moves on
#   make DEBUG=1      debug build (any stage)        -> iortcw-ps3-...-debug.pkg
#   make DIAG=1       game with the test diagnostics (fps on screen, [fps]/[prof]
#                     every 30 s, [hitch], [pad], [lock], [fog], [snd], [cam]
#                     in the log)                     -> iortcw-ps3-...-diag.pkg
#                     (MAPWALK and DEBUG builds always have them)
#   make clean        remove obj/ and build/ (not the .pkg files)
#
# Each variant has its own obj/ and build/ folder: switching STAGE or DEBUG
# does not need a make clean.
#---------------------------------------------------------------------------------

ifeq ($(strip $(PS3DEV)),)
  $(error "Set PS3DEV in your environment: export PS3DEV=/usr/local/ps3dev")
endif
ifeq ($(strip $(PSL1GHT)),)
  export PSL1GHT := $(PS3DEV)
endif

VERSION := 1.0
STAGE   ?= 2
DEBUG   ?= 0
DIAG    ?= 0
MAPWALK ?= 0
WALKMAPS ?=
WALKSECS ?= 0

APPID   := IORTCWPS3
ifeq ($(STAGE),1)
  TITLE   := Return to Castle Wolfenstein (stage 1)
  VARIANT := stage1
else ifeq ($(MAPWALK),1)
  TITLE   := Return to Castle Wolfenstein (map test)
  VARIANT := mapwalk
else
  TITLE   := Return to Castle Wolfenstein
  VARIANT := $(VERSION)
endif
ifeq ($(DEBUG),1)
  VARIANT := $(VARIANT)-debug
  DIAG    := 1
else ifeq ($(MAPWALK),1)
  DIAG    := 1
else ifeq ($(DIAG),1)
  VARIANT := $(VARIANT)-diag
endif

TARGET   := iortcw-ps3
OUT_PKG  := $(TARGET)-$(VARIANT).pkg
OBJDIR   := $(CURDIR)/obj/$(VARIANT)
BUILDDIR := $(CURDIR)/build/$(VARIANT)
PKGFILES := $(BUILDDIR)/pkgfiles

# Build stamp: the patch level of the tree (PATCHLEVEL, bumped by every
# patch). Goes to the first log line and to USRDIR/VERSION.TXT.
PATCHLEVEL := $(shell cat $(CURDIR)/PATCHLEVEL 2>/dev/null || echo 0)

include $(PSL1GHT)/ppu_rules

# after the include: ppu_rules sets ICON0 with ?=
ICON0 := $(CURDIR)/ICON0.PNG

#---------------------------------------------------------------------------------
# Sources
#---------------------------------------------------------------------------------
C := code

COMMON_SRCS := \
  $(C)/qcommon/cm_load.c $(C)/qcommon/cm_patch.c $(C)/qcommon/cm_polylib.c \
  $(C)/qcommon/cm_test.c $(C)/qcommon/cm_trace.c \
  $(C)/qcommon/cmd.c $(C)/qcommon/common.c $(C)/qcommon/cvar.c $(C)/qcommon/files.c \
  $(C)/qcommon/md4.c $(C)/qcommon/msg.c $(C)/qcommon/net_chan.c $(C)/qcommon/huffman.c \
  $(C)/qcommon/q_math.c $(C)/qcommon/q_shared.c \
  $(C)/qcommon/vm.c $(C)/qcommon/vm_interpreted.c

SERVER_SRCS := \
  $(C)/server/sv_bot.c $(C)/server/sv_client.c $(C)/server/sv_ccmds.c $(C)/server/sv_game.c \
  $(C)/server/sv_init.c $(C)/server/sv_main.c $(C)/server/sv_net_chan.c \
  $(C)/server/sv_snapshot.c $(C)/server/sv_world.c

BOTLIB_SRCS := $(addprefix $(C)/botlib/, \
  be_aas_bspq3.c be_aas_cluster.c be_aas_debug.c be_aas_entity.c be_aas_file.c \
  be_aas_main.c be_aas_move.c be_aas_optimize.c be_aas_reach.c be_aas_route.c \
  be_aas_routealt.c be_aas_routetable.c be_aas_sample.c be_ai_char.c be_ai_chat.c \
  be_ai_gen.c be_ai_goal.c be_ai_move.c be_ai_weap.c be_ai_weight.c be_ea.c \
  be_interface.c l_crc.c l_libvar.c l_log.c l_memory.c l_precomp.c l_script.c l_struct.c)

ZLIB_SRCS := $(addprefix $(C)/zlib-1.2.11/, \
  adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c ioapi.c unzip.c)

PS3_SRCS := \
  $(C)/sys/ps3_main.c $(C)/sys/ps3_sys.c $(C)/sys/ps3_log.c $(C)/sys/ps3_net_null.c \
  $(C)/sys/ps3_prof.c

PS3_ASM_SRCS := $(C)/sys/ps3_setjmp.S

# ---- stage 2 only ----
CLIENT_SRCS := $(addprefix $(C)/client/, \
  cl_cgame.c cl_cin.c cl_console.c cl_input.c cl_keys.c cl_main.c cl_net_chan.c \
  cl_parse.c cl_scrn.c cl_ui.c cl_avi.c \
  snd_adpcm.c snd_dma.c snd_mem.c snd_mix.c snd_wavelet.c \
  snd_main.c snd_codec.c snd_codec_wav.c) \
  $(C)/qcommon/md5.c $(C)/qcommon/puff.c

RENDERER_SRCS := $(addprefix $(C)/renderer/, \
  tr_animation.c tr_animation_front.c tr_backend.c tr_bsp.c tr_cmds.c tr_cmesh.c tr_curve.c tr_flares.c \
  tr_font.c tr_image.c tr_image_bmp.c tr_image_jpg.c tr_image_pcx.c tr_image_png.c \
  tr_image_tga.c tr_init.c tr_light.c tr_main.c tr_marks.c tr_mesh.c tr_model.c \
  tr_model_iqm.c tr_noise.c tr_scene.c tr_shade.c tr_shade_calc.c tr_shader.c \
  tr_shadows.c tr_sky.c tr_surface.c tr_world.c)

SPLINES_SRCS := $(addprefix $(C)/splines/, \
  math_angles.cpp math_matrix.cpp math_quaternion.cpp math_vector.cpp \
  q_parse.cpp splines.cpp util_str.cpp)

JPEG_SRCS := $(addprefix $(C)/jpeg-8c/, \
  jaricom.c jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c \
  jcinit.c jcmainct.c jcmarker.c jcmaster.c jcomapi.c jcparam.c jcprepct.c jcsample.c \
  jctrans.c jdapimin.c jdapistd.c jdarith.c jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c \
  jddctmgr.c jdhuff.c jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdpostct.c \
  jdsample.c jdtrans.c jerror.c jfdctflt.c jfdctfst.c jfdctint.c jidctflt.c jidctfst.c \
  jidctint.c jmemmgr.c jmemnobs.c jquant1.c jquant2.c jutils.c)

PS3_CLIENT_SRCS := \
  $(C)/sys/ps3_glimp.c $(C)/input/ps3_input.c $(C)/input/ps3_osk.c $(C)/audio/ps3_snd.c \
  $(C)/renderer/ps3/qgl_ps3.c \
  $(addprefix $(C)/gl/, ps3gl_main.c ps3gl_states.c ps3gl_matrices.c ps3gl_vertices.c \
    ps3gl_colors.c ps3gl_textures.c ps3gl_draw.c ps3gl_shaders.c)

ifeq ($(STAGE),1)
  ENGINE_SRCS := $(COMMON_SRCS) $(SERVER_SRCS) $(BOTLIB_SRCS) $(ZLIB_SRCS) $(PS3_SRCS) \
                 $(C)/null/null_client.c $(C)/null/null_input.c $(C)/null/null_snddma.c
  MODULES     := qagame
else
  ENGINE_SRCS := $(COMMON_SRCS) $(SERVER_SRCS) $(BOTLIB_SRCS) $(ZLIB_SRCS) $(PS3_SRCS) \
                 $(CLIENT_SRCS) $(RENDERER_SRCS) $(SPLINES_SRCS) $(JPEG_SRCS) $(PS3_CLIENT_SRCS)
  MODULES     := qagame cgame ui
endif

QAGAME_SRCS := $(addprefix $(C)/game/, \
  g_main.c ai_cast.c ai_cast_characters.c ai_cast_debug.c ai_cast_events.c \
  ai_cast_fight.c ai_cast_func_attack.c ai_cast_func_boss1.c ai_cast_funcs.c \
  ai_cast_script_actions.c ai_cast_script.c ai_cast_script_ents.c ai_cast_sight.c \
  ai_cast_think.c ai_chat.c ai_cmd.c ai_dmnet.c ai_dmq3.c ai_main.c ai_team.c \
  bg_animation.c bg_misc.c bg_pmove.c bg_slidemove.c bg_lib.c g_active.c g_alarm.c \
  g_bot.c g_client.c g_cmds.c g_combat.c g_items.c g_mem.c g_misc.c g_missile.c \
  g_mover.c g_props.c g_save.c g_script_actions.c g_script.c g_session.c g_spawn.c \
  g_svcmds.c g_target.c g_team.c g_tramcar.c g_trigger.c g_utils.c g_weapon.c \
  g_syscalls.c) \
  $(C)/qcommon/q_math.c $(C)/qcommon/q_shared.c

CGAME_SRCS := $(addprefix $(C)/cgame/, \
  cg_main.c cg_consolecmds.c cg_draw.c cg_drawtools.c cg_effects.c cg_ents.c \
  cg_event.c cg_flamethrower.c cg_info.c cg_localents.c cg_marks.c cg_newdraw.c \
  cg_particles.c cg_players.c cg_playerstate.c cg_predict.c cg_scoreboard.c \
  cg_servercmds.c cg_snapshot.c cg_sound.c cg_trails.c cg_view.c cg_weapons.c \
  cg_syscalls.c) \
  $(addprefix $(C)/game/, bg_animation.c bg_misc.c bg_pmove.c bg_slidemove.c bg_lib.c) \
  $(C)/ui/ui_shared.c $(C)/qcommon/q_math.c $(C)/qcommon/q_shared.c

UI_SRCS := $(addprefix $(C)/ui/, \
  ui_main.c ui_atoms.c ui_gameinfo.c ui_players.c ui_shared.c ui_syscalls.c) \
  $(addprefix $(C)/game/, bg_misc.c bg_lib.c) \
  $(C)/qcommon/q_math.c $(C)/qcommon/q_shared.c

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
# 64-bit truncation bugs become compile errors instead of crashes on hardware.
WERROR := -Werror=implicit-function-declaration -Werror=int-conversion \
          -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast

BASE_CFLAGS := -O2 -mno-altivec -fno-strict-aliasing -fno-common -MMD -MP \
  -Wall -Wno-unused -Wno-missing-braces -Wno-cpp $(WERROR) \
  -D__PS3__ -D__lv2ppu__ -DPS3_STAGE=$(STAGE) -DPS3_APPID=\"$(APPID)\" \
  -DNO_VM_COMPILED -DUSE_INTERNAL_ZLIB -DNO_GZIP \
  -include $(CURDIR)/$(C)/sys/ps3_platform.h \
  -I$(CURDIR)/$(C)/sys/include -I$(CURDIR)/$(C) -I$(CURDIR)/$(C)/zlib-1.2.11 \
  $(LIBPSL1GHT_INC)

ifeq ($(DEBUG),1)
  BASE_CFLAGS += -g -DPS3_DEBUG
else
  BASE_CFLAGS += -DNDEBUG
endif
ifeq ($(MAPWALK),1)
  BASE_CFLAGS += -DPS3_MAPWALK
endif
ifeq ($(DIAG),1)
  BASE_CFLAGS += -DPS3_DIAG
endif
BASE_CFLAGS += $(EXTRA_CFLAGS)

ENGINE_CFLAGS := $(BASE_CFLAGS) -I$(OBJDIR)
ifeq ($(STAGE),1)
  ENGINE_CFLAGS += -DDEDICATED
else
  # XMD_H: libjpeg's "typedef long INT32" would be 64-bit here (see ps3_platform.h)
  ENGINE_CFLAGS += -DUSE_INTERNAL_JPEG -DXMD_H -I$(CURDIR)/$(C)/jpeg-8c
endif
BOTLIB_CFLAGS := $(ENGINE_CFLAGS) -DBOTLIB

# The whole EBOOT shares one 64 KB TOC. Code off the rendering hot path gets
# its own TOC block per object (-mminimal-toc), keeping headroom for the rest.
MINTOC_DIRS := $(C)/botlib $(C)/server $(C)/client $(C)/jpeg-8c $(C)/splines $(C)/zlib-1.2.11
BOTLIB_CFLAGS += -mminimal-toc

# Game modules: -mminimal-toc keeps them out of the single 64 KB TOC.
MODULE_CFLAGS := $(BASE_CFLAGS) -mminimal-toc
QAGAME_CFLAGS := $(MODULE_CFLAGS) -DGAMEDLL -DQAGAME
CGAME_CFLAGS  := $(MODULE_CFLAGS) -DCGAMEDLL -DCGAME
UI_CFLAGS     := $(MODULE_CFLAGS) -DUI

# --no-multi-toc: ppu-gcc 7.2 has no -mcmodel, so a TOC over 64 KB makes ld
# split it and emit r2off stubs, which hang the PS3 at boot. Fail the link
# instead; if it fires, shrink TOC use (-mminimal-toc), never drop the flag.
LDFLAGS := -Wl,--no-multi-toc $(LIBPSL1GHT_LIB)
LIBS    := -lsysmodule -lsysutil -lrt -llv2 -lm
ifneq ($(STAGE),1)
  LIBS  := -lrsx -lgcm_sys -lio -laudio $(LIBS)
endif

#---------------------------------------------------------------------------------
# Objects
#---------------------------------------------------------------------------------
ENGINE_OBJS := $(patsubst %.c,$(OBJDIR)/engine/%.o,$(filter %.c,$(ENGINE_SRCS))) \
               $(patsubst %.cpp,$(OBJDIR)/engine/%.o,$(filter %.cpp,$(ENGINE_SRCS)))
ASM_OBJS    := $(patsubst %.S,$(OBJDIR)/engine/%.o,$(PS3_ASM_SRCS))
QAGAME_OBJS := $(patsubst %.c,$(OBJDIR)/qagame/%.o,$(QAGAME_SRCS))
CGAME_OBJS  := $(patsubst %.c,$(OBJDIR)/cgame/%.o,$(CGAME_SRCS))
UI_OBJS     := $(patsubst %.c,$(OBJDIR)/ui/%.o,$(UI_SRCS))
MODULE_OBJS := $(foreach m,$(MODULES),$(OBJDIR)/$(m)_module.o)

ENGINE_CFLAGS_DEFS := $(foreach m,$(MODULES),-DPS3_NATIVE_$(shell echo $(m) | tr a-z A-Z))
ENGINE_CFLAGS += $(ENGINE_CFLAGS_DEFS)
BOTLIB_CFLAGS += $(ENGINE_CFLAGS_DEFS)

ELF := $(OBJDIR)/$(TARGET).elf

#---------------------------------------------------------------------------------
# Rules
#---------------------------------------------------------------------------------
.PHONY: all clean check verify

all: $(OUT_PKG)

$(OUT_PKG): $(OBJDIR)/$(TARGET).pkg
	@cp $(OBJDIR)/$(TARGET).gnpdrm.pkg $@
	@echo "==> $@"

# ppu_rules: %.pkg <- %.self <- %.elf. PKGFILES is refreshed on every build.
$(OBJDIR)/$(TARGET).pkg: pkgfiles

.PHONY: pkgfiles
pkgfiles:
	@rm -rf $(PKGFILES)
	@mkdir -p $(PKGFILES)/USRDIR/main
	@printf "%s\n%s\npatch %s\nbuilt %s\n" "$(TITLE)" "$(TARGET) $(VARIANT)" "$(PATCHLEVEL)" \
	  "$$(date '+%Y-%m-%d %H:%M')" > $(PKGFILES)/USRDIR/VERSION.TXT

$(ELF): $(ASM_OBJS) $(ENGINE_OBJS) $(MODULE_OBJS)
	@echo "LD $(notdir $@)"
	@$(CXX) $(ENGINE_CFLAGS) $(LDFLAGS) $(ASM_OBJS) $(ENGINE_OBJS) $(MODULE_OBJS) $(LIBS) -o $@
	@$(MAKE) --no-print-directory check

# TOC guard (see LDFLAGS): .got must stay under 64 KB and no r2off stubs.
check:
	@got=$$(ppu-readelf -SW $(ELF) | awk '$$2==".got"{print $$6}'); \
	 stubs=$$(ppu-nm $(ELF) | grep -c r2off || true); \
	 echo "   .got = 0x$$got (limit 0x10000), r2off stubs = $$stubs"; \
	 if [ $$((0x$$got)) -ge $$((0x10000)) ] || [ "$$stubs" != "0" ]; then \
	   echo "ERROR: TOC over 64 KB -- this EBOOT would hang at boot"; exit 1; fi

# rewritten only when the patch level changes, so ps3_main.c recompiles exactly then
.PHONY: FORCE
$(OBJDIR)/ps3_rev.h: FORCE
	@mkdir -p $(dir $@)
	@echo '#define PS3_PATCHLEVEL "$(PATCHLEVEL)"' > $@.tmp
	@echo '#define PS3_WALKMAPS "$(WALKMAPS)"' >> $@.tmp
	@echo '#define PS3_WALKSECS $(WALKSECS)' >> $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(OBJDIR)/engine/$(C)/sys/ps3_main.o: $(OBJDIR)/ps3_rev.h

$(OBJDIR)/engine/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(ENGINE_CFLAGS) $(if $(filter $(MINTOC_DIRS),$(patsubst %/,%,$(dir $<))),-mminimal-toc) -c $< -o $@

# splines are C++; the C-only -Werror flags do not apply
$(OBJDIR)/engine/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(CXX) $(filter-out -Werror=implicit-function-declaration -Werror=int-conversion,$(ENGINE_CFLAGS)) -mminimal-toc -c $< -o $@

# the GCM/RSX code: syscall stubs break under aggressive inlining (-O1 -fno-inline)
$(OBJDIR)/engine/$(C)/sys/ps3_glimp.o: $(C)/sys/ps3_glimp.c
	@mkdir -p $(dir $@)
	@echo "CC $< [rsx]"
	@$(CC) $(filter-out -O2,$(ENGINE_CFLAGS)) -O1 -fno-inline -c $< -o $@

# the only file with AltiVec (int16 -> float conversion of the mix)
$(OBJDIR)/engine/$(C)/audio/ps3_snd.o: $(C)/audio/ps3_snd.c
	@mkdir -p $(dir $@)
	@echo "CC $< [vmx]"
	@$(CC) $(filter-out -mno-altivec,$(ENGINE_CFLAGS)) -maltivec -c $< -o $@

$(OBJDIR)/engine/$(C)/botlib/%.o: $(C)/botlib/%.c
	@mkdir -p $(dir $@)
	@echo "CC $< [botlib]"
	@$(CC) $(BOTLIB_CFLAGS) -c $< -o $@

$(OBJDIR)/engine/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) -mno-altivec -c $< -o $@

$(OBJDIR)/qagame/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $< [qagame]"
	@$(CC) $(QAGAME_CFLAGS) -c $< -o $@

# Game module: partial link with its data/bss bracketed, dllEntry/vmMain
# renamed, and every other symbol made local so nothing clashes with the
# engine (q_shared.c, Com_Printf...). Plain ld: gcc's PSL1GHT specs would
# inject lv2.ld.
$(OBJDIR)/%_module.o: $(C)/sys/ps3_module.ld.in
	@echo "LD $(notdir $@)"
	@sed 's/@MOD@/$*/g' $(C)/sys/ps3_module.ld.in > $(OBJDIR)/$*_module.ld
	@ppu-ld -r -T $(OBJDIR)/$*_module.ld $(filter %.o,$^) -o $(OBJDIR)/$*_partial.o
	@$(OBJCOPY) --redefine-sym dllEntry=$*_dllEntry --redefine-sym vmMain=$*_vmMain \
	  $(OBJDIR)/$*_partial.o $(OBJDIR)/$*_renamed.o
	@$(OBJCOPY) -G $*_dllEntry -G $*_vmMain -G __$*_data_start -G __$*_data_end \
	  -G __$*_bss_start -G __$*_bss_end $(OBJDIR)/$*_renamed.o $@

$(OBJDIR)/cgame/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $< [cgame]"
	@$(CC) $(CGAME_CFLAGS) -c $< -o $@

$(OBJDIR)/ui/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $< [ui]"
	@$(CC) $(UI_CFLAGS) -c $< -o $@

$(OBJDIR)/qagame_module.o: $(QAGAME_OBJS)
$(OBJDIR)/cgame_module.o: $(CGAME_OBJS)
$(OBJDIR)/ui_module.o: $(UI_OBJS)

# Which patch level the tree and the built PKG are at: make verify
verify:
	@echo "tree : patch $(PATCHLEVEL)"
	@if [ -f $(OUT_PKG) ]; then \
	   echo "pkg  : patch $$(sed -n 's/^patch //p' $(PKGFILES)/USRDIR/VERSION.TXT 2>/dev/null), $(OUT_PKG) built $$(date -r $(OUT_PKG) '+%Y-%m-%d %H:%M')"; \
	 else echo "pkg  : $(OUT_PKG) not built yet"; fi

clean:
	@rm -rf obj build
	@echo "clean (the .pkg files are kept)"

-include $(ENGINE_OBJS:.o=.d) $(QAGAME_OBJS:.o=.d) $(CGAME_OBJS:.o=.d) $(UI_OBJS:.o=.d)
