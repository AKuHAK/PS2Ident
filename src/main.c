#include <kernel.h>
#include <libcdvd.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <iopcontrol_special.h>
#include <errno.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <usbhdfsd-common.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <rom0_info.h>
#include <sbv_patches.h>
#include <sio.h>

#include <libgs.h>

#include "sysman/sysinfo.h"
#include "SYSMAN_rpc.h"

#include "main.h"
#include "dbms.h"
#include "menu.h"
#include "system.h"
#include "pad.h"
#include "graphics.h"
#include "crc.h"
#include "libcdvd_add.h"
#include "dvdplayer.h"
#include "OSDInit.h"
#include "ps1.h"
#include "modelname.h"

#include "UI.h"

#include "ident.h"
#define EXTERN_BIN2C(_irx) \
extern unsigned char _irx[]; \
extern unsigned int size_##_irx

EXTERN_BIN2C(SIO2MAN_irx);
EXTERN_BIN2C(PADMAN_irx);
EXTERN_BIN2C(MCMAN_irx);
EXTERN_BIN2C(MCSERV_irx);
EXTERN_BIN2C(POWEROFF_irx);
EXTERN_BIN2C(PS2DEV9_irx);
EXTERN_BIN2C(USBD_irx);
EXTERN_BIN2C(BDM_irx);
EXTERN_BIN2C(BDMFS_FATFS_irx);
EXTERN_BIN2C(USBMASS_BD_irx);
EXTERN_BIN2C(USBHDFSDFSV_irx);
EXTERN_BIN2C(SYSMAN_irx);
EXTERN_BIN2C(IOPRP_img);

extern void *_gp;

static int LoadEROMDRV(void)
{
    char eromdrv[] = "rom1:EROMDRV?";
    int fd         = 0;

    // Handle region-specific DVD Player of newer consoles.
    if (OSDGetDVDPlayerRegion(&eromdrv[12]) == 0 || (fd = open(eromdrv, O_RDONLY)) < 0)
        eromdrv[12] = '\0'; // Replace '?' with a NULL.

    close(fd);
    DEBUG_PRINTF("EROMDRV: %s\n", eromdrv);

    return SifLoadModuleEncrypted(eromdrv, 0, NULL);
}

#define SYSTEM_INIT_THREAD_STACK_SIZE 0x8000

struct SystemInitParams
{
    struct SystemInformation *SystemInformation;
    int InitCompleteSema;
};

