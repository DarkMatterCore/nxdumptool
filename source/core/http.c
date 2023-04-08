/*
 * http.c
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
#include "http.h"

/* Global variables. */

static Mutex g_httpMutex = 0;
static bool g_httpInterfaceInit = false;

bool httpInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_httpMutex)
    {
        ret = g_httpInterfaceInit;
        if (ret) break;

        /* Initialize CURL. */
        CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
        if (res != CURLE_OK)
        {
            LOG_MSG_ERROR("%s", curl_easy_strerror(res));
            break;
        }

        /* Update flags. */
        ret = g_httpInterfaceInit = true;
    }

    return ret;
}

void httpExit(void)
{
    SCOPED_LOCK(&g_httpMutex)
    {
        /* Cleanup CURL. */
        curl_global_cleanup();

        /* Update flag. */
        g_httpInterfaceInit = false;
    }
}

size_t httpWriteFileCallback(char *buffer, size_t size, size_t nitems, void *outstream)
{
    size_t total_size = (size * nitems);
    return (total_size ? fwrite(buffer, 1, total_size, (FILE*)outstream) : 0);
}

size_t httpWriteBufferCallback(char *buffer, size_t size, size_t nitems, void *outstream)
{
    size_t total_size = (size * nitems);
    HttpBuffer *http_buffer = (HttpBuffer*)outstream;

    if (!total_size) return 0;

    char *data_tmp = realloc(http_buffer->data, http_buffer->size + total_size);
    if (!data_tmp) return 0;

    http_buffer->data = data_tmp;
    data_tmp = NULL;

    memcpy(http_buffer->data + http_buffer->size, buffer, total_size);
    http_buffer->size += total_size;

    return total_size;
}

bool httpPerformGetRequest(const char *url, bool force_https, size_t *outsize, HttpWriteCallback write_cb, void *write_ptr, HttpProgressCallback progress_cb, void *progress_ptr)
{
    bool ret = false;

    SCOPED_LOCK(&g_httpMutex)
    {
        if (!g_httpInterfaceInit || !url || !*url)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        CURL *curl = NULL;
        CURLcode res = CURLE_OK;
        long http_code = 0;
        curl_off_t download_size = 0, content_length = 0;
        char curl_err_buf[CURL_ERROR_SIZE] = {0};
        const char *error_str = NULL;

        /* Start CURL session. */
        curl = curl_easy_init();
        if (!curl)
        {
            LOG_MSG_ERROR("Failed to start CURL session for \"%s\"!", url);
            break;
        }

        /* Set CURL options. */
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_buf);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_TIMEOUT);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, HTTP_LOW_SPEED_LIMIT);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, HTTP_LOW_SPEED_TIME);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, HTTP_BUFFER_SIZE);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)(force_https ? CURL_HTTP_VERSION_2TLS : CURL_HTTP_VERSION_1_1));

        if (write_cb) curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        if (write_ptr) curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_ptr);

        if (progress_cb)
        {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        }

        if (progress_ptr) curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress_ptr);

        /* Perform GET request. */
        res = curl_easy_perform(curl);

        /* Get HTTP request properties. */
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &download_size);
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);

        /* End CURL session. */
        curl_easy_cleanup(curl);

        /* Update return value. */
        ret = (res == CURLE_OK && http_code >= 200 && http_code <= 299 && (content_length <= 0 || download_size == content_length));
        if (ret)
        {
            /* Update output size. */
            if (outsize) *outsize = (size_t)download_size;
        } else {
            LOG_MSG_ERROR("curl_easy_perform failed for \"%s\"! (res %d, HTTP code %ld, download %ld, length %ld).", url, res, http_code, download_size, content_length);
            if (res != CURLE_OK)
            {
                /* Log CURL error info. */
                if (*curl_err_buf)
                {
                    size_t curl_err_buf_len = strlen(curl_err_buf);

                    if (curl_err_buf[curl_err_buf_len - 1] == '\n') curl_err_buf[--curl_err_buf_len] = '\0';
                    if (curl_err_buf[curl_err_buf_len - 1] == '\r') curl_err_buf[--curl_err_buf_len] = '\0';

                    error_str = curl_err_buf;
                } else {
                    error_str = curl_easy_strerror(res);
                }

                if (error_str) LOG_MSG_INFO("CURL error info: \"%s\".", error_str);
            }
        }
    }

    return ret;
}

bool httpDownloadFile(const char *path, const char *url, bool force_https, HttpProgressCallback progress_cb, void *progress_ptr)
{
    if (!path || !*path)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    FILE *fd = NULL;
    bool ret = false;

    /* Open output file. */
    fd = fopen(path, "wb");
    if (!fd)
    {
        LOG_MSG_ERROR("Failed to open \"%s\" for writing!", path);
        return false;
    }

    /* Perform HTTP GET request. */
    ret = httpPerformGetRequest(url, force_https, NULL, httpWriteFileCallback, fd, progress_cb, progress_ptr);

    /* Close output file. */
    fclose(fd);

    /* Delete output file if the request failed. */
    if (!ret) remove(path);

    /* Commit SD card filesystem changes. */
    utilsCommitSdCardFileSystemChanges();

    return ret;
}

char *httpDownloadData(size_t *outsize, const char *url, bool force_https, HttpProgressCallback progress_cb, void *progress_ptr)
{
    if (!outsize)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    HttpBuffer http_buffer = {0};
    bool ret = false;

    /* Perform HTTP GET request. */
    ret = httpPerformGetRequest(url, force_https, outsize, httpWriteBufferCallback, &http_buffer, progress_cb, progress_ptr);

    /* Free output buffer if the request failed. */
    if (!ret && http_buffer.data)
    {
        free(http_buffer.data);
        http_buffer.data = NULL;
    }

    return http_buffer.data;
}
