// rcpapercolor protocol renderer.
// Walks {type, ...props, children} trees, mirroring src/render/engine.cpp's
// two-pass (layout + paint) approach, and emits crisp SVG output to a 400x600
// surface. Faithful to the firmware's flex rules so what you see here is what
// the device draws.

const SCREEN_W = 400;
const SCREEN_H = 600;

// ─── LAYOUT PASS ─────────────────────────────────────────────────────────────

function naturalSize(node, parentCross, vertical, fontSystem) {
  switch (node.type) {
    case 'text': {
      const m = RC.measureText(node.value || '', node.size || 'body', node.weight, fontSystem);
      const pad = node.pad || 0;
      return { w: m.w + 2 * pad, h: m.h + 2 * pad };
    }
    case 'icon':  return { w: node.size || 16, h: node.size || 16 };
    case 'image': return { w: node.width || 0, h: node.height || 0 };
    case 'spacer':
      if (node.size != null) return { w: node.size, h: node.size };
      return { w: 0, h: 0 };
    case 'divider':
      return vertical
        ? { w: parentCross, h: node.thickness || 1 }
        : { w: node.thickness || 1, h: parentCross };
    case 'hbar':
      return vertical
        ? { w: parentCross, h: node.height || 12 }
        : { w: node.height || 12, h: parentCross };
    case 'sparkline':
      return vertical
        ? { w: parentCross, h: node.height || 40 }
        : { w: node.height || 40, h: parentCross };
    case 'donut': // proposed extension
      return { w: node.size || 80, h: node.size || 80 };
    case 'grid':  // proposed extension — heatmap grid
      return vertical
        ? { w: parentCross, h: (node.height || 60) }
        : { w: node.width || 60, h: parentCross };
    case 'marker': return { w: 1, h: 0 };
    case 'vstack':
    case 'hstack': return { w: 0, h: 0 }; // resolved recursively
    default: return { w: 0, h: 0 };
  }
}

// Returns true if the child should flex along its parent's main axis —
// i.e. share the remaining space rather than taking its natural size.
// Flex spacers always flex. In an hstack context, hbar / sparkline / divider
// also flex along the main (x) axis: their semantics are "fill the remaining
// row", with thickness/height describing the *cross* extent.
function isFlexMain(c, parentVertical) {
  if (c.type === 'spacer' && c.size == null) return true;
  if (!parentVertical && (c.type === 'hbar' || c.type === 'sparkline' || c.type === 'divider')) return true;
  return false;
}

function layoutStack(root, viewport, fontSystem) {
  const vertical = root.type === 'vstack';
  const pad = root.pad || 0;
  const gap = root.gap || 0;
  const align = root.align || 'start';
  const children = root.children || [];

  const inner = {
    x: viewport.x + pad,
    y: viewport.y + pad,
    w: Math.max(0, viewport.w - 2 * pad),
    h: Math.max(0, viewport.h - 2 * pad),
  };

  const crossAvail = vertical ? inner.w : inner.h;
  const mainAvail  = vertical ? inner.h : inner.w;

  const N = children.length;
  const childMain = new Array(N).fill(0);
  const childCross = new Array(N).fill(0);
  const flexMain = new Array(N).fill(false);
  let flexCount = 0;
  let fixedMain = 0;
  let maxCross = 0;

  // Pass 1: measure
  for (let i = 0; i < N; i++) {
    const c = children[i];
    if (c.type === 'spacer' && c.size == null) {
      flexCount++;
      flexMain[i] = true;
      continue;
    }
    if (isFlexMain(c, vertical)) {
      // hbar/sparkline/divider in hstack: flex along main, fixed cross.
      flexCount++;
      flexMain[i] = true;
      childMain[i] = 0;
      let crossH;
      if (c.type === 'hbar') crossH = c.height || 12;
      else if (c.type === 'sparkline') crossH = c.height || 40;
      else if (c.type === 'divider') crossH = c.thickness || 1;
      else crossH = 0;
      childCross[i] = crossH;
      if (childCross[i] > maxCross) maxCross = childCross[i];
      continue;
    }
    let sz;
    if (c.type === 'vstack' || c.type === 'hstack') {
      // Probe-layout nested stack to compute natural size.
      const probe = vertical
        ? { x: 0, y: 0, w: crossAvail, h: 32767 }
        : { x: 0, y: 0, w: 32767, h: crossAvail };
      const laid = layoutStack(c, probe, fontSystem);
      let usedMain = 0, usedCross = 0;
      // The inner-stack's main axis is x if it's an hstack, y if vstack.
      const innerVertical = c.type === 'vstack';
      for (const ch of laid.children) {
        // Skip children that flex along the inner stack's main axis —
        // their rects are inflated by the probe dimension (0x7FFF) and would
        // pollute the natural-size measurement of the inner stack.
        if (isFlexMain(ch.node, innerVertical)) continue;
        const endMain = vertical
          ? (ch.rect.y - laid.rect.y + ch.rect.h)
          : (ch.rect.x - laid.rect.x + ch.rect.w);
        const endCross = vertical
          ? (ch.rect.x - laid.rect.x + ch.rect.w)
          : (ch.rect.y - laid.rect.y + ch.rect.h);
        if (endMain > usedMain) usedMain = endMain;
        if (endCross > usedCross) usedCross = endCross;
      }
      // Add trailing pad (the probe inner subtracted pad on entry).
      const cpad = c.pad || 0;
      usedMain += cpad;
      usedCross += cpad;
      sz = vertical ? { w: usedCross, h: usedMain } : { w: usedMain, h: usedCross };
    } else {
      sz = naturalSize(c, crossAvail, vertical, fontSystem);
    }
    childMain[i]  = vertical ? sz.h : sz.w;
    childCross[i] = vertical ? sz.w : sz.h;
    fixedMain   += childMain[i];
    if (childCross[i] > maxCross) maxCross = childCross[i];
  }

  const gapsTotal = N > 1 ? (N - 1) * gap : 0;
  let flexEach = 0;
  if (flexCount > 0) {
    const rem = mainAvail - fixedMain - gapsTotal;
    flexEach = Math.max(0, Math.floor(rem / flexCount));
  }

  // Pass 2: place
  const out = { node: root, rect: viewport, children: [] };
  let cursor = vertical ? inner.y : inner.x;
  for (let i = 0; i < N; i++) {
    const c = children[i];
    let mainSz, crossSz;
    if (flexMain[i]) {
      mainSz = flexEach;
      crossSz = childCross[i] > 0 ? childCross[i] : crossAvail;
    } else {
      mainSz = childMain[i];
      crossSz = childCross[i] > 0 ? childCross[i] : crossAvail;
    }

    const crossBase = vertical ? inner.x : inner.y;
    let crossPos = crossBase;
    const refCross = Math.max(maxCross, crossSz);
    if (align === 'center') crossPos = crossBase + (refCross - crossSz) / 2;
    else if (align === 'end') crossPos = crossBase + (refCross - crossSz);

    const childVp = vertical
      ? { x: crossPos, y: cursor, w: crossSz, h: mainSz }
      : { x: cursor, y: crossPos, w: mainSz, h: crossSz };

    out.children.push(layoutNode(c, childVp, fontSystem));

    cursor += mainSz;
    if (i + 1 < N) cursor += gap;
  }
  return out;
}

