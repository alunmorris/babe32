# HTML Forms Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable the browser to parse, render, and submit HTML forms — covering text inputs, hidden fields, submit buttons, and GET/POST submission. This unlocks search engines, login pages, and other interactive sites.

**Architecture:** The HTML parser extracts `<form>`, `<input>`, `<textarea>`, `<select>`, and `<button>` tags into new element types. The page renderer creates LVGL textarea/dropdown/label widgets for each field. On submit, the form data is URL-encoded and sent via the fetcher (GET appends to URL, POST sends as request body). The existing on-screen keyboard associates with whichever textarea is focused.

**Tech Stack:** LVGL 8 (lv_textarea, lv_dropdown, lv_label as flat button), ESP32-S3, existing fetcher/parser

---

## Constraints

- **Flat widgets only**: No `lv_btn` (renders as noise on this display). Use `lv_label` with `LV_OBJ_FLAG_CLICKABLE` for submit buttons.
- **No shadows, gradients, rounded corners** — all styling must be flat.
- **Keyboard**: The existing on-screen keyboard must associate with whichever textarea the user taps (form fields or URL bar).
- **PSRAM text pool**: Form values are stored in the existing `ParseResult.text_pool` (64KB). Each input's current value lives in the LVGL textarea widget at render time, read on submit.
- **Single form per page**: For simplicity, track only the most recent `<form>` tag's action/method. Nested or multiple forms are rare on the pages this browser targets.

## Element Types to Add

| HTML | ElemType | Rendered as |
|------|----------|-------------|
| `<input type="text/search/email/password/url">` | `ELEM_INPUT` | `lv_textarea` (single line) |
| `<input type="hidden">` | `ELEM_HIDDEN` | Not rendered, value stored for submit |
| `<input type="submit">` / `<button type="submit">` | `ELEM_SUBMIT` | Flat clickable label |
| `<textarea>` | `ELEM_INPUT` | `lv_textarea` (multi-line) |
| `<select>` | `ELEM_SELECT` | `lv_dropdown` |
| `<form>` | (metadata) | Sets form action URL and method |

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `src/html_parser.h` | Modify | Add `ELEM_INPUT`, `ELEM_HIDDEN`, `ELEM_SUBMIT`, `ELEM_SELECT`; add fields to `PageElement` and `ParseResult` for form metadata |
| `src/html_parser.cpp` | Modify | Parse `<form>`, `<input>`, `<textarea>`, `<select>`, `<option>`, `<button>` |
| `src/page_renderer.h` | Modify | Add form submit callback type |
| `src/page_renderer.cpp` | Modify | Render form widgets; collect values on submit |
| `src/fetcher.h` | Modify | Add `fetch_page_post()` for POST requests |
| `src/fetcher.cpp` | Modify | Implement POST request in `do_request` |
| `src/ui_task.cpp` | Modify | Handle form submit callback; keyboard focus management for form fields |

---

## Task 1: Parser — Form Element Types

**Files:**
- Modify: `src/html_parser.h`
- Modify: `src/html_parser.cpp`

- [ ] **Step 1: Add element types and form fields to header**

In `html_parser.h`, add new element types and extend `PageElement`:

```c
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
    char    *href;    // resolved absolute URL (ELEM_LINK), or NULL
    char    *name;    // form field name (ELEM_INPUT/HIDDEN/SELECT)
    char    *value;   // default value (ELEM_INPUT/HIDDEN) or options (ELEM_SELECT)
    uint8_t  level;   // 1-6 for ELEM_HEADING, 0 otherwise
    bool     multiline; // true for <textarea>
} PageElement;
```

Add form metadata to `ParseResult`:

```c
typedef struct {
    PageElement elems[MAX_ELEMENTS];
    int         count;
    bool        error;
    int         http_status;
    char        form_action[512];   // form action URL (resolved)
    bool        form_is_post;       // true = POST, false = GET
    char        text_pool[65536];
    size_t      pool_used;
} ParseResult;
```

- [ ] **Step 2: Parse form tags in html_parser.cpp**

Add parsing for `<form>`, `<input>`, `<textarea>`, `<select>`, `<option>`, and `<button>` in the main tag dispatch. For `<select>`, accumulate `<option>` text values separated by `\n` (LVGL dropdown format).

