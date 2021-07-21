/*
 * config.c
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

#include "nxdt_utils.h"
#include "config.h"
#include "title.h"

#include <json-c/json.h>

#define JSON_VALIDATE_FIELD(type, name, ...) \
if (!strcmp(key, #name)) { \
    if (name##_found || !configValidateJson##type(val, ##__VA_ARGS__)) goto end; \
    name##_found = true; \
    continue; \
}

#define JSON_VALIDATE_OBJECT(type, name) \
if (!strcmp(key, #name)) { \
    if (name##_found || !configValidateJson##type##Object(val)) goto end; \
    name##_found = true; \
    continue; \
}

#define JSON_GETTER(functype, vartype, jsontype, ...) \
vartype configGet##functype(const char *path) { \
    vartype ret = (vartype)0; \
    SCOPED_LOCK(&g_configMutex) { \
        struct json_object *obj = configGetJsonObjectByPath(g_configJson, path); \
        if (!obj || !configValidateJson##functype(obj, ##__VA_ARGS__)) break; \
        ret = (vartype)json_object_get_##jsontype(obj); \
    } \
    return ret; \
}

#define JSON_SETTER(functype, vartype, jsontype, ...) \
void configSet##functype(const char *path, vartype value) { \
    SCOPED_LOCK(&g_configMutex) { \
        struct json_object *obj = configGetJsonObjectByPath(g_configJson, path); \
        if (!obj || !configValidateJson##functype(obj, ##__VA_ARGS__)) break; \
        if (json_object_set_##jsontype(obj, value)) { \
            configWriteConfigJson(); \
        } else { \
            LOG_MSG("Failed to update \"%s\"!", path); \
        } \
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

static struct json_object *configGetJsonObjectByPath(const struct json_object *obj, const char *path);

static bool configValidateJsonRootObject(const struct json_object *obj);
static bool configValidateJsonGameCardObject(const struct json_object *obj);
static bool configValidateJsonNspObject(const struct json_object *obj);
static bool configValidateJsonTicketObject(const struct json_object *obj);
static bool configValidateJsonNcaFsObject(const struct json_object *obj);

NX_INLINE bool configValidateJsonBoolean(const struct json_object *obj);
NX_INLINE bool configValidateJsonInteger(const struct json_object *obj, int lower_boundary, int upper_boundary);
NX_INLINE bool configValidateJsonObject(const struct json_object *obj);

static void configLogJsonError(void);

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
            LOG_MSG("Failed to parse JSON configuration!");
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

JSON_GETTER(Boolean, bool, boolean);
JSON_SETTER(Boolean, bool, boolean);

JSON_GETTER(Integer, int, int, INT32_MIN, INT32_MAX);
JSON_SETTER(Integer, int, int, INT32_MIN, INT32_MAX);

static bool configParseConfigJson(void)
{
    bool use_default_config = true, ret = false;
    
    /* Read config JSON. */
    g_configJson = json_object_from_file(CONFIG_PATH);
    if (!g_configJson)
    {
        configLogJsonError();
        goto end;
    }
    
    /* Validate configuration. */
    ret = configValidateJsonRootObject(g_configJson);
    use_default_config = !ret;
    
end:
    if (use_default_config)
    {
        /* Free config JSON. */
        configFreeConfigJson();
        
        /* Read default config JSON. */
        g_configJson = json_object_from_file(DEFAULT_CONFIG_PATH);
        if (g_configJson)
        {
            configWriteConfigJson();
            ret = true;
        } else {
            configLogJsonError();
        }
    }
    
    return ret;
}

static void configWriteConfigJson(void)
{
    if (!g_configJson) return;
    if (json_object_to_file_ext(CONFIG_PATH, g_configJson, JSON_C_TO_STRING_PRETTY) != 0) configLogJsonError();
}

static void configFreeConfigJson(void)
{
    if (!g_configJson) return;
    json_object_put(g_configJson);
    g_configJson = NULL;
}

static struct json_object *configGetJsonObjectByPath(const struct json_object *obj, const char *path)
{
    const struct json_object *parent_obj = obj;
    struct json_object *child_obj = NULL;
    char *path_dup = NULL, *pch = NULL, *state = NULL;
    
    if (!configValidateJsonObject(obj) || !path || !*path)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    /* Duplicate path to avoid problems with strtok_r(). */
    if (!(path_dup = strdup(path)))
    {
        LOG_MSG("Unable to duplicate input path! (\"%s\").", path);
        return NULL;
    }
    
    pch = strtok_r(path_dup, "/", &state);
    if (!pch)
    {
        LOG_MSG("Failed to tokenize input path! (\"%s\").", path);
        goto end;
    }
    
    while(pch)
    {
        if (!json_object_object_get_ex(parent_obj, pch, &child_obj))
        {
            LOG_MSG("Failed to retrieve JSON object by key for \"%s\"! (\"%s\").", pch, path);
            break;
        }
        
        pch = strtok_r(NULL, "/", &state);
        if (pch)
        {
            parent_obj = child_obj;
            child_obj = NULL;
        }
    }
    
end:
    if (path_dup) free(path_dup);
    
    return child_obj;
}

