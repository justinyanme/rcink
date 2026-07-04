# rcink DSL — v1

The DSL is a JSON document describing one page of an e-ink dashboard. Producer emits it; firmware renders it.

## Top-level layout

```json
{
  "type": "vstack",
  "children": [...]
}
```

The producer emits and validates a bare layout root. The root must be `vstack`.
Page ids, ordering, timestamps, and transport metadata live outside the DSL
payload.

Unknown node types or properties fail schema validation before ingest/transfer.

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
| `text`      | `value`             | `size`, `color`, `weight`, `align`, `bg`, `pad`, `dy`                         |
| `hbar`      | `pct` (0–100)       | `color`, `height`, `label_left`, `label_right`                                |
| `icon`      | `name`              | `size`, `color`                                                               |
| `image`     | (`name` or `url`)   | `width`, `height`, `format`                                                   |
| `sparkline` | `values: number[]`  | `color`, `height`                                                             |
| `marker`    | `pct`               | `color` — vertical tick on previous sibling hbar                              |
| `grid`      | `cols`, `rows`, `values: number[]` | `color` (accent token only; intensity ramp fixed), `gap`, `width`, `height` |

## Limits

Firmware-facing schema limits keep BLE payloads and small renderers predictable:

| Field                   | Limit |
|-------------------------|-------|
| stack `children`        | ≤ 32 nodes |
| `text.value`            | ≤ 128 chars |
| `sparkline.values`      | ≤ 64 samples |
| `grid.values`           | ≤ 256 cells |
| `hbar` labels           | ≤ 24 chars each |
| `icon.name`             | ≤ 16 chars |

## Tokens

### Color (semantic + concrete, both accepted)

| Token       | Intent              | s3 (6-ink) | t-amoled (RGB) | c3-x4 (B&W) | tlsr (BWR) |
|-------------|---------------------|------------|----------------|--------------|------------|
| `black`     | foreground default  | black      | black          | black        | black      |
| `white`     | background          | white      | white          | white        | white      |
| `accent`    | primary emphasis    | red        | red            | black        | red        |
| `warning`   | secondary emphasis  | yellow     | yellow         | black        | red        |
| `success`   | positive state      | green      | green          | black        | black      |
| `info`      | informational       | blue       | blue           | black        | black      |
| `muted`     | de-emphasised fg    | black      | black          | black        | black      |

Concrete tokens (`red`, `yellow`, `green`, `blue`) remain accepted; firmware maps each to the same ink as the semantic alias it most resembles. New code SHOULD use semantic tokens.

### Size

| Token  | Intent                  | s3 px | t-amoled px | c3-x4 px | tlsr px |
|--------|-------------------------|-------|-------------|----------|---------|
| `h1`   | big number / page title | 60    | 40          | 56       | 24      |
| `h2`   | section header          | 32    | 30          | 28       | 16      |
| `body` | default body            | 20    | 21          | 18       | 12      |
| `sm`   | caption / status bar    | 14    | 8           | 12       | 10      |

### Weight, Align

| Weight    | Align    |
|-----------|----------|
| `regular` | `start`  |
| `bold`    | `center` |
|           | `end`    |

## Wire format

UTF-8 JSON. Axiom stores compact JSON
(`json.dumps(..., ensure_ascii=False, separators=(",", ":"))`). BLE uses
canonical compact JSON with sorted keys before zlib + xxhash32 data versioning.

BLE transport adds zlib + chunking on top — see `producer/rcink/transport/ble_protocol.py`.

## Versioning

`schema.json` is the source of truth for v1. Breaking changes update the schema
and all producers/firmware together; no backward compatibility layer is kept.

## Golden testing

Each (device, page) pair has a checked-in PNG at `dsl/golden/<device>/<page>.png`. These are the visual contract every firmware's `host_test/` must match.

**CI / pytest:** `producer/tests/test_goldens.py` re-renders each page via the Python reference renderer (`rcink.preview_png`) at each device viewport and asserts byte-equal with the on-disk PNG. Any drift fails the test.

**Deliberate update** — when a producer change is supposed to alter visual output:

```bash
cd producer && uv run rcink-goldens regen
```

Inspect the resulting PNGs (open `dsl/golden/s3/overview.png` etc.), then commit them alongside the producer change.

**Per-firmware cross-validation** (future): each firmware's `host_test/` shim renders the same page set on the host (cross-compiled with stub I/O) and produces PNGs. CI compares those PNGs against the device's golden. Divergence between the Python reference and a firmware's C/C++ output indicates a bug in one side.

The Python reference renderer is "good enough not pixel-perfect" — font glyph anti-aliasing differs from each firmware's bundled font stack. What it does enforce strictly: layout math, color-token-to-ink mapping, primitive geometry, truncation thresholds. Those are what we want golden coverage on.