```cpp
// Form
else if (tag_is(name, name_len, "form")) {
    flush_acc();
    if (!closing) {
        char action_raw[512] = "";
        get_attr(tag_str, "action", action_raw, sizeof(action_raw));
        if (action_raw[0]) {
            char resolved[512];
            if (url_resolve(url_get_base(), action_raw, resolved, sizeof(resolved)))
                strncpy(r->form_action, resolved, sizeof(r->form_action) - 1);
        } else {
            strncpy(r->form_action, base_url, sizeof(r->form_action) - 1);
        }
        char method[16] = "get";
        get_attr(tag_str, "method", method, sizeof(method));
        r->form_is_post = (strcasecmp(method, "post") == 0);
    }
}
// Input
else if (tag_is(name, name_len, "input")) {
    flush_acc();
    char type[32] = "text";
    char fname[128] = "";
    char fvalue[256] = "";
    char placeholder[128] = "";
    get_attr(tag_str, "type", type, sizeof(type));
    get_attr(tag_str, "name", fname, sizeof(fname));
    get_attr(tag_str, "value", fvalue, sizeof(fvalue));
    get_attr(tag_str, "placeholder", placeholder, sizeof(placeholder));

    if (strcasecmp(type, "hidden") == 0) {
        if (r->count < MAX_ELEMENTS) {
            PageElement *e = &r->elems[r->count];
            e->type = ELEM_HIDDEN;
            e->name = pool_add(r, fname, strlen(fname));
            e->value = pool_add(r, fvalue, strlen(fvalue));
            e->text = NULL;
            r->count++;
        }
    } else if (strcasecmp(type, "submit") == 0) {
        const char *label = fvalue[0] ? fvalue : "Submit";
        if (r->count < MAX_ELEMENTS) {
            PageElement *e = &r->elems[r->count];
            e->type = ELEM_SUBMIT;
            e->text = pool_add(r, label, strlen(label));
            e->name = fname[0] ? pool_add(r, fname, strlen(fname)) : NULL;
            e->value = fvalue[0] ? pool_add(r, fvalue, strlen(fvalue)) : NULL;
            r->count++;
        }
    } else if (strcasecmp(type, "text") == 0 || strcasecmp(type, "search") == 0 ||
               strcasecmp(type, "email") == 0 || strcasecmp(type, "url") == 0 ||
               strcasecmp(type, "password") == 0) {
        if (r->count < MAX_ELEMENTS) {
            PageElement *e = &r->elems[r->count];
            e->type = ELEM_INPUT;
            e->name = pool_add(r, fname, strlen(fname));
            e->value = fvalue[0] ? pool_add(r, fvalue, strlen(fvalue)) : NULL;
            e->text = placeholder[0] ? pool_add(r, placeholder, strlen(placeholder)) : NULL;
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
            e->type = ELEM_INPUT;
            e->name = pool_add(r, fname, strlen(fname));
            e->value = NULL;  // content comes as text between tags
            e->text = NULL;
            e->multiline = true;
            r->count++;
        }
        // Text between <textarea>...</textarea> becomes the value
        // — handled by accumulator, flushed on closing tag
    }
}
// Button
else if (tag_is(name, name_len, "button")) {
    flush_acc();
    if (!closing) {
        char type[32] = "submit";
        get_attr(tag_str, "type", type, sizeof(type));
        if (strcasecmp(type, "submit") == 0) {
            // Button text comes from content — will be flushed on close
            // Mark that we're accumulating submit button text
        }
    }
}
// Select
else if (tag_is(name, name_len, "select")) {
    flush_acc();
    if (!closing) {
        char fname[128] = "";
        get_attr(tag_str, "name", fname, sizeof(fname));
        // Select element created; options accumulated
    }
}
```

Note: The `<textarea>` content and `<button>` label accumulation need state flags similar to `in_link`. Add `in_textarea`, `in_select`, `in_button` booleans and accumulate text accordingly.

- [ ] **Step 3: Build and verify no regressions**

Run: `pio run`
Expected: Clean build. Existing pages still parse correctly (new types only trigger on form tags).

- [ ] **Step 4: Commit**

```bash
git add src/html_parser.h src/html_parser.cpp
git commit -m "feat: parse HTML form elements (input, select, textarea, button)"
```

