#pragma once
#include "eluvio/bitcode_context.h"

#define KEYSZ 128
#define VALSZ 128
using namespace elv_context;

/*
 *
 *
 * Return:
 *   - pointer to the beginning of 'value' or NULL if EOF or error
 *   - out param 'pkey' - NULL if EOF or error
 */
char* cddl_get_key(char *p, char *pkey)
{
    char *pnext = p;
    char *kstart = NULL;
    memset(pkey, 0, KEYSZ);

    /* First find the opening '"' */
    while (*pnext != '"' && *pnext != '}')
        ++ pnext;

    if (*pnext == '}')
        return NULL;

    kstart = ++ pnext;
    while (*pnext != '"')
        ++ pnext;
    strncpy(pkey, kstart, (size_t)(pnext - kstart));
    return ++ pnext;
}

char* cddl_get_value(char *p, char *value)
{
    char *pnext = p;
    char *vstart = NULL;
    memset(value, 0, VALSZ);

    /* First find the ":" character */
    while (*pnext != ':')
        ++ pnext;
    ++ pnext;

    /* Then skip whitespace */
    while (*pnext == ' ' || *pnext == '\t')
        ++ pnext;

    vstart = pnext;

    while (*pnext != ' ' && *pnext != '\t' && *pnext != '\n'
        && *pnext != '\r' && *pnext != ',' && *pnext != '}')
        ++ pnext;
    strncpy(value, vstart, (size_t)(pnext - vstart));
    return pnext;
}

int cddl_parse_and_check(BitCodeCallContext* ctx, char *text) {

    char key[KEYSZ], value[VALSZ];
    char *p = text;
    int found = 0;

    do {
        p = cddl_get_key(p, key);
        if (p != NULL) {
            p = cddl_get_value(p, value);
            LOG_INFO(ctx, "cddl_parse_and_check", "key", key, "val", value);

            auto val = ctx->KVGet(key);
            if (val != "" && val.length() > 0) {
                LOG_INFO(ctx, "FOUND");
                found ++;
            } else {
                LOG_INFO(ctx, "NOT FOUND");
            }
        }
    } while(p != NULL);

    return found;
}