static void SystemInitThread(struct SystemInitParams *SystemInitParams)
{
    int fd, i, id, ret;
    GetRomName(SystemInitParams->SystemInformation->mainboard.romver);

    id = SifExecModuleBuffer(MCSERV_irx, size_MCSERV_irx, 0, NULL, &ret);
    DEBUG_PRINTF("MCSERV id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(PADMAN_irx, size_PADMAN_irx, 0, NULL, &ret);
    DEBUG_PRINTF("PADMAN id:%d ret:%d\n", id, ret);

    id = SifExecModuleBuffer(POWEROFF_irx, size_POWEROFF_irx, 0, NULL, &ret);
    DEBUG_PRINTF("POWEROFF id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(PS2DEV9_irx, size_PS2DEV9_irx, 0, NULL, &ret);
    DEBUG_PRINTF("DEV9 id:%d ret:%d\n", id, ret);

    SifLoadModule("rom0:ADDDRV", 0, NULL);
    SifLoadModule("rom0:ADDROM2", 0, NULL);

    // Initialize PlayStation Driver (PS1DRV)
    PS1DRVInit();

    // Initialize ROM DVD player.
    // It is normal for this to fail on consoles that have no DVD ROM chip (i.e. DEX or the SCPH-10000/SCPH-15000).
    DVDPlayerInit();

    LoadEROMDRV();

    /* Must be loaded last, after all devices have been initialized. */
    id = SifExecModuleBuffer(SYSMAN_irx, size_SYSMAN_irx, 0, NULL, &ret);
    DEBUG_PRINTF("SYSMAN id:%d ret:%d\n", id, ret);

    SysmanInit();

    GetPeripheralInformation(SystemInitParams->SystemInformation);

    char rom_extinfo[16];
    for (i = 0; i < 3; i++)
    {
        if (SystemInitParams->SystemInformation->ROMs[i].IsExists)
        {
            DEBUG_PRINTF("i = %d\n", i);
            sprintf(rom_extinfo, "rom%d:EXTINFO", i);
            fd = open(rom_extinfo, O_RDONLY);

            if (fd >= 0)
            {
                // This function returns part of EXTINFO data of the rom
                // This module contains information about Sony build environment at offst 0x10
                // first 15 symbols is build date/time that should be unique per rom and can be used as unique serial
                // Example for romver 0160EC20010704
                // 20010704-160707,ROMconf,PS20160EC20010704.bin,kuma@rom-server/~/f10k/g/app/rom
                // 20010704-160707 can be used as unique ID for Bios
                SystemInitParams->SystemInformation->ROMs[i].extinfo[0] = '\0';
                lseek(fd, 0x10, SEEK_SET);
                read(fd, SystemInitParams->SystemInformation->ROMs[i].extinfo, sizeof(SystemInitParams->SystemInformation->ROMs[i].extinfo));
                close(fd);
                SystemInitParams->SystemInformation->ROMs[i].extinfo[15] = '\0';
            }
            if (i == 0)
                strncpy(SystemInitParams->SystemInformation->mainboard.BOOT_ROM.extinfo, SystemInitParams->SystemInformation->ROMs[i].extinfo, 16);
            else if (i == 1)
                strncpy(SystemInitParams->SystemInformation->mainboard.DVD_ROM.extinfo, SystemInitParams->SystemInformation->ROMs[i].extinfo, 16);
            DEBUG_PRINTF("ROMs[%d].EXTINFO: %s\n", i, SystemInitParams->SystemInformation->ROMs[i].extinfo);
        }
    }

    SignalSema(SystemInitParams->InitCompleteSema);
    ExitDeleteThread();
}

int VBlankStartSema;

static int VBlankStartHandler(int cause)
{
    ee_sema_t sema;
    iReferSemaStatus(VBlankStartSema, &sema);
    if (sema.count < sema.max_count)
        iSignalSema(VBlankStartSema);

    return 0;
}

extern int UsbReadyStatus;

static void usb_callback(void *packet, void *common)
{
    UsbReadyStatus = (((SifCmdHeader_t *)packet)->opt == USBMASS_DEV_EV_CONN) ? 1 : 0;
}

int main(int argc, char *argv[])
{
    DEBUG_INIT_PRINTF();
    sio_puts("-- PS2IDENT START\n");
    static SifCmdHandlerData_t SifCmdbuffer;
    struct SystemInformation SystemInformation;
    void *SysInitThreadStack;
    ee_sema_t ThreadSema;
    int SystemInitSema;
    struct SystemInitParams InitThreadParams;
    int id, ret;

    //	chdir("mass:/PS2Ident/");
    if (argc < 1 || GetBootDeviceID() == BOOT_DEVICE_UNKNOWN)
    {
        Exit(-1);
    }

    SifInitRpc(0);
    while (!SifIopRebootBuffer(IOPRP_img, size_IOPRP_img))
    {
    };
    memset(&SystemInformation, 0, sizeof(SystemInformation));

    /* Go gather some information from the EE's peripherals while the IOP reset. */
    GetEEInformation(&SystemInformation);

    InitCRC32LookupTable();

    ThreadSema.init_count = 0;
    ThreadSema.max_count  = 1;
    ThreadSema.attr = ThreadSema.option = 0;
    InitThreadParams.InitCompleteSema = SystemInitSema = CreateSema(&ThreadSema);
    InitThreadParams.SystemInformation                 = &SystemInformation;

    SysInitThreadStack                                 = memalign(64, SYSTEM_INIT_THREAD_STACK_SIZE);

    ThreadSema.init_count                              = 0;
    ThreadSema.max_count                               = 1;
    ThreadSema.attr = ThreadSema.option = 0;
    VBlankStartSema                     = CreateSema(&ThreadSema);

    AddIntcHandler(kINTC_VBLANK_START, &VBlankStartHandler, 0);
    EnableIntc(kINTC_VBLANK_START);

    while (!SifIopSync())
    {
    };

#ifdef COH_SUPPORT
    id = SifLoadStartModule("rom0:CDVDFSV", 0, NULL, &ret);
    DEBUG_PRINTF("rom0:CDVDFSV id:%d ret:%d\n", id, ret);
#endif

    SifInitRpc(0);
    SifInitIopHeap();
    SifLoadFileInit();

    sbv_patch_enable_lmb();
    sbv_patch_fileio();

    id = SifExecModuleBuffer(SIO2MAN_irx, size_SIO2MAN_irx, 0, NULL, &ret);
    DEBUG_PRINTF("SIO2MAN id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(MCMAN_irx, size_MCMAN_irx, 0, NULL, &ret);
    DEBUG_PRINTF("MCMAN id:%d ret:%d\n", id, ret);

    SifSetCmdBuffer(&SifCmdbuffer, 1);
    SifAddCmdHandler(0, &usb_callback, NULL);

    id = SifExecModuleBuffer(USBD_irx, size_USBD_irx, 0, NULL, &ret);
    DEBUG_PRINTF("USBD id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(BDM_irx, size_BDM_irx, 0, NULL, &ret);
    DEBUG_PRINTF("BDM id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(BDMFS_FATFS_irx, size_BDMFS_FATFS_irx, 0, NULL, &ret);
    DEBUG_PRINTF("BDM_FATFS id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(USBMASS_BD_irx, size_USBMASS_BD_irx, 0, NULL, &ret);
    DEBUG_PRINTF("USBMASS_BD id:%d ret:%d\n", id, ret);
    id = SifExecModuleBuffer(USBHDFSDFSV_irx, size_USBHDFSDFSV_irx, 0, NULL, &ret);
    DEBUG_PRINTF("USBHDFSDFSV id:%d ret:%d\n", id, ret);

    sceCdInit(SCECdINoD);
    cdInitAdd();

    // Initialize system paths.
    OSDInitSystemPaths();

    // Initialize ROM version (must be done first).
    OSDInitROMVER();

    if (InitializeUI(0) != 0)
    {
        SifExitRpc();
        Exit(-1);
    }

    DEBUG_PRINTF("Loading database.\n");

    // PS2IDBMS_LoadDatabase("PS2Ident.db");

    DEBUG_PRINTF("Initializing hardware...\n");

    SysCreateThread(&SystemInitThread, SysInitThreadStack, SYSTEM_INIT_THREAD_STACK_SIZE, &InitThreadParams, 0x2);

    int FrameNum = 0;
    while (PollSema(SystemInitSema) != SystemInitSema)
    {
        RedrawLoadingScreen(FrameNum);
        DEBUG_PRINTF("Frame: %d\n", FrameNum);
        FrameNum++;
    }
    DeleteSema(SystemInitSema);
    DEBUG_PRINTF("DeleteSema done!\n");
    free(SysInitThreadStack);
    DEBUG_PRINTF("free done!\n");

    SifLoadFileExit();
    SifExitIopHeap();

    DEBUG_PRINTF("System init: Initializing RPCs.\n");

    PadInitPads();
    mcInit(MC_TYPE_XMC);

    DEBUG_PRINTF("done!\nEntering main menu.\n");

    MainMenu(&SystemInformation);

    PadDeinitPads();

    DisableIntc(kINTC_VBLANK_START);
    RemoveIntcHandler(kINTC_VBLANK_START, 0);
    DeleteSema(VBlankStartSema);
    SifRemoveCmdHandler(0);

    DeinitializeUI();

    // PS2IDBMS_UnloadDatabase();

    sceCdInit(SCECdEXIT);
    SysmanDeinit();
    SifExitRpc();

    return 0;
}

#ifdef DISABLE_LIBCGLUE_INIT
void _libcglue_rtc_update() {}
#endif