---

## Task 2: Renderer — Form Widgets

**Files:**
- Modify: `src/page_renderer.h`
- Modify: `src/page_renderer.cpp`

- [ ] **Step 1: Add form submit callback to header**

```c
typedef void (*form_submit_cb_t)(const char *action_url, bool is_post,
                                  const char *encoded_body);

void page_render(lv_obj_t *container, const ParseResult *result,
                 link_tap_cb_t on_link_tap,
                 form_submit_cb_t on_form_submit);
```

- [ ] **Step 2: Render form input elements**

For `ELEM_INPUT`: create `lv_textarea` (single or multi-line), flat styled like the URL bar. Register a click handler that shows the on-screen keyboard associated with this textarea.

```cpp
case ELEM_INPUT: {
    lv_obj_t *ta = lv_textarea_create(container);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, e->multiline ? 80 : 32);
    lv_textarea_set_one_line(ta, !e->multiline);
    if (e->value) lv_textarea_set_text(ta, e->value);
    if (e->text) lv_textarea_set_placeholder_text(ta, e->text);
    // Flat styling (same as URL bar)
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_shadow_width(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 4, 0);
    // Store field name as user data for form collection
    lv_obj_set_user_data(ta, (void *)e->name);
    // Register click to show keyboard
    lv_obj_add_event_cb(ta, form_ta_click_cb, LV_EVENT_CLICKED, NULL);
    break;
}
```

- [ ] **Step 3: Render submit button**

For `ELEM_SUBMIT`: flat clickable label, styled like the Retry button.

```cpp
case ELEM_SUBMIT: {
    lv_obj_t *btn = lv_label_create(container);
    lv_label_set_text(btn, e->text);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, form_submit_click_cb, LV_EVENT_CLICKED, NULL);
    break;
}
```

- [ ] **Step 4: Render select dropdown**

For `ELEM_SELECT`: `lv_dropdown` with options from `e->value` (newline-separated).

```cpp
case ELEM_SELECT: {
    lv_obj_t *dd = lv_dropdown_create(container);
    lv_obj_set_width(dd, LV_PCT(100));
    if (e->value) lv_dropdown_set_options(dd, e->value);
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_text_color(dd, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_radius(dd, 0, 0);
    lv_obj_set_style_shadow_width(dd, 0, 0);
    lv_obj_set_user_data(dd, (void *)e->name);
    break;
}
```

- [ ] **Step 5: Implement form data collection and URL encoding on submit**

When the submit button is clicked, walk all children of the container. For each `lv_textarea`, read `lv_textarea_get_text()` with the field name from `lv_obj_get_user_data()`. For each `lv_dropdown`, read the selected option. For hidden fields, read from the stored `ParseResult`. URL-encode all name=value pairs joined by `&`.

```cpp
// URL-encode a string into out. Returns bytes written (excluding null).
static size_t url_encode(const char *src, char *out, size_t out_len);

// Collect form data from rendered widgets + hidden fields
static void collect_form_data(lv_obj_t *container, const ParseResult *result,
                               char *out, size_t out_len);
```

- [ ] **Step 6: Build and verify**

Run: `pio run`

- [ ] **Step 7: Commit**

```bash
git add src/page_renderer.h src/page_renderer.cpp
git commit -m "feat: render form widgets and collect form data on submit"
```

---

## Task 3: Fetcher — POST Support

**Files:**
- Modify: `src/fetcher.h`
- Modify: `src/fetcher.cpp`

- [ ] **Step 1: Add POST fetch function**

```c
// POST request. body is URL-encoded form data. Returns body length or negative status.
int fetch_page_post(const char *url, const char *body, char **buf_out);
```

- [ ] **Step 2: Implement POST in fetcher.cpp**

Add a `do_request_post()` variant or extend `do_request()` with an optional body parameter. The HTTP request changes:
- Method: `POST` instead of `GET`
- Add `Content-Type: application/x-www-form-urlencoded`
- Add `Content-Length: <body_len>`
- Append body after headers

