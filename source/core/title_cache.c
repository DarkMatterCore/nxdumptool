
/*
 * title_cache.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <core/nxdt_utils.h>
#include <core/title_cache.h>
#include <core/nacp.h>

#define TITLE_CACHE_MAGIC       0x4E585443  /* "NXTC". */
#define TITLE_CACHE_VERSION     1
#define TITLE_CACHE_ALIGNMENT   0x10

/* Type definitions. */

typedef struct {
    u32 magic;          ///< "NXTC".
    u8 version;         ///< Must be set to TITLE_CACHE_VERSION.
    u8 language;        ///< SetLanguage.
    u8 reserved_1[0x2];
    u32 entry_count;
    u8 reserved_2[0x4];
} TitleCacheFileHeader;

NXDT_ASSERT(TitleCacheFileHeader, 0x10);

typedef struct {
    u64 title_id;
    u16 name_len;
    u16 publisher_len;
    u32 icon_size;      ///< JPEG icon size. Must not exceed NACP_MAX_ICON_SIZE.
    u32 blob_offset;    ///< Relative to the start of the blob area. Must be aligned to TITLE_CACHE_ALIGNMENT.
    u32 blob_size;
    u32 crc;            ///< Calculated over this entire struct with this field set to zero.
    u8 reserved[0x4];
} TitleCacheFileEntry;

NXDT_ASSERT(TitleCacheFileEntry, 0x20);

typedef struct {
    u64 title_id;
    char *name;         ///< Pointer to a dynamically allocated buffer that holds the title name string.
    char *publisher;    ///< Pointer to a dynamically allocated buffer that holds the title publisher string.
    u32 icon_size;      ///< JPEG icon size. Must not exceed NACP_MAX_ICON_SIZE.
    u8 *icon_data;      ///< Pointer to a dynamically allocated buffer that holds the JPEG icon data.
} TitleCacheEntry;

/* Function prototypes. */

static void titleCacheGetSystemLanguage(void);

static void titleCacheLoadFile(void);

static TitleCacheFileHeader *titleCacheLoadFileHeader(u8 *cache_file_data, size_t cache_file_size, size_t *out_cur_offset);
static TitleCacheFileEntry *titleCacheLoadFileEntries(u8 *cache_file_data, size_t cache_file_size, const TitleCacheFileHeader *cache_file_header, size_t *out_cur_offset);
static bool titleCacheDeserializeDataBlobs(u8 *cache_file_data, const TitleCacheFileHeader *cache_file_header, const TitleCacheFileEntry *cache_file_entries, size_t *out_cur_offset);

static void titleCacheSaveFile(void);

static bool titleCachePopulateFileHeader(u8 *cache_file_data, size_t *out_cur_offset);
static TitleCacheFileEntry *titleCacheSerializeDataBlobs(u8 **cache_file_data, size_t *out_cur_offset);

static TitleCacheEntry *titleCacheGenerateCacheEntryFromFileEntry(u8 *cache_file_data, const TitleCacheFileEntry *cache_file_entry, size_t *out_cur_offset);
static TitleCacheEntry *titleCacheGenerateCacheEntryFromApplicationMetadata(TitleApplicationMetadata *app_metadata);
NX_INLINE void titleCacheFreeCacheEntry(TitleCacheEntry **cache_entry);

static bool titleCacheReallocateCacheEntryArray(u32 extra_entry_count, bool free_entries);

static bool titleCacheAppendDataBlobToFileCacheBuffer(u8 **cache_file_data, const TitleCacheEntry *cache_entry, TitleCacheFileEntry *cache_file_entry, u32 *out_blob_offset, size_t *out_cur_offset);

NX_INLINE u32 titleCacheCalculateDataBlobSize(u16 name_len, u16 publisher_len, u32 icon_size);

static TitleCacheEntry *titleCacheGetEntryById(u64 title_id);

static int titleCacheEntrySortFunction(const void *a, const void *b);

/* Global variables. */

static bool g_titleCacheInit = false;
static Mutex g_titleCacheMutex = 0;

static SetLanguage g_systemLanguage = SetLanguage_ENUS; // Default to American English.

static TitleCacheEntry **g_titleCache = NULL;
static u32 g_titleCacheCount = 0;

static bool g_cacheFlushRequired = false;

