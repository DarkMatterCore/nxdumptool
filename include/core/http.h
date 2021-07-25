/*
 * http.h
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

#ifndef __HTTP_H__
#define __HTTP_H__

#include <curl/curl.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Callback definition to write downloaded data.
typedef curl_write_callback HttpWriteCallback;

/// Callback definition to handle progress updates.
typedef curl_xferinfo_callback HttpProgressCallback;

/// Used by httpWriteBufferCallback().
typedef struct {
    char *data;     ///< Dynamically allocated buffer.
    size_t size;    ///< Buffer size.
} HttpBuffer;

/// Initializes the HTTP client interface.
bool httpInitialize(void);

/// Closes the HTTP client interface.
void httpExit(void);

/// Writes downloaded data to an output file. May be used as the write callback for httpPerformGetRequest().
/// Expects 'outstream' / 'write_ptr' to be a FILE pointer.
size_t httpWriteFileCallback(char *buffer, size_t size, size_t nitems, void *outstream);

/// Writes downloaded data to an output buffer. May be used as the write callback for httpPerformGetRequest().
/// Expects 'outstream' / 'write_ptr' to be a pointer to a HttpBuffer element. Its 'data' member is reallocated throughout the download process.
size_t httpWriteBufferCallback(char *buffer, size_t size, size_t nitems, void *outstream);

/// Performs a HTTP GET request. Blocks the calling thread until the whole transfer is complete.
/// Callbacks are optional, but they should be provided to save downloaded data and/or handle progress updates.
/// If 'outsize' is provided, the download size will be stored in it if the request succeeds.
bool httpPerformGetRequest(const char *url, bool force_https, size_t *outsize, HttpWriteCallback write_cb, void *write_ptr, HttpProgressCallback progress_cb, void *progress_ptr);

/// Wrapper for httpPerformGetRequest() + httpWriteFileCallback() that opens/closes the output file on its own.
/// Returns false if the request fails.
bool httpDownloadFile(const char *path, const char *url, bool force_https, HttpProgressCallback progress_cb, void *progress_ptr);

/// Wrapper for httpPerformGetRequest() + httpWriteBufferCallback() that manages a HttpBuffer element on its own.
/// Returns a pointer to a dynamically allocated buffer that holds the downloaded data, which must be freed by the user.
/// Providing 'outsize' is mandatory. Returns NULL if the request fails.
char *httpDownloadData(size_t *outsize, const char *url, bool force_https, HttpProgressCallback progress_cb, void *progress_ptr);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_H__ */