```cpp
static int do_request(const char *url, const char *post_body, size_t *total_out) {
    // ... existing code ...
    const char *method = post_body ? "POST" : "GET";
    int req_len;
    if (post_body) {
        req_len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Proxy-Authorization: %s\r\n"
            "User-Agent: Mozilla/5.0 ...\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n\r\n"
            "%s",
            url, host, PROXY_AUTH, strlen(post_body), post_body);
    } else {
        // existing GET request
    }
    // ... rest unchanged ...
}
```

- [ ] **Step 3: Build and verify**

Run: `pio run`

- [ ] **Step 4: Commit**

```bash
git add src/fetcher.h src/fetcher.cpp
git commit -m "feat: add POST request support for form submission"
```

---

## Task 4: UI Task — Form Submit Wiring and Keyboard Focus

**Files:**
- Modify: `src/ui_task.cpp`

- [ ] **Step 1: Add form submit handler**

```cpp
static void on_form_submit(const char *action_url, bool is_post,
                            const char *encoded_body) {
    if (is_post) {
        // POST: fetch with body, navigate to result
        // Need to load via net_task with POST data
    } else {
        // GET: append ?encoded_body to action_url and navigate
        char full_url[1024];
        const char *sep = strchr(action_url, '?') ? "&" : "?";
        snprintf(full_url, sizeof(full_url), "%s%s%s", action_url, sep, encoded_body);
        do_navigate(full_url);
    }
}
```

- [ ] **Step 2: Wire form submit callback into page_render call**

Update the `page_render()` call to pass `on_form_submit`.

- [ ] **Step 3: Keyboard focus for form textareas**

When a form textarea is tapped, show the on-screen keyboard associated with that textarea (not the URL bar). The `form_ta_click_cb` in `page_renderer.cpp` needs to call back to `ui_task` to show the keyboard. Export a function:

```cpp
// In ui_task.h or via callback
void ui_show_keyboard_for(lv_obj_t *textarea);
```

This associates the keyboard with the given textarea, shows it, and resizes the content area.

- [ ] **Step 4: Handle POST via net_task**

For POST submissions, extend `net_task_load` to accept optional POST body:

```cpp
void net_task_load_post(const char *url, const char *post_body);
```

Or combine into a struct. The net task passes the body to `fetch_page_post()`.

- [ ] **Step 5: Build, upload, test**

Run: `pio run -t upload && pio device monitor`

Test with a search engine:
1. Navigate to `https://www.google.com`
2. Type in the search box
3. Tap "Google Search" button
4. Verify search results page loads

- [ ] **Step 6: Commit**

```bash
git add src/ui_task.cpp
git commit -m "feat: wire form submission with GET/POST and keyboard focus"
```

---

## Task 5: Integration Testing and Edge Cases

- [ ] **Step 1: Test GET form (search engine)**

Navigate to a search page. Type a query, submit. Verify the URL bar shows the query string and results load.

- [ ] **Step 2: Test POST form (if available)**

Find a POST form. Submit it. Verify the response page loads.

- [ ] **Step 3: Test forms with hidden fields**

Many forms include CSRF tokens or other hidden fields. Verify these are included in submission.

- [ ] **Step 4: Test forms with select dropdowns**

Verify dropdown renders, selection works, and selected value is submitted.

- [ ] **Step 5: Test keyboard switching between fields**

Tap different form fields. Verify the keyboard associates with the tapped field and URL bar keyboard still works.

- [ ] **Step 6: Final commit**

```bash
git add -A
git commit -m "feat: HTML forms support — text inputs, selects, GET/POST submit"
```

---

## Verification Checklist

- [ ] Text input fields render with placeholder text
- [ ] Submit buttons render as flat clickable labels
- [ ] Tapping a text field shows on-screen keyboard for that field
- [ ] GET form submission appends query string to action URL
- [ ] POST form submission sends body with correct Content-Type
- [ ] Hidden fields are included in form data
- [ ] Select dropdowns render and selection is included in form data
- [ ] Existing pages without forms render identically to before
- [ ] Back/forward navigation works across form submissions
- [ ] Keyboard switches correctly between URL bar and form fields

## Deferred

- **File upload** (`<input type="file">`): Requires multipart encoding, no use case on ESP32
- **Checkbox/radio**: Low priority, can add later as flat toggle widgets
- **JavaScript form validation**: Not possible without a JS engine
- **`<label>` association**: Nice-to-have, add if time permits
- **Multi-select**: Rare, defer
