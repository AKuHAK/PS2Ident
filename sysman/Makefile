#disable for PCSX2 support
DISABLE_ILINK_DUMPING = 0
DEBUG = 0

ifeq ($(DEBUG),1)
	IOP_CFLAGS += -DDEBUG
endif

ifeq ($(DISABLE_ILINK_DUMPING),1)
	IOP_CFLAGS += -DDISABLE_ILINK_DUMPING
endif

IOP_BIN  = sysman.irx
IOP_OBJS = main.o iLink.o dev9.o usb.o spu2.o rom.o imports.o

IOP_CFLAGS += -Wall -fno-builtin
IOP_INCS += -I../romdrv/include

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_BIN) $(IOP_OBJS_DIR) $(IOP_BIN:.irx=.notiopmod.elf) $(IOP_BIN:.irx=.notiopmod.stripped.elf)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
