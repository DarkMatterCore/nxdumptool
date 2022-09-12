/*
 * config.c
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "config.h"
#include "title.h"

#define CONFIG_VALIDATE_FIELD(type, name, ...) \
if (!strcmp(key, #name)) { \
    if (name##_found || !jsonValidate##type(val, ##__VA_ARGS__)) goto end; \
    name##_found = true; \
    continue; \
}

#define CONFIG_VALIDATE_OBJECT(type, name) \
if (!strcmp(key, #name)) { \
    if (name##_found || !configValidateJson##type##Object(val)) goto end; \
    name##_found = true; \
    continue; \
}

#define CONFIG_GETTER(functype, vartype, ...) \
vartype configGet##functype(const char *path) { \
    vartype ret = (vartype)0; \
    SCOPED_LOCK(&g_configMutex) { \
        if (!g_configInterfaceInit) break; \
        ret = jsonGet##functype(g_configJson, path); \
    } \
    return ret; \
}

#define CONFIG_SETTER(functype, vartype, ...) \
void configSet##functype(const char *path, vartype value) { \
    SCOPED_LOCK(&g_configMutex) { \
        if (!g_configInterfaceInit) break; \
        if (jsonSet##functype(g_configJson, path, value)) configWriteConfigJson(); \
    } \
}

/* Global variables. */

static Mutex g_configMutex = 0;
static bool g_configInterfaceInit = false;

static struct json_object *g_configJson = NULL;

/* Function prototypes. */

static bool configParseConfigJson(void);
static void configWriteConfigJson(void);
static void configFreeConfigJson(void);

static bool configValidateJsonRootObject(const struct json_object *obj);
static bool configValidateJsonGameCardObject(const struct json_object *obj);
static bool configValidateJsonNspObject(const struct json_object *obj);
static bool configValidateJsonTicketObject(const struct json_object *obj);
static bool configValidateJsonNcaFsObject(const struct json_object *obj);

bool configInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_configMutex)
    {
        ret = g_configInterfaceInit;
        if (ret) break;

        /* Parse JSON config. */
        if (!configParseConfigJson())
        {
            LOG_MSG_ERROR("Failed to parse JSON configuration!");
            break;
        }

        /* Update flags. */
        ret = g_configInterfaceInit = true;
    }

    return ret;
}

void configExit(void)
{
    SCOPED_LOCK(&g_configMutex)
    {
        /* Free JSON object. */
        /* We don't need to write it back to the SD card - setter functions do that on their own. */
        configFreeConfigJson();

        /* Update flag. */
        g_configInterfaceInit = false;
    }
}

CONFIG_GETTER(Boolean, bool);
CONFIG_SETTER(Boolean, bool);

CONFIG_GETTER(Integer, int);
CONFIG_SETTER(Integer, int);

static bool configParseConfigJson(void)
{
    bool use_default_config = true, ret = false;

    /* Read config JSON. */
    g_configJson = json_object_from_file(CONFIG_PATH);
    if (g_configJson)
    {
        /* Validate configuration. */
        ret = configValidateJsonRootObject(g_configJson);
        use_default_config = !ret;
    } else {
        jsonLogLastError();
    }

    if (use_default_config)
    {
        LOG_MSG_INFO("Loading default configuration.");

        /* Free config JSON. */
        configFreeConfigJson();

        /* Read default config JSON. */
        g_configJson = json_object_from_file(DEFAULT_CONFIG_PATH);
        if (g_configJson)
        {
            configWriteConfigJson();
            ret = true;
        } else {
            jsonLogLastError();
        }
    }

    if (!ret) LOG_MSG_ERROR("Failed to parse both current and default JSON configuration files!");

    return ret;
}

static void configWriteConfigJson(void)
{
    if (!g_configJson) return;
    if (json_object_to_file_ext(CONFIG_PATH, g_configJson, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY) != 0) jsonLogLastError();
}

static void configFreeConfigJson(void)
{
    if (!g_configJson) return;
    json_object_put(g_configJson);
    g_configJson = NULL;
}

