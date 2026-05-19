#Enable to build support for the TOOL's host interface
DSNET_HOST_SUPPORT ?= 0
DEBUG ?= 0
COH ?= 0

EE_SIO ?= 0
DISABLE_ILINK_DUMPING ?= 0
#Enable to build a headless (no-UI) ELF that auto-dumps to host0: for emulator testing
HEADLESS ?= 0

EE_BIN = PS2Ident_np.elf
EE_PACKED_BIN = PS2Ident.elf


EE_OBJS_DIR = obj/
EE_SRC_DIR = src/
EE_INC_DIR = include/

#IOP modules
EE_IOP_OBJS = SIO2MAN_irx.o MCMAN_irx.o MCSERV_irx.o PADMAN_irx.o POWEROFF_irx.o PS2DEV9_irx.o USBD_irx.o  USBHDFSD_irx.o USBHDFSDFSV_irx.o SYSMAN_irx.o IOPRP_img.o

EE_GRAPHICS_OBJS = buttons.o devices.o background_img.o font_Default.o
EE_OBJS = main.o system.o UI.o menu.o ident.o SYSMAN_rpc.o graphics.o font.o pad.o DeviceSupport.o crc.o libcdvd_add.o OSDInit.o modelname.o dvdplayer.o ps1.o $(EE_IOP_OBJS) $(EE_GRAPHICS_OBJS)

EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

EE_INCS := -I$(PS2SDK)/ports/include -I$(PS2SDK)/ports/include/freetype2 -I$(EE_INC_DIR)
EE_LDFLAGS := -L$(PS2SDK)/ports/lib
EE_LIBS := -lgs -lpng -lz -lcdvd -lmc -lpadx -lpatches -liopreboot -lfreetype -lm
EE_CFLAGS += $(EE_GPVAL) -Wno-missing-braces -O0 -g

EE_TEMP_FILES = SIO2MAN_irx.c MCMAN_irx.c MCSERV_irx.c PADMAN_irx.c POWEROFF_irx.c PS2DEV9_irx.c USBD_irx.c  USBHDFSD_irx.c USBHDFSDFSV_irx.c SYSMAN_irx.c buttons.c devices.c background_img.c font_Default.c IOPRP_img.c

EE_TEMP_FILES := $(EE_TEMP_FILES:%=$(EE_SRC_DIR)%)

ifeq ($(HEADLESS),1)
  EE_CFLAGS += -DHEADLESS
  DSNET_HOST_SUPPORT = 1
  EE_SIO = 1
  DEBUG = 1
endif

ifeq ($(DSNET_HOST_SUPPORT),1)
  EE_CFLAGS += -DDSNET_HOST_SUPPORT
  DEBUG = 1
endif

ifeq ($(DEBUG),1)
  IOP_CFLAGS += -DDEBUG
  EE_CFLAGS += -DDEBUG
endif

ifeq ($(EE_SIO),1)
  EE_CFLAGS += -DEE_UART
  EE_LIBS += -lsiocookie
endif

ifeq ($(COH),1)
  EE_CFLAGS += -DCOH_SUPPORT -DDISABLE_LIBCGLUE_INIT
  IOPRP_BIN = irx/ioprp_coh.img
else
  IOPRP_BIN = irx/ioprp.img
endif

ifeq ($(DISABLE_ILINK_DUMPING),1)
  IOP_CFLAGS += -DDISABLE_ILINK_DUMPING
  EE_CFLAGS += -DDISABLE_ILINK_DUMPING
endif

$(EE_PACKED_BIN): $(EE_BIN)
	ps2-packer $(EE_BIN) $(EE_PACKED_BIN)

all:
	$(MAKE) $(EE_PACKED_BIN)

clean:
	make clean -C sysman
	make clean -C usbhdfsdfsv
	rm -rf $(EE_BIN) $(EE_PACKED_BIN) $(EE_BIN_REL) $(EE_OBJS_DIR) $(EE_TEMP_FILES)

$(EE_SRC_DIR)SIO2MAN_irx.c: $(PS2SDK)/iop/irx/freesio2.irx
	bin2c $< $@ SIO2MAN_irx

$(EE_SRC_DIR)MCMAN_irx.c: $(PS2SDK)/iop/irx/mcman.irx
	bin2c $< $@ MCMAN_irx

$(EE_SRC_DIR)MCSERV_irx.c: $(PS2SDK)/iop/irx/mcserv.irx
	bin2c $< $@ MCSERV_irx

$(EE_SRC_DIR)PADMAN_irx.c: $(PS2SDK)/iop/irx/freepad.irx
	bin2c $< $@ PADMAN_irx

$(EE_SRC_DIR)POWEROFF_irx.c: $(PS2SDK)/iop/irx/poweroff.irx
	bin2c $< $@ POWEROFF_irx

$(EE_SRC_DIR)PS2DEV9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	bin2c $< $@ PS2DEV9_irx

$(EE_SRC_DIR)USBD_irx.c: $(PS2SDK)/iop/irx/usbd.irx
	bin2c $< $@ USBD_irx

$(EE_SRC_DIR)USBHDFSD_irx.c: $(PS2SDK)/iop/irx/usbhdfsd.irx
	bin2c $< $@ USBHDFSD_irx

usbhdfsdfsv/usbhdfsdfsv.irx:
	$(MAKE) -C usbhdfsdfsv

$(EE_SRC_DIR)USBHDFSDFSV_irx.c: usbhdfsdfsv/usbhdfsdfsv.irx
	bin2c $< $@ USBHDFSDFSV_irx

sysman/sysman.irx:
	$(MAKE) -C sysman
$(EE_SRC_DIR)SYSMAN_irx.c: sysman/sysman.irx
	bin2c $< $@ SYSMAN_irx

$(EE_SRC_DIR)background_img.c: resources/background.png
	bin2c $< $@ background

$(EE_SRC_DIR)font_Default.c: resources/NotoSansMono-CondensedBold.ttf
	bin2c $< $@ font_Default

$(EE_SRC_DIR)buttons.c: resources/buttons.png
	bin2c $< $@ buttons

$(EE_SRC_DIR)devices.c: resources/devices.png
	bin2c $< $@ devices

$(EE_SRC_DIR)IOPRP_img.c: $(IOPRP_BIN)
	bin2c $< $@ IOPRP_img


$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c
	$(DIR_GUARD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJS_DIR)%.o: $(EE_ASM_DIR)%.s
	$(DIR_GUARD)
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
include $(PS2SDK)/samples/Makefile.eeglobal