static bool configValidateJsonRootObject(const struct json_object *obj)
{
    bool ret = false, overclock_found = false, name_convention_found = false, dump_destination_found = false, gamecard_found = false;
    bool nsp_found = false, ticket_found = false, nca_fs_found = false;
    
    if (!configValidateJsonObject(obj)) goto end;
    
    json_object_object_foreach(obj, key, val)
    {
        JSON_VALIDATE_FIELD(Boolean, overclock);
        JSON_VALIDATE_FIELD(Integer, name_convention, TitleFileNameConvention_Full, TitleFileNameConvention_IdAndVersionOnly);
        JSON_VALIDATE_FIELD(Integer, dump_destination, ConfigDumpDestination_SdCard, ConfigDumpDestination_UsbHost);
        JSON_VALIDATE_OBJECT(GameCard, gamecard);
        JSON_VALIDATE_OBJECT(Nsp, nsp);
        JSON_VALIDATE_OBJECT(Ticket, ticket);
        JSON_VALIDATE_OBJECT(NcaFs, nca_fs);
        goto end;
    }
    
    ret = (overclock_found && name_convention_found && dump_destination_found && gamecard_found && nsp_found && ticket_found && nca_fs_found);
    
end:
    return ret;
}

static bool configValidateJsonGameCardObject(const struct json_object *obj)
{
    bool ret = false, append_key_area_found = false, keep_certificate_found = false, trim_dump_found = false, calculate_checksum_found = false, checksum_lookup_method_found = false;
    
    if (!configValidateJsonObject(obj)) goto end;
    
    json_object_object_foreach(obj, key, val)
    {
        JSON_VALIDATE_FIELD(Boolean, append_key_area);
        JSON_VALIDATE_FIELD(Boolean, keep_certificate);
        JSON_VALIDATE_FIELD(Boolean, trim_dump);
        JSON_VALIDATE_FIELD(Boolean, calculate_checksum);
        JSON_VALIDATE_FIELD(Integer, checksum_lookup_method, ConfigChecksumLookupMethod_None, ConfigChecksumLookupMethod_NoIntro);
        goto end;
    }
    
    ret = (append_key_area_found && keep_certificate_found && trim_dump_found && calculate_checksum_found && checksum_lookup_method_found);
    
end:
    return ret;
}

static bool configValidateJsonNspObject(const struct json_object *obj)
{
    bool ret = false, set_download_distribution_found = false, remove_console_data_found = false, remove_titlekey_crypto_found = false, replace_acid_key_sig_found = false;
    bool disable_linked_account_requirement_found = false, enable_screenshots_found = false, enable_video_capture_found = false, disable_hdcp_found = false, lookup_checksum_found = false;
    
    if (!configValidateJsonObject(obj)) goto end;
    
    json_object_object_foreach(obj, key, val)
    {
        JSON_VALIDATE_FIELD(Boolean, set_download_distribution);
        JSON_VALIDATE_FIELD(Boolean, remove_console_data);
        JSON_VALIDATE_FIELD(Boolean, remove_titlekey_crypto);
        JSON_VALIDATE_FIELD(Boolean, replace_acid_key_sig);
        JSON_VALIDATE_FIELD(Boolean, disable_linked_account_requirement);
        JSON_VALIDATE_FIELD(Boolean, enable_screenshots);
        JSON_VALIDATE_FIELD(Boolean, enable_video_capture);
        JSON_VALIDATE_FIELD(Boolean, disable_hdcp);
        JSON_VALIDATE_FIELD(Boolean, lookup_checksum);
        goto end;
    }
    
    ret = (set_download_distribution_found && remove_console_data_found && remove_titlekey_crypto_found && replace_acid_key_sig_found && disable_linked_account_requirement_found && \
           enable_screenshots_found && enable_video_capture_found && disable_hdcp_found && lookup_checksum_found);
    
end:
    return ret;
}

static bool configValidateJsonTicketObject(const struct json_object *obj)
{
    bool ret = false, remove_console_data_found = false;
    
    if (!configValidateJsonObject(obj)) goto end;
    
    json_object_object_foreach(obj, key, val)
    {
        JSON_VALIDATE_FIELD(Boolean, remove_console_data);
        goto end;
    }
    
    ret = remove_console_data_found;
    
end:
    return ret;
}

static bool configValidateJsonNcaFsObject(const struct json_object *obj)
{
    bool ret = false, use_layeredfs_dir_found = false;
    
    if (!configValidateJsonObject(obj)) goto end;
    
    json_object_object_foreach(obj, key, val)
    {
        JSON_VALIDATE_FIELD(Boolean, use_layeredfs_dir);
        goto end;
    }
    
    ret = use_layeredfs_dir_found;
    
end:
    return ret;
}

NX_INLINE bool configValidateJsonBoolean(const struct json_object *obj)
{
    return (obj != NULL && json_object_is_type(obj, json_type_boolean));
}

NX_INLINE bool configValidateJsonInteger(const struct json_object *obj, int lower_boundary, int upper_boundary)
{
    if (!obj || !json_object_is_type(obj, json_type_int) || lower_boundary > upper_boundary) return false;
    int val = json_object_get_int(obj);
    return (val >= lower_boundary && val <= upper_boundary);
}

NX_INLINE bool configValidateJsonObject(const struct json_object *obj)
{
    return (obj != NULL && json_object_is_type(obj, json_type_object) && json_object_object_length(obj) > 0);
}

static void configLogJsonError(void)
{
    size_t str_len = 0;
    char *str = (char*)json_util_get_last_err(); /* Drop the const. */
    if (!str || !(str_len = strlen(str))) return;
    
    /* Remove line breaks. */
    if (str[str_len - 1] == '\n') str[--str_len] = '\0';
    if (str[str_len - 1] == '\r') str[--str_len] = '\0';
    
    /* Log error message. */
    LOG_MSG("%s", str);
}
