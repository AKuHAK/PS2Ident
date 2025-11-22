#include <kernel.h>
#include <errno.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libcdvd.h>
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sbv_patches.h>
#include <osd_config.h>
#include <timer.h>
#include <limits.h>
#include <sys/types.h>
#include <stdlib.h>
#include <malloc.h>

#include <libgs.h>

#include <speedregs.h>
#include <smapregs.h>
#include <dev9regs.h>

#include "sysman/sysinfo.h"
#include "SYSMAN_rpc.h"

#include "main.h"
#include "ident.h"
#include "pad.h"
#include "graphics.h"
#include "libcdvd_add.h"
#include "dvdplayer.h"
#include "OSDInit.h"
#include "ps1.h"
#include "modelname.h"

#include "UI.h"
#include "menu.h"
#include "crc.h"
#include "dbms.h"

extern struct UIDrawGlobal UIDrawGlobal;

#define GS_REG_CSR          (volatile u64 *)0x12001000 // System Status
#define readDevMemEEIOP_src 1                          // 0=IOP / 1=EE

int readDevMemEEIOP(const void *MemoryStart, void *buffer, unsigned int NumBytes, int mode)
{
    int ret = 0;

    if (readDevMemEEIOP_src == 0)
    {
        ret = SysmanReadMemory(MemoryStart, buffer, NumBytes, mode);
    }
    else
    {
        if ((u32)MemoryStart & 0x80000000)
        {
            DI();
            ee_kmode_enter();
        }

        unsigned int i;
        const u8 *mpt = MemoryStart;
        u8 *bpt       = buffer;
        for (i = 0; i < NumBytes; i++, mpt++, bpt++)
        {
            *bpt = *mpt;
        }
        FlushCache(0); // should be enough to fix it

        if ((u32)MemoryStart & 0x80000000)
        {
            ee_kmode_exit();
            EI();
        }
    }
    return ret;
}

int GetEEInformation(struct SystemInformation *SystemInformation)
{
    unsigned short int revision;
    unsigned int value;

    revision                                       = GetCop0(15);
    SystemInformation->mainboard.ee.implementation = revision >> 8;
    SystemInformation->mainboard.ee.revision       = revision & 0xFF;

    asm("cfc1 %0, $0\n"
        : "=r"(revision)
        :);
    SystemInformation->mainboard.ee.FPUImplementation = revision >> 8;
    SystemInformation->mainboard.ee.FPURevision       = revision & 0xFF;

    value                                             = GetCop0(16);
    SystemInformation->mainboard.ee.ICacheSize        = value >> 9 & 3;
    SystemInformation->mainboard.ee.DCacheSize        = value >> 6 & 3;
    SystemInformation->mainboard.ee.RAMSize           = GetMemorySize();
    SystemInformation->mainboard.MachineType          = MachineType();

    revision                                          = (*GS_REG_CSR) >> 16;
    SystemInformation->mainboard.gs.revision          = revision & 0xFF;
    SystemInformation->mainboard.gs.id                = revision >> 8;

    ee_kmode_enter();
    SystemInformation->mainboard.ee.F520 = *(volatile unsigned int *)0xB000F520;
    SystemInformation->mainboard.ee.F540 = *(volatile unsigned int *)0xB000F540;
    SystemInformation->mainboard.ee.F550 = *(volatile unsigned int *)0xB000F550;
    ee_kmode_exit();

    return 0;
}

static u32 CalculateCRCOfROM(void *buffer1, void *buffer2, void *start, unsigned int length)
{
    u32 crc;
    unsigned int i, size = 0, prevSize;
    void *pDestBuffer, *pSrcBuffer;

    for (i = 0, prevSize = size, crc = CRC32_INITIAL_CHECKSUM, pDestBuffer = buffer1, pSrcBuffer = start; i < length; i += size, pSrcBuffer += size)
    {
        size = length - i > MEM_IO_BLOCK_SIZE ? MEM_IO_BLOCK_SIZE : length - i;

        SysmanSync(0);
        while (readDevMemEEIOP(pSrcBuffer, pDestBuffer, size, 1) != 0)
            nopdelay();

        pDestBuffer = (pDestBuffer == buffer1) ? buffer2 : buffer1;
        if (i > 0)
            crc = CalculateCRC32(UNCACHED_SEG(pDestBuffer), prevSize, crc);
        prevSize = size;
    }

    pDestBuffer = (pDestBuffer == buffer1) ? buffer2 : buffer1;
    SysmanSync(0);
    crc = CalculateCRC32(UNCACHED_SEG(pDestBuffer), prevSize, crc);
    return ReflectAndXORCRC32(crc);
}

int GetPeripheralInformation(struct SystemInformation *SystemInformation)
{
    t_SysmanHardwareInfo hwinfo;
    u32 stat, result;
    void *buffer1, *buffer2;
    char *pNewline;

    SysmanGetHardwareInfo(&hwinfo);

    memcpy(&SystemInformation->mainboard.iop, &hwinfo.iop, sizeof(SystemInformation->mainboard.iop));
    memcpy(SystemInformation->ROMs, hwinfo.ROMs, sizeof(SystemInformation->ROMs));
    memcpy(&SystemInformation->erom, &hwinfo.erom, sizeof(SystemInformation->erom));
    memcpy(&SystemInformation->mainboard.BOOT_ROM, &hwinfo.BOOT_ROM, sizeof(SystemInformation->mainboard.BOOT_ROM));
    memcpy(&SystemInformation->mainboard.DVD_ROM, &hwinfo.DVD_ROM, sizeof(SystemInformation->mainboard.DVD_ROM));
    memcpy(&SystemInformation->mainboard.ssbus, &hwinfo.ssbus, sizeof(SystemInformation->mainboard.ssbus));
    memcpy(&SystemInformation->mainboard.iLink, &hwinfo.iLink, sizeof(SystemInformation->mainboard.iLink));
    memcpy(&SystemInformation->mainboard.usb, &hwinfo.usb, sizeof(SystemInformation->mainboard.usb));
    memcpy(&SystemInformation->mainboard.spu2, &hwinfo.spu2, sizeof(SystemInformation->mainboard.spu2));
    SystemInformation->mainboard.BoardInf         = hwinfo.BoardInf;
    SystemInformation->mainboard.MPUBoardID       = hwinfo.MPUBoardID;
    SystemInformation->mainboard.ROMGEN_MonthDate = hwinfo.ROMGEN_MonthDate;
    SystemInformation->mainboard.ROMGEN_Year      = hwinfo.ROMGEN_Year;
    SystemInformation->mainboard.status           = 0;

    buffer1                                       = memalign(64, MEM_IO_BLOCK_SIZE);
    buffer2                                       = memalign(64, MEM_IO_BLOCK_SIZE);

    if (SystemInformation->mainboard.BOOT_ROM.IsExists)
    {
        SystemInformation->mainboard.BOOT_ROM.crc32 = CalculateCRCOfROM(buffer1, buffer2, (void *)SystemInformation->mainboard.BOOT_ROM.StartAddress, SystemInformation->mainboard.BOOT_ROM.size);
        DEBUG_PRINTF("BOOT ROM CRC32: 0x%08x\n", SystemInformation->mainboard.BOOT_ROM.crc32);
    }

    if (SystemInformation->mainboard.DVD_ROM.IsExists)
    {
        SystemInformation->mainboard.DVD_ROM.crc32 = CalculateCRCOfROM(buffer1, buffer2, (void *)SystemInformation->mainboard.DVD_ROM.StartAddress, SystemInformation->mainboard.DVD_ROM.size);
        DEBUG_PRINTF("DVD ROM CRC32: 0x%08x\n", SystemInformation->mainboard.DVD_ROM.crc32);
    }

    free(buffer1);
    free(buffer2);

    // Initialize model name
    if (ModelNameInit() == 0)
    {
        // Get model name
        strncpy(SystemInformation->mainboard.ModelName, ModelNameGet(), sizeof(SystemInformation->mainboard.ModelName) - 1);
        SystemInformation->mainboard.ModelName[sizeof(SystemInformation->mainboard.ModelName) - 1] = '\0';
        if (SystemInformation->mainboard.ModelName[0] == 0xFF)
        {
            SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_MNAME;
            SystemInformation->mainboard.ModelName[0] = '\0';
#ifdef COH_SUPPORT
            // Namco System 246/256 arcade boards have no model name stored. Use "System 256" as the model name.
            strncpy(SystemInformation->mainboard.ModelName, "System 256", sizeof(SystemInformation->mainboard.ModelName) - 1);
            SystemInformation->mainboard.ModelName[sizeof(SystemInformation->mainboard.ModelName) - 1] = '\0';
            SystemInformation->mainboard.status &= ~PS2IDB_STAT_ERR_MNAME; // Clear the error status since we have a valid name now.
#endif
        }
    }
    else
    {
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_MNAME;
        SystemInformation->mainboard.ModelName[0] = '\0';
    }

    // Get DVD Player version
    strncpy(SystemInformation->DVDPlayerVer, DVDPlayerGetVersion(), sizeof(SystemInformation->DVDPlayerVer) - 1);
    SystemInformation->DVDPlayerVer[sizeof(SystemInformation->DVDPlayerVer) - 1] = '\0';
    if ((pNewline = strrchr(SystemInformation->DVDPlayerVer, '\n')) != NULL)
        *pNewline = '\0'; // The DVD player version may have a newline in it.

    // Get OSD Player version
    strncpy(SystemInformation->OSDVer, OSDGetVersion(), sizeof(SystemInformation->OSDVer) - 1);
    SystemInformation->OSDVer[sizeof(SystemInformation->OSDVer) - 1] = '\0';
    if ((pNewline = strrchr(SystemInformation->OSDVer, '\n')) != NULL)
        *pNewline = '\0'; // The OSDVer may have a newline in it.

    // Get PS1DRV version
    strncpy(SystemInformation->PS1DRVVer, PS1DRVGetVersion(), sizeof(SystemInformation->PS1DRVVer) - 1);
    SystemInformation->PS1DRVVer[sizeof(SystemInformation->PS1DRVVer) - 1] = '\0';

    memset(SystemInformation->ConsoleID, 0, sizeof(SystemInformation->ConsoleID));
    memset(SystemInformation->iLinkID, 0, sizeof(SystemInformation->iLinkID));
    memset(SystemInformation->SMAP_MAC_address, 0, sizeof(SystemInformation->SMAP_MAC_address));
    memset(SystemInformation->mainboard.MECHACONVersion, 0, sizeof(SystemInformation->mainboard.MECHACONVersion));
    SystemInformation->DSPVersion = 0;
    memset(SystemInformation->mainboard.MRenewalDate, 0, sizeof(SystemInformation->mainboard.MRenewalDate));

    if (sceGetDspVersion(&SystemInformation->DSPVersion, &stat) == 0 || (stat & 0x80) != 0)
        DEBUG_PRINTF("Failed to read DSP version. Stat: %x\n", stat);
    sceCdAltMV(SystemInformation->mainboard.MECHACONVersion, &stat);
    DEBUG_PRINTF("MECHACON version: %u %u %u %u\n", SystemInformation->mainboard.MECHACONVersion[0], SystemInformation->mainboard.MECHACONVersion[1], SystemInformation->mainboard.MECHACONVersion[2], SystemInformation->mainboard.MECHACONVersion[3]);

    if (sceCdReadConsoleID(SystemInformation->ConsoleID, &result) == 0 || (result & 0x80))
    {
        DEBUG_PRINTF("Failed to read console ID. Stat: %x\n", result);
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_CONSOLEID;
    }
    if (SystemInformation->ConsoleID[0] == 0xFF && SystemInformation->ConsoleID[1] == 0xFF && SystemInformation->ConsoleID[2] == 0xFF &&
        SystemInformation->ConsoleID[3] == 0xFF && SystemInformation->ConsoleID[4] == 0xFF && SystemInformation->ConsoleID[5] == 0xFF &&
        SystemInformation->ConsoleID[6] == 0xFF && SystemInformation->ConsoleID[7] == 0xFF)
    {
        DEBUG_PRINTF("Console ID is all 0xFF.\n");
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_CONSOLEID;
    }
    if (sceCdRI(SystemInformation->iLinkID, &result) == 0 || (result & 0x80))
    {
        DEBUG_PRINTF("Failed to read i.Link ID. Stat: %x\n", result);
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_ILINKID;
    }
    if (SystemInformation->iLinkID[0] == 0xFF && SystemInformation->iLinkID[1] == 0xFF && SystemInformation->iLinkID[2] == 0xFF &&
        SystemInformation->iLinkID[3] == 0xFF && SystemInformation->iLinkID[4] == 0xFF && SystemInformation->iLinkID[5] == 0xFF &&
        SystemInformation->iLinkID[6] == 0xFF && SystemInformation->iLinkID[7] == 0xFF)
    {
        DEBUG_PRINTF("i.Link ID is all 0xFF.\n");
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_ILINKID;
    }
    if (SystemInformation->mainboard.MECHACONVersion[1] >= 5)
    { // v5.x MECHACON (SCPH-50000 and later) supports Mechacon Renewal Date.
        if (sceCdAltReadRenewalDate(SystemInformation->mainboard.MRenewalDate, &result) == 0 || (result & 0x80))
        {
            DEBUG_PRINTF("Failed to read M Renewal Date. Stat: %x\n", result);
            SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_MRENEWDATE;
        }
        /* mechacon 5.8 and 5.9 are the same chip patched with dex flag, so 5.8 and 5.9 -> 5.8 */
        SystemInformation->mainboard.MECHACONVersion[2] = SystemInformation->mainboard.MECHACONVersion[2] & 0xFE;
    }
    SysmanGetMACAddress(SystemInformation->SMAP_MAC_address);

    SystemInformation->mainboard.ADD010 = 0xFFFF;
    if (GetNVMWord(SystemInformation->mainboard.MECHACONVersion[1] >= 5 ? 0x001 : 0x010, &SystemInformation->mainboard.ADD010) != 0)
    {
        DEBUG_PRINTF("Failed to read ADD0x010.\n");
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_ADD010;
    }

    SystemInformation->mainboard.DragonFanConfig = 0xFFFF;
    if (SystemInformation->mainboard.MECHACONVersion[1] >= 5)
    {
        if (GetNVMWord(0x060, &SystemInformation->mainboard.DragonFanConfig) != 0)
        {
            DEBUG_PRINTF("Failed to read DragonFanConfig.\n");
            SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_ADD010;
        }
    }

    // Get the mainboard and chassis names, MODEL ID, console MODEL ID and EMCS ID.
    SystemInformation->mainboard.ModelID[0]    = SystemInformation->iLinkID[1];
    SystemInformation->mainboard.ModelID[1]    = SystemInformation->iLinkID[2];
    SystemInformation->mainboard.ModelID[2]    = SystemInformation->iLinkID[3];
    SystemInformation->mainboard.ConModelID[0] = SystemInformation->ConsoleID[0];
    SystemInformation->mainboard.ConModelID[1] = SystemInformation->ConsoleID[1];
    SystemInformation->mainboard.EMCSID        = SystemInformation->ConsoleID[7];
    DEBUG_PRINTF("GetPeripheralInformation: SystemInformation->mainboard.ModelID, 0x%02X, 0x%02X, 0x%02X\n", SystemInformation->mainboard.ModelID[0], SystemInformation->mainboard.ModelID[1], SystemInformation->mainboard.ModelID[2]);
    strncpy(SystemInformation->mainboard.MainboardName, GetMainboardModelDesc(&SystemInformation->mainboard), sizeof(SystemInformation->mainboard.MainboardName) - 1);
    SystemInformation->mainboard.MainboardName[sizeof(SystemInformation->mainboard.MainboardName) - 1] = '\0'; // Ensure null termination
    strncpy(SystemInformation->chassis, GetChassisDesc(&SystemInformation->mainboard), sizeof(SystemInformation->chassis) - 1);
    SystemInformation->chassis[sizeof(SystemInformation->chassis) - 1] = '\0'; // Ensure null termination
    DEBUG_PRINTF("GetPeripheralInformation: SystemInformation->mainboard.ModelID, 0x%02X, 0x%02X, 0x%02X\n", SystemInformation->mainboard.ModelID[0], SystemInformation->mainboard.ModelID[1], SystemInformation->mainboard.ModelID[2]);

    return 0;
}

int DumpRom(const char *filename, const struct SystemInformation *SystemInformation, struct DumpingStatus *DumpingStatus, unsigned int DumpingRegion)
{
    FILE *file;
    int result = 0;
    unsigned int BytesToRead, BytesRemaining, ROMSize, prevSize;
    const unsigned char *MemDumpStart;
    void *buffer1, *buffer2, *pBuffer;

    switch (DumpingRegion)
    {
        case DUMP_REGION_BOOT_ROM:
            ROMSize      = SystemInformation->mainboard.BOOT_ROM.size;
            MemDumpStart = (const unsigned char *)SystemInformation->mainboard.BOOT_ROM.StartAddress;
            break;
        case DUMP_REGION_DVD_ROM:
            ROMSize      = SystemInformation->mainboard.DVD_ROM.size;
            MemDumpStart = (const unsigned char *)SystemInformation->mainboard.DVD_ROM.StartAddress;
            break;
        default:
            return -EINVAL;
    }

    buffer1        = memalign(64, MEM_IO_BLOCK_SIZE);
    buffer2        = memalign(64, MEM_IO_BLOCK_SIZE);

    BytesRemaining = ROMSize;
    if ((file = fopen(filename, "wb")) != NULL)
    {
        for (pBuffer = buffer1, prevSize = BytesRemaining; BytesRemaining > 0; MemDumpStart += BytesToRead, BytesRemaining -= BytesToRead)
        {
            BytesToRead = BytesRemaining > MEM_IO_BLOCK_SIZE ? MEM_IO_BLOCK_SIZE : BytesRemaining;

            SysmanSync(0);
            while (readDevMemEEIOP(MemDumpStart, pBuffer, BytesToRead, 1) != 0)
                nopdelay();

            RedrawDumpingScreen(SystemInformation, DumpingStatus);
            pBuffer = pBuffer == buffer1 ? buffer2 : buffer1;
            if (BytesRemaining < ROMSize)
            {
                if (fwrite(UNCACHED_SEG(pBuffer), 1, prevSize, file) != prevSize)
                {
                    fclose(file);
                    result = -EIO;
                    break;
                }

                DumpingStatus[DumpingRegion].progress = 1.00f - (float)BytesRemaining / ROMSize;
            }
            prevSize = BytesToRead;
        }

        if (result == 0)
        {
            pBuffer = pBuffer == buffer1 ? buffer2 : buffer1;
            SysmanSync(0);

            if (fwrite(UNCACHED_SEG(pBuffer), 1, prevSize, file) == prevSize)
                DumpingStatus[DumpingRegion].progress = 1.00f - (float)BytesRemaining / ROMSize;
            else
                result = -EIO;
        }

        fclose(file);
    }
    else
        result = -ENOENT;

    DumpingStatus[DumpingRegion].status = (result == 0) ? 1 : result;

    free(buffer1);
    free(buffer2);

    return result;
}

int GetNVMWord(u16 address, u16 *word)
{
    unsigned char stat;

    if (sceCdReadNVM(address, word, &stat) != 1 || stat != 0)
        return -1;

    return 0;
}

int DumpMECHACON_EEPROM(const char *filename)
{
    FILE *file;
    int result;
    unsigned char stat;
    unsigned short int i;
    static unsigned short int IOBuffer[512];

    result = 0;
    if ((file = fopen(filename, "wb")) != NULL)
    {
        for (i = 0; i < 512; i++)
        {
            if (sceCdReadNVM(i, &IOBuffer[i], &stat) != 1 || stat != 0)
            {
                result = -EIO;
                break;
            }
        }

        if (fwrite(IOBuffer, 1, sizeof(IOBuffer), file) != sizeof(IOBuffer))
        {
            result = EIO;
        }
        fclose(file);
    }
    else
        result = -ENOENT;

    return result;
}

int DumpMECHACON_VERSION(const char *filename, const struct SystemInformation *SystemInformation)
{
    FILE *file;
    int result = 0;

    if ((file = fopen(filename, "wb")) != NULL)
    {

        if (fwrite(SystemInformation->mainboard.MECHACONVersion, 1, 4, file) != 4)
            result = EIO;

        fclose(file);
    }
    else
        result = -ENOENT;

    return result;
}

int WriteNewMainboardDBRecord(const char *path, const struct PS2IDBMainboardEntry *SystemInformation)
{
    FILE *file;
    int result;
    struct PS2IDB_NewMainboardEntryHeader header;

    if ((file = fopen(path, "wb")) != NULL)
    {
        header.magic[0] = '2';
        header.magic[1] = 'N';
        header.version  = PS2IDB_NEWENT_FORMAT_VERSION;
        if (fwrite(&header, sizeof(struct PS2IDB_NewMainboardEntryHeader), 1, file) == 1)
        {
            result = fwrite(SystemInformation, sizeof(struct PS2IDBMainboardEntry), 1, file) == 1 ? 0 : EIO;
        }
        else
            result = EIO;

        fclose(file);
    }
    else
        result = EIO;

    return result;
}

const char *GetiLinkSpeedDesc(unsigned char speed)
{
    static const char *speeds[] = {
        "S100",
        "S200",
        "S400",
        "Unknown"};

    if (speed > 3)
        speed = 3;

    return speeds[speed];
}

const char *GetiLinkComplianceLvlDesc(unsigned char level)
{
    static const char *levels[] = {
        "IEEE1394-1995",
        "IEEE1394A-2000",
        "Unknown"};

    if (level > 2)
        level = 2;

    return levels[level];
}

const char *GetiLinkVendorDesc(unsigned int vendor)
{
    const char *description;

    switch (vendor)
    {
        case 0x00A0B8:
            description = "LSI Logic";
            break;
        default:
            description = "Unknown";
            break;
    }

    return description;
}

const char *GetPHYVendDesc(unsigned int oui)
{
    const char *description;

    switch (oui)
    {
        case 0x070100:
            description = "ST Microelectronics";
            break;
        case 0x080017:
            description = "National Semiconductor";
            break;
        case 0x0050ef:
            description = "Broadcom Corporation";
            break;
        case 0x3fffff:
            description = "Gamestar???";
            break;
        default:
            description = "Unknown";
            break;
    }

    return description;
}

const char *GetPHYModelDesc(unsigned int oui, unsigned char model)
{
    const char *description;

    switch (model)
    {
        case 0x01:
            description = "STE100S"; // ST Microelectronics
            break;
        case 0x02:
            description = "DP83846A"; // National Semiconductor
            break;
        case 0x03:
            if (oui == 0x080017)
                description = "DP83847A"; // National Semiconductor
            else if (oui == 0x0050ef)
                description = "BCM5241"; // Broadcom Corporation
            else
                description = "Unknown";
            break;
        case 0x3f:
            description = "Gamestar???";
            break;
        default:
            description = "Unknown";
            break;
    }

    return description;
}

const char *GetSPEEDDesc(unsigned short int revision, const unsigned char MECHA_revision[4])
{
    const char *description;

    switch (revision)
    {
        case 0x0009:
            description = "TS ?? (Missing)";
            break;
        case 0x0010:
            description = "ES1 ?? (Missing)";
            break;
        case 0x0011:
            description = "CXD9624GG";
            break;
        case 0x0012:
            description = "CXD9624AGG";
            break;
        case 0x0013:
            if (MECHA_revision[1] == 0x06)
            {
                if (MECHA_revision[2] < 0x06)
                    description = "CXD9731AGP";
                else
                    description = "combined with IOP"; // CXD9731AGP on 70k, or part of iop on Deckard
            }
            else
                description = "CXD9731GP";
            break;
        case 0x0014:
            description = "CXD9764GP"; // desr unique SPEED chip
            break;
        default:
            description = "Unknown";
            break;
    }

    return description;
}

const char *GetSPEEDCapsDesc(unsigned short int caps)
{
    static char capsbuffer[64];
    unsigned int i;
    unsigned char capability, NumCapabilities;
    static const char *capabilities[] = {
        "SMAP",
        "ATA",
        "Unknown",
        "UART",
        "DVR",
        "Flash",
        "Unknown"};

    if (caps != 0)
    {
        capsbuffer[0] = '\0';
        for (i = 0, NumCapabilities = 0; i < 8; i++)
        {
            if (caps >> i & 1)
            {
                if (NumCapabilities > 0)
                    strcat(capsbuffer, ", ");

                capability = (i < 6) ? i : 6;
                strcat(capsbuffer, capabilities[capability]);
                NumCapabilities++;
            }
        }
    }
    else
        strcpy(capsbuffer, "None");

    return capsbuffer;
}

const char *GetSSBUSIFDesc(unsigned char revision, unsigned char EE_revision)
{
    const char *description;

    switch (revision)
    {
        case 0x12:
            description = "CXD9546R";
            break;
        case 0x20:
            description = "CXD9566R";
            break;
        case 0x30:
            description = "CXD9611R";
            break;
        case 0x31:
            if (EE_revision == 0x43)
                description = "combined with EE";
            else
                description = "CXD9611AR/CXD9611BR/CXD9686AR/CXD9686BR/CXD9686R"; // TODO: try to differ SSBUS 0x31 more
            break;
        case 0x32:
            description = "combined with SPU2";
            break;
        case 0xFF:
            description = "Not detected";
            break;
        default:
            description = "Missing chip";
            break;
    }

    return description;
}

const char *GetSPU2ChipDesc(unsigned short int revision, unsigned char EE_revision)
{
    const char *description;

    DEBUG_PRINTF("SPU2 revision: 0x%02x\n", revision);

    switch (revision)
    {
        case 0x06:
            description = "CXD2942R";
            break;
        case 0x07:
            description = "CXD2942AR/CXD2942BR/CXD2950R"; // TODO: seems CXD2950R exclusive for GH-013, GH-015, GH-016
            break;
        case 0x08:
            if (EE_revision == 0x43)
                description = "combined with EE";
            else
                description = "CXD2947R";
            break;
        case 0x10:
            description = "CXD2955R";
            break;
        default:
            description = "Missing";
            break;
    }

    return description;
}

const char *GetGSChipDesc(unsigned char revision)
{
    const char *description;

    DEBUG_PRINTF("GS revision: 0x%02x\n", revision);

    switch (revision)
    {
        case 0x08:
            description = "CXD2934GB";
            break;
        case 0x15:
            description = "CXD2944GB";
            break;
        case 0x19:
            description = "CXD2949GB/CXD2949BGB";
            break;
        case 0x1b:
            description = "CXD2949CGB/CXD2949DGB";
            break;
        case 0x1c:
        case 0x1d:
            description = "combined with EE";
            break;
        // CXD2972GB - check CECH-C and E
        case 0x1e:
            description = "CXD2980CGB/CXD2980AGB"; // TODO: check A, B, C versions of chip
            break;
        case 0x1f:
            description = "CXD2980GB/CXD2980BGB";
            break;
        default:
            description = "Missing chip";
            break;
    }

    return description;
}

const char *GetEEChipDesc(unsigned char revision, unsigned char GS_revision)
{
    const char *description;

    DEBUG_PRINTF("EE revision: 0x%02x\n", revision);

    // TODO: more researches on EE+GS
    switch (revision)
    {
        case 0x14:
            description = "CXD9542GB";
            break;
        case 0x20:
            description = "CXD9615GB";
            break;
        case 0x30:
        case 0x31:
            description = "CXD9708GB/CXD9832GB";
            break;
        case 0x42:
            if (GS_revision == 0x1c)
                description = "CXD9797GB/CXD9833GB EE+GS";
            else if (GS_revision == 0x1d)
                description = "CXD2953AGB EE+GS";
            else
                description = "Missing chip";
            break;
        case 0x43:
            description = "CXD2976GB"; // EE + IOP + SPU2
            break;
        default:
            description = "Missing chip";
            break;
    }

    return description;
}

const char *GetIOPChipDesc(unsigned char revision, unsigned char EE_revision)
{
    const char *description;

    DEBUG_PRINTF("IOP revision: 0x%02x\n", revision);

    switch (revision)
    {
        case 0x15:
            description = "CXD9553GB";
            break;
        case 0x1f:
            description = "CXD9619GB";
            break;
        case 0x20:
            description = "CXD9660GB";
            break;
        case 0x21:
            description = "CXD9697GP/CXD9732GP";
            break;
        case 0x22:
            description = "CXD9783GP";
            break;
        case 0x24:
            description = "CXD9798GP/CXD9799GP/CXD9799AGP";
            break;
        case 0x30: // Deckard PowerPC
            if (EE_revision == 0x43)
                description = "combined with EE";
            else
                description = "CXD9796GP/CXD9209GP"; // TODO: CXD9796GP - 75k only, check if it is possible to check Deckard version
            break;
        default:
            description = "Missing chip";
            break;
    }

    return description;
}

const char *GetBOOTROMDesc(const char *extinfo, const char *romver, const char *dvdplVer)
{
    const char *description;

    if (romver[4] == 'T' && romver[5] == 'D')
    {
        description = "TOOL flashrom no label";
        return description;
    }
    char combined[20];
    snprintf(combined, sizeof(combined), "%s%c", extinfo, romver[5]);

    // clang-format off
         if (strcmp(combined, "20000117-050310C") == 0) description = "00-100";
    else if (strcmp(combined, "20000117-050652D") == 0) description = "01-100";
    else if (strcmp(combined, "20000217-181313C") == 0) description = "00-101";
    else if (strcmp(combined, "20000217-181557D") == 0) description = "01-101 (Not confirmed)";
    else if (strcmp(combined, "20000224-172900D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20000727-013725C") == 0) description = "B10010";
    else if (strcmp(combined, "20000727-013728D") == 0) description = "B11010 (Not confirmed)";
    else if (strcmp(combined, "20000901-114731Z") == 0) description = "A-000-010";
    else if (strcmp(combined, "20000902-234318C") == 0) description = "B10020)";
    else if (strcmp(combined, "20000902-234321C") == 0) description = "B20020";
    else if (strcmp(combined, "20000902-234323D") == 0) description = "B21020 (Not confirmed)";
    else if (strcmp(combined, "20001027-185015C") == 0) description = "B00012 (split chip)";
    else if (strcmp(combined, "20001027-191435C") == 0) description = "B00012 (combined chip)";
    else if (strcmp(combined, "20001228-190952C") == 0) description = "B10030 (Not confirmed)";
    else if (strcmp(combined, "20001228-234538C") == 0) description = "B20030";
    else if (strcmp(combined, "20010118-210306C") == 0) description = "B00030";
    else if (strcmp(combined, "20010118-210307D") == 0) description = "B01030";
    else if (strcmp(combined, "20010427-140035C") == 0) description = "B00040";
    else if (strcmp(combined, "20010427-140043C") == 0) description = "B10040";
    else if (strcmp(combined, "20010704-160658C") == 0) description = "B10050";
    else if (strcmp(combined, "20010704-160707C") == 0) description = "B20050";
    else if (strcmp(combined, "20010730-223219C") == 0) description = "B40050";
    else if (strcmp(combined, "20011004-175812C") == 0) description = "B00060";
    else if (strcmp(combined, "20011004-175827C") == 0) description = "B10060";
    else if (strcmp(combined, "20011004-175839C") == 0) description = "B20060";
    else if (strcmp(combined, "20020207-164243C") == 0) description = "B10070";
    else if (strcmp(combined, "20020319-181154C") == 0) description = "B20080";
    else if (strcmp(combined, "20020426-130151C") == 0) description = "B00090";
    else if (strcmp(combined, "20020426-130201C") == 0) description = "B20090";
    else if (strcmp(combined, "20020426-130207C") == 0) description = "B40090";
    else if (strcmp(combined, "20021119-163841Z") == 0) description = "namco 2 unknwon chip";
    else if (strcmp(combined, "20030110-133906D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030110-134044C") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030206-083918C") == 0) description = "B00102 (Not confirmed)";
    else if (strcmp(combined, "20030206-083919D") == 0) description = "B01102 (Not confirmed)";
    else if (strcmp(combined, "20030224-185856D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030227-193050C") == 0)
    {
        if (dvdplVer[4] == 'A')
            description = "B30103"; // AU
        else
            description = "B20101"; // EU
    }
    else if (strcmp(combined, "20030227-193050D") == 0) description = "B21101 (Not confirmed)";
    else if (strcmp(combined, "20030325-181554C") == 0) description = "B10102";
    else if (strcmp(combined, "20030325-181555D") == 0) description = "B11102 (Not confirmed)";
    else if (strcmp(combined, "20030520-144137D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030520-144207D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030623-142351C") == 0) description = "B00103 (Not confirmed)";
    else if (strcmp(combined, "20030623-142356C") == 0) description = "B10103 (Not confirmed)";
    else if (strcmp(combined, "20030623-142357D") == 0) description = "B11103 (Not confirmed)";
    else if (strcmp(combined, "20030623-142401C") == 0) description = "B20103";
    else if (strcmp(combined, "20030623-142406C") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030623-142411C") == 0) description = "B60103";
    else if (strcmp(combined, "20030623-142419C") == 0) description = "B70103";
    else if (strcmp(combined, "20030623-142420C") == 0) description = "B71103 (Not confirmed)";
    else if (strcmp(combined, "20030623-142424C") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030623-142429C") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20030822-152237C") == 0) description = "B00120";
    else if (strcmp(combined, "20030822-152247C") == 0) description = "B20120 (Not confirmed)";
    else if (strcmp(combined, "20031028-053521C") == 0) description = "XB00010";
    else if (strcmp(combined, "20040329-172942C") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20040519-145634Z") == 0) description = "namco s256 no label";
    else if (strcmp(combined, "20040614-100905C") == 0) description = "B3110A";
    else if (strcmp(combined, "20040614-100909C") == 0) description = "B1110A";
    else if (strcmp(combined, "20040614-100914C") == 0) description = "B2110A";
    else if (strcmp(combined, "20040614-100915D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20040614-100920C") == 0) description = "B4110A";
    else if (strcmp(combined, "20040917-150737C") == 0) description = "xpd-005 no label";
    else if (strcmp(combined, "20050620-175641C") == 0) description = "B6120B";
    else if (strcmp(combined, "20050620-175642D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20060210-142424C") == 0) description = "B6130B";
    else if (strcmp(combined, "20060905-125923C") == 0) description = "B6140B";
    else if (strcmp(combined, "20060905-125924D") == 0) description = "Unknown chip";
    else if (strcmp(combined, "20080220-175343C") == 0) description = "B6150B";
    else if (strcmp(combined, "20100415-124238C") == 0) description = "B6160B";
    // clang-format on
    else
        description = "Unknown (provide bios dump)";

    return description;
}

const char *GetDVDROMDesc(const char *dvdplVer)
{
    const char *description;

    // clang-format off
         if (strncmp(dvdplVer, "1.10U", 5) == 0) description = "D110010";
    else if (strncmp(dvdplVer, "1.20U", 5) == 0) description = "D110020 (Not confirmed)";
    else if (strncmp(dvdplVer, "1.20E", 5) == 0) description = "D221020";
    else if (strncmp(dvdplVer, "1.20A", 5) == 0) description = "D341020 (Not confirmed)";
    else if (strncmp(dvdplVer, "2.00J", 5) == 0) description = "D020020";
    else if (strncmp(dvdplVer, "1.30U", 5) == 0) description = "D110030 (Not confirmed)";
    else if (strncmp(dvdplVer, "1.30E", 5) == 0) description = "D221030";
    else if (strncmp(dvdplVer, "1.30A", 5) == 0) description = "D341030 (Not confirmed)";
    else if (strncmp(dvdplVer, "2.02J", 5) == 0) description = "D020030";
    else if (strncmp(dvdplVer, "2.10J", 5) == 0) description = "D020040";
    else if (strncmp(dvdplVer, "2.10U", 5) == 0) description = "D110040";
    else if (strncmp(dvdplVer, "2.10E", 5) == 0) description = "D221040";
    else if (strncmp(dvdplVer, "2.10A", 5) == 0) description = "D341040";
    else if (strncmp(dvdplVer, "2.12U", 5) == 0) description = "D110050";
    else if (strncmp(dvdplVer, "2.12G", 5) == 0) description = "D630050";
    else if (strncmp(dvdplVer, "2.12K", 5) == 0) description = "D430050";
    else if (strncmp(dvdplVer, "2.13E", 5) == 0) description = "D221060";
    else if (strncmp(dvdplVer, "2.13A", 5) == 0) description = "D341060 (Not confirmed)";
    else if (strncmp(dvdplVer, "2.14J", 5) == 0) description = "Unknown chip";
    else if (strncmp(dvdplVer, "2.15G", 5) == 0) description = "D630080";
    else if (strncmp(dvdplVer, "2.16J", 5) == 0) description = "D020090";
    else if (strncmp(dvdplVer, "2.16D", 5) == 0) description = "D552090";
    else if (strncmp(dvdplVer, "3.00J", 5) == 0) description = "D020110 (Not confirmed)";
    else if (strncmp(dvdplVer, "3.00U", 5) == 0) description = "D110110";
    else if (strncmp(dvdplVer, "3.00E", 5) == 0) description = "D221110";
    else if (strncmp(dvdplVer, "3.00A", 5) == 0) description = "D341110";
    else if (strncmp(dvdplVer, "3.02J", 5) == 0) description = "D020111";
    else if (strncmp(dvdplVer, "3.02U", 5) == 0) description = "Unknown chip";
    else if (strncmp(dvdplVer, "3.02E", 5) == 0) description = "D221111";
    else if (strncmp(dvdplVer, "3.02K", 5) == 0) description = "Unknown chip";
    else if (strncmp(dvdplVer, "3.02G", 5) == 0) description = "D630111";
    else if (strncmp(dvdplVer, "3.02D", 5) == 0) description = "D552111";
    else if (strncmp(dvdplVer, "3.02C", 5) == 0) description = "D762110";
    else if (strncmp(dvdplVer, "3.03J", 5) == 0) description = "D020111";
    else if (strncmp(dvdplVer, "3.03E", 5) == 0) description = "D221111 (Not confirmed)";
    else if (strncmp(dvdplVer, "3.04M", 5) == 0) description = "Unknown chip";
    else if (strncmp(dvdplVer, "3.10", 4) == 0) description = "D1010U";
    else if (strncmp(dvdplVer, "3.11", 4) == 0) description = "D0020U";
    // clang-format on
    else
        description = "Unknown (provide dvdrom dump)";

    return description;
}

const char *GetMECHACONChipDesc(unsigned int revision)
{
    const char *description;

    if (revision >= 0x050000)
    {
        revision = revision & 0xfffeff; // Retail and debug chips are identical
        if (revision != 0x050607)
            revision = revision & 0xffff00; // Mexico unit is unique
    }
    DEBUG_PRINTF("MECHACON revision: 0x%08x\n", revision);

    switch (revision)
    {
        case 0x010200:
            description = "CXP101064-605R";
            break;
        case 0x010300:
            description = "CXP101064-602R"; // DTL-T10000 and retail models, Japan region locked
            break;
        case 0x010900:
            description = "CXP102064-751R"; // only DTL-T10000
            break;
        case 0x02040A:
            description = "CXP102064-651R"; // only Arcade machines
            break;
        case 0x020501:
        case 0x020502:
        case 0x020503:
            description = "CXP102064-702R"; // DTL-H3000x
            break;
        case 0x020701:
        case 0x020702:
        case 0x020703:
            description = "CXP102064-703R"; // DTL-H3000x, DTL-H3010x
            break;
        case 0x020900:
        case 0x020901:
        case 0x020902:
        case 0x020903:
        case 0x020904:
            description = "CXP102064-704R"; // DTL-H3000x, DTL-H3010x
            break;
        case 0x020D00:
        case 0x020D01:
        case 0x020D02:
        case 0x020D04:
        case 0x020D05:
            description = "CXP102064-705R/-752R"; // DTL-H3000x, DTL-H3010x, DTL-T10000
            break;
        // Japanese region only v1-v2
        case 0x010600:
            description = "CXP102064-001R (Not confirmed)";
            break;
        case 0x010700:
            description = "CXP102064-003R";
            break;
        case 0x010800:
            description = "CXP102064-002R";
            break;
        case 0x020000:
            description = "CXP102064-004R (Not confirmed)";
            break;
        case 0x020200:
            description = "CXP102064-005R";
            break;
        case 0x020800:
            description = "CXP102064-006R";
            break;
        case 0x020C00:
            description = "CXP102064-007R";
            break;
        case 0x020E00:
            description = "CXP102064-008R";
            break;
        // US region only
        case 0x020401:
            description = "CXP102064-101R";
            break;
        case 0x020601:
            description = "CXP102064-102R";
            break;
        case 0x020C01:
            description = "CXP102064-103R";
            break;
        case 0x020E01:
            description = "CXP102064-104R";
            break;
        // EU region only
        case 0x020402:
            description = "CXP102064-201R";
            break;
        case 0x020602:
            description = "CXP102064-202R";
            break;
        case 0x020C02:
            description = "CXP102064-203R";
            break;
        case 0x020E02:
            description = "CXP102064-204R";
            break;
        // Australia region only
        case 0x020403:
            description = "CXP102064-301R";
            break;
        case 0x020603:
            description = "CXP102064-302R";
            break;
        case 0x020C03:
            description = "CXP102064-303R";
            break;
        case 0x020E03:
            description = "CXP102064-304R";
            break;
        // Japan region only
        case 0x030200:
            description = "CXP103049-001GG";
            break;
        case 0x030600:
            description = "CXP103049-002GG";
            break;
        case 0x030800:
            description = "CXP103049-003GG";
            break;
        // US region only
        case 0x030001:
            description = "CXP103049-101GG";
            break;
        case 0x030201:
            description = "CXP103049-102GG";
            break;
        case 0x030601:
            description = "CXP103049-103GG";
            break;
        // EU region only
        case 0x030002:
            description = "CXP103049-201GG";
            break;
        case 0x030202:
            description = "CXP103049-202GG";
            break;
        case 0x030602:
            description = "CXP103049-203GG";
            break;
        // Australia region only
        case 0x030003:
            description = "CXP103049-301GG";
            break;
        case 0x030203:
            description = "CXP103049-302GG";
            break;
        case 0x030603:
            description = "CXP103049-303GG";
            break;
        // Asia region only
        case 0x030404:
            description = "CXP103049-401GG";
            break;
        case 0x030604:
            description = "CXP103049-402GG";
            break;
        case 0x030804:
            description = "CXP103049-403GG";
            break;
        // Russia region only
        case 0x030605:
            description = "CXP103049-501GG";
            break;
        // Dragon
        case 0x050000:
            description = "CXR706080-101GG";
            break;
        case 0x050200:
            description = "CXR706080-102GG";
            break;
        case 0x050400:
            description = "CXR706080-103GG";
            break;
        case 0x050600:
            description = "CXR706080-104GG";
            break;
        case 0x050C00:
            description = "CXR706080-105GG/CXR706F080-1GG";
            break;
        case 0x050607:
            description = "CXR706080-106GG";
            break;
        /* case 0x050800:
            description = "CXR706080-701GG (Not confirmed)";
            break; */
        case 0x050A00:
            description = "CXR706080-702GG";
            break;
        case 0x050E00:
            description = "CXR706080-703GG";
            break;
        case 0x060000:
            description = "CXR716080-101GG";
            break;
        case 0x060200:
            description = "CXR716080-102GG";
            break;
        case 0x060400:
            description = "CXR716080-103GG";
            break;
        case 0x060600:
            description = "CXR716080-104GG";
            break;
        /* case 0x060800:
            description = "CXR716080-105GG (Not confirmed)";
            break; */
        case 0x060A00:
            description = "CXR716080-106GG";
            break;
        case 0x060C00:
            description = "CXR726080-301GB";
            break;
        default:
            description = "Unknown";
    }

    return description;
}

const char *GetSystemTypeDesc(unsigned char type)
{
    const char *description;

    if (type == 0)
        description = "PlayStation 2";
    else if (type == 1)
        description = "PSX/Arcade";
    else
        description = "Unknown";

    return description;
}

const char *GetRegionDesc(unsigned char region)
{
    const char *description;

    switch (region)
    {
        case 0:
            description = "Japan";
            break;
        case 1:
            description = "USA";
            break;
        case 2:
            description = "Europe";
            break;
        case 3:
            description = "Oceania";
            break;
        case 4:
            description = "Asia";
            break;
        case 5:
            description = "Russia";
            break;
        case 6:
            description = "China";
            break;
        case 7:
            description = "Mexico";
            break;
        case 0xA:
            description = "Arcade";
            break;
        default:
            description = "Unknown";
            break;
    }
    return description;
}

const char *GetMRPDesc(unsigned short int id)
{
    const char *description;

    switch (id)
    {
        case 0x00:
        case 0xFF:
            description = "Not detected";
            break;
        case 0x10:
            description = "Gmain-1.0";
            break;
        case 0x20:
            description = "Gmain-2.0";
            break;
        case 0x30:
            description = "Gmain-3.0";
            break;
        case 0x40:
            description = "B3system-1.0";
            break;
        case 0x50:
            description = "Gmain-4.0";
            break;
        case 0x58:
            description = "Gmain-5.0";
            break;
        case 0x60:
            description = "MPU-4.0";
            break;
        case 0x61:
            description = "MPU-4.0";
            break;
        case 0x70:
            description = "Gmain-10.0/11.0";
            break;
        case 0x78:
            description = "Gmain-12.0";
            break;
        case 0x7C:
            description = "Gmain-13.0";
            break;
        default:
            description = "Unknown board";
            break;
    }

    return description;
}

const char *GetModelIDDesc(unsigned short int ModelId)
{
    const char *description;

    // clang-format off
         if (ModelId == 0xd200) description = "DTL-H10000";
    else if (ModelId == 0xd201) description = "SCPH-10000";
    else if (ModelId == 0xd202) description = "SCPH-15000/18000";
    else if (ModelId == 0xd203) description = "SCPH-30001";
    else if (ModelId == 0xd204) description = "SCPH-30002/R";
    else if (ModelId == 0xd205) description = "SCPH-30003/R";
    else if (ModelId == 0xd206) description = "SCPH-30004/R";
    else if (ModelId == 0xd207) description = "DTL-H30001";
    else if (ModelId == 0xd208) description = "DTL-H30002";
    else if (ModelId == 0xd209) description = "COH-H30000";
    else if (ModelId == 0xd20a) description = "SCPH-18000";
    else if (ModelId == 0xd20b) description = "COH-H31000";
    else if (ModelId == 0xd20c) description = "SCPH-30000";
    else if (ModelId == 0xd20d) description = "DTL-H30000";
    else if (ModelId == 0xd20e) description = "COH-H31100";
    else if (ModelId == 0xd20f) description = "SCPH-35001 GT";
    else if (ModelId == 0xd210) description = "SCPH-35002 GT";
    else if (ModelId == 0xd211) description = "SCPH-35003 GT";
    else if (ModelId == 0xd212) description = "SCPH-35004 GT";
    else if (ModelId == 0xd213) description = "SCPH-35000 GT";
    else if (ModelId == 0xd214) description = "SCPH-30001/R";
    else if (ModelId == 0xd215) description = "SCPH-30005 R";
    else if (ModelId == 0xd216) description = "SCPH-30006 R";
    else if (ModelId == 0xd217) description = "SCPH-39000";
    else if (ModelId == 0xd218) description = "SCPH-39001";
    else if (ModelId == 0xd219) description = "SCPH-39002";
    else if (ModelId == 0xd21a) description = "SCPH-39003";
    else if (ModelId == 0xd21b) description = "SCPH-39004";
 /* else if (ModelId == 0xd21c) description = "SCPH-30007 R"; */
    else if (ModelId == 0xd21d) description = "SCPH-37000 L";
    else if (ModelId == 0xd21e) description = "SCPH-37000 B";
    else if (ModelId == 0xd21f) description = "SCPH-39008";
    else if (ModelId == 0xd220) description = "SCPH-39000 TB";
    else if (ModelId == 0xd221) description = "SCPH-39000 RC";
    else if (ModelId == 0xd222) description = "SCPH-39006";
 /* else if (ModelId == 0xd223) description = "SCPH-39005";
    else if (ModelId == 0xd224) description = "SCPH-39007"; */
    else if (ModelId == 0xd225) description = "DTL-H10100";
    else if (ModelId == 0xd226) description = "DTL-H30100";
    else if (ModelId == 0xd227) description = "DTL-H30101";
    else if (ModelId == 0xd228) description = "DTL-H30102";
    else if (ModelId == 0xd229) description = "DTL-H30105";
    else if (ModelId == 0xd22a) description = "SCPH-39000 S";
    else if (ModelId == 0xd22b) description = "SCPH-39000 AQ";
    else if (ModelId == 0xd22c) description = "SCPH-39000 SA";
    else if (ModelId == 0xd22d) description = "SCPH-39010/N";
    // Deckard
    else if (ModelId == 0xd301) description = "DTL-H50000";
    else if (ModelId == 0xd302) description = "DTL-H50001";
    else if (ModelId == 0xd303) description = "DTL-H50002";
    else if (ModelId == 0xd304) description = "DTL-H50009";
    // d305 - d31d ??
    else if (ModelId == 0xd31e) description = "DTL-H70002";
 /* else if (ModelId == 0xd31f) description = "DTL-H70011S ???";
    else if (ModelId == 0xd320) description = "???";
    else if (ModelId == 0xd321) description = "???"; */
    else if (ModelId == 0xd322) description = "DTL-H75000";
 /* else if (ModelId == 0xd323) description = "DTL-H77000 ???";
    else if (ModelId == 0xd324) description = "DTL-H79000 ???";
    else if (ModelId == 0xd325) description = "DTL-H90000 R-chassis ???"; */
    else if (ModelId == 0xd326) description = "DTL-H90000"; // P-chassis
    // d327-d37f
    // X-chassis
    else if (ModelId == 0xd380) description = "DESR-7000";
    else if (ModelId == 0xd381) description = "DESR-5000";
    else if (ModelId == 0xd382) description = "DESR-7100";
    else if (ModelId == 0xd383) description = "DESR-5100";
    else if (ModelId == 0xd384) description = "DESR-5100/S";
    else if (ModelId == 0xd385) description = "DESR-7500";
    else if (ModelId == 0xd386) description = "DESR-5500";
    else if (ModelId == 0xd387) description = "DESR-7700";
    else if (ModelId == 0xd388) description = "DESR-5700";
    // d389 - d400 ??
    // H, I, J -chassis
    else if (ModelId == 0xd401) description = "SCPH-50001/N";
    else if (ModelId == 0xd402) description = "SCPH-50010/N";
    else if (ModelId == 0xd403) description = "SCPH-50000";
    else if (ModelId == 0xd404) description = "SCPH-50000 MB/NH";
    else if (ModelId == 0xd405) description = "SCPH-50002";
    else if (ModelId == 0xd406) description = "SCPH-50003";
    else if (ModelId == 0xd407) description = "SCPH-50004";
    else if (ModelId == 0xd408) description = "SCPH-50002 SS";
    else if (ModelId == 0xd409) description = "SCPH-50003 SS";
    else if (ModelId == 0xd40a) description = "SCPH-50004 SS";
    else if (ModelId == 0xd40b) description = "SCPH-50001";
    else if (ModelId == 0xd40c) description = "SCPH-50005/N";
    else if (ModelId == 0xd40d) description = "SCPH-50006";
    else if (ModelId == 0xd40e) description = "SCPH-50007";
    else if (ModelId == 0xd40f) description = "SCPH-50008";
 /* else if (ModelId == 0xd410) description = "???"; */
    else if (ModelId == 0xd411) description = "SCPH-50000 NB";
    else if (ModelId == 0xd412) description = "SCPH-50000 TSS";
    else if (ModelId == 0xd413) description = "SCPH-55000 GU";
    else if (ModelId == 0xd414) description = "SCPH-55000 GT";
    else if (ModelId == 0xd415) description = "SCPH-50009 SS";
    else if (ModelId == 0xd416) description = "SCPH-50003 AQ";
    else if (ModelId == 0xd417) description = "SCPH-55005 GT/N ???";
    else if (ModelId == 0xd418) description = "SCPH-55006 GT";
 /* else if (ModelId == 0xd419) description = "SCPH-55007 GT ???"; */
    else if (ModelId == 0xd41a) description = "SCPH-50008 SS";
    else if (ModelId == 0xd41b) description = "SCPH-50004 AQ";
    else if (ModelId == 0xd41c) description = "SCPH-50005 SS/N";
    else if (ModelId == 0xd41d) description = "SCPH-50005 AQ/N";
    else if (ModelId == 0xd41e) description = "SCPH-50000 CW";
    else if (ModelId == 0xd41f) description = "SCPH-50000 SA";
    else if (ModelId == 0xd420) description = "SCPH-50004 SS";
 /* else if (ModelId == 0xd421) description = "???"; */
    else if (ModelId == 0xd422) description = "SCPH-50002 SS";
    else if (ModelId == 0xd423) description = "SCPH-50003 SS";
    else if (ModelId == 0xd424) description = "SCPH-50000 PW";
    else if (ModelId == 0xd425) description = "SCPH-50011";
    // K-chassis
    else if (ModelId == 0xd426) description = "SCPH-70004";
    else if (ModelId == 0xd427) description = "SCPH-70003";
    else if (ModelId == 0xd428) description = "SCPH-70002";
    else if (ModelId == 0xd429) description = "SCPH-70011";
    else if (ModelId == 0xd42a) description = "SCPH-70012";
    else if (ModelId == 0xd42b) description = "SCPH-70000";
    else if (ModelId == 0xd42c) description = "SCPH-70005";
    else if (ModelId == 0xd42d) description = "SCPH-70006";
    else if (ModelId == 0xd42e) description = "SCPH-70007";
    else if (ModelId == 0xd42f) description = "SCPH-70000 GT";
    else if (ModelId == 0xd430) description = "SCPH-70008";
    else if (ModelId == 0xd431) description = "SCPH-70002 SS";
    else if (ModelId == 0xd432) description = "SCPH-70003 SS";
    else if (ModelId == 0xd433) description = "SCPH-70004 SS";
    else if (ModelId == 0xd434) description = "SCPH-70008 SS";
    else if (ModelId == 0xd435) description = "SCPH-70001";
    else if (ModelId == 0xd436) description = "SCPH-70010";
    else if (ModelId == 0xd437) description = "SCPH-70000 CW";
    else if (ModelId == 0xd438) description = "SCPH-70003 SS";
 /* else if (ModelId == 0xd439) description = "SCPH-70000 SS ???"; */
    else if (ModelId == 0xd43a) description = "SCPH-70008 SS";
    // L-chassis
    else if (ModelId == 0xd43b) description = "SCPH-75001";
    else if (ModelId == 0xd43c) description = "SCPH-75002";
    else if (ModelId == 0xd43d) description = "SCPH-75003";
    else if (ModelId == 0xd43e) description = "SCPH-75004";
    else if (ModelId == 0xd43f) description = "SCPH-75000 SSS";
    else if (ModelId == 0xd440) description = "SCPH-75002 SS";
    else if (ModelId == 0xd441) description = "SCPH-75003 SS";
    else if (ModelId == 0xd442) description = "SCPH-75004 SS";
    else if (ModelId == 0xd443) description = "SCPH-75000";
    else if (ModelId == 0xd444) description = "SCPH-75000 CW";
    else if (ModelId == 0xd445) description = "SCPH-75006";
    else if (ModelId == 0xd446) description = "SCPH-75007";
    else if (ModelId == 0xd447) description = "SCPH-75005";
    else if (ModelId == 0xd448) description = "SCPH-75010";
    else if (ModelId == 0xd449) description = "SCPH-75000 FF";
 /* else if (ModelId == 0xd44a) description = "???";
    else if (ModelId == 0xd44b) description = "???"; */
    else if (ModelId == 0xd44c) description = "SCPH-75008";
    else if (ModelId == 0xd44d) description = "SCPH-75008 SS";
    // M-chassis
    else if (ModelId == 0xd44e) description = "SCPH-77001";
    else if (ModelId == 0xd44f) description = "SCPH-77002";
    else if (ModelId == 0xd450) description = "SCPH-77003";
    else if (ModelId == 0xd451) description = "SCPH-77004";
    else if (ModelId == 0xd452) description = "SCPH-77002 SS";
    else if (ModelId == 0xd453) description = "SCPH-77003 SS";
    else if (ModelId == 0xd454) description = "SCPH-77004 SS";
    else if (ModelId == 0xd455) description = "SCPH-77000";
    else if (ModelId == 0xd456) description = "SCPH-77000 CW";
    else if (ModelId == 0xd457) description = "SCPH-77005";
    else if (ModelId == 0xd458) description = "SCPH-77006";
    else if (ModelId == 0xd459) description = "SCPH-77007";
    else if (ModelId == 0xd45a) description = "SCPH-77008";
 /* else if (ModelId == 0xd45b) description = "SCPH-77010 ???";
    else if (ModelId == 0xd45c) description = "SCPH-77008 SS ???"; */
    else if (ModelId == 0xd45d) description = "SCPH-77001 SS";
    else if (ModelId == 0xd45e) description = "SCPH-77003 PK";
    else if (ModelId == 0xd45f) description = "SCPH-77004 PK";
 /* else if (ModelId == 0xd460) description = "SCPH-77008 PK ???"; */
    else if (ModelId == 0xd461) description = "SCPH-77000 SS";
    else if (ModelId == 0xd462) description = "SCPH-77000 PK";
 /* else if (ModelId == 0xd463) description = "???"; */
    else if (ModelId == 0xd464) description = "SCPH-77002 PK";
    // N-chassis
    else if (ModelId == 0xd465) description = "SCPH-79001";
 /* else if (ModelId == 0xd466) description = "SCPH-79000 ???"; */
    else if (ModelId == 0xd467) description = "SCPH-79000 CW";
    else if (ModelId == 0xd468) description = "SCPH-79002";
    else if (ModelId == 0xd469) description = "SCPH-79001 SS";
 /* else if (ModelId == 0xd46a) description = "SCPH-79005 ???"; */
    else if (ModelId == 0xd46b) description = "SCPH-79006";
 /* else if (ModelId == 0xd46c) description = "SCPH-79007 ???"; */
    else if (ModelId == 0xd46d) description = "SCPH-79000 SS";
    else if (ModelId == 0xd46e) description = "SCPH-79003";
    else if (ModelId == 0xd46f) description = "SCPH-79004";
    else if (ModelId == 0xd470) description = "SCPH-79010";
    else if (ModelId == 0xd471) description = "SCPH-79003 SS";
 /* else if (ModelId == 0xd472) description = "SCPH-79004 SS ???"; */
    else if (ModelId == 0xd473) description = "SCPH-79008";
    else if (ModelId == 0xd474) description = "SCPH-79001 CW";
    // P/R-chassis
    else if (ModelId == 0xd475) description = "SCPH-90000";
    else if (ModelId == 0xd476) description = "SCPH-90000 CW";
    else if (ModelId == 0xd477) description = "SCPH-90000 SS";
    else if (ModelId == 0xd478) description = "SCPH-90006";
    else if (ModelId == 0xd479) description = "SCPH-90006 CW";
    else if (ModelId == 0xd47a) description = "SCPH-90006 SS"; // Needs confirmation
    else if (ModelId == 0xd47b) description = "SCPH-90005";
 /* else if (ModelId == 0xd47c) description = "SCPH-90005 CW ???";
    else if (ModelId == 0xd47d) description = "SCPH-90005 SS ???"; */
    else if (ModelId == 0xd47e) description = "SCPH-90007";
    else if (ModelId == 0xd47f) description = "SCPH-90007 CW";
 /* else if (ModelId == 0xd480) description = "SCPH-90007 SS ???"; */
    else if (ModelId == 0xd481) description = "SCPH-90001";
    else if (ModelId == 0xd482) description = "SCPH-90001 SS";
    else if (ModelId == 0xd483) description = "SCPH-90004";
    else if (ModelId == 0xd484) description = "SCPH-90004 SS";
    else if (ModelId == 0xd485) description = "SCPH-90002";
    else if (ModelId == 0xd486) description = "SCPH-90003";
 /* else if (ModelId == 0xd487) description = "SCPH-90005 CR ???"; */
    else if (ModelId == 0xd488) description = "SCPH-90006 CR";
    else if (ModelId == 0xd489) description = "SCPH-90007 CR";
    else if (ModelId == 0xd48a) description = "SCPH-90010";
    else if (ModelId == 0xd48b) description = "SCPH-90000 CR";
    else if (ModelId == 0xd48c) description = "SCPH-90008";
    else if (ModelId == 0xd48d) description = "SCPH-90008 SS";
 /* else if (ModelId == 0xd48e) description = "SCPH-90008 CR ???"; */
    else if (ModelId == 0xd48f) description = "PX300-1";
    else if (ModelId == 0xd490) description = "PX300-2";
    else if (ModelId == 0xd491) description = "SCPH-90010 CR";
    // clang-format on
    else
        description = "Missing sticker and nvram";

    return description;
}

const char *GetEMCSIDDesc(unsigned char id)
{
    const char *description;
    switch (id)
    {
        case 0x00:
            description = "Japan";
            break;
        case 0x01:
        case 0x20:
        case 0x21: // TODO: why they changed FOXC id in the slims middle age?
            description = "FOXC";
            break;
        case 0x02:
        case 0x30:
            description = "SZMT";
            break;
        case 0x03:
            description = "SKZ";
            break;
        case 0x10:
            description = "S EMCS";
            break;
        case 0x11:
            description = "SKD";
            break;
        case 0x18:
            description = "S EMCS (PSX)";
            break;
        case 0x40:
            description = "S WUXI";
            break;
        default:
            description = "Sticker missing";
            break;
    }
    return description;
}

const char *GetADD010Desc(unsigned short int id)
{
    const char *description;

    switch (id)
    {
        case 0x0000:
            description = "DEX A";
            break;
        case 0x0001:
            description = "A";
            break;
        case 0x0801:
            description = "AB";
            break;
        case 0x0809:
            description = "C (Arcade)";
            break;
        case 0x080C:
            description = "DEX B (Old)";
            break;
        case 0x090C:
            description = "B/C (Old)";
            break;
        case 0x8808:
            description = "DEX D";
            break;
        case 0x8809:
            description = "C/D";
            break;
        case 0x8C08:
            description = "DEX B";
            break;
        case 0x8C09:
            description = "B";
            break;
        case 0xA809:
            description = "F SONY";
            break;
        case 0xA829:
            description = "F SANYO";
            break;
        case 0xB009:
            description = "G SONY";
            break;
        case 0xB029:
            description = "G SANYO";
            break;
        case 0xB41B:
            description = "H/I/J/X SONY";
            break;
        case 0xB43B:
            description = "H/I/J/X SANYO";
            break;
        case 0xBC2B:
            description = "Slim";
            break;
        case 0xFFFF:
            description = "Not Present";
            break;
        default:
            description = "Sticker missing";
            break;
    }

    return description;
}

const char *GetDSPDesc(unsigned char revision)
{
    static const char *revisions[] = {
        "CXD1869Q",
        "CXD1869AQ",
        "CXD1869BQ/CXD1886Q-1/CXD1886",
        "CXD3098Q/CXD1886Q-1/CXD3098AQ",
        "Missing"};

    if (revision > 4)
        revision = 4;

    return revisions[revision];
}

const char *GetChassisDesc(const struct PS2IDBMainboardEntry *mainboard)
{
    const char *description;

    if (mainboard->MECHACONVersion[3] == 0x01)
        description = "X-chassis";
    else if (mainboard->MECHACONVersion[1] == 0x06)
    {
        if (mainboard->MECHACONVersion[2] < 0x06)
            description = "K-chassis"; // all of them has bios and mechacon different from Fats, other chips can be the same
        else if ((mainboard->MECHACONVersion[2] & 0xFE) == 0x06)
            description = "L-chassis"; // all of them has bios and mechacon different from M chassis, other chips can be the same
        else if ((mainboard->MECHACONVersion[2] & 0xFE) == 0x0a)
            description = "M-chassis"; // all of them has bios and mechacon different from L and P chassis, other chips can be the same
        else if (!strncmp(mainboard->romver, "025", 3))
            description = "P-chassis"; // Bravia differs only in bootrom from early 90k
        else if (!strncmp(mainboard->romver, "023", 3))
            description = "R-chassis"; // late 90k differs only in bootrom from early 90k
        else if (!strncmp(mainboard->romver + 6, "20080220", 8))
            description = "N,P-chassis (work in progress)"; // TODO: only way to determine 79k from 90k is EEPROM, all 90k in fanconfig area has checksum 0x0c, all 79k differs
        else
            description = "Unknown";
    }
    else if (mainboard->MECHACONVersion[1] == 0x05)
    {
        if (mainboard->spu2.revision == 0x10)
        {
            if ((mainboard->MECHACONVersion[2] & 0xFE) == 0x0c)
                description = "J-chassis"; // seems J chassis has mecha 5.12
            else
                description = "I-chassis";
        }
        else
            description = "H-Chassis"; // old SPU2 and SSBUSC
    }
    else if (mainboard->MECHACONVersion[1] == 0x03)
    {
        if (mainboard->MECHACONVersion[2] == 0x09)
            description = "PS3-chassis"; // PS3 reports 3.9 software version
        else if ((mainboard->MECHACONVersion[2] >= 0x06) && (mainboard->MECHACONVersion[2] < 0x09))
            description = "G-chassis"; // 3.6, 3.8 - G-chassis
        else if (mainboard->MECHACONVersion[2] < 0x06)
            description = "F-chassis"; // 3.0, 3.2, 3.4 - F-chassis
        else
            description = "Unknown";
    }
    else if (mainboard->MECHACONVersion[1] == 0x02)
    {
        if (mainboard->MECHACONVersion[2] > 0x0a)
            description = "D-chassis";
        else if (mainboard->MECHACONVersion[2] < 0x04)
            description = "A, AB-chassis"; // TODO: differ by romver?
        else if (!strncmp(mainboard->romver, "015", 3))
            description = "D-chassis";
        else
            description = "B,C-chassis (work in progress)"; // TODO:  only way to determine C from D is EEPROM, seems all C chassis has add0x10 = 8809
    }
    else if (mainboard->MECHACONVersion[1] == 0x01)
        description = "A-chassis"; // late 90k differs only in bootrom
    else
        description = "Unknown";

    return description;
}

const char *GetMainboardModelDesc(const struct PS2IDBMainboardEntry *mainboard)
{
    const char *description;

    description = "Work in progress";

    return description;
}

unsigned int CalculateCPUCacheSize(unsigned char value)
{ // 2^(12+value)
    return (1U << (12 + value));
}

int WriteSystemInformation(FILE *stream, const struct SystemInformation *SystemInformation)
{
    unsigned int i, modelID;
    unsigned short int conModelID;
    u32 Serial;
    u8 MayBeModded = 0;
    const char *dvdplVer;
    const char *OSDVer;

    // MayBeModded = CheckROM(&SystemInformation->mainboard); // TODO: replace with redump lookup
    // DEBUG_PRINTF("CheckROM() = %d\n", MayBeModded);

    // Header
    fputs("Log file generated by Playstation 2 Ident v" PS2IDENT_VERSION ", built on "__DATE__
          " "__TIME__
          "\r\n\r\n",
          stream);
    DEBUG_PRINTF("fputs Log file generated by Playstation 2 Ident v");
    fprintf(stream, "ROMVER:            %s\r\n", SystemInformation->mainboard.romver);

    // ROM region sizes
    fprintf(stream, "ROM region sizes:\r\n");
    for (i = 0; i <= 2; i++)
    {
        fprintf(stream, "    ROM%u:          ", i);
        if (SystemInformation->ROMs[i].IsExists)
            fprintf(stream, "0x%X (%u bytes)\r\n", SystemInformation->ROMs[i].StartAddress, SystemInformation->ROMs[i].size);
        else
            fputs("<Not detected>\r\n", stream);
    }
    fprintf(stream, "    EROM:          ");
    if (SystemInformation->erom.IsExists)
        fprintf(stream, "0x%X (%u bytes)\r\n", SystemInformation->erom.StartAddress, SystemInformation->erom.size);
    else
        fprintf(stream, "<Not detected>\r\n");

    // Physical ROM chip sizes
    fputs("ROM chip sizes:\r\n"
          "    Boot ROM:      ",
          stream);
    if (SystemInformation->mainboard.BOOT_ROM.IsExists)
    {
        fprintf(stream, "0x%X (%u Mbit)    CRC32: 0x%08x\r\n",
                SystemInformation->mainboard.BOOT_ROM.StartAddress, SystemInformation->mainboard.BOOT_ROM.size / 1024 / 128,
                SystemInformation->mainboard.BOOT_ROM.crc32);
    }
    else
        fputs("<Not detected>\r\n", stream);

    fputs("    DVD ROM:       ", stream);
    if (SystemInformation->mainboard.DVD_ROM.IsExists)
    {
        fprintf(stream, "0x%X (%u Mbit)    CRC32: 0x%08x\r\n",
                SystemInformation->mainboard.DVD_ROM.StartAddress, SystemInformation->mainboard.DVD_ROM.size / 1024 / 128,
                SystemInformation->mainboard.DVD_ROM.crc32);
    }
    else
        fputs("<Not detected>\r\n", stream);
    fputs("    Boot EXTINFO:  ", stream);
    fprintf(stream, "%s (%s)\r\n", SystemInformation->mainboard.BOOT_ROM.extinfo, GetBOOTROMDesc(SystemInformation->mainboard.BOOT_ROM.extinfo, SystemInformation->mainboard.romver, SystemInformation->DVDPlayerVer));
    if (SystemInformation->mainboard.DVD_ROM.IsExists)
    {
        fputs("    DVD  EXTINFO:  ", stream);
        fprintf(stream, "%s (%s)\r\n", SystemInformation->mainboard.DVD_ROM.extinfo, GetDVDROMDesc(SystemInformation->DVDPlayerVer));

        // Version numbers
        dvdplVer = SystemInformation->DVDPlayerVer[0] == '\0' ? "-" : SystemInformation->DVDPlayerVer;
        fputs("    DVD Player:    ", stream);
        fprintf(stream, "%s\r\n", dvdplVer);
    }
    OSDVer = SystemInformation->OSDVer[0] == '\0' ? "-" : SystemInformation->OSDVer;
    fprintf(stream, "    OSDVer:        %s\r\n"
                    "    PS1DRV:        %s\r\n",
            OSDVer, SystemInformation->PS1DRVVer);

    // Chip revisions
    fprintf(stream, "EE/GS:\r\n"
                    "    Implementation:      0x%02x\r\n"
                    "    Revision:            %u.%u (%s)\r\n"
                    "    EE_F520:             0x%08x\r\n"
                    "    EE_F540:             0x%08x\r\n"
                    "    EE_F550:             0x%08x\r\n"
                    "    FPU implementation:  0x%02x\r\n"
                    "    FPU revision:        %u.%u\r\n"
                    "    ICache size:         0x%02x (%u KB)\r\n"
                    "    DCache size:         0x%02x (%u KB)\r\n"
                    "    RAM size:            %u bytes\r\n"
                    "    GS revision:         %u.%02u (%s)\r\n"
                    "    GS ID:               0x%02x\r\n",
            SystemInformation->mainboard.ee.implementation, SystemInformation->mainboard.ee.revision >> 4, SystemInformation->mainboard.ee.revision & 0xF, GetEEChipDesc(SystemInformation->mainboard.ee.revision & 0xFF, SystemInformation->mainboard.gs.revision & 0xFF),
            SystemInformation->mainboard.ee.F520, SystemInformation->mainboard.ee.F540, SystemInformation->mainboard.ee.F550,
            SystemInformation->mainboard.ee.FPUImplementation, SystemInformation->mainboard.ee.FPURevision >> 4, SystemInformation->mainboard.ee.FPURevision & 0xF,
            SystemInformation->mainboard.ee.ICacheSize, CalculateCPUCacheSize(SystemInformation->mainboard.ee.ICacheSize) / 1024,
            SystemInformation->mainboard.ee.DCacheSize, CalculateCPUCacheSize(SystemInformation->mainboard.ee.DCacheSize) / 1024,
            SystemInformation->mainboard.ee.RAMSize,
            SystemInformation->mainboard.gs.revision >> 4, SystemInformation->mainboard.gs.revision & 0xF,
            GetGSChipDesc(SystemInformation->mainboard.gs.revision & 0xFF),
            SystemInformation->mainboard.gs.id);

    fprintf(stream, "IOP:\r\n"
                    "    Implementation:      0x%02x\r\n"
                    "    Revision:            %u.%u (%s)\r\n"
                    "    RAM size:            %u bytes\r\n"
                    "    SSBUS I/F revision:  %u.%u (%s)\r\n",
            SystemInformation->mainboard.iop.revision >> 8,
            (SystemInformation->mainboard.iop.revision & 0xFF) >> 4, SystemInformation->mainboard.iop.revision & 0xF, GetIOPChipDesc(SystemInformation->mainboard.iop.revision, SystemInformation->mainboard.ee.revision),
            SystemInformation->mainboard.iop.RAMSize,
            SystemInformation->mainboard.ssbus.revision >> 4, SystemInformation->mainboard.ssbus.revision & 0xF,
            GetSSBUSIFDesc(SystemInformation->mainboard.ssbus.revision, SystemInformation->mainboard.ee.revision));

    fputs("    AIF revision:        ", stream);
    if (SystemInformation->mainboard.ssbus.status & PS2DB_SSBUS_HAS_AIF)
        fprintf(stream, "%u\r\n", SystemInformation->mainboard.ssbus.AIFRevision);
    else
        fputs("<Not detected>\r\n", stream);

    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MVER))
    {
        fprintf(stream, "MECHACON:\r\n"
                        "    Revision:            %u.%02u (%s)\r\n"
                        "    MagicGate region:    0x%02x (%s)\r\n"
                        "    System type:         0x%02x (%s)\r\n"
                        "    DSP revision:        %u (%s)\r\n",
                SystemInformation->mainboard.MECHACONVersion[1], SystemInformation->mainboard.MECHACONVersion[2],
                GetMECHACONChipDesc((unsigned int)(SystemInformation->mainboard.MECHACONVersion[1]) << 16 | (unsigned int)(SystemInformation->mainboard.MECHACONVersion[2]) << 8 | SystemInformation->mainboard.MECHACONVersion[0]),
                SystemInformation->mainboard.MECHACONVersion[0], GetRegionDesc(SystemInformation->mainboard.MECHACONVersion[0]),
                SystemInformation->mainboard.MECHACONVersion[3], GetSystemTypeDesc(SystemInformation->mainboard.MECHACONVersion[3]),
                SystemInformation->DSPVersion, GetDSPDesc(SystemInformation->DSPVersion));
    }
    else
    {
        fputs("MECHACON:\r\n"
              "    Revision:            -.-\r\n"
              "    MagicGate region:    -\r\n"
              "    System type:         -\r\n"
              "    DSP revision:        -\r\n",
              stream);
    }

    fprintf(stream, "    M Renewal Date:      ");
    if (SystemInformation->mainboard.MECHACONVersion[1] < 5 || (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MRENEWDATE))
        fprintf(stream, "----/--/-- --:--\r\n");
    else
        fprintf(stream, "20%02x/%02x/%02x %02x:%02x\r\n", SystemInformation->mainboard.MRenewalDate[0], SystemInformation->mainboard.MRenewalDate[1], SystemInformation->mainboard.MRenewalDate[2], SystemInformation->mainboard.MRenewalDate[3], SystemInformation->mainboard.MRenewalDate[4]);

    fputs("Mainboard:\r\n"
          "    Model name:          ",
          stream);
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MNAME))
        fprintf(stream, "%s\r\n", SystemInformation->mainboard.ModelName);
    else
        fputs("-\r\n", stream);

    fprintf(stream, "    Mainboard model:     %s\r\n"
                    "    Chassis:             %s\r\n"
                    "    ROMGEN:              %04x-%04x\r\n"
                    "    Machine type:        0x%08x\r\n"
                    "    BoardInf:            0x%02x (%s)\r\n"
                    "    MPU Board ID:        0x%04x\r\n"
                    "    SPU2 revision:       0x%02x (%s)\r\n",
            SystemInformation->mainboard.MainboardName, SystemInformation->chassis,
            SystemInformation->mainboard.ROMGEN_MonthDate, SystemInformation->mainboard.ROMGEN_Year, SystemInformation->mainboard.MachineType,
            SystemInformation->mainboard.BoardInf, GetMRPDesc(SystemInformation->mainboard.BoardInf), SystemInformation->mainboard.MPUBoardID,
            SystemInformation->mainboard.spu2.revision, GetSPU2ChipDesc(SystemInformation->mainboard.spu2.revision, SystemInformation->mainboard.ee.revision));

    fputs("    ADD0x010:            ", stream);
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ADD010))
    {
        fprintf(stream, "0x%04x (%s)\r\n",
                SystemInformation->mainboard.ADD010, GetADD010Desc(SystemInformation->mainboard.ADD010));
    }
    else
    {
        fputs("-\r\n", stream);
    }

    // i.Link Model ID
    fputs("    i.Link Model ID:     ", stream);
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ILINKID))
    {
        modelID = SystemInformation->mainboard.ModelID[0] | SystemInformation->mainboard.ModelID[1] << 8 | SystemInformation->mainboard.ModelID[2] << 16;
        fprintf(stream, "0x%06x\r\n", modelID);
    }
    else
    {
        fputs("-\r\n", stream);
    }

    // SDMI Model ID (only 1 last byte, but we will keep 2 bytes)
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_CONSOLEID))
    {
        conModelID = SystemInformation->mainboard.ConModelID[0] | SystemInformation->mainboard.ConModelID[1] << 8;
        Serial     = (SystemInformation->ConsoleID[6]) << 16 | (SystemInformation->ConsoleID[5]) << 8 | (SystemInformation->ConsoleID[4]);
        fprintf(stream, "    Console Model ID:    0x%04x (%s)\r\n"
                        // "    SDMI Company ID:     %02x-%02x-%02x\r\n"
                        "    EMCS ID:             0x%02x (%s)\r\n"
                        "    Serial range:        %03dxxxx\r\n",
                conModelID, GetModelIDDesc(conModelID),
                // SystemInformation->ConsoleID[3], SystemInformation->ConsoleID[2], SystemInformation->ConsoleID[1],
                SystemInformation->mainboard.EMCSID, GetEMCSIDDesc(SystemInformation->mainboard.EMCSID),
                Serial / 10000);
    }
    else
    {
        fputs("    Console Model ID:    -\r\n"
              //   "    SDMI Company ID:     -\r\n"
              "    EMCS ID:             -\r\n"
              "    Serial range:        -\r\n",
              stream);
    }

    fprintf(stream, "    USB HC revision:     %u.%u\r\n",
            SystemInformation->mainboard.usb.HcRevision >> 4, SystemInformation->mainboard.usb.HcRevision & 0xF);

    if ((SystemInformation->mainboard.ssbus.status & PS2DB_SSBUS_HAS_SPEED) && (SystemInformation->mainboard.MECHACONVersion[1] >= 6))
    {
        fprintf(stream, "DEV9:\r\n"
                        "    MAC vendor:          %02x:%02x:%02x\r\n"
                        "    SPEED revision:      0x%04x (%s)\r\n"
                        "    SPEED capabilities:  %04x.%04x (%s)\r\n",
                SystemInformation->SMAP_MAC_address[0], SystemInformation->SMAP_MAC_address[1], SystemInformation->SMAP_MAC_address[2],
                SystemInformation->mainboard.ssbus.SPEED.rev1, GetSPEEDDesc(SystemInformation->mainboard.ssbus.SPEED.rev1, SystemInformation->mainboard.MECHACONVersion), SystemInformation->mainboard.ssbus.SPEED.rev3, SystemInformation->mainboard.ssbus.SPEED.rev8, GetSPEEDCapsDesc(SystemInformation->mainboard.ssbus.SPEED.rev3));
        fprintf(stream, "    PHY OUI:             0x%06x (%s)\r\n"
                        "    PHY model:           0x%02x (%s)\r\n"
                        "    PHY revision:        0x%02x\r\n",
                SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI, GetPHYVendDesc(SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI), SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_VMDL, GetPHYModelDesc(SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI, SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_VMDL), SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_REV);
    }
    else if (!(SystemInformation->mainboard.ssbus.status & PS2DB_SSBUS_HAS_SPEED) && (SystemInformation->mainboard.MECHACONVersion[1] < 6))
    {
        fprintf(stream, "DEV9:\r\n    ***No expansion device connected***\r\n");
    }

    fprintf(stream, "i.Link:\r\n"
                    "    Ports:               %u\r\n"
                    "    Max speed:           %u (%s)\r\n"
                    "    Compliance level:    %u (%s)\r\n"
                    "    Vendor ID:           0x%06x (%s)\r\n"
                    "    Product ID:          0x%06x\r\n",
            SystemInformation->mainboard.iLink.NumPorts,
            SystemInformation->mainboard.iLink.MaxSpeed,
            GetiLinkSpeedDesc(SystemInformation->mainboard.iLink.MaxSpeed),
            SystemInformation->mainboard.iLink.ComplianceLevel,
            GetiLinkComplianceLvlDesc(SystemInformation->mainboard.iLink.ComplianceLevel),
            SystemInformation->mainboard.iLink.VendorID,
            GetiLinkVendorDesc(SystemInformation->mainboard.iLink.VendorID),
            SystemInformation->mainboard.iLink.ProductID);

    if (SystemInformation->mainboard.status || MayBeModded)
    {
        fprintf(stream, "Remarks:\r\n");

        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MVER)
            fprintf(stream, "    Unable to get MECHACON version.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MNAME)
            fprintf(stream, "    Unable to get model name.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MRENEWDATE)
            fprintf(stream, "    Unable to get M renewal date.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ILINKID)
            fprintf(stream, "    Unable to get i.Link ID.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_CONSOLEID)
            fprintf(stream, "    Unable to get console ID.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ADD010)
            fprintf(stream, "    Unable to get ADD0x010.\r\n");
        if (MayBeModded)
            fprintf(stream, "    ROM may not be clean.\r\n");
    }

    return 0;
}

int WriteExpDeviceInformation(FILE *stream, const struct SystemInformation *SystemInformation)
{
    // Header
    fputs("Expansion Device Log file generated by Playstation 2 Ident v" PS2IDENT_VERSION ", built on "__DATE__
          " "__TIME__
          "\r\n\r\n",
          stream);

    fprintf(stream, "DEV9:\r\n"
                    "    MAC vendor:          %02x:%02x:%02x\r\n"
                    "    SPEED revision:      0x%04x (%s)\r\n"
                    "    SPEED capabilities:  %04x.%04x (%s)\r\n",
            SystemInformation->SMAP_MAC_address[0], SystemInformation->SMAP_MAC_address[1], SystemInformation->SMAP_MAC_address[2],
            SystemInformation->mainboard.ssbus.SPEED.rev1, GetSPEEDDesc(SystemInformation->mainboard.ssbus.SPEED.rev1, SystemInformation->mainboard.MECHACONVersion), SystemInformation->mainboard.ssbus.SPEED.rev3, SystemInformation->mainboard.ssbus.SPEED.rev8, GetSPEEDCapsDesc(SystemInformation->mainboard.ssbus.SPEED.rev3));
    fprintf(stream, "    PHY OUI:             0x%06x (%s)\r\n"
                    "    PHY model:           0x%02x (%s)\r\n"
                    "    PHY revision:        0x%02x\r\n",
            SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI, GetPHYVendDesc(SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI), SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_VMDL, GetPHYModelDesc(SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI, SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_VMDL), SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_REV);


    return 0;
}
