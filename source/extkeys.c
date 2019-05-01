#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "extkeys.h"
#include "ui.h"
#include "util.h"

/* Extern variables */

extern int breaks;
extern int font_height;
extern char strbuf[NAME_BUF_LEN * 4];

/**
 * Reads a line from file f and parses out the key and value from it.
 * The format of a line must match /^ *[A-Za-z0-9_] *[,=] *.+$/.
 * If a line ends in \r, the final \r is stripped.
 * The input file is assumed to have been opened with the 'b' flag.
 * The input file is assumed to contain only ASCII.
 *
 * A line cannot exceed 512 bytes in length.
 * Lines that are excessively long will be silently truncated.
 *
 * On success, *key and *value will be set to point to the key and value in
 * the input line, respectively.
 * *key and *value may also be NULL in case of empty lines.
 * On failure, *key and *value will be set to NULL.
 * End of file is considered failure.
 *
 * Because *key and *value will point to a static buffer, their contents must be
 * copied before calling this function again.
 * For the same reason, this function is not thread-safe.
 *
 * The key will be converted to lowercase.
 * An empty key is considered a parse error, but an empty value is returned as
 * success.
 *
 * This function assumes that the file can be trusted not to contain any NUL in
 * the contents.
 *
 * Whitespace (' ', ASCII 0x20, as well as '\t', ASCII 0x09) at the beginning of
 * the line, at the end of the line as well as around = (or ,) will be ignored.
 *
 * @param f the file to read
 * @param key pointer to change to point to the key
 * @param value pointer to change to point to the value
 * @return 0 on success,
 *         1 on end of file,
 *         -1 on parse error (line too long, line malformed)
 *         -2 on I/O error
 */
static int get_kv(FILE *f, char **key, char **value)
{
#define SKIP_SPACE(p) do {\
    for (; (*p == ' ' || *p == '\t'); ++p);\
} while(0);
    
    static char line[512];
    char *k, *v, *p, *end;
    
    *key = *value = NULL;
    
    errno = 0;
    
    if (fgets(line, (int)sizeof(line), f) == NULL)
    {
        if (feof(f))
        {
            return 1;
        } else {
            return -2;
        }
    }
    
    if (errno != 0) return -2;
    
    if (*line == '\n' || *line == '\r' || *line == '\0') return 0;
    
    /* Not finding \r or \n is not a problem.
     * The line might just be exactly 512 characters long, we have no way to
     * tell.
     * Additionally, it's possible that the last line of a file is not actually
     * a line (i.e., does not end in '\n'); we do want to handle those.
     */
    if ((p = strchr(line, '\r')) != NULL || (p = strchr(line, '\n')) != NULL)
    {
        end = p;
        *p = '\0';
    } else {
        end = (line + strlen(line) + 1);
    }
    
    p = line;
    SKIP_SPACE(p);
    k = p;
    
    /* Validate key and convert to lower case. */
    for (; *p != ' ' && *p != ',' && *p != '\t' && *p != '='; ++p)
    {
        if (*p == '\0') return -1;
        
        if (*p >= 'A' && *p <= 'Z')
        {
            *p = 'a' + (*p - 'A');
            continue;
        }
        
        if (*p != '_' && (*p < '0' && *p > '9') && (*p < 'a' && *p > 'z')) return -1;
    }
    
    /* Bail if the final ++p put us at the end of string */
    if (*p == '\0') return -1;
    
    /* We should be at the end of key now and either whitespace or [,=]
     * follows.
     */
    if (*p == '=' || *p == ',')
    {
        *p++ = '\0';
    } else {
        *p++ = '\0';
        SKIP_SPACE(p);
        if (*p != '=' && *p != ',') return -1;
        *p++ = '\0';
    }
    
    /* Empty key is an error. */
    if (*k == '\0') return -1;
    
    SKIP_SPACE(p);
    v = p;
    
    /* Skip trailing whitespace */
    for (p = end - 1; *p == '\t' || *p == ' '; --p);
    
    *(p + 1) = '\0';
    
    *key = k;
    *value = v;
    
    return 0;
    
#undef SKIP_SPACE
}

static int ishex(char c)
{
    if ('a' <= c && c <= 'f') return 1;
    if ('A' <= c && c <= 'F') return 1;
    if ('0' <= c && c <= '9') return 1;
    return 0;
}

static char hextoi(char c)
{
    if ('a' <= c && c <= 'f') return (c - 'a' + 0xA);
    if ('A' <= c && c <= 'F') return (c - 'A' + 0xA);
    if ('0' <= c && c <= '9') return (c - '0');
    return 0;
}

int parse_hex_key(unsigned char *key, const char *hex, unsigned int len)
{
    if (strlen(hex) != (2 * len))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: key (%s) must be %u hex digits!", hex, 2 * len);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    u32 i;
    for(i = 0; i < (2 * len); i++)
    {
        if (!ishex(hex[i]))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: key (%s) must be %u hex digits!", hex, 2 * len);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            return 0;
        }
    }
    
    memset(key, 0, len);
    
    for(i = 0; i < (2 * len); i++)
    {
        char val = hextoi(hex[i]);
        if ((i & 1) == 0) val <<= 4;
        key[i >> 1] |= val;
    }
    
    return 1;
}

int extkeys_initialize_keyset(nca_keyset_t *keyset, FILE *f)
{
    u32 i;
    int ret;
    char *key, *value;
    char test_name[0x100];
    
    memset(keyset, 0, sizeof(nca_keyset_t));
    
    while((ret = get_kv(f, &key, &value)) != 1 && ret != -2)
    {
        if (ret == 0)
        {
            if (key == NULL || value == NULL) continue;
            
            if (strcmp(key, "header_key") == 0)
            {
                if (!parse_hex_key(keyset->header_key, value, sizeof(keyset->header_key))) return 0;
                keyset->key_cnt++;
            } else {
                memset(test_name, 0, sizeof(test_name));
                
                for(i = 0; i < 0x20; i++)
                {
                    snprintf(test_name, sizeof(test_name), "key_area_key_application_%02x", i);
                    if (strcmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(keyset->key_area_keys[i][0], value, sizeof(keyset->key_area_keys[i][0]))) return 0;
                        keyset->key_cnt++;
                        break;
                    }
                    
                    snprintf(test_name, sizeof(test_name), "key_area_key_ocean_%02x", i);
                    if (strcmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(keyset->key_area_keys[i][1], value, sizeof(keyset->key_area_keys[i][1]))) return 0;
                        keyset->key_cnt++;
                        break;
                    }
                    
                    snprintf(test_name, sizeof(test_name), "key_area_key_system_%02x", i);
                    if (strcmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(keyset->key_area_keys[i][2], value, sizeof(keyset->key_area_keys[i][2]))) return 0;
                        keyset->key_cnt++;
                        break;
                    }
                }
            }
        }
    }
    
    if (!keyset->key_cnt) return -1;
    
    return 1;
}