bool titleCacheInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_titleCacheMutex)
    {
        ret = g_titleCacheInit;
        if (ret) break;

        /* Get system language. */
        titleCacheGetSystemLanguage();

        /* Load title cache file. */
        titleCacheLoadFile();

        /* Update flags. */
        ret = g_titleCacheInit = true;
    }

    return ret;
}

void titleCacheExit(void)
{
    SCOPED_LOCK(&g_titleCacheMutex)
    {
        if (g_titleCache)
        {
            /* Write title cache file. */
            /* This will return immediately if there's no pending changes. */
            titleCacheSaveFile();

            /* Free title cache entries. */
            for(u32 i = 0; i < g_titleCacheCount; i++) titleCacheFreeCacheEntry(&(g_titleCache[i]));

            free(g_titleCache);
            g_titleCache = NULL;
        }

        g_titleCacheCount = 0;

        g_titleCacheInit = false;
    }
}

bool titleCacheCheckIfEntryExists(u64 title_id)
{
    bool ret = false;

    SCOPED_LOCK(&g_titleCacheMutex)
    {
        /* Retrieve title cache entry. */
        ret = (titleCacheGetEntryById(title_id) != NULL);
    }

    return ret;
}

TitleApplicationMetadata *titleCacheGetApplicationMetadataEntryById(u64 title_id)
{
    TitleApplicationMetadata *out = NULL;

    SCOPED_LOCK(&g_titleCacheMutex)
    {
        /* Retrieve title cache entry. */
        TitleCacheEntry *cache_entry = titleCacheGetEntryById(title_id);
        if (!cache_entry)
        {
            //LOG_MSG_DEBUG("Title cache entry with ID %016lX unavailable!", title_id);
            break;
        }

        /* Allocate memory for the output TitleApplicationMetadata. */
        out = calloc(1, sizeof(TitleApplicationMetadata));
        if (!out)
        {
            LOG_MSG_ERROR("Failed to allocate memory for TitleApplicationMetadata.");
            break;
        }

        /* Populate output. */
        out->title_id = title_id;
        snprintf(out->lang_entry.name, sizeof(out->lang_entry.name), "%s", cache_entry->name);
        snprintf(out->lang_entry.author, sizeof(out->lang_entry.author), "%s", cache_entry->publisher);
        out->icon_size = cache_entry->icon_size;

        out->icon = malloc(out->icon_size);
        if (!out->icon)
        {
            LOG_MSG_ERROR("Failed to allocate 0x%X byte-long buffer for TitleApplicationMetadata icon.", out->icon_size);
            free(out);
            out = NULL;
            break;
        }

        memcpy(out->icon, cache_entry->icon_data, out->icon_size);
    }

    return out;
}

bool titleCacheAddEntry(TitleApplicationMetadata *app_metadata, bool force_add)
{
    bool ret = false;

    SCOPED_LOCK(&g_titleCacheMutex)
    {
        TitleCacheEntry *cache_entry = NULL;

        if (!app_metadata || !app_metadata->title_id || !app_metadata->icon_size || app_metadata->icon_size > NACP_MAX_ICON_SIZE || !app_metadata->icon)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Check if the requested title is available within our title cache. */
        cache_entry = titleCacheGetEntryById(app_metadata->title_id);
        if (cache_entry)
        {
            if (!force_add)
            {
                ret = true;
                break;
            }

            NacpLanguageEntry *lang_entry = &(app_metadata->lang_entry);

            /* Free previous data. */
            free(cache_entry->name);
            free(cache_entry->publisher);
            free(cache_entry->icon_data);

            /* Populate title cache entry fields. */
            cache_entry->name = strndup(lang_entry->name, sizeof(lang_entry->name));
            cache_entry->publisher = strndup(lang_entry->author, sizeof(lang_entry->author));
            cache_entry->icon_size = app_metadata->icon_size;

            cache_entry->icon_data = malloc(cache_entry->icon_size);
            if (cache_entry->icon_data) memcpy(cache_entry->icon_data, app_metadata->icon, cache_entry->icon_size);

            if (!cache_entry->name || !cache_entry->publisher || !cache_entry->icon_data)
            {
                LOG_MSG_ERROR("Failed to populate existent title cache entry! (title %016lX).", cache_entry->title_id);
                break;
            }
        } else {
            /* Generate title cache entry. */
            cache_entry = titleCacheGenerateCacheEntryFromApplicationMetadata(app_metadata);
            if (!cache_entry) break;

            /* Reallocate title cache entry pointer array. */
            if (!titleCacheReallocateCacheEntryArray(1, false))
            {
                titleCacheFreeCacheEntry(&cache_entry);
                break;
            }

            /* Set title cache entry pointer. */
            g_titleCache[g_titleCacheCount++] = cache_entry;

            /* Sort title cache entries by title ID. */
            if (g_titleCacheCount > 1) qsort(g_titleCache, g_titleCacheCount, sizeof(TitleCacheEntry*), &titleCacheEntrySortFunction);
        }

        /* Update flags. */
        ret = g_cacheFlushRequired = true;
    }

    return ret;
}

