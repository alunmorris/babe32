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

// Parse a CSS color name or #hex value. Returns 0xRRGGBB | 0x01000000, or 0 on failure.
static uint32_t parse_css_color(const char *s) {
    while (*s == ' ') s++;
    if (*s == '#') {
        s++;
        unsigned long v = strtoul(s, nullptr, 16);
        size_t len = 0;
        while (isxdigit((uint8_t)s[len])) len++;
        if (len == 3)
            v = ((v & 0xF00) << 8 | (v & 0x0F0) << 4 | (v & 0x00F)) * 0x11;
        else if (len != 6)
            return 0;
        return (uint32_t)v | 0x01000000;
    }
    // Named colors (common subset)
    struct { const char *name; uint32_t val; } names[] = {
        {"black",   0x000000}, {"white",   0xFFFFFF}, {"red",     0xFF0000},
        {"green",   0x008000}, {"blue",    0x0000FF}, {"yellow",  0xFFFF00},
        {"cyan",    0x00FFFF}, {"magenta", 0xFF00FF}, {"orange",  0xFFA500},
        {"purple",  0x800080}, {"gray",    0x808080}, {"grey",    0x808080},
        {"lime",    0x00FF00}, {"navy",    0x000080}, {"teal",    0x008080},
        {"brown",   0xA52A2A}, {"silver",  0xC0C0C0}, {"maroon", 0x800000},
        {"pink",    0xFFC0CB}, {"coral",   0xFF7F50},
    };
    for (auto &n : names) {
        if (strcasecmp(s, n.name) == 0) return n.val | 0x01000000;
    }
    return 0;
}

// Extract color from style attribute, e.g. style="color:blue"
static uint32_t parse_style_color(const char *tag_str) {
    char style[256] = "";
    // Manual extraction - get_attr would work but style values can contain semicolons
    const char *sp = strcasestr(tag_str, "style");
    if (!sp) return 0;
    sp += 5;
    while (*sp == ' ' || *sp == '=') sp++;
    char quote = *sp;
    if (quote != '"' && quote != '\'') return 0;
    sp++;
    const char *end = strchr(sp, quote);
    if (!end) return 0;
    size_t len = (size_t)(end - sp);
    if (len >= sizeof(style)) len = sizeof(style) - 1;
    memcpy(style, sp, len);
    style[len] = '\0';

    // Find "color:" (but not "background-color:")
    char *cp = style;
    while ((cp = strcasestr(cp, "color")) != nullptr) {
        // Make sure it's not "background-color"
        if (cp > style && cp[-1] != ' ' && cp[-1] != ';' && cp[-1] != '"') {
            cp += 5;
            continue;
        }
        cp += 5;
        while (*cp == ' ' || *cp == ':') cp++;
        // Extract value until ; or end
        char val[64];
        size_t vi = 0;
        while (*cp && *cp != ';' && *cp != '"' && *cp != '\'' && vi < sizeof(val) - 1) {
            val[vi++] = *cp++;
        }
        val[vi] = '\0';
        // Trim trailing spaces
        while (vi > 0 && val[vi-1] == ' ') val[--vi] = '\0';
        uint32_t c = parse_css_color(val);
        if (c) return c;
        break;
    }
    return 0;
}

