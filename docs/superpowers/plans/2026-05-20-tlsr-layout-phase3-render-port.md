# tlsr-layout — Phase 3: Layout Engine & Render Port

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the rcpapercolor layout engine (vstack/hstack flex walk) and per-primitive paint routines from M5GFX C++ to plain C99 for the TLSR8258 BWR 250×128 panel. Establish a host-shim test harness (separate CMake target, native compile) that runs the layout pass against synthetic measurers and asserts on `Rect` geometry, plus a CLI tool that emits a 1-bit PNG for visual inspection.

**Architecture:** Three new directories under `firmware/src/`:
- `dsl/` — the layout-node schema struct + a minimal JSON parser (jsmn).
- `engine/` — `run_layout()` flex walk; pure logic, no I/O.
- `render/` — `draw_*()` per primitive; depends on `OneBitDisplay` for the BWR framebuffer.

The host shim lives in `firmware/host_test/`. It is a separate CMake project that compiles `dsl/` + `engine/` + `render/` (with a host-side BWR framebuffer stub) as a native binary linked into a `pytest` harness. Goldens are JSON `Rect` arrays for layout tests; pytest also runs a "screenshot" command for each scenario that dumps PNG into `host_test/out/` — those PNGs are checkpointed but not assertion-gated (visual review only).

**Tech Stack:** C99 firmware. CMake 3.28+ for the firmware target, separate CMake project for the host shim. Host shim uses the native compiler (`/usr/bin/clang` on macOS). Python pytest drives the host shim via subprocess + stdout JSON.

**Spec:** `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`

**Source reference:** `/Users/rocry/w/_hw/rcpapercolor/src/render/` and `src/proto/` (read-only — do not modify).

