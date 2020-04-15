#pragma once

#ifndef __UTILS_H__
#define __UTILS_H__

#include <switch.h>

#define APP_BASE_PATH               "sdmc:/switch/nxdumptool/"

#define LOGFILE(fmt, ...)           utilsWriteLogMessage(__func__, fmt, ##__VA_ARGS__)

#define MEMBER_SIZE(type, member)   sizeof(((type*)NULL)->member)

#define SLEEP(x)                    svcSleepThread((x) * (u64)1000000000)

#define MAX_ELEMENTS(x)             ((sizeof((x))) / (sizeof((x)[0])))

#define ROUND_UP(x, y)              ((x) + (((y) - ((x) % (y))) % (y)))                 /* Aligns 'x' bytes to a 'y' bytes boundary. */



typedef enum {
    UtilsCustomFirmwareType_Atmosphere = 0,
    UtilsCustomFirmwareType_SXOS       = 1,
    UtilsCustomFirmwareType_ReiNX      = 2
} UtilsCustomFirmwareType;

typedef struct {
    u16 major : 6;
    u16 minor : 6;
    u16 micro : 4;
    u16 bugfix;
} TitleVersion;




u64 utilsHidKeysAllDown(void);
u64 utilsHidKeysAllHeld(void);

void utilsWaitForButtonPress(void);

void utilsConsoleErrorScreen(const char *fmt, ...);

void utilsWriteLogMessage(const char *func_name, const char *fmt, ...);

void utilsOverclockSystem(bool restore);

bool utilsInitializeResources(void);
void utilsCloseResources(void);



static inline FsStorage *utilsGetEmmcBisSystemStorage(void)
{
    return NULL;
}

static inline u8 utilsGetCustomFirmwareType(void)
{
    return UtilsCustomFirmwareType_Atmosphere;
}



#endif /* __UTILS_H__ */