void titleCacheFlushCacheFile(void)
{
    SCOPED_LOCK(&g_titleCacheMutex) titleCacheSaveFile();
}

/* Loosely based on code from libnx's nacpGetLanguageEntry(). */
static void titleCacheGetSystemLanguage(void)
{
    Result rc = 0;
    u64 lang_code = 0;

    /* Get system language. */
    rc = setGetSystemLanguage(&lang_code);
    if (R_SUCCEEDED(rc))
    {
        /* Convert the retrieved language code into a SetLanguage value. */
        rc = setMakeLanguage(lang_code, &g_systemLanguage);

        /* Use American English for unsupported system languages. */
        if (g_systemLanguage < SetLanguage_JA || (R_SUCCEEDED(rc) && g_systemLanguage >= SetLanguage_Total)) g_systemLanguage = SetLanguage_ENUS;
    }

    LOG_MSG_INFO("Retrieved system language: %d.", g_systemLanguage);
}

static void titleCacheLoadFile(void)
{
    FILE *cache_file = NULL;
    size_t cache_file_size = 0;
    u8 *cache_file_data = NULL;

    TitleCacheFileHeader *cache_file_header = NULL;
    TitleCacheFileEntry *cache_file_entries = NULL;

    size_t cur_offset = 0;

    bool success = false;

    /* Open title cache file. */
    cache_file = fopen(TITLE_CACHE_PATH, "rb");
    if (!cache_file)
    {
        LOG_MSG_ERROR("Unable to open title cache at \"" TITLE_CACHE_PATH "\" for reading!");
        return;
    }

    /* Get title cache file size and validate it. */
    fseek(cache_file, 0, SEEK_END);
    cache_file_size = ftell(cache_file);
    rewind(cache_file);

    if (!cache_file_size)
    {
        LOG_MSG_ERROR("Title cache file at \"" TITLE_CACHE_PATH "\" is empty!");
        goto end;
    }

    /* Allocate memory for the whole cache file. */
    cache_file_data = malloc(cache_file_size);
    if (!cache_file_data)
    {
        LOG_MSG_ERROR("Failed to allocate 0x%lX byte-long block for the title cache file!", cache_file_size);
        goto end;
    }

    /* Read whole title cache file. */
    if (fread(cache_file_data, 1, cache_file_size, cache_file) != cache_file_size)
    {
        LOG_MSG_ERROR("Failed to read 0x%lX byte-long title cache file! (%d).", cache_file_size, errno);
        goto end;
    }

    /* Load title cache file header. */
    if (!(cache_file_header = titleCacheLoadFileHeader(cache_file_data, cache_file_size, &cur_offset))) goto end;

    /* Read title cache file entries. */
    if (!(cache_file_entries = titleCacheLoadFileEntries(cache_file_data, cache_file_size, cache_file_header, &cur_offset))) goto end;

    /* Deserialize data blobs. */
    success = titleCacheDeserializeDataBlobs(cache_file_data, cache_file_header, cache_file_entries, &cur_offset);

end:
    if (cache_file_data) free(cache_file_data);

    if (cache_file)
    {
        fclose(cache_file);

        if (!success)
        {
            remove(TITLE_CACHE_PATH);
            utilsCommitSdCardFileSystemChanges();
        }
    }
}