static bool configValidateJsonRootObject(const struct json_object *obj)
{
    bool ret = false, overclock_found = false, naming_convention_found = false, output_storage_found = false, gamecard_found = false;
    bool nsp_found = false, ticket_found = false, nca_fs_found = false;

    if (!jsonValidateObject(obj)) goto end;

    json_object_object_foreach(obj, key, val)
    {
        CONFIG_VALIDATE_FIELD(Boolean, overclock);
        CONFIG_VALIDATE_FIELD(Integer, naming_convention, TitleNamingConvention_Full, TitleNamingConvention_Count - 1);
        CONFIG_VALIDATE_FIELD(Integer, output_storage, ConfigOutputStorage_SdCard, ConfigOutputStorage_Count - 1);
        CONFIG_VALIDATE_OBJECT(GameCard, gamecard);
        CONFIG_VALIDATE_OBJECT(Nsp, nsp);
        CONFIG_VALIDATE_OBJECT(Ticket, ticket);
        CONFIG_VALIDATE_OBJECT(NcaFs, nca_fs);
        goto end;
    }

    ret = (overclock_found && naming_convention_found && output_storage_found && gamecard_found && nsp_found && ticket_found && nca_fs_found);

end:
    return ret;
}

static bool configValidateJsonGameCardObject(const struct json_object *obj)
{
    bool ret = false, prepend_key_area_found = false, keep_certificate_found = false, trim_dump_found = false, calculate_checksum_found = false, checksum_lookup_method_found = false;

    if (!jsonValidateObject(obj)) goto end;

    json_object_object_foreach(obj, key, val)
    {
        CONFIG_VALIDATE_FIELD(Boolean, prepend_key_area);
        CONFIG_VALIDATE_FIELD(Boolean, keep_certificate);
        CONFIG_VALIDATE_FIELD(Boolean, trim_dump);
        CONFIG_VALIDATE_FIELD(Boolean, calculate_checksum);
        CONFIG_VALIDATE_FIELD(Integer, checksum_lookup_method, ConfigChecksumLookupMethod_None, ConfigChecksumLookupMethod_Count - 1);
        goto end;
    }

    ret = (prepend_key_area_found && keep_certificate_found && trim_dump_found && calculate_checksum_found && checksum_lookup_method_found);

end:
    return ret;
}

static bool configValidateJsonNspObject(const struct json_object *obj)
{
    bool ret = false, set_download_distribution_found = false, remove_console_data_found = false, remove_titlekey_crypto_found = false;
    bool disable_linked_account_requirement_found = false, enable_screenshots_found = false, enable_video_capture_found = false, disable_hdcp_found = false, append_authoringtool_data_found = false, lookup_checksum_found = false;

    if (!jsonValidateObject(obj)) goto end;

    json_object_object_foreach(obj, key, val)
    {
        CONFIG_VALIDATE_FIELD(Boolean, set_download_distribution);
        CONFIG_VALIDATE_FIELD(Boolean, remove_console_data);
        CONFIG_VALIDATE_FIELD(Boolean, remove_titlekey_crypto);
        CONFIG_VALIDATE_FIELD(Boolean, disable_linked_account_requirement);
        CONFIG_VALIDATE_FIELD(Boolean, enable_screenshots);
        CONFIG_VALIDATE_FIELD(Boolean, enable_video_capture);
        CONFIG_VALIDATE_FIELD(Boolean, disable_hdcp);
        CONFIG_VALIDATE_FIELD(Boolean, lookup_checksum);
        CONFIG_VALIDATE_FIELD(Boolean, append_authoringtool_data);
        goto end;
    }

    ret = (set_download_distribution_found && remove_console_data_found && remove_titlekey_crypto_found && disable_linked_account_requirement_found && \
           enable_screenshots_found && enable_video_capture_found && disable_hdcp_found && append_authoringtool_data_found && lookup_checksum_found);

end:
    return ret;
}

static bool configValidateJsonTicketObject(const struct json_object *obj)
{
    bool ret = false, remove_console_data_found = false;

    if (!jsonValidateObject(obj)) goto end;

    json_object_object_foreach(obj, key, val)
    {
        CONFIG_VALIDATE_FIELD(Boolean, remove_console_data);
        goto end;
    }

    ret = remove_console_data_found;

end:
    return ret;
}

static bool configValidateJsonNcaFsObject(const struct json_object *obj)
{
    bool ret = false, use_layeredfs_dir_found = false;

    if (!jsonValidateObject(obj)) goto end;

    json_object_object_foreach(obj, key, val)
    {
        CONFIG_VALIDATE_FIELD(Boolean, use_layeredfs_dir);
        goto end;
    }

    ret = use_layeredfs_dir_found;

end:
    return ret;
}
