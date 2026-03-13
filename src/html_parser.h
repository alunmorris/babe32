// 060326 Single-pass HTML tokenizer -> PageElement array in PSRAM
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ELEM_HEADING,
    ELEM_PARAGRAPH,
    ELEM_LINK,
    ELEM_LINEBREAK
} ElemType;

typedef struct {
    ElemType type;
    char    *text;    // in PSRAM text pool
    char    *href;    // resolved absolute URL (ELEM_LINK only), or NULL
    uint8_t  level;   // 1-6 for ELEM_HEADING, 0 otherwise
} PageElement;

#define MAX_ELEMENTS 512

typedef struct {
    PageElement elems[MAX_ELEMENTS];
    int         count;
    bool        error;              // true if fetch/parse failed
    int         http_status;        // HTTP status code (e.g. 404), 0 if unknown
    char        text_pool[65536];   // 64KB inline text pool
    size_t      pool_used;
} ParseResult;

// Allocate ParseResult in PSRAM
ParseResult *parse_result_alloc();
void         parse_result_free(ParseResult *r);

// Parse html_buf (null-terminated) into r. base_url used for link resolution.
void html_parse(const char *html_buf, const char *base_url, ParseResult *r);