static TitleCacheFileHeader *titleCacheLoadFileHeader(u8 *cache_file_data, size_t cache_file_size, size_t *out_cur_offset)
{
    if (!cache_file_data || cache_file_size < sizeof(TitleCacheFileHeader) || !out_cur_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    /* Validate title cache file header. */
    TitleCacheFileHeader *cache_file_header = (TitleCacheFileHeader*)(cache_file_data + *out_cur_offset);
    if (__builtin_bswap32(cache_file_header->magic) != TITLE_CACHE_MAGIC || cache_file_header->version != TITLE_CACHE_VERSION || \
        cache_file_header->language != (u8)g_systemLanguage || !cache_file_header->entry_count)
    {
        LOG_DATA_ERROR(cache_file_header, sizeof(TitleCacheFileHeader), "Invalid title cache file header! Data dump:");
        return NULL;
    }

    /* Update current offset. */
    *out_cur_offset += sizeof(TitleCacheFileHeader);

    return cache_file_header;
}

static TitleCacheFileEntry *titleCacheLoadFileEntries(u8 *cache_file_data, size_t cache_file_size, const TitleCacheFileHeader *cache_file_header, size_t *out_cur_offset)
{
    size_t cache_file_entries_size = 0;

    if (!cache_file_data || !cache_file_header || !out_cur_offset || \
        cache_file_size < (*out_cur_offset + (cache_file_entries_size = ((size_t)cache_file_header->entry_count * sizeof(TitleCacheFileEntry)))))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    TitleCacheFileEntry *cache_file_entries = NULL;
    u32 cur_blob_offset = 0;
    bool success = false;

    /* Get pointer to title cache file entries. */
    cache_file_entries = (TitleCacheFileEntry*)(cache_file_data + *out_cur_offset);

    /* Validate title cache file entries. */
    for(u32 i = 0; i < cache_file_header->entry_count; i++)
    {
        TitleCacheFileEntry *cur_cache_file_entry = &(cache_file_entries[i]);
        volatile u32 *crc = (volatile u32*)&(cur_cache_file_entry->crc); // Used to prevent the compiler from optimizing away store/load operations.

        /* Verify checksum. */
        u32 stored_crc = *crc;
        *crc = 0;

        u32 calc_crc = crc32Calculate(cur_cache_file_entry, sizeof(TitleCacheFileEntry));
        *crc = stored_crc;

        if (calc_crc != stored_crc)
        {
            LOG_DATA_ERROR(cur_cache_file_entry, sizeof(TitleCacheFileEntry), "Checksum mismatch on title cache entry #%u! (%08X != %08X). Data dump:", i, calc_crc, stored_crc);
            goto end;
        }

        /* Validate entry fields. */
        u32 calc_blob_size = titleCacheCalculateDataBlobSize(cur_cache_file_entry->name_len, cur_cache_file_entry->publisher_len, cur_cache_file_entry->icon_size);

        if (!cur_cache_file_entry->title_id || !cur_cache_file_entry->name_len || !cur_cache_file_entry->publisher_len || !cur_cache_file_entry->icon_size || cur_cache_file_entry->icon_size > NACP_MAX_ICON_SIZE || \
            !IS_ALIGNED(cur_cache_file_entry->blob_offset, TITLE_CACHE_ALIGNMENT) || cur_cache_file_entry->blob_size != calc_blob_size)
        {
            LOG_DATA_ERROR(cur_cache_file_entry, sizeof(TitleCacheFileEntry), "Invalid properties for title cache entry #%u! (blob size 0x%X, next blob offset 0x%X). Data dump:", i, calc_blob_size, cur_blob_offset);
            goto end;
        }

        cur_blob_offset += ALIGN_UP(cur_cache_file_entry->blob_size, TITLE_CACHE_ALIGNMENT);
    }

    /* Validate full title cache file size. */
    size_t full_cache_size = (sizeof(TitleCacheFileHeader) + cache_file_entries_size + cur_blob_offset);
    if (cache_file_size < full_cache_size)
    {
        LOG_MSG_ERROR("Title cache file size is too small to hold all data blobs! (0x%lX < 0x%lX).", cache_file_size, full_cache_size);
        goto end;
    }

    /* Update current offset. */
    *out_cur_offset += cache_file_entries_size;

    /* Update flag. */
    success = true;

end:
    return (success ? cache_file_entries : NULL);
}

static bool titleCacheDeserializeDataBlobs(u8 *cache_file_data, const TitleCacheFileHeader *cache_file_header, const TitleCacheFileEntry *cache_file_entries, size_t *out_cur_offset)
{
    if (!cache_file_data || !cache_file_header || !cache_file_entries || !out_cur_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u32 entry_count = cache_file_header->entry_count, extra_entry_count = 0;
    bool success = false, free_entries = false;

    /* Reallocate title cache entry pointer array. */
    if (!titleCacheReallocateCacheEntryArray(entry_count, false)) goto end;

    free_entries = true;

    /* Fill new title cache entries. */
    for(u32 i = 0; i < entry_count; i++)
    {
        const TitleCacheFileEntry *cur_cache_file_entry = &(cache_file_entries[i]);
        TitleCacheEntry *cache_entry = NULL;

        /* Generate title cache entry. */
        cache_entry = titleCacheGenerateCacheEntryFromFileEntry(cache_file_data, cur_cache_file_entry, out_cur_offset);
        if (!cache_entry)
        {
            LOG_MSG_ERROR("Failed to generate title cache entry for %016lX!", cur_cache_file_entry->title_id);
            continue;
        }

        /* Set title cache entry pointer. */
        g_titleCache[g_titleCacheCount + extra_entry_count] = cache_entry;

        /* Increase extra title cache entry counter. */
        extra_entry_count++;
    }

    /* Check retrieved title cache entry count. */
    if (!extra_entry_count)
    {
        LOG_MSG_ERROR("Unable to generate title cache entries! (%u element[s]).", entry_count);
        goto end;
    }

    /* Update title cache entry count. */
    g_titleCacheCount += extra_entry_count;

    /* Free extra allocated pointers if we didn't use them. */
    if (extra_entry_count < entry_count) titleCacheReallocateCacheEntryArray(0, false);

    /* Sort title cache entries by title ID. */
    if (g_titleCacheCount > 1) qsort(g_titleCache, g_titleCacheCount, sizeof(TitleCacheEntry*), &titleCacheEntrySortFunction);

    /* Update flag. */
    success = true;

end:
    /* Free previously allocated title cache entry pointers. Ignore return value. */
    if (!success && free_entries) titleCacheReallocateCacheEntryArray(extra_entry_count, true);

    return success;
}

static void titleCacheSaveFile(void)
{
    u8 *cache_file_data = NULL;

    size_t full_cache_size = (sizeof(TitleCacheFileHeader) + ((size_t)g_titleCacheCount * sizeof(TitleCacheFileEntry)));
    size_t cur_offset = 0;

    TitleCacheFileEntry *cache_file_entries = NULL;

    FILE *cache_file = NULL;

    bool success = false;

    if (!g_titleCacheInit || !g_titleCache || !g_titleCacheCount || !g_cacheFlushRequired)
    {
        //LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    /* Allocate memory for the title cache file (minus the data blobs). */
    cache_file_data = calloc(1, full_cache_size);
    if (!cache_file_data)
    {
        LOG_MSG_ERROR("Failed to allocate 0x%lX byte-long block for the title cache file data!", full_cache_size);
        goto end;
    }

    /* Populate title cache file header. */
    if (!titleCachePopulateFileHeader(cache_file_data, &cur_offset)) goto end;

    /* Serialize data blobs. */
    if (!(cache_file_entries = titleCacheSerializeDataBlobs(&cache_file_data, &cur_offset))) goto end;

    /* Sanity check: make sure the calculated total file size is equal to the current offset. */
    for(u32 i = 0; i < g_titleCacheCount; i++) full_cache_size += ALIGN_UP(cache_file_entries[i].blob_size, TITLE_CACHE_ALIGNMENT);

    if (full_cache_size != cur_offset)
    {
        LOG_MSG_ERROR("Title cache file size mismatch! (0x%lX != 0x%lX).", full_cache_size, cur_offset);
        goto end;
    }

    /* Open title cache file. */
    cache_file = fopen(TITLE_CACHE_PATH, "wb");
    if (!cache_file)
    {
        LOG_MSG_ERROR("Unable to open title cache at \"" TITLE_CACHE_PATH "\" for writing!");
        goto end;
    }

    /* Write full title cache file. */
    if (fwrite(cache_file_data, 1, full_cache_size, cache_file) != full_cache_size)
    {
        LOG_MSG_ERROR("Failed to write 0x%lX byte-long title cache file! (%d).", full_cache_size, errno);
        goto end;
    }

    /* Update flags. */
    g_cacheFlushRequired = false;
    success = true;

end:
    if (cache_file_data) free(cache_file_data);

    if (cache_file)
    {
        fclose(cache_file);
        if (!success) remove(TITLE_CACHE_PATH);
        utilsCommitSdCardFileSystemChanges();
    }
}

static bool titleCachePopulateFileHeader(u8 *cache_file_data, size_t *out_cur_offset)
{
    if (!g_titleCacheCount || !cache_file_data || !out_cur_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Populate title cache file header. */
    TitleCacheFileHeader *cache_file_header = (TitleCacheFileHeader*)(cache_file_data + *out_cur_offset);

    memset(cache_file_header, 0, sizeof(TitleCacheFileHeader));

    cache_file_header->magic = __builtin_bswap32(TITLE_CACHE_MAGIC);
    cache_file_header->version = TITLE_CACHE_VERSION;
    cache_file_header->language = (u8)g_systemLanguage;
    cache_file_header->entry_count = g_titleCacheCount;

    /* Update current offset. */
    *out_cur_offset += sizeof(TitleCacheFileHeader);

    return true;
}

static TitleCacheFileEntry *titleCacheSerializeDataBlobs(u8 **cache_file_data, size_t *out_cur_offset)
{
    if (!g_titleCacheCount || !cache_file_data || !*cache_file_data || !out_cur_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    u32 cache_file_entries_offset = *out_cur_offset, cur_blob_offset = 0;
    TitleCacheFileEntry *cache_file_entries = (TitleCacheFileEntry*)(*cache_file_data + cache_file_entries_offset);
    bool success = false;

    /* Update current offset. */
    *out_cur_offset += ((size_t)g_titleCacheCount * sizeof(TitleCacheFileEntry));

    /* Populate title cache file entries and generate data blobs. */
    for(u32 i = 0; i < g_titleCacheCount; i++)
    {
        TitleCacheEntry *cur_cache_entry = g_titleCache[i];
        TitleCacheFileEntry *cur_cache_file_entry = &(cache_file_entries[i]);

        /* Append data blob for the current title cache file entry to our buffer. */
        /* Fair warning: this will reallocate our title cache file buffer. */
        if (!(success = titleCacheAppendDataBlobToFileCacheBuffer(cache_file_data, cur_cache_entry, cur_cache_file_entry, &cur_blob_offset, out_cur_offset))) break;

        /* Update pointer to title cache file entries. */
        cache_file_entries = (TitleCacheFileEntry*)(*cache_file_data + cache_file_entries_offset);

        //cur_cache_file_entry = &(cache_file_entries[i]);
        //LOG_DATA_DEBUG(cur_cache_file_entry, sizeof(TitleCacheFileEntry), "Title cache file entry #%u (%p):", i, cur_cache_file_entry);
    }

    return (success ? cache_file_entries : NULL);
}

static TitleCacheEntry *titleCacheGenerateCacheEntryFromFileEntry(u8 *cache_file_data, const TitleCacheFileEntry *cache_file_entry, size_t *out_cur_offset)
{
    if (!cache_file_data || !cache_file_entry || !out_cur_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    u8 *data_blob = (cache_file_data + *out_cur_offset);

    TitleCacheEntry *out = NULL;
    size_t data_offset = 0;

    bool success = true;

    /* Allocate memory for the output title cache entry. */
    out = malloc(sizeof(TitleCacheEntry));
    if (!out)
    {
        LOG_MSG_ERROR("Failed to allocate memory for the output title cache entry! (title %016lX).", cache_file_entry->title_id);
        goto end;
    }

    /* Populate title cache entry fields. */
    out->title_id = cache_file_entry->title_id;

    out->name = strndup((char*)data_blob + data_offset, cache_file_entry->name_len);
    data_offset += ALIGN_UP(cache_file_entry->name_len, TITLE_CACHE_ALIGNMENT);

    out->publisher = strndup((char*)data_blob + data_offset, cache_file_entry->publisher_len);
    data_offset += ALIGN_UP(cache_file_entry->publisher_len, TITLE_CACHE_ALIGNMENT);

    out->icon_size = cache_file_entry->icon_size;

    out->icon_data = malloc(out->icon_size);
    if (out->icon_data) memcpy(out->icon_data, data_blob + data_offset, out->icon_size);

    if (!out->name || !out->publisher || !out->icon_data)
    {
        LOG_MSG_ERROR("Failed to populate output title cache entry! (title %016lX).", cache_file_entry->title_id);
        goto end;
    }

    /* Update current offset. */
    /* Make sure to skip over data blob padding if the data blob size is unaligned. */
    *out_cur_offset += ALIGN_UP(cache_file_entry->blob_size, TITLE_CACHE_ALIGNMENT);

    /* Update flag. */
    success = true;

end:
    if (!success && out) titleCacheFreeCacheEntry(&out);

    return out;
}

static TitleCacheEntry *titleCacheGenerateCacheEntryFromApplicationMetadata(TitleApplicationMetadata *app_metadata)
{
    if (!app_metadata || !app_metadata->title_id || !app_metadata->icon_size || app_metadata->icon_size > NACP_MAX_ICON_SIZE || !app_metadata->icon)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    NacpLanguageEntry *lang_entry = &(app_metadata->lang_entry);
    TitleCacheEntry *out = NULL;
    bool success = true;

    /* Allocate memory for the output title cache entry. */
    out = malloc(sizeof(TitleCacheEntry));
    if (!out)
    {
        LOG_MSG_ERROR("Failed to allocate memory for the output title cache entry! (title %016lX).", app_metadata->title_id);
        goto end;
    }

    /* Populate title cache entry fields. */
    out->title_id = app_metadata->title_id;
    out->name = strndup(lang_entry->name, sizeof(lang_entry->name));
    out->publisher = strndup(lang_entry->author, sizeof(lang_entry->author));
    out->icon_size = app_metadata->icon_size;

    out->icon_data = malloc(out->icon_size);
    if (out->icon_data) memcpy(out->icon_data, app_metadata->icon, out->icon_size);

    if (!out->name || !out->publisher || !out->icon_data)
    {
        LOG_MSG_ERROR("Failed to populate output title cache entry! (title %016lX).", app_metadata->title_id);
        goto end;
    }

    /* Update flag. */
    success = true;

end:
    if (!success && out) titleCacheFreeCacheEntry(&out);

    return out;
}

NX_INLINE void titleCacheFreeCacheEntry(TitleCacheEntry **cache_entry)
{
    TitleCacheEntry *ptr = NULL;

    if (!cache_entry || !(ptr = *cache_entry)) return;

    if (ptr->name) free(ptr->name);
    if (ptr->publisher) free(ptr->publisher);
    if (ptr->icon_data) free(ptr->icon_data);

    free(ptr);
    *cache_entry = NULL;
}

static bool titleCacheReallocateCacheEntryArray(u32 extra_entry_count, bool free_entries)
{
    TitleCacheEntry **tmp_title_cache = NULL;
    u32 realloc_entry_count = (!free_entries ? (g_titleCacheCount + extra_entry_count) : g_titleCacheCount);
    bool success = false;

    if (free_entries)
    {
        if (!g_titleCache)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            goto end;
        }

        /* Free previously allocated title cache entries. */
        for(u32 i = 0; i <= extra_entry_count; i++) titleCacheFreeCacheEntry(&(g_titleCache[g_titleCacheCount + i]));
    }

    if (realloc_entry_count)
    {
        /* Reallocate title cache entry pointer array. */
        tmp_title_cache = realloc(g_titleCache, realloc_entry_count * sizeof(TitleCacheEntry*));
        if (tmp_title_cache)
        {
            /* Update title cache entry pointer. */
            g_titleCache = tmp_title_cache;
            tmp_title_cache = NULL;

            /* Clear new title cache entry pointer array area (if needed). */
            if (realloc_entry_count > g_titleCacheCount) memset(g_titleCache + g_titleCacheCount, 0, extra_entry_count * sizeof(TitleCacheEntry*));
        } else {
            LOG_MSG_ERROR("Failed to reallocate title cache entry pointer array! (%u element[s]).", realloc_entry_count);
            goto end;
        }
    } else
    if (g_titleCache)
    {
        /* Free title cache entry pointer array. */
        free(g_titleCache);
        g_titleCache = NULL;
    }

    /* Update flag. */
    success = true;

end:
    return success;
}

static bool titleCacheAppendDataBlobToFileCacheBuffer(u8 **cache_file_data, const TitleCacheEntry *cache_entry, TitleCacheFileEntry *cache_file_entry, u32 *out_blob_offset, size_t *out_cur_offset)
{
    if (!cache_file_data || !*cache_file_data || !cache_entry || !cache_file_entry || !out_blob_offset || !IS_ALIGNED(*out_blob_offset, TITLE_CACHE_ALIGNMENT) || !out_cur_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u8 *ptr = NULL;
    size_t cache_file_entry_offset = (size_t)((u8*)cache_file_entry - *cache_file_data);
    u32 data_offset = 0, aligned_blob_size = 0;

    bool success = false;

    /* Populate current title cache file entry. */
    cache_file_entry->title_id = cache_entry->title_id;
    cache_file_entry->name_len = (u16)strlen(cache_entry->name);
    cache_file_entry->publisher_len = (u16)strlen(cache_entry->publisher);
    cache_file_entry->icon_size = cache_entry->icon_size;
    cache_file_entry->blob_offset = *out_blob_offset;
    cache_file_entry->blob_size = titleCacheCalculateDataBlobSize(cache_file_entry->name_len, cache_file_entry->publisher_len, cache_file_entry->icon_size);

    /* Update title cache file entry checksum. */
    cache_file_entry->crc = 0;
    cache_file_entry->crc = crc32Calculate(cache_file_entry, sizeof(TitleCacheFileEntry));

    /* Reallocate title cache file data buffer to append blob data. */
    aligned_blob_size = ALIGN_UP(cache_file_entry->blob_size, TITLE_CACHE_ALIGNMENT);
    ptr = realloc(*cache_file_data, *out_cur_offset + aligned_blob_size);
    if (!ptr)
    {
        LOG_MSG_ERROR("Failed to reallocate title cache file data buffer!");
        goto end;
    }

    *cache_file_data = ptr;

    /* Adjust pointers after buffer reallocation. */
    cache_file_entry = (TitleCacheFileEntry*)(ptr + cache_file_entry_offset);
    ptr += *out_cur_offset;

    /* Populate data blob. */
    memset(ptr + data_offset, 0, ALIGN_UP(cache_file_entry->name_len, TITLE_CACHE_ALIGNMENT));
    memcpy(ptr + data_offset, cache_entry->name, cache_file_entry->name_len);
    data_offset += ALIGN_UP(cache_file_entry->name_len, TITLE_CACHE_ALIGNMENT);

    memset(ptr + data_offset, 0, ALIGN_UP(cache_file_entry->publisher_len, TITLE_CACHE_ALIGNMENT));
    memcpy(ptr + data_offset, cache_entry->publisher, cache_file_entry->publisher_len);
    data_offset += ALIGN_UP(cache_file_entry->publisher_len, TITLE_CACHE_ALIGNMENT);

    memcpy(ptr + data_offset, cache_entry->icon_data, cache_entry->icon_size);

    /* Update current offsets. */
    /* Make sure to add data blob padding (always needed if the data blob size is unaligned). */
    *out_cur_offset += aligned_blob_size;

    if (aligned_blob_size > cache_file_entry->blob_size)
    {
        u32 padding = (aligned_blob_size - cache_file_entry->blob_size);
        memset(ptr + cache_file_entry->blob_size, 0, padding);
    }

    *out_blob_offset += aligned_blob_size;

    /* Update flag. */
    success = true;

end:
    return success;
}

NX_INLINE u32 titleCacheCalculateDataBlobSize(u16 name_len, u16 publisher_len, u32 icon_size)
{
    if (!name_len || !publisher_len || !icon_size) return 0;
    return (icon_size + ALIGN_UP(name_len, TITLE_CACHE_ALIGNMENT) + ALIGN_UP(publisher_len, TITLE_CACHE_ALIGNMENT));
}

static TitleCacheEntry *titleCacheGetEntryById(u64 title_id)
{
    if (!g_titleCacheInit || !g_titleCache || !g_titleCacheCount || !title_id) return NULL;

    for(u32 i = 0; i < g_titleCacheCount; i++)
    {
        TitleCacheEntry *cur_cache_entry = g_titleCache[i];
        if (cur_cache_entry && cur_cache_entry->title_id == title_id) return cur_cache_entry;
    }

    return NULL;
}

static int titleCacheEntrySortFunction(const void *a, const void *b)
{
    const TitleCacheEntry *title_cache_entry_1 = *((const TitleCacheEntry**)a);
    const TitleCacheEntry *title_cache_entry_2 = *((const TitleCacheEntry**)b);

    if (title_cache_entry_1->title_id < title_cache_entry_2->title_id)
    {
        return -1;
    } else
    if (title_cache_entry_1->title_id > title_cache_entry_2->title_id)
    {
        return 1;
    }

    return 0;
}
