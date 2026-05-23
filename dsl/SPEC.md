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
