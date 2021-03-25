/*
 * log.c
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include "utils.h"

#define LOG_FILE_NAME   APP_TITLE ".log"
#define LOG_BUF_SIZE    0x400000            /* 4 MiB. */
#define LOG_FORCE_FLUSH 0                   /* Forces a log buffer flush each time the logfile is written to. */

/* Global variables. */

static Mutex g_logMutex = 0;

static char g_lastLogMsg[0x100] = {0};

static FsFile g_logFile = {0};
static s64 g_logFileOffset = 0;

static char *g_logBuffer = NULL;
static size_t g_logBufferLength = 0;

static const char *g_utf8Bom = "\xEF\xBB\xBF";
static const char *g_logStrFormat = "[%d-%02d-%02d %02d:%02d:%02d.%lu] %s -> ";
static const char *g_logLineBreak = "\r\n";

/* Function prototypes. */

static void _logWriteStringToLogFile(const char *src, bool lock);
static void _logWriteFormattedStringToLogFile(bool save, const char *func_name, const char *fmt, va_list args, bool lock);

static void _logFlushLogFile(bool lock);

static bool logAllocateLogBuffer(void);
static bool logOpenLogFile(void);

void logWriteStringToLogFile(const char *src)
{
    _logWriteStringToLogFile(src, true);
}

void logWriteFormattedStringToLogFile(const char *func_name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _logWriteFormattedStringToLogFile(true, func_name, fmt, args, true);
    va_end(args);
}