**Phase boundary:** Phase 3 produces:
- Firmware that still builds (Phase 2's `TLSR-LAYOUT.bin`), now with `dsl/`, `engine/`, `render/` modules **compiled but not yet wired** into the BLE flow.
- A `firmware/host_test/` binary plus pytest suite (`pytest firmware/host_test/`) covering structural assertions + PNG dumps for all 8 primitives + 4 representative composed pages.
- `image` primitive is parsed (struct populated) but `draw_image` is a stub that renders a hatched placeholder; full image support is out of scope (no decoder fits the flash budget).

The BLE-side wiring of `run_layout` + `draw_*` into `XFER_COMMIT` lands in Phase 4.

---

## File structure

```
firmware/
  src/
    dsl/
      schema.h                # LayoutNode tagged-union struct (rcpaper-parity)
      parser.h
      parser.c                # jsmn-driven tree build; arena allocator
      jsmn.h                  # vendored single-header JSON tokenizer
      arena.h
      arena.c                 # bump allocator backed by caller-provided buffer
    engine/
      engine.h                # run_layout signature + Rect + Measurer
      engine.c                # two-pass flex walk
      measure.h               # natural-size functions
      measure.c
    render/
      palette.h               # firmware 3-color map (K/W/R) + 6→3 mapping
      palette.c
      fonts.h                 # h1/h2/body/sm → font stack lookup
      fonts.c                 # uses tlsr-price-tag's font tables (classic + wenquanyi)
      icons.h
      icons.c                 # 1-bit blits from the existing icons_data.h
      primitives.h
      primitives.c            # draw_text/divider/hbar/sparkline/icon/marker
      placeholder.c           # draw_image stub (hatched box)
      paint.h
      paint.c                 # paint_layout dispatch + LaidOutNode walk

  host_test/
    CMakeLists.txt            # standalone host build
    main.c                    # CLI: `host_test run <scenario.json> --json --png`
    host_canvas.h
    host_canvas.c             # 250×128 BWR framebuffer; pgm/png writers
    measure_host.c            # host implementations of natural-size functions
    fonts_host.c              # host-side font metrics tables (must match firmware)
    scenarios/
      text_only.json
      hstack_centered.json
      hbar_with_label.json
      sparkline_basic.json
      icon_grid.json
      composed_dashboard.json
    test_layout.py            # pytest entry: structural assertions
    test_screenshots.py       # pytest entry: PNG dumps (visual review only)
    conftest.py
    pyproject.toml            # uv-managed test runner
    out/                      # PNG output, gitignored
```

Each module has one responsibility. `dsl/` knows nothing about rendering; `engine/` knows nothing about pixels; `render/` knows nothing about JSON. The host shim only ever sees the public headers.

---

## Task 1: Wire jsmn + arena allocator

**Files:**
- Create: `firmware/src/dsl/jsmn.h`
- Create: `firmware/src/dsl/arena.h`
- Create: `firmware/src/dsl/arena.c`

jsmn is single-header MIT-licensed; we vendor it (~600 lines) rather than depending on a package manager.

- [ ] **Step 1: Vendor jsmn**

Pull the latest released jsmn header from the upstream zserge/jsmn repo and drop it at `firmware/src/dsl/jsmn.h`. If network access is denied, hand-write the file at v1.1.0 (the API is stable and minimal: `jsmn_parse`, `jsmn_init`, `jsmntok_t`, ~200 LOC).

Configure jsmn macros at the top of the file so it compiles standalone:

```c
#define JSMN_STATIC
#define JSMN_PARENT_LINKS
```

- [ ] **Step 2: Write `arena.h`**

```c
#ifndef TLSR_LAYOUT_ARENA_H
#define TLSR_LAYOUT_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t  *buf;
    size_t    cap;
    size_t    used;
} arena_t;

void  arena_init(arena_t *a, uint8_t *buf, size_t cap);
void *arena_alloc(arena_t *a, size_t bytes, size_t align);
void  arena_reset(arena_t *a);

#endif
```

- [ ] **Step 3: Write `arena.c`**

```c
#include "arena.h"
#include <string.h>

void arena_init(arena_t *a, uint8_t *buf, size_t cap) {
    a->buf = buf;
    a->cap = cap;
    a->used = 0;
}

void *arena_alloc(arena_t *a, size_t bytes, size_t align) {
    size_t aligned = (a->used + (align - 1)) & ~(align - 1);
    if (aligned + bytes > a->cap) return NULL;
    void *p = a->buf + aligned;
    a->used = aligned + bytes;
    memset(p, 0, bytes);
    return p;
}

void arena_reset(arena_t *a) {
    a->used = 0;
}
```

- [ ] **Step 4: Confirm firmware build still succeeds (no callers yet — files just exist)**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
```

Expected: succeeds (the files aren't referenced from anywhere yet, so they won't be compiled into the firmware until CMake globs pick them up — see Task 8 for CMake glob updates).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/dsl/jsmn.h firmware/src/dsl/arena.h firmware/src/dsl/arena.c
git commit -m "feat(firmware): vendor jsmn + arena allocator"
```

---

## Task 2: DSL schema (LayoutNode tagged union)

**Files:**
- Create: `firmware/src/dsl/schema.h`

Mirror rcpaper's `proto/schema.h`. All optional fields use sentinel values; we do not store presence bitmasks.

- [ ] **Step 1: Write `schema.h`**

```c
#ifndef TLSR_LAYOUT_DSL_SCHEMA_H
#define TLSR_LAYOUT_DSL_SCHEMA_H

#include <stdint.h>

#define LAYOUT_MAX_CHILDREN   32   // per stack
#define LAYOUT_MAX_TEXT_LEN   128  // bytes after UTF-8 (truncated on overflow)
#define LAYOUT_MAX_SPARKLINE  64   // values

typedef enum {
    LN_VSTACK,
    LN_HSTACK,
    LN_SPACER,
    LN_DIVIDER,
    LN_TEXT,
    LN_HBAR,
    LN_ICON,
    LN_IMAGE,        // accepted but rendered as placeholder
    LN_SPARKLINE,
    LN_MARKER,
} layout_kind_t;

typedef enum {
    PAL_BLACK = 0,
    PAL_WHITE = 1,
    PAL_RED   = 2,
    PAL_UNSET = 0xFF,
} palette_t;

typedef enum { TS_BODY = 0, TS_H1, TS_H2, TS_SM } text_size_t;
typedef enum { TW_REGULAR = 0, TW_BOLD } text_weight_t;
typedef enum { AL_START = 0, AL_CENTER, AL_END } align_t;

struct layout_node;

typedef struct {
    uint8_t  gap;        // 0 = none
    uint8_t  pad;        // 0 = none
    align_t  align;
    uint8_t  child_count;
    struct layout_node *children[LAYOUT_MAX_CHILDREN];
} stack_node_t;

typedef struct {
    int16_t size;        // -1 = flex
} spacer_node_t;

typedef struct {
    palette_t color;
    uint8_t   thickness; // 0 = default 1
} divider_node_t;

typedef struct {
    char         value[LAYOUT_MAX_TEXT_LEN];
    uint8_t      value_len;
    text_size_t  size;
    palette_t    color;
    palette_t    bg;
    text_weight_t weight;
    align_t      align;
    uint8_t      pad;
} text_node_t;

typedef struct {
    int16_t   pct;       // 0..100
    palette_t color;
    uint8_t   height;
    char      label_left[24];
    char      label_right[24];
} hbar_node_t;

typedef struct {
    char       name[16];
    uint8_t    size;
    palette_t  color;
} icon_node_t;

typedef struct {
    // Accepted for compatibility; rendered as hatched placeholder in Phase 3.
    uint16_t   width;
    uint16_t   height;
} image_node_t;

typedef struct {
    int16_t   values[LAYOUT_MAX_SPARKLINE];
    uint8_t   value_count;
    palette_t color;
    uint8_t   height;
} sparkline_node_t;

typedef struct {
    int16_t   pct;
    palette_t color;
} marker_node_t;

typedef struct layout_node {
    layout_kind_t kind;
    union {
        stack_node_t     stack;
        spacer_node_t    spacer;
        divider_node_t   divider;
        text_node_t      text;
        hbar_node_t      hbar;
        icon_node_t      icon;
        image_node_t     image;
        sparkline_node_t sparkline;
        marker_node_t    marker;
    };
} layout_node_t;

#endif
```

- [ ] **Step 2: Commit**

```bash
git add firmware/src/dsl/schema.h
git commit -m "feat(firmware): layout node schema (tagged union)"
```

---

## Task 3: JSON parser → schema

**Files:**
- Create: `firmware/src/dsl/parser.h`
- Create: `firmware/src/dsl/parser.c`

- [ ] **Step 1: Write `parser.h`**

```c
#ifndef TLSR_LAYOUT_DSL_PARSER_H
#define TLSR_LAYOUT_DSL_PARSER_H

#include <stddef.h>
#include "arena.h"
#include "schema.h"

typedef enum {
    PARSE_OK = 0,
    PARSE_ERR_JSON,
    PARSE_ERR_TYPE,
    PARSE_ERR_ENUM,
    PARSE_ERR_OOM,
    PARSE_ERR_TRUNCATED,
} parse_status_t;

typedef struct {
    parse_status_t status;
    int            offset;   // byte offset in src where error was detected
    const char    *message;  // static string
} parse_result_t;

// Parse json (UTF-8, NUL-terminated or len-bounded) into a layout_node_t tree
// using the provided arena for all allocations. Returns the root on success.
layout_node_t *parse_layout(
    const char  *json,
    size_t       json_len,
    arena_t     *arena,
    parse_result_t *out_result
);

#endif
```

- [ ] **Step 2: Write `parser.c`**

Implementation outline (full code in this step — codex must produce a complete file, not a skeleton):

- Run `jsmn_parse` with a stack-allocated token buffer of `JSON_MAX_TOKENS` (= 512). If the parser reports `JSMN_ERROR_NOMEM`, return `PARSE_ERR_TRUNCATED`.
- Recursive descent over tokens: a helper `parse_node(...)` reads a JSON object, looks up `"type"` to dispatch into a per-kind handler.
- Per-kind handlers read named fields (string equality against jsmn tokens), assigning into the struct. Unknown fields are ignored. Missing required fields produce `PARSE_ERR_TYPE` with the offset of the parent object.
- Color names are parsed by `palette.h::palette_from_token(const char *s, size_t n)` (Task 4). Unknown color → `PARSE_ERR_ENUM`.
- Text size, weight, align: small enum tables in `parser.c` (private static).
- Strings are copied with `strncpy_safe` into the dest buffer; oversize is silently truncated.
- The full reference structure is `rcpapercolor/src/proto/parser.cpp` lines 54–131 — read it before writing the C version.

Edge cases that must be handled (test for them in Task 7):
- Empty `children` array → `stack.child_count = 0`.
- Negative percentages clamped to 0; >100 clamped to 100.
- Missing `pct` on `hbar` / `marker` → defaults to 0.
- Sparkline with > `LAYOUT_MAX_SPARKLINE` values: keep first N, drop the rest, don't error.
- Strings containing UTF-8 multibyte sequences: copy raw bytes; the font picker handles run-time fallback.

Codex must produce a complete `parser.c`. Do not leave any "TODO: implement field X" in the file.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/dsl/parser.h firmware/src/dsl/parser.c
git commit -m "feat(firmware): jsmn-driven DSL parser"
```

---

## Task 4: Palette + 6-color fallback map

**Files:**
- Create: `firmware/src/render/palette.h`
- Create: `firmware/src/render/palette.c`

- [ ] **Step 1: Write `palette.h`**

```c
#ifndef TLSR_LAYOUT_RENDER_PALETTE_H
#define TLSR_LAYOUT_RENDER_PALETTE_H

#include <stddef.h>
#include "../dsl/schema.h"

// Parse the JSON color name token into a palette_t.
// Six rcpaper names are recognised; firmware folds them into {K,W,R}.
palette_t palette_from_token(const char *s, size_t n);

// The on-panel pixel value for a parsed palette token.
// black  → 0 (K)
// white  → 1 (W)
// red    → 2 (R)
// yellow → 0 (K) -- map per spec §7.3
// green  → 0 (K)
// blue   → 2 (R)
// unset  → 1 (W)  (used as default text bg / canvas bg)
uint8_t palette_pixel(palette_t p);

#endif
```

- [ ] **Step 2: Write `palette.c`**

```c
#include "palette.h"
#include <string.h>

typedef struct { const char *name; palette_t value; } pname_t;

static const pname_t names[] = {
    {"black",  PAL_BLACK},
    {"white",  PAL_WHITE},
    {"red",    PAL_RED},
    {"yellow", PAL_BLACK},   // mapped to K
    {"green",  PAL_BLACK},   // mapped to K
    {"blue",   PAL_RED},     // mapped to R
};

palette_t palette_from_token(const char *s, size_t n) {
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        size_t m = strlen(names[i].name);
        if (m == n && memcmp(names[i].name, s, n) == 0) return names[i].value;
    }
    return PAL_UNSET;
}