function layoutNode(node, vp, fontSystem) {
  if (node.type === 'vstack' || node.type === 'hstack') return layoutStack(node, vp, fontSystem);
  return { node, rect: vp, children: [] };
}

// ─── PAINT PASS ──────────────────────────────────────────────────────────────

function paintNode(laid, ink, fontSystem, parentRect) {
  const n = laid.node;
  const r = laid.rect;
  switch (n.type) {
    case 'vstack':
    case 'hstack':
      return laid.children.map((c, i) => (
        <g key={i}>{paintNode(c, ink, fontSystem, r)}</g>
      ));
    case 'spacer': return null;
    case 'divider':
      // In hstack context, this is actually handled as flexMain (above) —
      // the painter still draws a hairline through the assigned rect.
      // Here we render it as a hairline along the rect's primary axis.
      if (n.thickness && r.w > r.h) {
        // wide rect → horizontal rule
        return <rect x={r.x} y={r.y + (r.h - n.thickness) / 2} width={r.w} height={n.thickness} fill={ink[n.color || 'black']} />;
      }
      if (n.thickness && r.h >= r.w) {
        // tall rect → vertical rule
        return <rect x={r.x + (r.w - n.thickness) / 2} y={r.y} width={n.thickness} height={r.h} fill={ink[n.color || 'black']} />;
      }
      return <rect x={r.x} y={r.y} width={r.w} height={n.thickness || 1} fill={ink[n.color || 'black']} />;
    case 'text':      return paintText(n, r, ink, fontSystem);
    case 'hbar':      return paintHbar(n, r, ink, fontSystem);
    case 'icon':      return paintIcon(n, r, ink);
    case 'image':     return paintImage(n, r, ink, fontSystem);
    case 'sparkline': return paintSparkline(n, r, ink);
    case 'donut':     return paintDonut(n, r, ink, fontSystem);
    case 'grid':      return paintGrid(n, r, ink);
    case 'marker':    return null; // overlay; handled by parent if used
    default: return null;
  }
}

function paintText(n, r, ink, fontSystem) {
  const color = ink[n.color || 'black'];
  let bg = n.bg ? ink[n.bg] : null;
  // Proposed extension: `pattern: 'halftone'` swaps solid bg for a halftone
  // pattern in the same color. Resolves to a <pattern> in defs.
  if (bg && n.pattern === 'halftone' && n.bg) {
    bg = `url(#ht-${n.bg})`;
  }
  const m = RC.measureText(n.value || '', n.size || 'body', n.weight, fontSystem);
  const pad = n.pad || 0;
  const isDisplay = (n.size === 'h1' || n.size === 'h2');
  const family = isDisplay ? fontSystem.display : fontSystem.body;
  const weightCss = n.weight === 'bold' ? (fontSystem.displayWeight || 700) : (isDisplay ? (fontSystem.displayWeight || 700) : 400);
  let tx;
  if (n.align === 'center')      tx = r.x + (r.w - m.w) / 2;
  else if (n.align === 'right' || n.align === 'end') tx = r.x + r.w - m.w - pad;
  else                           tx = r.x + pad;
  // Baseline-from-top: M5GFX puts cursor at top-left for setCursor + print;
  // we approximate by placing text baseline at y + pad + cap-height.
  const ty = r.y + pad + m.px * 0.84;
  return (
    <>
      {bg && <rect x={r.x} y={r.y} width={r.w} height={r.h} fill={bg} />}
      <text
        x={tx}
        y={ty}
        fill={color}
        fontFamily={family}
        fontWeight={weightCss}
        fontSize={m.px}
        style={{ letterSpacing: n.tracking || 0 }}
      >
        {n.value}
      </text>
    </>
  );
}