static bool add_elem(ParseResult *r, ElemType type, const char *text,
                     size_t text_len, const char *href, uint8_t level,
                     bool bold = false, uint32_t color = 0) {
    if (r->count >= MAX_ELEMENTS) return false;
    if (type != ELEM_LINEBREAK && text_len == 0) return true;
    PageElement *e = &r->elems[r->count];
    memset(e, 0, sizeof(*e));
    e->type  = type;
    e->level = level;
    e->bold  = bold;
    e->color = color;
    e->text  = pool_add(r, text, text_len);
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
    size_t alen = strlen(attr);
    const char *p = tag;
    while ((p = strcasestr(p, attr)) != NULL) {
        // Must be preceded by whitespace (word boundary)
        if (p > tag && !isspace((uint8_t)p[-1])) { p += alen; continue; }
        const char *q = p + alen;
        while (*q == ' ') q++;
        if (*q != '=') { p += alen; continue; }
        q++;
        while (*q == ' ') q++;
        char quote = 0;
        if (*q == '"' || *q == '\'') quote = *q++;
        size_t i = 0;
        while (*q && i < out_len - 1) {
            if (quote && *q == quote) break;
            if (!quote && (*q == ' ' || *q == '>')) break;
            out[i++] = *q++;
        }
        out[i] = '\0';
        return i > 0;
    }
    return false;
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

    static char text_acc[8192];
    size_t acc_len = 0;

    ElemType cur_type  = ELEM_PARAGRAPH;
    uint8_t  cur_level = 0;
    bool     in_body   = false;
    bool     in_link   = false;
    char     link_href[512] = "";
    // Form state
    bool     in_select   = false;
    int      select_elem = -1;       // index of current ELEM_SELECT in elems[]
    char     select_name[128] = "";
    static char select_opts[2048];   // \n-separated options for current <select>
    size_t   select_opts_len = 0;
    bool     in_textarea = false;
    int      textarea_elem = -1;     // index of current ELEM_INPUT (textarea)
    bool     in_bold = false;
    bool     acc_all_bold = true;   // true if ALL accumulated text is bold
    uint32_t cur_color = 0;        // current inline color (0 = default)
    uint32_t acc_color = 0;        // color seen during accumulation
    int      color_depth = 0;      // nesting depth of color-setting tags

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
                         cur_level, acc_all_bold, acc_color);
            }
            acc_len = 0;
            acc_all_bold = true;
            acc_color = cur_color;
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
                    if (!in_bold) acc_all_bold = false;
                    if (cur_color) acc_color = cur_color;
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
            if (!closing) {
                cur_type = ELEM_PARAGRAPH; cur_level = 0;
                uint32_t c = parse_style_color(tag_str);
                if (c) { cur_color = c; color_depth++; }
            } else if (color_depth > 0) {
                color_depth--;
                if (color_depth == 0) cur_color = 0;
            }
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
        // Bold — don't flush, to avoid creating line breaks
        else if (tag_is(name, name_len, "b") ||
                 tag_is(name, name_len, "strong")) {
            in_bold = !closing;
        }
        // Inline color: <span style="color:...">, <font color="...">
        else if (tag_is(name, name_len, "span") ||
                 tag_is(name, name_len, "font")) {
            if (!closing) {
                uint32_t c = parse_style_color(tag_str);
                // Also try <font color="...">
                if (!c && tag_is(name, name_len, "font")) {
                    char cval[64] = "";
                    if (get_attr(tag_str, "color", cval, sizeof(cval)))
                        c = parse_css_color(cval);
                }
                if (c) {
                    cur_color = c;
                    color_depth++;
                }
            } else if (color_depth > 0) {
                color_depth--;
                if (color_depth == 0) cur_color = 0;
            }
        }
        // Form
        else if (tag_is(name, name_len, "form")) {
            flush_acc();
            if (!closing) {
                char action_raw[512] = "";
                get_attr(tag_str, "action", action_raw, sizeof(action_raw));
                if (action_raw[0]) {
                    char resolved[512];
                    if (url_resolve(url_get_base(), action_raw,
                                    resolved, sizeof(resolved)))
                        strncpy(r->form_action, resolved, sizeof(r->form_action) - 1);
                } else {
                    strncpy(r->form_action, base_url, sizeof(r->form_action) - 1);
                }
                char method[16] = "get";
                get_attr(tag_str, "method", method, sizeof(method));
                r->form_is_post = (strcasecmp(method, "post") == 0);
            }
        }
        // Input (self-closing)
        else if (tag_is(name, name_len, "input") && !closing) {
            flush_acc();
            char itype[32] = "text";
            char fname[128] = "";
            char fvalue[256] = "";
            char placeholder[128] = "";
            get_attr(tag_str, "type", itype, sizeof(itype));
            get_attr(tag_str, "name", fname, sizeof(fname));
            get_attr(tag_str, "value", fvalue, sizeof(fvalue));
            get_attr(tag_str, "placeholder", placeholder, sizeof(placeholder));

            if (strcasecmp(itype, "hidden") == 0) {
                if (r->count < MAX_ELEMENTS) {
                    PageElement *e = &r->elems[r->count];
                    memset(e, 0, sizeof(*e));
                    e->type  = ELEM_HIDDEN;
                    e->name  = fname[0] ? pool_add(r, fname, strlen(fname)) : NULL;
                    e->value = fvalue[0] ? pool_add(r, fvalue, strlen(fvalue)) : NULL;
                    r->count++;
                }
            } else if (strcasecmp(itype, "submit") == 0) {
                const char *label = fvalue[0] ? fvalue : "Submit";
                if (r->count < MAX_ELEMENTS) {
                    PageElement *e = &r->elems[r->count];
                    memset(e, 0, sizeof(*e));
                    e->type  = ELEM_SUBMIT;
                    e->text  = pool_add(r, label, strlen(label));
                    e->name  = fname[0] ? pool_add(r, fname, strlen(fname)) : NULL;
                    e->value = fvalue[0] ? pool_add(r, fvalue, strlen(fvalue)) : NULL;
                    r->count++;
                }
            } else if (strcasecmp(itype, "text") == 0 ||
                       strcasecmp(itype, "search") == 0 ||
                       strcasecmp(itype, "email") == 0 ||
                       strcasecmp(itype, "url") == 0 ||
                       strcasecmp(itype, "password") == 0) {
                if (r->count < MAX_ELEMENTS) {
                    PageElement *e = &r->elems[r->count];
                    memset(e, 0, sizeof(*e));
                    e->type      = ELEM_INPUT;
                    e->name      = fname[0] ? pool_add(r, fname, strlen(fname)) : NULL;
                    e->value     = fvalue[0] ? pool_add(r, fvalue, strlen(fvalue)) : NULL;
                    e->text      = placeholder[0] ? pool_add(r, placeholder, strlen(placeholder)) : NULL;
                    e->multiline = false;
                    r->count++;
                }
            }
        }
        // Textarea
        else if (tag_is(name, name_len, "textarea")) {
            flush_acc();
            if (!closing) {
                char fname[128] = "";
                get_attr(tag_str, "name", fname, sizeof(fname));
                if (r->count < MAX_ELEMENTS) {
                    PageElement *e = &r->elems[r->count];
                    memset(e, 0, sizeof(*e));
                    e->type      = ELEM_INPUT;
                    e->name      = fname[0] ? pool_add(r, fname, strlen(fname)) : NULL;
                    e->multiline = true;
                    textarea_elem = r->count;
                    in_textarea   = true;
                    r->count++;
                }
            } else {
                // Accumulated text becomes the textarea default value
                if (in_textarea && textarea_elem >= 0 && acc_len > 0) {
                    text_acc[acc_len] = '\0';
                    decode_entities(text_acc);
                    // Trim
                    size_t s = 0;
                    while (s < acc_len && isspace((uint8_t)text_acc[s])) s++;
                    size_t e = strlen(text_acc);
                    while (e > s && isspace((uint8_t)text_acc[e-1])) e--;
                    if (e > s)
                        r->elems[textarea_elem].value = pool_add(r, text_acc + s, e - s);
                    acc_len = 0;
                }
                in_textarea   = false;
                textarea_elem = -1;
            }
        }
        // Select
        else if (tag_is(name, name_len, "select")) {
            flush_acc();
            if (!closing) {
                get_attr(tag_str, "name", select_name, sizeof(select_name));
                select_opts_len = 0;
                select_opts[0]  = '\0';
                in_select = true;
                // Create the element now; options filled on </select>
                if (r->count < MAX_ELEMENTS) {
                    PageElement *e = &r->elems[r->count];
                    memset(e, 0, sizeof(*e));
                    e->type = ELEM_SELECT;
                    e->name = select_name[0] ? pool_add(r, select_name, strlen(select_name)) : NULL;
                    select_elem = r->count;
                    r->count++;
                }
            } else {
                // Store accumulated options
                if (in_select && select_elem >= 0 && select_opts_len > 0)
                    r->elems[select_elem].value = pool_add(r, select_opts, select_opts_len);
                in_select   = false;
                select_elem = -1;
            }
        }
        // Option (inside select) — capture text on close
        else if (tag_is(name, name_len, "option") && in_select) {
            if (closing && acc_len > 0) {
                text_acc[acc_len] = '\0';
                decode_entities(text_acc);
                size_t s = 0;
                while (s < acc_len && isspace((uint8_t)text_acc[s])) s++;
                size_t e = strlen(text_acc);
                while (e > s && isspace((uint8_t)text_acc[e-1])) e--;
                if (e > s) {
                    if (select_opts_len > 0 && select_opts_len < sizeof(select_opts) - 1)
                        select_opts[select_opts_len++] = '\n';
                    size_t len = e - s;
                    if (select_opts_len + len < sizeof(select_opts) - 1) {
                        memcpy(select_opts + select_opts_len, text_acc + s, len);
                        select_opts_len += len;
                        select_opts[select_opts_len] = '\0';
                    }
                }
                acc_len = 0;
            } else if (!closing) {
                acc_len = 0;  // reset for new option text
            }
        }
        // Button
        else if (tag_is(name, name_len, "button")) {
            if (!closing) {
                flush_acc();  // flush prior text before button
            } else {
                // Accumulated text is button label
                if (acc_len > 0) {
                    text_acc[acc_len] = '\0';
                    decode_entities(text_acc);
                    size_t s = 0;
                    while (s < acc_len && isspace((uint8_t)text_acc[s])) s++;
                    size_t e = strlen(text_acc);
                    while (e > s && isspace((uint8_t)text_acc[e-1])) e--;
                    if (e > s && r->count < MAX_ELEMENTS) {
                        PageElement *el = &r->elems[r->count];
                        memset(el, 0, sizeof(*el));
                        el->type = ELEM_SUBMIT;
                        el->text = pool_add(r, text_acc + s, e - s);
                        r->count++;
                    }
                    acc_len = 0;
                }
            }
        }
        // Block elements — paragraph break
        else if (tag_is(name, name_len, "div") ||
                 tag_is(name, name_len, "li")  ||
                 tag_is(name, name_len, "td")  ||
                 tag_is(name, name_len, "tr")) {
            flush_acc();
            if (tag_is(name, name_len, "div")) {
                if (!closing) {
                    uint32_t c = parse_style_color(tag_str);
                    if (c) { cur_color = c; color_depth++; }
                } else if (color_depth > 0) {
                    color_depth--;
                    if (color_depth == 0) cur_color = 0;
                }
            }
        }

        p = tag_end + 1;
    }
    flush_acc();
    Serial.printf("HTML parse: %d elements, %zu bytes pool\n",
                  r->count, r->pool_used);
}
