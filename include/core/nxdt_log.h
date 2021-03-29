/*
 * nxdt_log.h
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

#pragma once

#ifndef __NXDT_LOG_H__
#define __NXDT_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Helper macros.
#define LOG_MSG(fmt, ...)                       logWriteFormattedStringToLogFile(__func__, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF(dst, dst_size, fmt, ...)    logWriteFormattedStringToBuffer(dst, dst_size, __func__, fmt, ##__VA_ARGS__)
#define LOG_DATA(data, data_size, fmt, ...)     logWriteBinaryDataToLogFile(data, data_size, __func__, fmt, ##__VA_ARGS__)

/// Writes the provided string to the logfile.
void logWriteStringToLogFile(const char *src);

/// Writes a formatted log string to the logfile.
__attribute__((format(printf, 2, 3))) void logWriteFormattedStringToLogFile(const char *func_name, const char *fmt, ...);

/// Writes a formatted log string to the provided buffer.
/// If the buffer isn't big enough to hold both its current contents and the new formatted string, it will be resized.
__attribute__((format(printf, 4, 5))) void logWriteFormattedStringToBuffer(char **dst, size_t *dst_size, const char *func_name, const char *fmt, ...);

/// Writes a formatted log string + a hex string representation of the provided binary data to the logfile.
__attribute__((format(printf, 4, 5))) void logWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...);

/// Forces a flush operation on the logfile.
void logFlushLogFile(void);

/// Closes the logfile.
void logCloseLogFile(void);

/// Stores the last log message in the provided buffer.
void logGetLastMessage(char *dst, size_t dst_size);

/// (Un)locks the log mutex. Can be used to block other threads and prevent them from writing data to the logfile.
/// Use with caution.
void logControlMutex(bool lock);

#ifdef __cplusplus
}
#endif

#endif /* __NXDT_LOG_H__ */
