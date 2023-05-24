/*
 * nxdt_bfsar.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "nxdt_utils.h"
#include "nxdt_bfsar.h"
#include "romfs.h"
#include "title.h"

#define BFSAR_FILENAME      "qlaunch.bfsar"
#define BFSAR_ROMFS_PATH    "/sound/" BFSAR_FILENAME

/* Global variables. */

static Mutex g_bfsarMutex = 0;
static bool g_bfsarInterfaceInit = false;

static char g_bfsarPath[FS_MAX_PATH] = {0};

bool bfsarInitialize(void)
{
    bool use_root = true;
    const char *launch_path = utilsGetLaunchPath();
    char *ptr1 = NULL, *ptr2 = NULL;

    TitleInfo *title_info = NULL;

    NcaContext *nca_ctx = NULL;

    RomFileSystemContext romfs_ctx = {0};
    RomFileSystemFileEntry *romfs_file_entry = NULL;

    FILE *bfsar_file = NULL;
    u8 *bfsar_data = NULL;
    size_t bfsar_size = 0, wr = 0;

    bool ret = false;

    SCOPED_LOCK(&g_bfsarMutex)
    {
        ret = g_bfsarInterfaceInit;
        if (ret) break;

        /* Generate BFSAR file path. */
        if (launch_path)
        {
            ptr1 = strchr(launch_path, '/');
            ptr2 = strrchr(launch_path, '/');

            if (ptr1 && ptr2 && ptr1 != ptr2)
            {
                /* Create BFSAR file in the current working directory. */
                snprintf(g_bfsarPath, sizeof(g_bfsarPath), "%.*s" BFSAR_FILENAME, (int)((ptr2 - launch_path) + 1), launch_path);
                use_root = false;
            }
        }

        /* Create BFSAR file in the SD card root directory. */
        if (use_root) sprintf(g_bfsarPath, DEVOPTAB_SDMC_DEVICE "/" BFSAR_FILENAME);

        LOG_MSG_DEBUG("BFSAR path: \"%s\".", g_bfsarPath);

        /* Check if the BFSAR file is already available and not empty. */
        bfsar_file = fopen(g_bfsarPath, "rb");
        if (bfsar_file)
        {
            fseek(bfsar_file, 0, SEEK_END);
            bfsar_size = ftell(bfsar_file);
            if (bfsar_size)
            {
                ret = g_bfsarInterfaceInit = true;
                break;
            }
        }

        /* Get title info. */
        if (!(title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_BuiltInSystem, QLAUNCH_TID)))
        {
            LOG_MSG_ERROR("Failed to get title info for qlaunch!");
            break;
        }

        /* Allocate memory for a temporary NCA context. */
        nca_ctx = calloc(1, sizeof(NcaContext));
        if (!nca_ctx)
        {
            LOG_MSG_ERROR("Failed to allocate memory for temporary NCA context!");
            break;
        }

        /* Initialize NCA context. */
        /* Don't allow invalid NCA signatures. */
        if (!ncaInitializeContext(nca_ctx, NcmStorageId_BuiltInSystem, 0, titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Program, 0), title_info->version.value, NULL) || \
            !nca_ctx->valid_main_signature)
        {
            LOG_MSG_ERROR("Failed to initialize qlaunch Program NCA context!");
            break;
        }

        /* Initialize RomFS context. */
        if (!romfsInitializeContext(&romfs_ctx, &(nca_ctx->fs_ctx[1]), NULL))
        {
            LOG_MSG_ERROR("Failed to initialize RomFS context for qlaunch Program NCA!");
            break;
        }

        /* Get RomFS file entry. */
        if (!(romfs_file_entry = romfsGetFileEntryByPath(&romfs_ctx, BFSAR_ROMFS_PATH)))
        {
            LOG_MSG_ERROR("Failed to retrieve RomFS file entry for \"" BFSAR_ROMFS_PATH "\"!");
            break;
        }

        /* Check file size. */
        bfsar_size = romfs_file_entry->size;
        if (!bfsar_size)
        {
            LOG_MSG_ERROR("File size for qlaunch's \"" BFSAR_ROMFS_PATH "\" is zero!");
            break;
        }

        /* Allocate memory for BFSAR data. */
        if (!(bfsar_data = malloc(bfsar_size)))
        {
            LOG_MSG_ERROR("Failed to allocate 0x%lX bytes for qlaunch's \"" BFSAR_ROMFS_PATH "\"!", bfsar_size);
            break;
        }

        /* Read BFSAR data. */
        if (!romfsReadFileEntryData(&romfs_ctx, romfs_file_entry, bfsar_data, bfsar_size, 0))
        {
            LOG_MSG_ERROR("Failed to read 0x%lX bytes long \"" BFSAR_ROMFS_PATH "\" from qlaunch!", bfsar_size);
            break;
        }

        /* Create BFSAR file. */
        bfsar_file = fopen(g_bfsarPath, "wb");
        if (!bfsar_file)
        {
            LOG_MSG_ERROR("Failed to open \"%s\" for writing!", g_bfsarPath);
            break;
        }

        /* Write BFSAR data. */
        wr = fwrite(bfsar_data, 1, bfsar_size, bfsar_file);
        if (wr != bfsar_size)
        {
            LOG_MSG_ERROR("Failed to write 0x%lX bytes block to \"%s\"!", bfsar_size, g_bfsarPath);
            break;
        }

        /* Update flags. */
        ret = g_bfsarInterfaceInit = true;
    }

    if (bfsar_file)
    {
        fclose(bfsar_file);

        /* Commit SD card filesystem changes. */
        utilsCommitSdCardFileSystemChanges();
    }

    if (bfsar_data) free(bfsar_data);

    romfsFreeContext(&romfs_ctx);

    if (nca_ctx) free(nca_ctx);

    titleFreeTitleInfo(&title_info);

    return ret;
}

void bfsarExit(void)
{
    SCOPED_LOCK(&g_bfsarMutex)
    {
        /* Clear BFSAR file path. */
        *g_bfsarPath = '\0';
        g_bfsarInterfaceInit = false;
    }
}

const char *bfsarGetFilePath(void)
{
    const char *ret = NULL;

    SCOPED_TRY_LOCK(&g_bfsarMutex)
    {
        if (g_bfsarInterfaceInit) ret = (const char*)g_bfsarPath;
    }

    return ret;
}
