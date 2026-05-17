/* Function prototypes */
struct SystemInformation;
struct DumpingStatus;
int MainMenu(const struct SystemInformation *SystemInformation);
#ifdef HEADLESS
int RunHeadlessDump(const struct SystemInformation *SystemInformation);
#endif

struct ProgressBarCoords
{
    float x, y;
    float width, height;
};

#define MAX_DEVICES_IN_ROW 2
#define DEVICE_LIST_X      128
#define DEVICE_LIST_Y      64

void RedrawDumpingScreen(const struct SystemInformation *SystemInformation, const struct DumpingStatus *DumpingStatus);
void RedrawLoadingScreen(unsigned int frame);
