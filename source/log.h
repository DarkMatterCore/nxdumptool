/*
 * log.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __LOG_H__
#define __LOG_H__

/// Helper macros.
#define LOG_MSG(fmt, ...)                       logWriteFormattedStringToLogFile(__func__, fmt, ##__VA_ARGS__)
#define LOG_MSG_BUF(dst, dst_size, fmt, ...)    logWriteFormattedStringToBuffer(dst, dst_size, __func__, fmt, ##__VA_ARGS__)
#define LOG_DATA(data, data_size, fmt, ...)     logWriteBinaryDataToLogFile(data, data_size, __func__, fmt, ##__VA_ARGS__)

/// Writes the provided string to the logfile.
void logWriteStringToLogFile(const char *src);

/// Writes a formatted log string to the logfile.
void logWriteFormattedStringToLogFile(const char *func_name, const char *fmt, ...);

/// Writes a formatted log string to the provided buffer.
/// If the buffer isn't big enough to hold both its current contents and the new formatted string, it will be resized.
void logWriteFormattedStringToBuffer(char **dst, size_t *dst_size, const char *func_name, const char *fmt, ...);

/// Writes a formatted log string + a hex string representation of the provided binary data to the logfile.
void logWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...);

/// Forces a flush operation on the logfile.
void logFlushLogFile(void);

/// Closes the logfile.
void logCloseLogFile(void);

/// (Un)locks the log mutex. Can be used to block other threads and prevent them from writing data to the logfile.
/// Use with caution.
void logControlMutex(bool lock);

#endif /* __LOG_H__ */