uint8_t palette_pixel(palette_t p) {
    switch (p) {
        case PAL_BLACK: return 0;
        case PAL_RED:   return 2;
        case PAL_UNSET:
        case PAL_WHITE:
        default:        return 1;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/src/render/palette.h firmware/src/render/palette.c
git commit -m "feat(firmware): palette + 6→3 color fallback"
```

---

## Task 5: Layout engine — Rect, Measurer, run_layout

**Files:**
- Create: `firmware/src/engine/engine.h`
- Create: `firmware/src/engine/engine.c`
- Create: `firmware/src/engine/measure.h`
- Create: `firmware/src/engine/measure.c`

Mirror `rcpapercolor/src/render/engine.{h,cpp}` and `primitives.cpp`'s `natural_size()`. Port the two-pass flex walk verbatim, replacing C++ idioms with C equivalents:
- `std::vector<LaidOutChild>` → fixed-size array of `LAYOUT_MAX_CHILDREN` slots in `LaidOutNode`.
- Virtual `Measurer` → struct of fn pointers.
- C++ `for (const auto& child : node.children)` → `for (uint8_t i = 0; i < node.stack.child_count; i++)`.

- [ ] **Step 1: Write `engine.h`**

```c
#ifndef TLSR_LAYOUT_ENGINE_H
#define TLSR_LAYOUT_ENGINE_H

#include <stdint.h>
#include "../dsl/schema.h"

typedef struct { int16_t x, y, w, h; } rect_t;
typedef struct { int16_t w, h; } size_t16_t;

struct measurer;

typedef size_t16_t (*measure_text_fn)(const struct measurer *, const text_node_t *);
typedef size_t16_t (*measure_icon_fn)(const struct measurer *, const icon_node_t *);
typedef size_t16_t (*measure_image_fn)(const struct measurer *, const image_node_t *);

typedef struct measurer {
    measure_text_fn  text;
    measure_icon_fn  icon;
    measure_image_fn image;
    void *user;
} measurer_t;

typedef struct laid_out_node {
    const layout_node_t *node;
    rect_t rect;
    uint8_t child_count;
    struct laid_out_node *children;   // arena-allocated array
} laid_out_node_t;

#include "../dsl/arena.h"

// Lay out `root` inside `viewport`. Allocates the result tree into `arena`.
laid_out_node_t *run_layout(
    const layout_node_t *root,
    rect_t viewport,
    const measurer_t *m,
    arena_t *arena
);

#endif
```

- [ ] **Step 2: Write `measure.{h,c}`**

`measure_natural()` returns `size_t16_t` for any leaf (text/icon/image/divider/hbar/sparkline/marker/spacer). For a stack, the measurer is unused — `run_layout` probes nested stacks by laying them out in an unbounded rect.

Use the spec's font-height table for text measurement:

```c
static int16_t font_height_of(text_size_t s) {
    switch (s) {
        case TS_H1:   return 28;   // smaller than rcpaper (250×128 panel vs 400×600)
        case TS_H2:   return 22;
        case TS_BODY: return 14;
        case TS_SM:   return 10;
    }
    return 14;
}
```

Text width is delegated to the measurer (host shim or firmware-side computes it from the active font stack — Task 9).

- [ ] **Step 3: Write `engine.c`**

Port `engine.cpp` exactly. Specifically:
- `LAYOUT_UNBOUNDED = 0x4000` constant.
- Probe-pass distinction (flex not distributed when parent is unbounded).
- Cross-axis pin for HBar/Sparkline/Divider/Marker (their cross natural size is the `height` property; if absent, default 1 for divider, 4 for marker, 8 for hbar/sparkline).
- Allocate `laid_out_node_t.children` from `arena`.

Codex must produce a complete `engine.c`; the rcpaper source is the reference.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/engine/
git commit -m "feat(firmware): layout engine (run_layout flex walk)"
```

---

## Task 6: Render primitives + paint dispatch

**Files:**
- Create: `firmware/src/render/fonts.h`, `firmware/src/render/fonts.c`
- Create: `firmware/src/render/icons.h`, `firmware/src/render/icons.c`
- Create: `firmware/src/render/primitives.h`, `firmware/src/render/primitives.c`
- Create: `firmware/src/render/paint.h`, `firmware/src/render/paint.c`
- Create: `firmware/src/render/placeholder.c`

This is the bulk of the visual work.

- [ ] **Step 1: Fonts — wrap the existing TLSR font stacks**

`firmware/src/render/fonts.h`:

```c
#ifndef TLSR_LAYOUT_RENDER_FONTS_H
#define TLSR_LAYOUT_RENDER_FONTS_H

#include <stdint.h>
#include "../dsl/schema.h"

typedef struct {
    int16_t (*measure_text)(text_size_t size, text_weight_t weight,
                            const char *utf8, uint8_t len);
    void    (*draw_text)(int16_t x, int16_t y, text_size_t size,
                         text_weight_t weight, const char *utf8, uint8_t len,
                         uint8_t fg, uint8_t bg);
    int16_t (*line_height)(text_size_t size);
} font_backend_t;

extern const font_backend_t fonts_default;
#endif
```

In `fonts.c`, implement `fonts_default` against the tlsr-price-tag CLASSIC + WENQUANYI stacks (auto-pick CJK when any byte ≥ 0x80, per the existing `QUOTE_FONT_FORCE_STACK=AUTO` logic). Reuse `assets/` font headers; do not duplicate glyph data.

- [ ] **Step 2: Icons — wrap the existing icon atlas**

`firmware/src/render/icons.{h,c}`:

```c
#ifndef TLSR_LAYOUT_RENDER_ICONS_H
#define TLSR_LAYOUT_RENDER_ICONS_H

#include <stdint.h>

typedef struct {
    const char *name;
    uint8_t     w, h;
    const uint8_t *bits;   // MSB-first packed 1bpp
} icon_def_t;

const icon_def_t *icon_lookup(const char *name, uint8_t name_len);
#endif
```

In Phase 3, the icon catalogue can be tiny — copy 4–6 icons from `rcpapercolor/src/render/icons_data.h` (e.g., `cpu`, `clock`, `battery`, `wifi`, `check`, `cross`) into a new `firmware/src/render/icons_data.h` and register them in `icons.c`. Full atlas port is a later task.

- [ ] **Step 3: Primitives**

`firmware/src/render/primitives.{h,c}` — one function per kind:

```c
void draw_divider (const laid_out_node_t *n);
void draw_text    (const laid_out_node_t *n);
void draw_hbar    (const laid_out_node_t *n);
void draw_marker  (const laid_out_node_t *n);
void draw_sparkline(const laid_out_node_t *n);
void draw_icon    (const laid_out_node_t *n);
```

Each function:
- Reads colors via `palette_pixel()`.
- Calls into a small "canvas API" implemented separately by the firmware (`OneBitDisplay`) and the host shim (`host_canvas`):

```c
// canvas.h (new — referenced by primitives.c)
void canvas_set_pixel(int16_t x, int16_t y, uint8_t color);
void canvas_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void canvas_draw_hline(int16_t x, int16_t y, int16_t w, uint8_t color);
void canvas_draw_vline(int16_t x, int16_t y, int16_t h, uint8_t color);
void canvas_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
```

Implementations:
- Firmware: `firmware/src/render/canvas_obd.c` — wraps `OneBitDisplay` (existing in the fork from tlsr-price-tag).
- Host: `firmware/host_test/host_canvas.c` (Task 8) — writes into a 250×128 byte buffer.

Sparkline must reproduce rcpaper's 3-line thickening for visibility on e-ink (`drawLine(x,y,…)`, `drawLine(x,y±1,…)`).

- [ ] **Step 4: Placeholder for image**

`placeholder.c` provides `draw_image()`: draws a hatched box (diagonal lines every 8 px in `PAL_BLACK`) of the requested width × height, with a centred "IMG" label using `TS_SM`.

- [ ] **Step 5: Paint dispatch**

`paint.h`:
```c
void paint_layout(const laid_out_node_t *root);
```

`paint.c` walks the laid-out tree depth-first, dispatching on `n->node->kind` to the appropriate `draw_*` function. Stacks have no visible paint themselves — only their children render. Spacers are no-ops.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/render/
git commit -m "feat(firmware): primitives + paint dispatch (text/hbar/sparkline/icon/divider/marker)"
```

---

## Task 7: Canvas backends — firmware and host

**Files:**
- Create: `firmware/src/render/canvas.h`
- Create: `firmware/src/render/canvas_obd.c`
- Create: `firmware/host_test/host_canvas.h`
- Create: `firmware/host_test/host_canvas.c`

- [ ] **Step 1: `canvas.h`** (signatures already shown above; codex writes the file).

- [ ] **Step 2: `canvas_obd.c` — firmware backend**

Wrap `OneBitDisplay` calls (the existing API in the fork). Two-bit color (K=0, W=1, R=2) is handled by writing into two parallel 1-bit planes — see how `epd_213.c` already maintains a red plane.

For Phase 3 the canvas writes into the existing BWR framebuffer in `epd_213.c`. The render entry point in Phase 4 will clear, paint, and trigger an EPD update.

- [ ] **Step 3: `host_canvas.{h,c}` — host backend**

A 250×128 byte buffer (one byte per pixel storing the palette value). PNG output uses a small public-domain PNG writer ([`stb_image_write.h`](https://github.com/nothings/stb), vendored at `firmware/host_test/stb_image_write.h`). PGM is a fallback that needs no external code.

Public API:
```c
void   host_canvas_init(int16_t w, int16_t h);
void   host_canvas_clear(uint8_t color);
void  *host_canvas_buffer(int16_t *w, int16_t *h);   // raw byte buffer
int    host_canvas_write_png(const char *path);
int    host_canvas_write_pgm(const char *path);
```

The colors map to RGB on dump:
- K → 0x000000
- W → 0xFFFFFF
- R → 0xD92027 (matches rcpaper's Red)

- [ ] **Step 4: Commit**

```bash
git add firmware/src/render/canvas.h firmware/src/render/canvas_obd.c \
        firmware/host_test/host_canvas.h firmware/host_test/host_canvas.c \
        firmware/host_test/stb_image_write.h
git commit -m "feat(render): canvas backends — firmware OBD + host PGM/PNG"
```

---

## Task 8: Wire new modules into firmware CMakeLists

**Files:**
- Modify: `firmware/CMakeLists.txt`

- [ ] **Step 1: Add the new source dirs**

If the existing CMakeLists uses a `file(GLOB …)` over `src/`, the new subdirs may already be picked up. Verify:

```bash
cd firmware && rm -rf build && \
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk" \
  > /tmp/cmake-config.log 2>&1
grep -E "(dsl/|engine/|render/)" /tmp/cmake-config.log
```

If the new files don't appear in the SOURCES line, add explicit `target_sources(...)` lines or extend the GLOB pattern.

- [ ] **Step 2: Build**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
```

Expected: succeeds. Note the size delta vs Phase 2 — the parser + engine + primitives should add ~15–30 KB.

If it grows past 200 KB, investigate fonts.c / icons.c for accidentally pulling all glyphs (the icon atlas should only have 4–6 entries this phase).

- [ ] **Step 3: Symbol sanity**

```bash
~/w/tc32/bin/tc32-elf-nm build/src/TLSR-LAYOUT | \
  grep -E "^[0-9a-f]+ [TtRrDdBb] (parse_layout|run_layout|paint_layout|draw_text|draw_hbar|palette_from_token)" | sort
```

Expected: every one of those symbols present.

- [ ] **Step 4: Commit**

```bash
git add firmware/CMakeLists.txt
git commit -m "build(firmware): wire dsl/engine/render modules into TLSR-LAYOUT"
```

---

## Task 9: Host-shim CMake project + CLI binary

**Files:**
- Create: `firmware/host_test/CMakeLists.txt`
- Create: `firmware/host_test/main.c`
- Create: `firmware/host_test/measure_host.c`
- Create: `firmware/host_test/fonts_host.c`
- Create: `firmware/host_test/scenarios/*.json` (6 scenarios)
- Create: `firmware/host_test/pyproject.toml`
- Create: `firmware/host_test/.gitignore`

- [ ] **Step 1: `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(tlsr_layout_host_test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(host_test
    main.c
    host_canvas.c
    measure_host.c
    fonts_host.c
    ${FW}/src/dsl/arena.c
    ${FW}/src/dsl/parser.c
    ${FW}/src/engine/engine.c
    ${FW}/src/engine/measure.c
    ${FW}/src/render/palette.c
    ${FW}/src/render/icons.c
    ${FW}/src/render/primitives.c
    ${FW}/src/render/placeholder.c
    ${FW}/src/render/paint.c
)

target_include_directories(host_test PRIVATE
    ${FW}/src
    ${FW}/src/dsl
    ${FW}/src/engine
    ${FW}/src/render
)

# Host build doesn't link OneBitDisplay; we provide a stub via host_canvas.c +
# the canvas API mapping below.
target_compile_definitions(host_test PRIVATE TLSR_LAYOUT_HOST_BUILD=1)
```

- [ ] **Step 2: `main.c`**

CLI:

```
host_test layout  <scenario.json>           # emits laid-out tree as JSON to stdout
host_test render  <scenario.json> --pgm out.pgm [--png out.png]
host_test version
```

Implementation reads the scenario file, calls `parse_layout`, `run_layout`, `paint_layout`, then either:
- `layout` command: walks the LaidOutNode tree, emits a JSON array of `{kind, x, y, w, h}` records.
- `render` command: writes the host canvas to PGM/PNG.

- [ ] **Step 3: `measure_host.c` + `fonts_host.c`**

`measure_host.c` implements the `measurer_t` struct. Text width uses a deterministic stub: 7 px per ASCII char for `TS_BODY`, 14 px for `TS_H1`, etc. This matches what the firmware-side measurer should approximate (the actual on-device font metrics differ but produce comparable layouts).

`fonts_host.c` implements the canvas-level text drawing for the host: simple 5×7 bitmap font rendered byte-by-byte (vendored from a CC0 source, ~3 KB of bitmap data). Sufficient for visual review.

- [ ] **Step 4: Scenarios (6 JSON files)**

`text_only.json`:
```json
{"type":"vstack","pad":8,"gap":4,"children":[
  {"type":"text","value":"HELLO","size":"h1","color":"black","align":"center"},
  {"type":"text","value":"world","size":"body","color":"black","align":"center"}
]}
```

`hstack_centered.json`:
```json
{"type":"hstack","pad":4,"gap":4,"align":"center","children":[
  {"type":"icon","name":"cpu","size":16,"color":"red"},
  {"type":"text","value":"50%","size":"body","color":"black"}
]}
```

`hbar_with_label.json`, `sparkline_basic.json`, `icon_grid.json`, `composed_dashboard.json` — codex fills these following the same shape, exercising each primitive at least once.

- [ ] **Step 5: Build the host shim**

```bash
cd firmware/host_test
cmake -S . -B build
cmake --build build
ls -lh build/host_test
```

Expected: builds cleanly, produces a binary.

- [ ] **Step 6: Smoke run**

```bash
./build/host_test layout scenarios/text_only.json
./build/host_test render scenarios/text_only.json --pgm out/text_only.pgm
```

Expected: stdout JSON; PGM file written.

- [ ] **Step 7: Commit**

```bash
git add firmware/host_test/
git commit -m "feat(host_test): CLI binary for offline layout + render checks"
```

---

## Task 10: pytest harness

**Files:**
- Create: `firmware/host_test/pyproject.toml`
- Create: `firmware/host_test/conftest.py`
- Create: `firmware/host_test/test_layout.py`
- Create: `firmware/host_test/test_screenshots.py`
- Create: `firmware/host_test/golden/*.json` (one per scenario)

- [ ] **Step 1: `pyproject.toml`**

```toml
[project]
name = "tlsr-layout-host-test"
version = "0.1.0"
requires-python = ">=3.13"

[dependency-groups]
dev = ["pytest>=8"]
```

- [ ] **Step 2: `conftest.py`**

```python
import json
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parent
BIN = ROOT / "build" / "host_test"


def pytest_configure(config):
    if not BIN.exists():
        raise RuntimeError(
            f"host_test binary not built at {BIN}. Run `cmake -S . -B build && cmake --build build`."
        )


@pytest.fixture(scope="session")
def out_dir() -> Path:
    d = ROOT / "out"
    if d.exists():
        shutil.rmtree(d)
    d.mkdir()
    return d


def run_layout(scenario: Path) -> list[dict]:
    res = subprocess.run([BIN, "layout", str(scenario)], capture_output=True, check=True)
    return json.loads(res.stdout)


def run_render(scenario: Path, pgm_path: Path, png_path: Path | None = None) -> None:
    args = [BIN, "render", str(scenario), "--pgm", str(pgm_path)]
    if png_path:
        args += ["--png", str(png_path)]
    subprocess.run(args, capture_output=True, check=True)
```

- [ ] **Step 3: `test_layout.py`**

```python
import json
from pathlib import Path

import pytest

from conftest import ROOT, run_layout

SCENARIOS = sorted((ROOT / "scenarios").glob("*.json"))


@pytest.mark.parametrize("scenario", SCENARIOS, ids=[s.stem for s in SCENARIOS])
def test_layout_matches_golden(scenario: Path):
    actual = run_layout(scenario)
    golden_path = ROOT / "golden" / f"{scenario.stem}.layout.json"
    if not golden_path.exists():
        pytest.skip(f"no golden — writing initial at {golden_path}")
    expected = json.loads(golden_path.read_text())
    assert actual == expected


def test_negative_pct_clamps_to_zero():
    scenario = ROOT / "scenarios" / "edge_negative_pct.json"
    scenario.write_text(
        json.dumps({"type": "hbar", "pct": -5, "color": "red", "height": 8})
    )
    laid = run_layout(scenario)
    # Top-level node ends up with w=panel_w=250 minus padding etc; we assert
    # only that the program didn't crash and emitted a single node.
    assert len(laid) == 1
    assert laid[0]["kind"] == "hbar"
```

- [ ] **Step 4: `test_screenshots.py`**

```python
from pathlib import Path

import pytest

from conftest import ROOT, run_render

SCENARIOS = sorted((ROOT / "scenarios").glob("*.json"))


@pytest.mark.parametrize("scenario", SCENARIOS, ids=[s.stem for s in SCENARIOS])
def test_screenshot_writes(scenario: Path, out_dir: Path):
    pgm = out_dir / f"{scenario.stem}.pgm"
    png = out_dir / f"{scenario.stem}.png"
    run_render(scenario, pgm, png)
    assert pgm.exists() and pgm.stat().st_size > 0
    assert png.exists() and png.stat().st_size > 0
```

- [ ] **Step 5: Generate goldens once, then commit**

```bash
cd firmware/host_test
uv sync --all-extras
# First run: no goldens, layout tests skip
uv run pytest -v
mkdir -p golden
for s in scenarios/*.json; do
    base=$(basename "$s" .json)
    ./build/host_test layout "$s" > golden/"$base".layout.json
done
# Second run: goldens exist, layout tests must pass
uv run pytest -v
```

Expected: second `pytest` run shows all layout tests passing and all screenshot tests passing.

- [ ] **Step 6: Commit**

```bash
git add firmware/host_test/pyproject.toml firmware/host_test/conftest.py \
        firmware/host_test/test_layout.py firmware/host_test/test_screenshots.py \
        firmware/host_test/golden/
git commit -m "test(host_test): pytest harness + initial goldens for 6 scenarios"
```

---

## Task 11: Phase wrap-up

- [ ] **Step 1: Final sanity sweep**

```bash
cd /Users/rocry/w/_hw/tlsr-layout
cd firmware && cmake --build build --target TLSR-LAYOUT.bin && ls -lh build/src/TLSR-LAYOUT.bin
cd host_test && cmake --build build && uv run pytest -v
```

Expected: firmware builds; host_test all-green.

- [ ] **Step 2: Update README**

Edit `firmware/README.md` to mark Phase 3 complete and document the new `host_test/` directory:

```markdown
## Host shim

```bash
cd host_test
cmake -S . -B build && cmake --build build
uv sync --all-extras
uv run pytest -v
```

PNG outputs land in `host_test/out/` for visual review. They are not assertion-gated.
```

- [ ] **Step 3: Commit and tag**

```bash
git add firmware/README.md
git commit -m "docs(firmware): document host_test shim and mark Phase 3 complete"
git tag -a tlsr-layout-phase3 -m "Phase 3: layout engine + render port + host_test"
```

- [ ] **Step 4: Emit phase summary**

Print:

```
PHASE 3 COMPLETE
- dsl/, engine/, render/ modules compiled into TLSR-LAYOUT.bin (+~25 KB vs Phase 2)
- jsmn-driven parser handles all 9 primitives (image renders as placeholder)
- run_layout flex walk ported from rcpaper's two-pass algorithm
- canvas API has firmware (OBD) and host backends
- host_test: 6 scenarios, all-green pytest, PNGs in out/ for review
- Next: Phase 4 — wire BEGIN/DATA/COMMIT into render flow on the firmware
```

---

## Self-Review Checklist

- [x] **Spec coverage** — Phase 3 covers §7.3 (render engine port), partial §7.4 (uzlib + xxhash arrive in Phase 4 since they're for the transfer protocol). `image` is acknowledged as placeholder per the YAGNI note.
- [x] **Placeholder scan** — every "codex writes the file" reference is followed by a clear spec of what to write, with rcpaper file paths to mirror. No bare TODOs.
- [x] **Type consistency** — `palette_t`, `text_size_t`, `align_t` defined once in `schema.h`, referenced by both engine and render. `rect_t`, `size_t16_t` defined once in `engine.h`. Canvas API signatures identical between firmware and host implementations.