function paintHbar(n, r, ink, fontSystem) {
  const fg = ink[n.color || 'black'];
  const stroke = ink.black;
  let pct = n.pct;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  const innerW = Math.max(0, r.w - 4);
  const fill = Math.floor((innerW * pct) / 100);
  return (
    <>
      <rect x={r.x + 0.5} y={r.y + 0.5} width={r.w - 1} height={r.h - 1} fill="none" stroke={stroke} strokeWidth={1} />
      {fill > 0 && <rect x={r.x + 2} y={r.y + 2} width={fill} height={r.h - 4} fill={fg} />}
      {n.label_left && (
        <text x={r.x + 6} y={r.y + r.h / 2 + 4} fontFamily={fontSystem.body} fontWeight={700} fontSize={11} fill={ink.white}>
          {n.label_left}
        </text>
      )}
      {n.label_right && (
        <text x={r.x + r.w - 6} y={r.y + r.h / 2 + 4} textAnchor="end" fontFamily={fontSystem.body} fontWeight={700} fontSize={11} fill={ink.black}>
          {n.label_right}
        </text>
      )}
    </>
  );
}

function paintIcon(n, r, ink) {
  const s = n.size || Math.min(r.w, r.h);
  const x = r.x + (r.w - s) / 2;
  const y = r.y + (r.h - s) / 2;
  const color = ink[n.color || 'black'];
  return <IconGlyph name={n.name} x={x} y={y} size={s} color={color} ink={ink} />;
}

function paintImage(n, r, ink, fontSystem) {
  return (
    <>
      <rect x={r.x + 0.5} y={r.y + 0.5} width={r.w - 1} height={r.h - 1}
        fill={ink.white} stroke={ink.black} strokeWidth={1} />
      <text x={r.x + 4} y={r.y + 14} fontFamily={fontSystem.body} fontSize={10} fill={ink.black}>
        img:{n.name}
      </text>
    </>
  );
}

function paintSparkline(n, r, ink) {
  if (!n.values || !n.values.length) return null;
  const fg = ink[n.color || 'black'];
  const lo = Math.min(...n.values);
  const hi = Math.max(...n.values);
  const span = hi === lo ? 1 : (hi - lo);
  const yOf = v => r.y + r.h - ((v - lo) / span) * (r.h - 2) - 1;
  const pts = n.values.map((v, i) => {
    const x = r.x + ((i) * (r.w - 1)) / (n.values.length - 1 || 1);
    return [x, yOf(v)];
  });
  const path = pts.map(([x, y], i) => (i === 0 ? `M${x},${y}` : `L${x},${y}`)).join('');
  return (
    <>
      {n.fill && (
        <path
          d={path + ` L${r.x + r.w - 1},${r.y + r.h - 1} L${r.x},${r.y + r.h - 1} Z`}
          fill={fg}
          fillOpacity={0.2}
        />
      )}
      <path d={path} stroke={fg} strokeWidth={2} fill="none" />
      {n.dots && pts.map(([x, y], i) => (
        <rect key={i} x={x - 2} y={y - 2} width={4} height={4} fill={fg} />
      ))}
      <line x1={r.x} y1={r.y + r.h - 0.5} x2={r.x + r.w} y2={r.y + r.h - 0.5} stroke={ink.black} strokeWidth={1} />
    </>
  );
}

// Proposed primitive: donut chart
function paintDonut(n, r, ink, fontSystem) {
  const cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  const ro = Math.min(r.w, r.h) / 2 - 2;
  const ri = ro - (n.stroke || 14);
  const pct = Math.max(0, Math.min(100, n.pct));
  const color = ink[n.color || 'black'];
  const trackColor = ink[n.track || 'white'];
  // Track ring (full)
  const trackPath = ringPath(cx, cy, ri, ro, 0, 360);
  // Value arc
  const sweep = pct / 100 * 360;
  const valuePath = pct >= 99.999
    ? ringPath(cx, cy, ri, ro, 0, 359.99)
    : ringPath(cx, cy, ri, ro, -90, -90 + sweep);
  return (
    <>
      <path d={trackPath} fill={trackColor} stroke={ink.black} strokeWidth={1} />
      <path d={valuePath} fill={color} stroke={ink.black} strokeWidth={1} />
      {n.label && (
        <text x={cx} y={cy + 4} textAnchor="middle" fontFamily={fontSystem.display} fontWeight={fontSystem.displayWeight || 700} fontSize={n.label_size || 20} fill={ink.black}>
          {n.label}
        </text>
      )}
    </>
  );
}

