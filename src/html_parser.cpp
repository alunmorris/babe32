// 060326 Single-pass HTML tokenizer
#include "html_parser.h"
#include "url_utils.h"
#include <Arduino.h>
#include <string.h>
#include <ctype.h>

ParseResult *parse_result_alloc() {
    ParseResult *r = (ParseResult *)heap_caps_malloc(sizeof(ParseResult),
                                                      MALLOC_CAP_SPIRAM);
    if (r) memset(r, 0, sizeof(ParseResult));
    return r;
}

void parse_result_free(ParseResult *r) {
    if (r) heap_caps_free(r);
}

static char *pool_add(ParseResult *r, const char *text, size_t len) {
    if (r->pool_used + len + 1 > sizeof(r->text_pool)) return NULL;
    char *p = r->text_pool + r->pool_used;
    memcpy(p, text, len);
    p[len] = '\0';
    r->pool_used += len + 1;
    return p;
}

static bool add_elem(ParseResult *r, ElemType type, const char *text,
                     size_t text_len, const char *href, uint8_t level) {
    if (r->count >= MAX_ELEMENTS) return false;
    if (type != ELEM_LINEBREAK && text_len == 0) return true;
    PageElement *e = &r->elems[r->count];
    e->type  = type;
    e->level = level;
    e->text  = pool_add(r, text, text_len);
    e->href  = NULL;
    if (type == ELEM_LINK && href)
        e->href = pool_add(r, href, strlen(href));
    if (e->text || type == ELEM_LINEBREAK) r->count++;
    return true;
}

static bool tag_is(const char *name, size_t name_len, const char *match) {
    size_t ml = strlen(match);
    if (name_len < ml) return false;
    return strncasecmp(name, match, ml) == 0 &&
           (name_len == ml || !isalnum((uint8_t)name[ml]));
}

