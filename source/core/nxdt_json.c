/*
 * nxdt_json.c
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
#include "nxdt_json.h"

#define JSON_GETTER(functype, vartype, jsontype, ...) \
vartype jsonGet##functype(const struct json_object *obj, const char *path) { \
    vartype ret = (vartype)0; \
    struct json_object *child = jsonGetObjectByPath(obj, path, NULL); \
    if (child && jsonValidate##functype(child, ##__VA_ARGS__)) ret = (vartype)json_object_get_##jsontype(child); \
    return ret; \
}

#define JSON_SETTER(functype, vartype, jsontype, ...) \
bool jsonSet##functype(const struct json_object *obj, const char *path, vartype value) { \
    bool ret = false; \
    struct json_object *child = jsonGetObjectByPath(obj, path, NULL); \
    if (child && jsonValidate##functype(child, ##__VA_ARGS__)) { \
        ret = (json_object_set_##jsontype(child, value) == 1); \
        if (!ret) LOG_MSG("Failed to update \"%s\"!", path); \
    } \
    return ret; \
}

struct json_object *jsonParseFromString(const char *str, size_t size)
{
    if (!str || !*str)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Calculate string size if it wasn't provided. */
    if (!size) size = (strlen(str) + 1);
    
    struct json_tokener *tok = NULL;
    struct json_object *obj = NULL;
    enum json_tokener_error jerr = json_tokener_success;
    
    /* Allocate tokener. */
    tok = json_tokener_new();
    if (!tok)
    {
        LOG_MSG("json_tokener_new failed!");
        goto end;
    }
    
    /* Parse JSON buffer. */
    obj = json_tokener_parse_ex(tok, str, (int)size);
    if ((jerr = json_tokener_get_error(tok)) != json_tokener_success)
    {
        LOG_MSG("json_tokener_parse_ex failed! Reason: \"%s\".", json_tokener_error_desc(jerr));
        
        if (obj)
        {
            json_object_put(obj);
            obj = NULL;
        }
    }
    
end:
    if (tok) json_tokener_free(tok);
    
    return obj;
}

struct json_object *jsonGetObjectByPath(const struct json_object *obj, const char *path, char **out_last_element)
{
    const struct json_object *parent_obj = obj;
    struct json_object *child_obj = NULL;
    char *path_dup = NULL, *pch = NULL, *state = NULL, *prev_pch = NULL;
    
    if (!jsonValidateObject(obj) || !path || !*path)
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
    
    /* Tokenize input path. */
    pch = strtok_r(path_dup, "/", &state);
    if (!pch)
    {
        LOG_MSG("Failed to tokenize input path! (\"%s\").", path);
        goto end;
    }
    
    while(pch)
    {
        prev_pch = pch;
        
        pch = strtok_r(NULL, "/", &state);
        if (pch || !out_last_element)
        {
            /* Retrieve JSON object using the current path element. */
            if (!json_object_object_get_ex(parent_obj, prev_pch, &child_obj))
            {
                LOG_MSG("Failed to retrieve JSON object by key for \"%s\"! (\"%s\").", prev_pch, path);
                break;
            }
            
            /* Update parent and child pointers if we can still proceed. */
            if (pch)
            {
                parent_obj = child_obj;
                child_obj = NULL;
            }
        } else {
            /* No additional path elements can be found + the user wants the string for the last path element. */
            /* Let's start by setting the last parent object as the return value. */
            child_obj = (struct json_object*)parent_obj; /* Drop the const. */
            
            /* Duplicate last path element string. */
            *out_last_element = strdup(prev_pch);
            if (!*out_last_element)
            {
                LOG_MSG("Failed to duplicate last path element \"%s\"! (\"%s\").", prev_pch, path);
                child_obj = NULL;
            }
        }
    }
    
end:
    if (path_dup) free(path_dup);
    
    return child_obj;
}

void jsonLogLastError(void)
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

JSON_GETTER(Boolean, bool, boolean);
JSON_SETTER(Boolean, bool, boolean);

JSON_GETTER(Integer, int, int, INT32_MIN, INT32_MAX);
JSON_SETTER(Integer, int, int, INT32_MIN, INT32_MAX);

JSON_GETTER(String, const char*, string);
JSON_SETTER(String, const char*, string);

/* Special handling for JSON arrays. */

struct json_object *jsonGetArray(const struct json_object *obj, const char *path)
{
    struct json_object *ret = NULL, *child = jsonGetObjectByPath(obj, path, NULL);
    if (child && jsonValidateArray(child)) ret = child;
    return ret;
}

bool jsonSetArray(const struct json_object *obj, const char *path, struct json_object *value)
{
    if (!obj || !path || !*path || !jsonValidateArray(value))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    bool ret = false;
    struct json_object *parent_obj = NULL;
    char *key = NULL;
    
    /* Get parent JSON object. */
    parent_obj = jsonGetObjectByPath(obj, path, &key);
    if (!parent_obj)
    {
        LOG_MSG("Failed to retrieve parent JSON object! (\"%s\").", path);
        return false;
    }
    
    /* Set new JSON array. */
    if (json_object_object_add(parent_obj, key, value) != 0)
    {
        LOG_MSG("json_object_object_add failed! (\"%s\").", path);
        goto end;
    }
    
    /* Update return value. */
    ret = true;
    
end:
    if (key) free(key);
    
    return ret;
}