function ringPath(cx, cy, ri, ro, a0, a1) {
  const rad = a => (a * Math.PI) / 180;
  const x0o = cx + ro * Math.cos(rad(a0)), y0o = cy + ro * Math.sin(rad(a0));
  const x1o = cx + ro * Math.cos(rad(a1)), y1o = cy + ro * Math.sin(rad(a1));
  const x0i = cx + ri * Math.cos(rad(a1)), y0i = cy + ri * Math.sin(rad(a1));
  const x1i = cx + ri * Math.cos(rad(a0)), y1i = cy + ri * Math.sin(rad(a0));
  const large = Math.abs(a1 - a0) > 180 ? 1 : 0;
  return [
    `M${x0o},${y0o}`,
    `A${ro},${ro} 0 ${large} 1 ${x1o},${y1o}`,
    `L${x0i},${y0i}`,
    `A${ri},${ri} 0 ${large} 0 ${x1i},${y1i}`,
    'Z',
  ].join(' ');
}

// Proposed primitive: heatmap grid (GitHub contributions, habits, etc.)
function paintGrid(n, r, ink) {
  const cols = n.cols || 14, rows = n.rows || 7;
  const cell = Math.min((r.w - (cols - 1) * (n.gap || 2)) / cols, (r.h - (rows - 1) * (n.gap || 2)) / rows);
  const gap = n.gap || 2;
  const values = n.values || [];
  const colorFor = v => {
    if (v == null || v <= 0) return ink.white;
    if (n.color === 'green') {
      if (v < 25) return '#C8DBC0';
      if (v < 50) return '#80B080';
      if (v < 75) return '#5C9858';
      return ink.green;
    }
    const base = ink[n.color || 'black'];
    return base;
  };
  const out = [];
  for (let cIdx = 0; cIdx < cols; cIdx++) {
    for (let rIdx = 0; rIdx < rows; rIdx++) {
      const i = cIdx * rows + rIdx;
      const v = values[i];
      const x = r.x + cIdx * (cell + gap);
      const y = r.y + rIdx * (cell + gap);
      out.push(
        <rect key={`${cIdx}-${rIdx}`} x={x} y={y} width={cell} height={cell}
              fill={colorFor(v)} stroke={ink.black} strokeWidth={0.5} />
      );
    }
  }
  return <>{out}</>;
}

// ─── ICON GLYPHS ────────────────────────────────────────────────────────────
// Tiny chunky SVG icons, drawn within {x,y,size} bounding box.

