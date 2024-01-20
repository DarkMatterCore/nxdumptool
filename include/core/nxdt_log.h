/*
 * nxdt_log.h
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

#pragma once

#ifndef __NXDT_LOG_H__
#define __NXDT_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Used to control logfile verbosity.
#define LOG_LEVEL_DEBUG     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_WARNING   2
#define LOG_LEVEL_ERROR     3
#define LOG_LEVEL_NONE      4

/// Defines the log level used throughout the application.
/// Log messages with a log value lower than this one won't be compiled into the binary.
/// If a value lower than LOG_LEVEL_DEBUG or equal to/greater than LOG_LEVEL_NONE is used, logfile output will be entirely disabled.
#define LOG_LEVEL           LOG_LEVEL_DEBUG /* TODO: change before release (warning?). */

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG) && (LOG_LEVEL < LOG_LEVEL_NONE)

/// Helper macros.

#define LOG_MSG_GENERIC(level, fmt, ...)                    logWriteFormattedStringToLogFile(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF_GENERIC(dst, dst_size, level, fmt, ...) logWriteFormattedStringToBuffer(dst, dst_size, level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_DATA_GENERIC(data, data_size, level, fmt, ...)  logWriteBinaryDataToLogFile(data, data_size, level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#if LOG_LEVEL == LOG_LEVEL_DEBUG
#define LOG_MSG_DEBUG(fmt, ...)                             LOG_MSG_GENERIC(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF_DEBUG(dst, dst_size, fmt, ...)          LOG_MSG_BUF_GENERIC(dst, dst_size, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_DATA_DEBUG(data, data_size, fmt, ...)           LOG_DATA_GENERIC(data, data_size, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#else
#define LOG_MSG_DEBUG(fmt, ...)                             do {} while(0)
#define LOG_MSG_BUF_DEBUG(dst, dst_size, fmt, ...)          do {} while(0)
#define LOG_DATA_DEBUG(data, data_size, fmt, ...)           do {} while(0)
#endif  /* LOG_LEVEL == LOG_LEVEL_DEBUG */

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_MSG_INFO(fmt, ...)                              LOG_MSG_GENERIC(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF_INFO(dst, dst_size, fmt, ...)           LOG_MSG_BUF_GENERIC(dst, dst_size, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DATA_INFO(data, data_size, fmt, ...)            LOG_DATA_GENERIC(data, data_size, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#else
#define LOG_MSG_INFO(fmt, ...)                              do {} while(0)
#define LOG_MSG_BUF_INFO(dst, dst_size, fmt, ...)           do {} while(0)
#define LOG_DATA_INFO(data, data_size, fmt, ...)            do {} while(0)
#endif  /* LOG_LEVEL <= LOG_LEVEL_INFO */

#if LOG_LEVEL <= LOG_LEVEL_WARNING
#define LOG_MSG_WARNING(fmt, ...)                           LOG_MSG_GENERIC(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF_WARNING(dst, dst_size, fmt, ...)        LOG_MSG_BUF_GENERIC(dst, dst_size, LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_DATA_WARNING(data, data_size, fmt, ...)         LOG_DATA_GENERIC(data, data_size, LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#else
#define LOG_MSG_WARNING(fmt, ...)                           do {} while(0)
#define LOG_MSG_BUF_WARNING(dst, dst_size, fmt, ...)        do {} while(0)
#define LOG_DATA_WARNING(data, data_size, fmt, ...)         do {} while(0)
#endif  /* LOG_LEVEL <= LOG_LEVEL_WARNING */

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_MSG_ERROR(fmt, ...)                             LOG_MSG_GENERIC(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF_ERROR(dst, dst_size, fmt, ...)          LOG_MSG_BUF_GENERIC(dst, dst_size, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DATA_ERROR(data, data_size, fmt, ...)           LOG_DATA_GENERIC(data, data_size, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#else
#define LOG_MSG_ERROR(fmt, ...)                             do {} while(0)
#define LOG_MSG_BUF_ERROR(dst, dst_size, fmt, ...)          do {} while(0)
#define LOG_DATA_ERROR(data, data_size, fmt, ...)           do {} while(0)
#endif  /* LOG_LEVEL <= LOG_LEVEL_ERROR */

/// Writes the provided string to the logfile.
/// If the logfile hasn't been created and/or opened, this function takes care of it.
void logWriteStringToLogFile(const char *src);

/// Writes a formatted log string to the logfile.
/// If the logfile hasn't been created and/or opened, this function takes care of it.
__attribute__((format(printf, 5, 6))) void logWriteFormattedStringToLogFile(u8 level, const char *file_name, int line, const char *func_name, const char *fmt, ...);

/// Writes a formatted log string to the provided buffer.
/// If the buffer isn't big enough to hold both its current contents and the new formatted string, it will be resized.
__attribute__((format(printf, 7, 8))) void logWriteFormattedStringToBuffer(char **dst, size_t *dst_size, u8 level, const char *file_name, int line, const char *func_name, const char *fmt, ...);

/// Writes a formatted log string + a hex string representation of the provided binary data to the logfile.
/// If the logfile hasn't been created and/or opened, this function takes care of it.
__attribute__((format(printf, 7, 8))) void logWriteBinaryDataToLogFile(const void *data, size_t data_size, u8 level, const char *file_name, int line, const char *func_name, const char *fmt, ...);

/// Forces a flush operation on the logfile.
void logFlushLogFile(void);

/// Write any pending data to the logfile, flushes it and then closes it.
void logCloseLogFile(void);

/// Returns a pointer to a dynamically allocated buffer that holds the last error message string, or NULL if there's none.
/// The allocated buffer must be freed by the calling function using free().
char *logGetLastMessage(void);

/// (Un)locks the log mutex. Can be used to block other threads and prevent them from writing data to the logfile.
/// Use with caution.
void logControlMutex(bool lock);

#else   /* (LOG_LEVEL >= LOG_LEVEL_DEBUG) && (LOG_LEVEL < LOG_LEVEL_NONE) */

/// Helper macros.

#define LOG_MSG_GENERIC(level, fmt, ...)                    do {} while(0)
#define LOG_MSG_BUF_GENERIC(dst, dst_size, level, fmt, ...) do {} while(0)
#define LOG_DATA_GENERIC(data, data_size, level, fmt, ...)  do {} while(0)

#define LOG_MSG_DEBUG(fmt, ...)                             do {} while(0)
#define LOG_MSG_BUF_DEBUG(dst, dst_size, fmt, ...)          do {} while(0)
#define LOG_DATA_DEBUG(data, data_size, fmt, ...)           do {} while(0)

#define LOG_MSG_INFO(fmt, ...)                              do {} while(0)
#define LOG_MSG_BUF_INFO(dst, dst_size, fmt, ...)           do {} while(0)
#define LOG_DATA_INFO(data, data_size, fmt, ...)            do {} while(0)

#define LOG_MSG_WARNING(fmt, ...)                           do {} while(0)
#define LOG_MSG_BUF_WARNING(dst, dst_size, fmt, ...)        do {} while(0)
#define LOG_DATA_WARNING(data, data_size, fmt, ...)         do {} while(0)

#define LOG_MSG_ERROR(fmt, ...)                             do {} while(0)
#define LOG_MSG_BUF_ERROR(dst, dst_size, fmt, ...)          do {} while(0)
#define LOG_DATA_ERROR(data, data_size, fmt, ...)           do {} while(0)

#endif  /* (LOG_LEVEL >= LOG_LEVEL_DEBUG) && (LOG_LEVEL < LOG_LEVEL_NONE) */

#ifdef __cplusplus
}
#endif

#endif /* __NXDT_LOG_H__ */
