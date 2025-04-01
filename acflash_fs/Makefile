
#      ___   ______________    ___   _____ __  __   ___________
#     /   | / ____/ ____/ /   /   | / ___// / / /  / ____/ ___/
#    / /| |/ /   / /_  / /   / /| | \__ \/ /_/ /  / /_   \__ \ 
#   / ___ / /___/ __/ / /___/ ___ |___/ / __  /  / __/  ___/ / 
#  /_/  |_\____/_/   /_____/_/  |_/____/_/ /_/  /_/    /____/  
#                                                              
# FILEXIO API for ACFLASH.IRX

IOP_BIN = acflash_fs.irx
IOP_OBJS = acflash_fs.o imports.o
IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0


all: $(IOP_BIN)
	cp $< ..

clean:
	rm -f -r $(IOP_OBJS) $(IOP_BIN)

ifeq ($(DEBUG), 1)
  $(info -- debug enabled)
  IOP_CFLAGS += -DDEBUG=1
endif

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal