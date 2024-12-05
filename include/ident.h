struct RequiredFileSpaceStat
{
    unsigned int IsFile;
    unsigned int length;
};

struct SystemInformation
{
    struct PS2IDBMainboardEntry mainboard;
    t_PS2DBROMHardwareInfo ROMs[3];
    t_PS2DBROMHardwareInfo erom;
    unsigned char ConsoleID[8];        // EEPROM
    unsigned char iLinkID[8];          // EEPROM
    char chassis[14];
    char DVDPlayerVer[16]; // TODO: move to ROM
    char OSDVer[16];       // TODO: move to ROM
    char PS1DRVVer[32];    // TODO: move to ROM
    u8 DSPVersion;
    unsigned char SMAP_MAC_address[6]; // only for 70k is real, Deckard from eeprom, FATS from network adapter
};

struct DumpingStatus
{
    float progress;
    int status; // 0 = In progress, 1 = complete, <0 = failed.
};

int GetEEInformation(struct SystemInformation *SystemInformation);
int GetPeripheralInformation(struct SystemInformation *SystemInformation);

const char *GetiLinkSpeedDesc(unsigned char speed);
const char *GetiLinkComplianceLvlDesc(unsigned char level);
const char *GetiLinkVendorDesc(unsigned int vendor);
const char *GetSPEEDDesc(unsigned short int revision, const unsigned char MECHA_revision[4]);
const char *GetSPEEDCapsDesc(unsigned short int caps);
const char *GetPHYVendDesc(unsigned int oui);
const char *GetPHYModelDesc(unsigned int oui, unsigned char model);
const char *GetSSBUSIFDesc(unsigned char revision, unsigned char EE_revision);
const char *GetSPU2ChipDesc(unsigned short int revision, unsigned char EE_revision);
const char *GetIOPChipDesc(unsigned char revision, unsigned char EE_revision);
const char *GetGSChipDesc(unsigned char revision);
const char *GetEEChipDesc(unsigned char revision, unsigned char GS_revision);
const char *GetBOOTROMDesc(const char *extinfo, const char *romver, const char *dvdplVer);
const char *GetDVDROMDesc(const char *dvdplVer);
const char *GetMECHACONChipDesc(unsigned int revision);
const char *GetSystemTypeDesc(unsigned char type);
const char *GetRegionDesc(unsigned char region);
const char *GetMRPDesc(unsigned short int id);
const char *GetModelIDDesc(unsigned short int ModelId);
const char *GetEMCSIDDesc(unsigned char id);
const char *GetADD010Desc(unsigned short int id);
const char *GetDSPDesc(unsigned char revision);
const char *GetChassisDesc(const struct PS2IDBMainboardEntry *mainboard);
const char *GetMainboardModelDesc(const struct PS2IDBMainboardEntry *mainboard);

int GetNVMWord(u16 address, u16 *word);

unsigned int CalculateCPUCacheSize(unsigned char value);

int DumpRom(const char *filename, const struct SystemInformation *SystemInformation, struct DumpingStatus *DumpingStatus, unsigned int DumpingRegion);
int WriteNewMainboardDBRecord(const char *path, const struct PS2IDBMainboardEntry *SystemInformation);
int DumpMECHACON_EEPROM(const char *filename);
int DumpMECHACON_VERSION(const char *filename, const struct SystemInformation *SystemInformation);
int WriteSystemInformation(FILE *stream, const struct SystemInformation *SystemInformation);
int WriteExpDeviceInformation(FILE *stream, const struct SystemInformation *SystemInformation);

int readDevMemEEIOP(const void *MemoryStart, void *buffer, unsigned int NumBytes, int mode);

enum DUMP_REGIONS
{
    DUMP_REGION_BOOT_ROM = 0,
    DUMP_REGION_DVD_ROM,
    DUMP_REGION_EEPROM,
    DUMP_REGION_COUNT
};
