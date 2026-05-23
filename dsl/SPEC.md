# rcink DSL — v1

The DSL is a JSON document describing one page of an e-ink dashboard. Producer emits it; firmware renders it.

## Top-level page

```json
{
  "version": 1,
  "id": "overview",
  "layout": { "type": "vstack", "children": [...] }
}
```

| Field     | Type    | Notes                                                          |
|-----------|---------|----------------------------------------------------------------|
| `version` | integer | major version. Devices reject unknown major. v1 = 1.           |
| `id`      | string  | page id; latest-wins per device.                               |
| `layout`  | object  | root node, must be a stack.                                    |

Unknown node types are skipped at render time — they don't bump version.

## Layout nodes

| `type`    | Required props | Optional props                                            |
|-----------|----------------|-----------------------------------------------------------|
| `vstack`  | `children`     | `gap`, `pad`, `align` (`start`/`center`/`end`)            |
| `hstack`  | `children`     | `gap`, `pad`, `align`                                     |
| `spacer`  | —              | `size` (px; omit → flex)                                  |
| `divider` | —              | `color`, `thickness`                                      |

## Content nodes

| `type`      | Required props      | Optional props                                                                |
|-------------|---------------------|-------------------------------------------------------------------------------|
| `text`      | `value`             | `size`, `color`, `weight`, `align`, `bg`, `pad`, `max_lines`                  |
| `hbar`      | `pct` (0–100)       | `color`, `height`, `label_left`, `label_right`                                |
| `icon`      | `name`              | `size`, `color`                                                               |
| `image`     | (`name` or `url`)   | `width`, `height`, `format`                                                   |
| `sparkline` | `values: number[]`  | `color`, `height`, `baseline` (`auto`/`0`)                                    |
| `marker`    | `pct`               | `color` — vertical tick on previous sibling hbar                              |
| `grid`      | `cols`, `rows`, `values: number[]` | `gap`, `width`, `height`                                       |

## Tokens

### Color (semantic + concrete, both accepted)

| Token       | Intent              | s3 (6-ink) | c3-x4 (B&W) | tlsr (BWR) |
|-------------|---------------------|-----------|--------------|------------|
| `black`     | foreground default  | black     | black        | black      |
| `white`     | background          | white     | white        | white      |
| `accent`    | primary emphasis    | red       | black        | red        |
| `warning`   | secondary emphasis  | yellow    | black        | red        |
| `success`   | positive state      | green     | black        | black      |
| `info`      | informational       | blue      | black        | black      |
| `muted`     | de-emphasised fg    | black     | black        | black      |

Concrete tokens (`red`, `yellow`, `green`, `blue`) remain accepted; firmware maps each to the same ink as the semantic alias it most resembles. New code SHOULD use semantic tokens.

### Size

| Token  | Intent                  | s3 px | c3-x4 px | tlsr px |
|--------|-------------------------|-------|----------|---------|
| `H1`   | big number / page title | 60    | 56       | 24      |
| `H2`   | section header          | 32    | 28       | 16      |
| `Body` | default body            | 20    | 18       | 12      |
| `Sm`   | caption / status bar    | 14    | 12       | 10      |

### Weight, Align

| Weight    | Align    |
|-----------|----------|
| `Regular` | `Start`  |
| `Bold`    | `Center` |
|           | `End`    |

## Wire format

UTF-8 JSON, whitespace-stripped (`json.dumps(..., separators=(",", ":"))`).

BLE transport adds zlib + chunking on top — see `producer/rcink/transport/ble_protocol.py`.

## Versioning

`version` is an integer. Bump on breaking changes. Adding new primitives or props is additive and does not bump version — devices skip unknown nodes.

## Golden testing

Each (device, page) pair has a checked-in PNG at `dsl/golden/<device>/<page>.png`. These are the visual contract every firmware's `host_test/` must match.

**CI / pytest:** `producer/tests/test_goldens.py` re-renders each page via the Python reference renderer (`rcink.preview_png`) at each device viewport and asserts byte-equal with the on-disk PNG. Any drift fails the test.

**Deliberate update** — when a producer change is supposed to alter visual output:

```bash
cd producer && uv run rcink-goldens
```

Inspect the resulting PNGs (open `dsl/golden/s3/overview.png` etc.), then commit them alongside the producer change.

**Per-firmware cross-validation** (future): each firmware's `host_test/` shim renders the same page set on the host (cross-compiled with stub I/O) and produces PNGs. CI compares those PNGs against the device's golden. Divergence between the Python reference and a firmware's C/C++ output indicates a bug in one side.

The Python reference renderer is "good enough not pixel-perfect" — font glyph anti-aliasing differs from each firmware's bundled font stack. What it does enforce strictly: layout math, color-token-to-ink mapping, primitive geometry, truncation thresholds. Those are what we want golden coverage on.