void logWriteFormattedStringToBuffer(char **dst, size_t *dst_size, const char *func_name, const char *fmt, ...)
{
    if (!dst || !dst_size || (!*dst && *dst_size) || (*dst && !*dst_size) || !func_name || !*func_name || !fmt || !*fmt) return;
    
    va_list args;
    
    int str1_len = 0, str2_len = 0;
    size_t log_str_len = 0;
    
    char *dst_ptr = *dst, *tmp_str = NULL;
    size_t dst_cur_size = *dst_size, dst_str_len = (dst_ptr ? strlen(dst_ptr) : 0);
    
    if (dst_str_len >= dst_cur_size) return;
    
    va_start(args, fmt);
    
    /* Get current time with nanosecond precision. */
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    
    /* Get local time. */
    struct tm *ts = localtime(&(now.tv_sec));
    ts->tm_year += 1900;
    ts->tm_mon++;
    
    /* Get formatted string length. */
    str1_len = snprintf(NULL, 0, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
    if (str1_len <= 0) goto end;
    
    str2_len = vsnprintf(NULL, 0, fmt, args);
    if (str2_len <= 0) goto end;
    
    log_str_len = (size_t)(str1_len + str2_len + 3);
    
    if (!dst_cur_size || log_str_len > (dst_cur_size - dst_str_len))
    {
        /* Update buffer size. */
        dst_cur_size = (dst_str_len + log_str_len);
        
        /* Reallocate buffer. */
        tmp_str = realloc(dst_ptr, dst_cur_size);
        if (!tmp_str) goto end;
        
        dst_ptr = tmp_str;
        tmp_str = NULL;
        
        /* Clear allocated area. */
        memset(dst_ptr + dst_str_len, 0, log_str_len);
        
        /* Update pointers. */
        *dst = dst_ptr;
        *dst_size = dst_cur_size;
    }
    
    /* Generate formatted string. */
    sprintf(dst_ptr + dst_str_len, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
    vsprintf(dst_ptr + dst_str_len + (size_t)str1_len, fmt, args);
    strcat(dst_ptr, g_logLineBreak);
    
end:
    va_end(args);
}

void logWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...)
{
    if (!data || !data_size || !func_name || !*func_name || !fmt || !*fmt) return;
    
    va_list args;
    size_t data_str_size = ((data_size * 2) + 3);
    char *data_str = NULL;
    
    mutexLock(&g_logMutex);
    
    /* Allocate memory for the hex string representation of the provided binary data. */
    data_str = calloc(data_str_size, sizeof(char));
    if (!data_str) goto end;
    
    /* Generate hex string representation. */
    utilsGenerateHexStringFromData(data_str, data_str_size, data, data_size);
    strcat(data_str, g_logLineBreak);
    
    /* Write formatted string. */
    va_start(args, fmt);
    _logWriteFormattedStringToLogFile(false, func_name, fmt, args, false);
    va_end(args);
    
    /* Write hex string representation. */
    _logWriteStringToLogFile(data_str, false);
    
end:
    if (data_str) free(data_str);
    
    mutexUnlock(&g_logMutex);
}

void logFlushLogFile(void)
{
    _logFlushLogFile(true);
}

void logCloseLogFile(void)
{
    mutexLock(&g_logMutex);
    
    /* Flush log buffer. */
    _logFlushLogFile(false);
    
    /* Close logfile. */
    if (serviceIsActive(&(g_logFile.s)))
    {
        fsFileClose(&g_logFile);
        memset(&g_logFile, 0, sizeof(FsFile));
        
        /* Commit SD card filesystem changes. */
        utilsCommitSdCardFileSystemChanges();
    }
    
    /* Free log buffer. */
    if (g_logBuffer)
    {
        free(g_logBuffer);
        g_logBuffer = NULL;
    }
    
    g_logFileOffset = 0;
    
    mutexUnlock(&g_logMutex);
}

void logGetLastMessage(char *dst, size_t dst_size)
{
    mutexLock(&g_logMutex);
    if (dst && dst_size > 1 && *g_lastLogMsg) snprintf(dst, dst_size, "%s", g_lastLogMsg);
    mutexUnlock(&g_logMutex);
}

void logControlMutex(bool lock)
{
    if (lock)
    {
        mutexLock(&g_logMutex);
    } else {
        mutexUnlock(&g_logMutex);
    }
}

static void _logWriteStringToLogFile(const char *src, bool lock)
{
    if (!src || !*src) return;
    
    if (lock) mutexLock(&g_logMutex);
    
    Result rc = 0;
    size_t src_len = strlen(src), tmp_len = 0;
    
    /* Make sure we have allocated memory for the log buffer and opened the logfile. */
    if (!logAllocateLogBuffer() || !logOpenLogFile()) goto end;
    
    /* Check if the formatted string length is lower than the log buffer size. */
    if (src_len < LOG_BUF_SIZE)
    {
        /* Flush log buffer contents (if needed). */
        if ((g_logBufferLength + src_len) >= LOG_BUF_SIZE)
        {
            _logFlushLogFile(false);
            if (g_logBufferLength) goto end;
        }
        
        /* Copy string into the log buffer. */
        strcpy(g_logBuffer + g_logBufferLength, src);
        g_logBufferLength += src_len;
    } else {
        /* Flush log buffer. */
        _logFlushLogFile(false);
        if (g_logBufferLength) goto end;
        
        /* Write string data until it no longer exceeds the log buffer size. */
        while(src_len >= LOG_BUF_SIZE)
        {
            rc = fsFileWrite(&g_logFile, g_logFileOffset, src + tmp_len, LOG_BUF_SIZE, FsWriteOption_Flush);
            if (R_FAILED(rc)) goto end;
            
            g_logFileOffset += LOG_BUF_SIZE;
            tmp_len += LOG_BUF_SIZE;
            src_len -= LOG_BUF_SIZE;
        }
        
        /* Copy any remaining data from the string into the log buffer. */
        if (src_len)
        {
            strcpy(g_logBuffer, src + tmp_len);
            g_logBufferLength = src_len;
        }
    }
    
#if LOG_FORCE_FLUSH == 1
    /* Flush log buffer. */
    _logFlushLogFile(false);
#endif
    
end:
    if (lock) mutexUnlock(&g_logMutex);
}

static void _logWriteFormattedStringToLogFile(bool save, const char *func_name, const char *fmt, va_list args, bool lock)
{
    if (!func_name || !*func_name || !fmt || !*fmt) return;
    
    if (lock) mutexLock(&g_logMutex);
    
    Result rc = 0;
    
    int str1_len = 0, str2_len = 0;
    size_t log_str_len = 0;
    
    char *tmp_str = NULL;
    size_t tmp_len = 0;
    
    /* Get current time with nanosecond precision. */
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    
    /* Get local time. */
    struct tm *ts = localtime(&(now.tv_sec));
    ts->tm_year += 1900;
    ts->tm_mon++;
    
    /* Get formatted string length. */
    str1_len = snprintf(NULL, 0, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
    if (str1_len <= 0) goto end;
    
    str2_len = vsnprintf(NULL, 0, fmt, args);
    if (str2_len <= 0) goto end;
    
    log_str_len = (size_t)(str1_len + str2_len + 2);
    
    /* Save log message to our global stack buffer (if needed). */
    if (save)
    {
        tmp_len = (strlen(func_name) + 2);
        if ((tmp_len + (size_t)str2_len) < sizeof(g_lastLogMsg))
        {
            sprintf(g_lastLogMsg, "%s: ", func_name);
            vsprintf(g_lastLogMsg + tmp_len, fmt, args);
        }
        
        tmp_len = 0;
    }
    
    /* Make sure we have allocated memory for the log buffer and opened the logfile. */
    if (!logAllocateLogBuffer() || !logOpenLogFile()) goto end;
    
    /* Check if the formatted string length is less than the log buffer size. */
    if (log_str_len < LOG_BUF_SIZE)
    {
        /* Flush log buffer contents (if needed). */
        if ((g_logBufferLength + log_str_len) >= LOG_BUF_SIZE)
        {
            _logFlushLogFile(false);
            if (g_logBufferLength) goto end;
        }
        
        /* Nice and easy string formatting using the log buffer. */
        sprintf(g_logBuffer + g_logBufferLength, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
        vsprintf(g_logBuffer + g_logBufferLength + (size_t)str1_len, fmt, args);
        strcat(g_logBuffer, g_logLineBreak);
        g_logBufferLength += log_str_len;
    } else {
        /* Flush log buffer. */
        _logFlushLogFile(false);
        if (g_logBufferLength) goto end;
        
        /* Allocate memory for a temporary buffer. This will hold the formatted string. */
        tmp_str = calloc(log_str_len + 1, sizeof(char));
        if (!tmp_str) goto end;
        
        /* Generate formatted string. */
        sprintf(tmp_str, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
        vsprintf(tmp_str + (size_t)str1_len, fmt, args);
        strcat(tmp_str, g_logLineBreak);
        
        /* Write formatted string data until it no longer exceeds the log buffer size. */
        while(log_str_len >= LOG_BUF_SIZE)
        {
            rc = fsFileWrite(&g_logFile, g_logFileOffset, tmp_str + tmp_len, LOG_BUF_SIZE, FsWriteOption_Flush);
            if (R_FAILED(rc)) goto end;
            
            g_logFileOffset += LOG_BUF_SIZE;
            tmp_len += LOG_BUF_SIZE;
            log_str_len -= LOG_BUF_SIZE;
        }
        
        /* Copy any remaining data from the formatted string into the log buffer. */
        if (log_str_len)
        {
            strcpy(g_logBuffer, tmp_str + tmp_len);
            g_logBufferLength = log_str_len;
        }
    }
    
#if LOG_FORCE_FLUSH == 1
    /* Flush log buffer. */
    _logFlushLogFile(false);
#endif
    
end:
    if (tmp_str) free(tmp_str);
    
    if (lock) mutexUnlock(&g_logMutex);
}

static void _logFlushLogFile(bool lock)
{
    if (lock) mutexLock(&g_logMutex);
    
    if (!serviceIsActive(&(g_logFile.s)) || !g_logBuffer || !g_logBufferLength) goto end;
    
    /* Write log buffer contents and flush the written data right away. */
    Result rc = fsFileWrite(&g_logFile, g_logFileOffset, g_logBuffer, g_logBufferLength, FsWriteOption_Flush);
    if (R_SUCCEEDED(rc))
    {
        /* Update global variables. */
        g_logFileOffset += (s64)g_logBufferLength;
        *g_logBuffer = '\0';
        g_logBufferLength = 0;
    }
    
end:
    if (lock) mutexUnlock(&g_logMutex);
}

static bool logAllocateLogBuffer(void)
{
    if (g_logBuffer) return true;
    g_logBuffer = memalign(LOG_BUF_SIZE, LOG_BUF_SIZE);
    return (g_logBuffer != NULL);
}

static bool logOpenLogFile(void)
{
    if (serviceIsActive(&(g_logFile.s))) return true;
    
    Result rc = 0;
    char path[FS_MAX_PATH] = {0}, *ptr1 = NULL, *ptr2 = NULL;
    
    /* Get SD card FsFileSystem object. */
    FsFileSystem *sdmc_fs = utilsGetSdCardFileSystemObject();
    if (!sdmc_fs) return false;
    
    /* Generate logfile path. */
    if (g_appLaunchPath)
    {
        ptr1 = strchr(g_appLaunchPath, '/');
        ptr2 = strrchr(g_appLaunchPath, '/');
        
        if (ptr1 != ptr2)
        {
            /* Create logfile in the current working directory. */
            snprintf(path, sizeof(path), "%.*s", (int)((ptr2 - ptr1) + 1), ptr1);
            
            size_t path_len = strlen(path);
            snprintf(path + path_len, sizeof(path) - path_len, LOG_FILE_NAME);
        } else {
            /* Create logfile in the SD card root directory. */
            sprintf(path, "/" LOG_FILE_NAME);
        }
    } else {
        /* Create logfile in the SD card root directory. */
        sprintf(path, "/" LOG_FILE_NAME);
    }
    
    /* Create file. This will fail if the logfile exists, so we don't check its return value. */
    fsFsCreateFile(sdmc_fs, path, 0, 0);
    
    /* Open file. */
    rc = fsFsOpenFile(sdmc_fs, path, FsOpenMode_Write | FsOpenMode_Append, &g_logFile);
    if (R_SUCCEEDED(rc))
    {
        /* Get file size. */
        rc = fsFileGetSize(&g_logFile, &g_logFileOffset);
        if (R_SUCCEEDED(rc))
        {
            /* Write UTF-8 BOM right away (if needed). */
            if (!g_logFileOffset)
            {
                size_t utf8_bom_len = strlen(g_utf8Bom);
                fsFileWrite(&g_logFile, g_logFileOffset, g_utf8Bom, utf8_bom_len, FsWriteOption_Flush);
                g_logFileOffset += (s64)utf8_bom_len;
            }
        } else {
            fsFileClose(&g_logFile);
        }
    }
    
    return R_SUCCEEDED(rc);
}