function IconGlyph({ name, x, y, size, color, ink }) {
  const t = (px) => px * (size / 16);
  const tx = (px) => x + t(px);
  const ty = (py) => y + t(py);
  switch (name) {
    case 'battery':
      return (
        <>
          <rect x={tx(1)} y={ty(4)} width={t(12)} height={t(8)} fill="none" stroke={color} strokeWidth={1} />
          <rect x={tx(13)} y={ty(6)} width={t(2)} height={t(4)} fill={color} />
          <rect x={tx(2.5)} y={ty(5.5)} width={t(7)} height={t(5)} fill={color} />
        </>
      );
    case 'wifi':
      return (
        <>
          <path d={`M ${tx(2)} ${ty(6)} Q ${tx(8)} ${ty(0)} ${tx(14)} ${ty(6)}`} stroke={color} strokeWidth={1.5} fill="none" />
          <path d={`M ${tx(5)} ${ty(9)} Q ${tx(8)} ${ty(5)} ${tx(11)} ${ty(9)}`} stroke={color} strokeWidth={1.5} fill="none" />
          <rect x={tx(7)} y={ty(11)} width={t(2)} height={t(2)} fill={color} />
        </>
      );
    case 'wifi_off':
      return (
        <>
          <path d={`M ${tx(2)} ${ty(6)} Q ${tx(8)} ${ty(0)} ${tx(14)} ${ty(6)}`} stroke={color} strokeWidth={1.5} fill="none" strokeOpacity={0.3} />
          <line x1={tx(2)} y1={ty(2)} x2={tx(14)} y2={ty(14)} stroke={color} strokeWidth={2} />
        </>
      );
    case 'warning':
      return (
        <>
          <path d={`M ${tx(8)} ${ty(1)} L ${tx(15)} ${ty(14)} L ${tx(1)} ${ty(14)} Z`} fill={color} stroke={ink.black} strokeWidth={0.5} />
          <rect x={tx(7.25)} y={ty(5)} width={t(1.5)} height={t(5)} fill={ink.black} />
          <rect x={tx(7.25)} y={ty(11)} width={t(1.5)} height={t(1.5)} fill={ink.black} />
        </>
      );
    case 'claude':
      return (
        <>
          <circle cx={tx(8)} cy={ty(8)} r={t(7)} fill={color} />
          <text x={tx(8)} y={ty(11)} textAnchor="middle" fontSize={t(10)} fontWeight={900} fontFamily="serif" fill={ink.white}>C</text>
        </>
      );
    case 'codex':
      return (
        <>
          <rect x={tx(1)} y={ty(1)} width={t(14)} height={t(14)} fill={color} />
          <text x={tx(8)} y={ty(11.5)} textAnchor="middle" fontSize={t(9)} fontWeight={900} fontFamily="monospace" fill={ink.white}>&gt;_</text>
        </>
      );
    case 'check':
      return <path d={`M ${tx(2)} ${ty(8)} L ${tx(7)} ${ty(13)} L ${tx(14)} ${ty(3)}`} stroke={color} strokeWidth={2.5} fill="none" />;
    case 'circle':
      return <circle cx={tx(8)} cy={ty(8)} r={t(6)} fill="none" stroke={color} strokeWidth={1.5} />;
    case 'square':
      return <rect x={tx(2)} y={ty(2)} width={t(12)} height={t(12)} fill={color} />;
    case 'arrow_up':
      return <path d={`M ${tx(8)} ${ty(2)} L ${tx(14)} ${ty(10)} L ${tx(2)} ${ty(10)} Z`} fill={color} />;
    case 'arrow_down':
      return <path d={`M ${tx(8)} ${ty(14)} L ${tx(2)} ${ty(6)} L ${tx(14)} ${ty(6)} Z`} fill={color} />;
    case 'thermometer':
      return (
        <>
          <rect x={tx(6.5)} y={ty(1.5)} width={t(3)} height={t(9)} rx={t(1.5)} fill="none" stroke={color} strokeWidth={1.2} />
          <rect x={tx(7.5)} y={ty(5)} width={t(1)} height={t(6)} fill={color} />
          <circle cx={tx(8)} cy={ty(12)} r={t(2.4)} fill={color} stroke={color} strokeWidth={0.5} />
        </>
      );
    case 'droplet':
      return (
        <path d={`M ${tx(8)} ${ty(1.5)} C ${tx(3)} ${ty(8)}, ${tx(2.5)} ${ty(11.5)}, ${tx(8)} ${ty(14.5)} C ${tx(13.5)} ${ty(11.5)}, ${tx(13)} ${ty(8)}, ${tx(8)} ${ty(1.5)} Z`} fill={color} />
      );
    case 'led':
      return (
        <>
          <circle cx={tx(8)} cy={ty(8)} r={t(6.5)} fill={color} stroke={ink.black} strokeWidth={0.8} />
          <circle cx={tx(6)} cy={ty(6)} r={t(1.8)} fill={ink.white} opacity={0.85} />
          {/* corona */}
          <circle cx={tx(8)} cy={ty(8)} r={t(7.5)} fill="none" stroke={color} strokeWidth={0.6} opacity={0.4} />
        </>
      );
    case 'mic':
      return (
        <>
          <rect x={tx(6)} y={ty(1.5)} width={t(4)} height={t(8)} rx={t(2)} fill={color} />
          <path d={`M ${tx(3)} ${ty(8)} Q ${tx(3)} ${ty(11.5)} ${tx(8)} ${ty(11.5)} Q ${tx(13)} ${ty(11.5)} ${tx(13)} ${ty(8)}`} fill="none" stroke={color} strokeWidth={1.3} />
          <rect x={tx(7.25)} y={ty(11.5)} width={t(1.5)} height={t(2.5)} fill={color} />
          <rect x={tx(5)} y={ty(14)} width={t(6)} height={t(1)} fill={color} />
        </>
      );
    case 'speaker':
      return (
        <>
          <path d={`M ${tx(2)} ${ty(6)} L ${tx(6)} ${ty(6)} L ${tx(10)} ${ty(2)} L ${tx(10)} ${ty(14)} L ${tx(6)} ${ty(10)} L ${tx(2)} ${ty(10)} Z`} fill={color} />
          <path d={`M ${tx(12)} ${ty(5)} Q ${tx(15)} ${ty(8)} ${tx(12)} ${ty(11)}`} fill="none" stroke={color} strokeWidth={1.2} />
          <path d={`M ${tx(13.5)} ${ty(3)} Q ${tx(17.5)} ${ty(8)} ${tx(13.5)} ${ty(13)}`} fill="none" stroke={color} strokeWidth={1.2} opacity={0.6} />
        </>
      );
    case 'sd':
      return (
        <>
          <path d={`M ${tx(3)} ${ty(2)} L ${tx(10)} ${ty(2)} L ${tx(13)} ${ty(5)} L ${tx(13)} ${ty(14)} L ${tx(3)} ${ty(14)} Z`} fill={color} stroke={ink.black} strokeWidth={0.6} />
          {[5, 7, 9, 11].map((cx, i) => (
            <rect key={i} x={tx(cx - 0.4)} y={ty(2)} width={t(0.8)} height={t(2.4)} fill={ink.white} />
          ))}
        </>
      );
    case 'ir':
      return (
        <>
          <rect x={tx(6.5)} y={ty(2)} width={t(3)} height={t(5)} rx={t(0.6)} fill={color} />
          <path d={`M ${tx(3)} ${ty(9)} Q ${tx(8)} ${ty(13)} ${tx(13)} ${ty(9)}`} fill="none" stroke={color} strokeWidth={1.3} />
          <path d={`M ${tx(1.5)} ${ty(11)} Q ${tx(8)} ${ty(16.5)} ${tx(14.5)} ${ty(11)}`} fill="none" stroke={color} strokeWidth={1.1} opacity={0.55} />
          <circle cx={tx(8)} cy={ty(10.5)} r={t(0.9)} fill={color} />
        </>
      );
    case 'clock':
      return (
        <>
          <circle cx={tx(8)} cy={ty(8)} r={t(6.5)} fill="none" stroke={color} strokeWidth={1.3} />
          <rect x={tx(7.5)} y={ty(3.5)} width={t(1)} height={t(5)} fill={color} />
          <rect x={tx(7.5)} y={ty(7.5)} width={t(4.5)} height={t(1)} fill={color} />
          <circle cx={tx(8)} cy={ty(8)} r={t(0.8)} fill={color} />
        </>
      );
    case 'imu':
      return (
        <>
          {/* isometric cube */}
          <path d={`M ${tx(8)} ${ty(2)} L ${tx(14)} ${ty(5)} L ${tx(14)} ${ty(11)} L ${tx(8)} ${ty(14)} L ${tx(2)} ${ty(11)} L ${tx(2)} ${ty(5)} Z`} fill={color} stroke={ink.black} strokeWidth={0.6} />
          <path d={`M ${tx(2)} ${ty(5)} L ${tx(8)} ${ty(8)} L ${tx(14)} ${ty(5)}`} fill="none" stroke={ink.white} strokeWidth={0.8} />
          <path d={`M ${tx(8)} ${ty(8)} L ${tx(8)} ${ty(14)}`} stroke={ink.white} strokeWidth={0.8} />
        </>
      );
    case 'chip':
      return (
        <>
          <rect x={tx(3)} y={ty(3)} width={t(10)} height={t(10)} fill={color} stroke={ink.black} strokeWidth={0.6} />
          <rect x={tx(5)} y={ty(5)} width={t(6)} height={t(6)} fill={ink.white} />
          {[5, 8, 11].map((p) => (
            <g key={p}>
              <rect x={tx(p - 0.4)} y={ty(1)} width={t(0.8)} height={t(2)} fill={color} />
              <rect x={tx(p - 0.4)} y={ty(13)} width={t(0.8)} height={t(2)} fill={color} />
              <rect x={tx(1)} y={ty(p - 0.4)} width={t(2)} height={t(0.8)} fill={color} />
              <rect x={tx(13)} y={ty(p - 0.4)} width={t(2)} height={t(0.8)} fill={color} />
            </g>
          ))}
        </>
      );
    case 'eink':
      return (
        <>
          <rect x={tx(2)} y={ty(2)} width={t(12)} height={t(12)} fill="none" stroke={color} strokeWidth={1.2} />
          <rect x={tx(4)} y={ty(4.5)} width={t(8)} height={t(1)} fill={color} />
          <rect x={tx(4)} y={ty(7)} width={t(5)} height={t(1)} fill={color} />
          <rect x={tx(4)} y={ty(9.5)} width={t(8)} height={t(1)} fill={color} />
          <rect x={tx(4)} y={ty(12)} width={t(3)} height={t(1)} fill={color} />
        </>
      );
    case 'calendar':
      return (
        <>
          <rect x={tx(2)} y={ty(3)} width={t(12)} height={t(11)} fill="none" stroke={color} strokeWidth={1.2} />
          <rect x={tx(2)} y={ty(3)} width={t(12)} height={t(3)} fill={color} />
          <rect x={tx(4.5)} y={ty(1.5)} width={t(1.5)} height={t(3)} fill={color} />
          <rect x={tx(10)} y={ty(1.5)} width={t(1.5)} height={t(3)} fill={color} />
          <rect x={tx(4)} y={ty(8)} width={t(2)} height={t(2)} fill={color} />
          <rect x={tx(7)} y={ty(8)} width={t(2)} height={t(2)} fill={color} />
          <rect x={tx(10)} y={ty(8)} width={t(2)} height={t(2)} fill={color} />
          <rect x={tx(4)} y={ty(11)} width={t(2)} height={t(2)} fill={color} />
          <rect x={tx(7)} y={ty(11)} width={t(2)} height={t(2)} fill={color} />
        </>
      );
    case 'github':
      return (
        <>
          <circle cx={tx(8)} cy={ty(8)} r={t(6.5)} fill={color} />
          <path d={`M ${tx(6)} ${ty(13)} L ${tx(6)} ${ty(11)} Q ${tx(4)} ${ty(11)} ${tx(4)} ${ty(9)} Q ${tx(4)} ${ty(7.5)} ${tx(5)} ${ty(7)} Q ${tx(5)} ${ty(5.5)} ${tx(5.5)} ${ty(5)} Q ${tx(6.5)} ${ty(5.2)} ${tx(7)} ${ty(5.8)} Q ${tx(8)} ${ty(5.5)} ${tx(9)} ${ty(5.8)} Q ${tx(9.5)} ${ty(5.2)} ${tx(10.5)} ${ty(5)} Q ${tx(11)} ${ty(5.5)} ${tx(11)} ${ty(7)} Q ${tx(12)} ${ty(7.5)} ${tx(12)} ${ty(9)} Q ${tx(12)} ${ty(11)} ${tx(10)} ${ty(11)} L ${tx(10)} ${ty(13)}`} fill="none" stroke={ink.white} strokeWidth={1.1} />
        </>
      );
    case 'chart':
      return (
        <>
          <rect x={tx(2)} y={ty(10)} width={t(2.5)} height={t(4)} fill={color} />
          <rect x={tx(5.5)} y={ty(7)} width={t(2.5)} height={t(7)} fill={color} />
          <rect x={tx(9)} y={ty(4)} width={t(2.5)} height={t(10)} fill={color} />
          <rect x={tx(12.5)} y={ty(9)} width={t(2.5)} height={t(5)} fill={color} />
        </>
      );
    case 'todo':
      return (
        <>
          <rect x={tx(2)} y={ty(2)} width={t(4)} height={t(4)} fill="none" stroke={color} strokeWidth={1.3} />
          <path d={`M ${tx(2.5)} ${ty(4.2)} L ${tx(4)} ${ty(5.5)} L ${tx(6)} ${ty(2.5)}`} stroke={color} strokeWidth={1.5} fill="none" />
          <rect x={tx(8)} y={ty(3)} width={t(7)} height={t(1.5)} fill={color} />
          <rect x={tx(2)} y={ty(8)} width={t(4)} height={t(4)} fill="none" stroke={color} strokeWidth={1.3} />
          <rect x={tx(8)} y={ty(9)} width={t(7)} height={t(1.5)} fill={color} />
        </>
      );
    case 'refresh':
      return (
        <>
          <path d={`M ${tx(2)} ${ty(8)} A ${t(6)} ${t(6)} 0 1 1 ${tx(8)} ${ty(14)}`} fill="none" stroke={color} strokeWidth={1.6} />
          <path d={`M ${tx(2)} ${ty(8)} L ${tx(5)} ${ty(5)} L ${tx(5)} ${ty(11)} Z`} fill={color} />
        </>
      );
    case 'sleep':
      return (
        <>
          <text x={tx(2)} y={ty(7)} fontSize={t(7)} fontWeight={900} fill={color} fontFamily="sans-serif">Z</text>
          <text x={tx(6)} y={ty(11)} fontSize={t(5.5)} fontWeight={900} fill={color} fontFamily="sans-serif">z</text>
          <text x={tx(10)} y={ty(14)} fontSize={t(4)} fontWeight={900} fill={color} fontFamily="sans-serif">z</text>
        </>
      );
    case 'sparkles':
      return (
        <>
          <path d={`M ${tx(8)} ${ty(1)} L ${tx(9.5)} ${ty(6.5)} L ${tx(15)} ${ty(8)} L ${tx(9.5)} ${ty(9.5)} L ${tx(8)} ${ty(15)} L ${tx(6.5)} ${ty(9.5)} L ${tx(1)} ${ty(8)} L ${tx(6.5)} ${ty(6.5)} Z`} fill={color} />
        </>
      );
    case 'sun':
      return (
        <>
          <circle cx={tx(8)} cy={ty(8)} r={t(3)} fill={color} />
          {[0, 45, 90, 135, 180, 225, 270, 315].map((a, i) => {
            const rad = (a * Math.PI) / 180;
            const x1 = tx(8) + t(5) * Math.cos(rad);
            const y1 = ty(8) + t(5) * Math.sin(rad);
            const x2 = tx(8) + t(7) * Math.cos(rad);
            const y2 = ty(8) + t(7) * Math.sin(rad);
            return <line key={i} x1={x1} y1={y1} x2={x2} y2={y2} stroke={color} strokeWidth={1.5} />;
          })}
        </>
      );
    case 'bolt':
      return (
        <path d={`M ${tx(9)} ${ty(1)} L ${tx(2)} ${ty(9)} L ${tx(7)} ${ty(9)} L ${tx(5)} ${ty(15)} L ${tx(13)} ${ty(7)} L ${tx(8)} ${ty(7)} Z`} fill={color} />
      );
    case 'button':
      return (
        <>
          <circle cx={tx(8)} cy={ty(8)} r={t(6.5)} fill="none" stroke={color} strokeWidth={1.2} />
          <circle cx={tx(8)} cy={ty(8)} r={t(4)} fill={color} />
        </>
      );
    case 'usbc':
      return (
        <>
          <rect x={tx(1.5)} y={ty(6)} width={t(13)} height={t(4)} rx={t(2)} fill="none" stroke={color} strokeWidth={1.2} />
          <rect x={tx(3.5)} y={ty(7.2)} width={t(9)} height={t(1.6)} rx={t(0.8)} fill={color} />
        </>
      );
    case 'antenna':
      return (
        <>
          <rect x={tx(7.5)} y={ty(7)} width={t(1)} height={t(8)} fill={color} />
          <path d={`M ${tx(8)} ${ty(7)} L ${tx(4)} ${ty(2)} M ${tx(8)} ${ty(7)} L ${tx(12)} ${ty(2)} M ${tx(8)} ${ty(7)} L ${tx(6)} ${ty(1)} M ${tx(8)} ${ty(7)} L ${tx(10)} ${ty(1)}`} stroke={color} strokeWidth={1.3} />
          <circle cx={tx(8)} cy={ty(7)} r={t(1.2)} fill={color} />
        </>
      );
    case 'flask':
      return (
        <>
          <path d={`M ${tx(6)} ${ty(2)} L ${tx(6)} ${ty(7)} L ${tx(3)} ${ty(13)} Q ${tx(3)} ${ty(14.5)} ${tx(5)} ${ty(14.5)} L ${tx(11)} ${ty(14.5)} Q ${tx(13)} ${ty(14.5)} ${tx(13)} ${ty(13)} L ${tx(10)} ${ty(7)} L ${tx(10)} ${ty(2)} Z`} fill="none" stroke={color} strokeWidth={1.2} />
          <path d={`M ${tx(4.5)} ${ty(10)} L ${tx(11.5)} ${ty(10)} L ${tx(13)} ${ty(13)} Q ${tx(13)} ${ty(14.5)} ${tx(11)} ${ty(14.5)} L ${tx(5)} ${ty(14.5)} Q ${tx(3)} ${ty(14.5)} ${tx(3)} ${ty(13)} Z`} fill={color} />
          <rect x={tx(5)} y={ty(1)} width={t(6)} height={t(1.2)} fill={color} />
        </>
      );
    case 'palette':
      return (
        <>
          <path d={`M ${tx(8)} ${ty(2)} Q ${tx(14.5)} ${ty(2)} ${tx(14.5)} ${ty(8)} Q ${tx(14.5)} ${ty(11)} ${tx(11)} ${ty(11)} L ${tx(10)} ${ty(11)} Q ${tx(8.5)} ${ty(11)} ${tx(8.5)} ${ty(12.5)} Q ${tx(8.5)} ${ty(14.5)} ${tx(7)} ${ty(14.5)} Q ${tx(1.5)} ${ty(14.5)} ${tx(1.5)} ${ty(8)} Q ${tx(1.5)} ${ty(2)} ${tx(8)} ${ty(2)} Z`} fill={color} stroke={ink.black} strokeWidth={0.6} />
          <circle cx={tx(4.5)} cy={ty(6)} r={t(1.1)} fill={ink.white} />
          <circle cx={tx(7)} cy={ty(4)} r={t(1.1)} fill={ink.white} />
          <circle cx={tx(10.5)} cy={ty(5)} r={t(1.1)} fill={ink.white} />
          <circle cx={tx(11.5)} cy={ty(8.5)} r={t(1.1)} fill={ink.white} />
        </>
      );
    case 'dot':
      return <circle cx={tx(8)} cy={ty(8)} r={t(3)} fill={color} />;
    default:
      // M5GFX placeholder: filled square
      return <rect x={x} y={y} width={size} height={size} fill={color} />;
  }
}

