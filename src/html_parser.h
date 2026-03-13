// 060326 Single-pass HTML tokenizer -> PageElement array in PSRAM
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ELEM_HEADING,
    ELEM_PARAGRAPH,
    ELEM_LINK,
    ELEM_LINEBREAK,
    ELEM_INPUT,      // text input or textarea
    ELEM_HIDDEN,     // hidden input (not rendered)
    ELEM_SUBMIT,     // submit button
    ELEM_SELECT      // dropdown select
} ElemType;

typedef struct {
    ElemType type;
    char    *text;    // label/placeholder for inputs, button text for submit
    char    *href;    // resolved absolute URL (ELEM_LINK only), or NULL
    char    *name;    // form field name (ELEM_INPUT/HIDDEN/SELECT)
    char    *value;   // default value (ELEM_INPUT/HIDDEN) or options (ELEM_SELECT, \n-separated)
    uint8_t  level;   // 1-6 for ELEM_HEADING, 0 otherwise
    bool     multiline; // true for <textarea>
} PageElement;

#define MAX_ELEMENTS 1024

typedef struct {
    PageElement elems[MAX_ELEMENTS];
    int         count;
    bool        error;              // true if fetch/parse failed
    int         http_status;        // HTTP status code (e.g. 404), 0 if unknown
    char        form_action[512];   // form action URL (resolved)
    bool        form_is_post;       // true = POST, false = GET
    char        text_pool[131072];  // 128KB inline text pool
    size_t      pool_used;
} ParseResult;

// Allocate ParseResult in PSRAM
ParseResult *parse_result_alloc();
void         parse_result_free(ParseResult *r);

// Parse html_buf (null-terminated) into r. base_url used for link resolution.
void html_parse(const char *html_buf, const char *base_url, ParseResult *r);
