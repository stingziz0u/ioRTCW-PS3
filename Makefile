#---------------------------------------------------------------------------------
# ioRTWC-PS3 -- iortcw (Return to Castle Wolfenstein, single player) for PS3
#
#   make              full game (stage 2)            -> iortcw-ps3-$(VERSION).pkg
#   make STAGE=1      headless engine test           -> iortcw-ps3-stage1.pkg
#   make DEBUG=1      debug build (any stage)        -> iortcw-ps3-...-debug.pkg
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

VERSION := 0.1
STAGE   ?= 2
DEBUG   ?= 0

APPID   := IORTCWPS3
ifeq ($(STAGE),1)
  TITLE   := Return to Castle Wolfenstein (stage 1)
  VARIANT := stage1
else
  TITLE   := Return to Castle Wolfenstein
  VARIANT := $(VERSION)
endif
ifeq ($(DEBUG),1)
  VARIANT := $(VARIANT)-debug
endif

TARGET   := iortcw-ps3
OUT_PKG  := $(TARGET)-$(VARIANT).pkg
OBJDIR   := $(CURDIR)/obj/$(VARIANT)
BUILDDIR := $(CURDIR)/build/$(VARIANT)
PKGFILES := $(BUILDDIR)/pkgfiles

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
  $(C)/sys/ps3_main.c $(C)/sys/ps3_sys.c $(C)/sys/ps3_log.c $(C)/sys/ps3_net_null.c

PS3_ASM_SRCS := $(C)/sys/ps3_setjmp.S

ifeq ($(STAGE),1)
  ENGINE_SRCS := $(COMMON_SRCS) $(SERVER_SRCS) $(BOTLIB_SRCS) $(ZLIB_SRCS) $(PS3_SRCS) \
                 $(C)/null/null_client.c $(C)/null/null_input.c $(C)/null/null_snddma.c
  MODULES     := qagame
else
  $(error "STAGE=2 (client) is not written yet: use make STAGE=1")
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
endif
BASE_CFLAGS += $(EXTRA_CFLAGS)

ENGINE_CFLAGS := $(BASE_CFLAGS)
ifeq ($(STAGE),1)
  ENGINE_CFLAGS += -DDEDICATED
endif
BOTLIB_CFLAGS := $(ENGINE_CFLAGS) -DBOTLIB

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

#---------------------------------------------------------------------------------
# Objects
#---------------------------------------------------------------------------------
ENGINE_OBJS := $(patsubst %.c,$(OBJDIR)/engine/%.o,$(ENGINE_SRCS))
ASM_OBJS    := $(patsubst %.S,$(OBJDIR)/engine/%.o,$(PS3_ASM_SRCS))
QAGAME_OBJS := $(patsubst %.c,$(OBJDIR)/qagame/%.o,$(QAGAME_SRCS))
MODULE_OBJS := $(foreach m,$(MODULES),$(OBJDIR)/$(m)_module.o)

ENGINE_CFLAGS_DEFS := $(foreach m,$(MODULES),-DPS3_NATIVE_$(shell echo $(m) | tr a-z A-Z))
ENGINE_CFLAGS += $(ENGINE_CFLAGS_DEFS)
BOTLIB_CFLAGS += $(ENGINE_CFLAGS_DEFS)

ELF := $(OBJDIR)/$(TARGET).elf

#---------------------------------------------------------------------------------
# Rules
#---------------------------------------------------------------------------------
.PHONY: all clean check getlog

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
	@printf "%s\n%s\nbuilt %s\n" "$(TITLE)" "$(TARGET) $(VARIANT)" "$$(date '+%Y-%m-%d %H:%M')" \
	  > $(PKGFILES)/USRDIR/VERSION.TXT

$(ELF): $(ASM_OBJS) $(ENGINE_OBJS) $(MODULE_OBJS)
	@echo "LD $(notdir $@)"
	@$(CC) $(ENGINE_CFLAGS) $(LDFLAGS) $(ASM_OBJS) $(ENGINE_OBJS) $(MODULE_OBJS) $(LIBS) -o $@
	@$(MAKE) --no-print-directory check

# TOC guard (see LDFLAGS): .got must stay under 64 KB and no r2off stubs.
check:
	@got=$$(ppu-readelf -SW $(ELF) | awk '$$2==".got"{print $$6}'); \
	 stubs=$$(ppu-nm $(ELF) | grep -c r2off || true); \
	 echo "   .got = 0x$$got (limit 0x10000), r2off stubs = $$stubs"; \
	 if [ $$((0x$$got)) -ge $$((0x10000)) ] || [ "$$stubs" != "0" ]; then \
	   echo "ERROR: TOC over 64 KB -- this EBOOT would hang at boot"; exit 1; fi

$(OBJDIR)/engine/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(ENGINE_CFLAGS) -c $< -o $@

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

$(OBJDIR)/qagame_module.o: $(QAGAME_OBJS)

# Download the log over FTP (webMAN): make getlog PS3IP=192.168.x.x
getlog:
	@test -n "$(PS3IP)" || (echo "usage: make getlog PS3IP=192.168.x.x"; exit 1)
	@curl -s -o iortcw-ps3.log ftp://$(PS3IP)/dev_hdd0/game/$(APPID)/USRDIR/iortcw-ps3.log
	@curl -s -o iortcw-ps3.old.log ftp://$(PS3IP)/dev_hdd0/game/$(APPID)/USRDIR/iortcw-ps3.old.log || true
	@echo "==> iortcw-ps3.log ($$(wc -l < iortcw-ps3.log) lines)"

clean:
	@rm -rf obj build
	@echo "clean (the .pkg files are kept)"

-include $(ENGINE_OBJS:.o=.d) $(QAGAME_OBJS:.o=.d)
