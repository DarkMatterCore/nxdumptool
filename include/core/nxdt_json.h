/*
 * nxdt_json.h
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

#ifndef __NXDT_JSON_H__
#define __NXDT_JSON_H__

#include <json-c/json.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Parses a JSON object using the provided string.
/// If 'size' is zero, strlen() is used to retrieve the input string length.
/// json_object_put() must be used to free the returned JSON object.
/// Returns NULL if an error occurs.
struct json_object *jsonParseFromString(const char *str, size_t size);

/// Retrieves a JSON object from another object using a path.
/// Path elements must be separated using forward slashes.
/// If 'out_last_element' is provided, the parent JSON object from the last path element will be returned instead.
/// Furthermore, a string duplication of the last path element will be stored in 'out_last_element', which must be freed by the user.
/// Returns NULL if an error occurs.
struct json_object *jsonGetObjectByPath(const struct json_object *obj, const char *path, char **out_last_element);

/// Logs the last JSON error, if available.
void jsonLogLastError(void);

/// Getters and setters for various data types.
/// Path elements must be separated using forward slashes.

bool jsonGetBoolean(const struct json_object *obj, const char *path);
bool jsonSetBoolean(const struct json_object *obj, const char *path, bool value);

int jsonGetInteger(const struct json_object *obj, const char *path);
bool jsonSetInteger(const struct json_object *obj, const char *path, int value);

const char *jsonGetString(const struct json_object *obj, const char *path);
bool jsonSetString(const struct json_object *obj, const char *path, const char *value);

struct json_object *jsonGetArray(const struct json_object *obj, const char *path);
bool jsonSetArray(const struct json_object *obj, const char *path, struct json_object *value);

/// Helper functions to validate specific JSON object types.

NX_INLINE bool jsonValidateBoolean(const struct json_object *obj)
{
    return (obj != NULL && json_object_is_type(obj, json_type_boolean));
}

NX_INLINE bool jsonValidateInteger(const struct json_object *obj, int lower_boundary, int upper_boundary)
{
    if (!obj || !json_object_is_type(obj, json_type_int) || lower_boundary > upper_boundary) return false;
    int val = json_object_get_int(obj);
    return (val >= lower_boundary && val <= upper_boundary);
}

NX_INLINE bool jsonValidateString(const struct json_object *obj)
{
    return (obj != NULL && json_object_is_type(obj, json_type_string) && json_object_get_string_len(obj) > 0);
}

NX_INLINE bool jsonValidateArray(const struct json_object *obj)
{
    return (obj != NULL && json_object_is_type(obj, json_type_array) && json_object_array_length(obj) > 0);
}

NX_INLINE bool jsonValidateObject(const struct json_object *obj)
{
    return (obj != NULL && json_object_is_type(obj, json_type_object) && json_object_object_length(obj) > 0);
}

#ifdef __cplusplus
}
#endif

#endif /* __NXDT_JSON_H__ */
