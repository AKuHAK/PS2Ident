EE_BIN = PS2Ident.elf

#IOP modules
EE_IOP_OBJS = SIO2MAN_irx.o MCMAN_irx.o MCSERV_irx.o PADMAN_irx.o POWEROFF_irx.o PS2DEV9_irx.o USBD_irx.o USBHDFSD_irx.o USBHDFSDFSV_irx.o SYSMAN_irx.o

EE_GRAPHICS_OBJS = buttons.o devices.o background_img.o
EE_OBJS = main.o system.o UI.o menu.o ident.o dbms.o SYSMAN_rpc.o graphics.o font.o pad.o DeviceSupport.o crc16.o libcdvd_add.o OSDInit.o modelname.o dvdplayer.o ps1.o $(EE_IOP_OBJS) $(EE_GRAPHICS_OBJS)

EE_INCS := -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I. -I$(PS2SDK)/ports/include
EE_LDFLAGS :=  -L$(PS2SDK)/ee/lib -L$(PS2SDK)/ports/lib -nostartfiles $(PS2SDK)/ee/startup/crt0.o -Tlinkfile -s
EE_LIBS := -lgs -lpng -lz -lm -lcdvd -lmc -lpadx -lpatches -lfreetype -lc -lkernel
EE_GPVAL = -G8192
EE_CFLAGS += -D_EE -O2 -mgpopt $(EE_GPVAL)

EE_TEMP_FILES = SIO2MAN_irx.c MCMAN_irx.c MCSERV_irx.c PADMAN_irx.c POWEROFF_irx.c PS2DEV9_irx.c USBD_irx.c USBHDFSD_irx.c USBHDFSDFSV_irx.c SYSMAN_irx.c buttons.c devices.c background_img.c

%.o : %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.s
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

$(EE_BIN) : $(EE_OBJS)
	$(EE_CC) $(EE_CFLAGS) $(EE_LDFLAGS) -o $(EE_BIN) $(EE_OBJS) $(EE_LIBS)

all:
	$(MAKE) $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_BIN_REL) $(EE_OBJS) $(EE_TEMP_FILES)

SIO2MAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/freesio2.irx SIO2MAN_irx.c SIO2MAN_irx

MCMAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/mcman.irx MCMAN_irx.c MCMAN_irx

MCSERV_irx.c:
	bin2c $(PS2SDK)/iop/irx/mcserv.irx MCSERV_irx.c MCSERV_irx

PADMAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/freepad.irx PADMAN_irx.c PADMAN_irx

POWEROFF_irx.c:
	bin2c $(PS2SDK)/iop/irx/poweroff.irx POWEROFF_irx.c POWEROFF_irx

PS2DEV9_irx.c:
	bin2c $(PS2SDK)/iop/irx/ps2dev9.irx PS2DEV9_irx.c PS2DEV9_irx

USBD_irx.c:
	bin2c $(PS2SDK)/iop/irx/usbd.irx USBD_irx.c USBD_irx

USBHDFSD_irx.c:
	bin2c $(PS2SDK)/iop/irx/usbhdfsd.irx USBHDFSD_irx.c USBHDFSD_irx

USBHDFSDFSV_irx.c:
	bin2c irx/usbhdfsdfsv.irx USBHDFSDFSV_irx.c USBHDFSDFSV_irx

SYSMAN_irx.c:
	bin2c irx/sysman.irx SYSMAN_irx.c SYSMAN_irx

background_img.c:
	bin2c resources/background.png background_img.c background

buttons.c:
	bin2c resources/buttons.png buttons.c buttons

devices.c:
	bin2c resources/devices.png devices.c devices

include $(PS2SDK)/samples/Makefile.pref