// ─── HALFTONE PATTERN (riso) ────────────────────────────────────────────────

function HalftoneDefs({ id, color, density = 'med' }) {
  const sizes = { light: 5, med: 3, dense: 2 };
  const s = sizes[density];
  return (
    <pattern id={id} x="0" y="0" width={s} height={s} patternUnits="userSpaceOnUse">
      <circle cx={s / 2} cy={s / 2} r={s / 3} fill={color} />
    </pattern>
  );
}

// ─── ROOT: PaperColorScreen ────────────────────────────────────────────────

function PaperColorScreen({ tree, fontKey = 'firmware', inkKey = 'default', flash = false, scale = 1, className = '' }) {
  const fontSystem = RC.FONT_SYSTEMS[fontKey] || RC.FONT_SYSTEMS.firmware;
  const ink = RC.INK_SETS[inkKey] || RC.INK;
  const w = SCREEN_W, h = SCREEN_H;
  const laid = React.useMemo(() => {
    try {
      return layoutNode(tree, { x: 0, y: 0, w, h }, fontSystem);
    } catch (e) {
      console.error('layout error', e);
      return null;
    }
  }, [tree, fontSystem]);

  return (
    <svg
      className={'pc-screen ' + className}
      viewBox={`0 0 ${w} ${h}`}
      width={w * scale}
      height={h * scale}
      style={{
        background: ink.white,
        shapeRendering: 'crispEdges',
        display: 'block',
      }}
    >
      <defs>
        <filter id="paperGrain" x="0" y="0" width="100%" height="100%">
          <feTurbulence type="fractalNoise" baseFrequency="0.85" numOctaves="2" />
          <feColorMatrix values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 0.04 0" />
          <feComposite operator="in" in2="SourceGraphic" />
        </filter>
        <HalftoneDefs id="ht-red" color={ink.red} density="med" />
        <HalftoneDefs id="ht-blue" color={ink.blue} density="med" />
        <HalftoneDefs id="ht-yellow" color={ink.yellow} density="med" />
        <HalftoneDefs id="ht-green" color={ink.green} density="med" />
        <HalftoneDefs id="ht-black" color={ink.black} density="med" />
      </defs>
      {/* paper */}
      <rect x="0" y="0" width={w} height={h} fill={ink.white} />
      {/* content */}
      <g style={{ shapeRendering: 'crispEdges' }}>
        {laid && paintNode(laid, ink, fontSystem)}
      </g>
      {/* grain overlay */}
      <rect x="0" y="0" width={w} height={h} fill={ink.black} opacity="0.025" filter="url(#paperGrain)" />
      {/* flash overlay */}
      {flash && <rect x="0" y="0" width={w} height={h} fill={ink.white} className="pc-flash" />}
    </svg>
  );
}

window.PaperColorScreen = PaperColorScreen;
window.IconGlyph = IconGlyph;
window.SCREEN_W = SCREEN_W;
window.SCREEN_H = SCREEN_H;