static bool get_attr(const char *tag, const char *attr, char *out, size_t out_len) {
    const char *p = strcasestr(tag, attr);
    if (!p) return false;
    p += strlen(attr);
    while (*p == ' ') p++;
    if (*p != '=') return false;
    p++;
    while (*p == ' ') p++;
    char quote = 0;
    if (*p == '"' || *p == '\'') quote = *p++;
    size_t i = 0;
    while (*p && i < out_len - 1) {
        if (quote && *p == quote) break;
        if (!quote && (*p == ' ' || *p == '>')) break;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static void decode_entities(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '&') {
            if      (strncmp(r, "&amp;",  5) == 0) { *w++ = '&';  r += 5; }
            else if (strncmp(r, "&lt;",   4) == 0) { *w++ = '<';  r += 4; }
            else if (strncmp(r, "&gt;",   4) == 0) { *w++ = '>';  r += 4; }
            else if (strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' ';  r += 6; }
            else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"';  r += 6; }
            else if (strncmp(r, "&#39;",  5) == 0) { *w++ = '\''; r += 5; }
            else if (strncmp(r, "&#", 2) == 0) {
                // Numeric entity &#NN; or &#xNN; — skip non-ASCII, keep ASCII
                char *semi = strchr(r, ';');
                if (semi && semi - r < 12) {
                    long cp = 0;
                    if (r[2] == 'x' || r[2] == 'X')
                        cp = strtol(r + 3, nullptr, 16);
                    else
                        cp = strtol(r + 2, nullptr, 10);
                    if (cp >= 0x20 && cp <= 0x7E)
                        *w++ = (char)cp;
                    r = semi + 1;
                } else {
                    *w++ = *r++;
                }
            }
            else                                    { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

void html_parse(const char *html, const char *base_url, ParseResult *r) {
    url_set_base(base_url);

    static char text_acc[4096];
    size_t acc_len = 0;

    ElemType cur_type  = ELEM_PARAGRAPH;
    uint8_t  cur_level = 0;
    bool     in_body   = false;
    bool     in_link   = false;
    char     link_href[512] = "";

    const char *p = html;

    auto flush_acc = [&]() {
        if (acc_len > 0) {
            text_acc[acc_len] = '\0';
            decode_entities(text_acc);
            size_t start = 0;
            while (start < acc_len && isspace((uint8_t)text_acc[start])) start++;
            size_t end = strlen(text_acc);
            while (end > start && isspace((uint8_t)text_acc[end-1])) end--;
            size_t len = end - start;
            if (len > 0) {
                add_elem(r, in_link ? ELEM_LINK : cur_type,
                         text_acc + start, len,
                         in_link ? link_href : NULL,
                         cur_level);
            }
            acc_len = 0;
        }
    };

    while (*p) {
        if (*p != '<') {
            if (in_body && acc_len < sizeof(text_acc) - 1) {
                uint8_t c = (uint8_t)*p;
                if (c >= 0x80) {
                    // Skip UTF-8 multi-byte sequence (Montserrat has no glyphs)
                    if      (c >= 0xF0) p += 4;  // 4-byte
                    else if (c >= 0xE0) p += 3;  // 3-byte
                    else                p += 2;   // 2-byte
                    continue;
                }
                if (isspace(c)) {
                    if (acc_len > 0 && text_acc[acc_len-1] != ' ')
                        text_acc[acc_len++] = ' ';
                } else {
                    text_acc[acc_len++] = *p;
                }
            }
            p++;
            continue;
        }

        // Tag
        const char *tag_start = p + 1;
        const char *tag_end   = strchr(tag_start, '>');
        if (!tag_end) break;

        bool closing = (tag_start[0] == '/');
        const char *name = closing ? tag_start + 1 : tag_start;
        size_t name_len = 0;
        while (name[name_len] && name[name_len] != ' ' &&
               name[name_len] != '>' && name[name_len] != '/') name_len++;

        size_t tag_str_len = (size_t)(tag_end - tag_start);
        static char tag_str[1024];
        size_t copy_len = tag_str_len < sizeof(tag_str) - 1 ? tag_str_len : sizeof(tag_str) - 1;
        memcpy(tag_str, tag_start, copy_len);
        tag_str[copy_len] = '\0';

        // Skip script/style blocks entirely
        if (!closing && (tag_is(name, name_len, "script") ||
                         tag_is(name, name_len, "style"))) {
            const char *close_tag = tag_is(name, name_len, "script")
                                    ? "</script>" : "</style>";
            const char *close = strcasestr(tag_end + 1, close_tag);
            p = close ? close + strlen(close_tag) : tag_end + 1;
            continue;
        }

        if (!in_body) {
            if (tag_is(name, name_len, "body")) in_body = true;
            p = tag_end + 1;
            continue;
        }

        // Headings h1-h6
        if (name_len == 2 && tolower((uint8_t)name[0]) == 'h' &&
            name[1] >= '1' && name[1] <= '6') {
            flush_acc();
            if (!closing) {
                cur_type  = ELEM_HEADING;
                cur_level = name[1] - '0';
            } else {
                cur_type  = ELEM_PARAGRAPH;
                cur_level = 0;
            }
        }
        // Paragraphs
        else if (tag_is(name, name_len, "p")) {
            flush_acc();
            if (!closing) { cur_type = ELEM_PARAGRAPH; cur_level = 0; }
        }
        // Line break
        else if (tag_is(name, name_len, "br")) {
            flush_acc();
            add_elem(r, ELEM_LINEBREAK, "", 0, NULL, 0);
        }
        // Links
        else if (tag_is(name, name_len, "a")) {
            flush_acc();
            if (!closing) {
                char href_raw[512] = "";
                if (get_attr(tag_str, "href", href_raw, sizeof(href_raw))) {
                    char resolved[512];
                    if (url_resolve(url_get_base(), href_raw,
                                    resolved, sizeof(resolved))) {
                        strncpy(link_href, resolved, sizeof(link_href) - 1);
                        in_link = true;
                    }
                }
            } else {
                in_link = false;
                link_href[0] = '\0';
            }
        }
        // Block elements — paragraph break
        else if (tag_is(name, name_len, "div") ||
                 tag_is(name, name_len, "li")  ||
                 tag_is(name, name_len, "td")  ||
                 tag_is(name, name_len, "tr")) {
            flush_acc();
        }

        p = tag_end + 1;
    }
    flush_acc();
    Serial.printf("HTML parse: %d elements, %zu bytes pool\n",
                  r->count, r->pool_used);
}
