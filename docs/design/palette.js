// Spectra 6 ink palette + font metrics for rcpapercolor preview.
// Color values mirror src/render/palette.cpp (color_to_m5gfx) but nudged
// toward reflective-ink-on-cream so the on-screen sim *feels* like Spectra 6.

const INK = {
  // The 6 addressable colors. Order matches the firmware enum.
  black:  '#1A1612',
  white:  '#ECE5D2',  // paper, slightly warm cream
  red:    '#C8312A',
  yellow: '#E5B72D',
  green:  '#3E8242',
  blue:   '#234B8E',
};

// Per-style "ink set" overrides — variations can use slightly different
// renderings of the same six tokens (e.g. riso = punchier, swiss = flatter).
const INK_SETS = {
  default: INK,
  punchy:  { ...INK, red: '#D43027', green: '#2F8A4A', blue: '#1F3F95', yellow: '#F0BD22' },
  muted:   { ...INK, red: '#B43A33', green: '#467142', blue: '#2D4A7A', yellow: '#D9AE36', black: '#1F1B17' },
};

// Firmware-side font metrics (px). Source: src/render/fonts_icons.cpp.
// These are the *box heights* — actual cap heights are ~70% of these.
const FONT_HEIGHTS = { h1: 36, h2: 28, body: 18, sm: 14 };
// Heuristic font-size in px to approximate cap height for canvas measureText.
const FONT_PX = { h1: 30, h2: 22, body: 15, sm: 11 };

// Per-style font systems. Each defines: display (h1/h2), body, and the
// CSS font-stack to use in <text> rendering. Loaded as Google Fonts.
const FONT_SYSTEMS = {
  // Faithful to FreeSans (M5GFX built-in). Closest metric-compatible
  // free family is Arimo/Liberation Sans.
  firmware: {
    name: 'Firmware (FreeSans)',
    body: '"Arimo", "Liberation Sans", "Helvetica", sans-serif',
    display: '"Arimo", "Liberation Sans", "Helvetica", sans-serif',
    googleFonts: ['Arimo:wght@400;700'],
  },
  magazine: {
    name: 'Magazine',
    body: '"Work Sans", sans-serif',
    display: '"Archivo Black", sans-serif',
    googleFonts: ['Archivo+Black', 'Work+Sans:wght@400;500;700'],
  },
  swiss: {
    name: 'Swiss / Poster',
    body: '"Space Grotesk", sans-serif',
    display: '"Space Grotesk", sans-serif',
    displayWeight: 700,
    googleFonts: ['Space+Grotesk:wght@400;500;700'],
  },
  editorial: {
    name: 'Editorial',
    body: '"Newsreader", serif',
    display: '"DM Serif Display", serif',
    googleFonts: ['DM+Serif+Display', 'Newsreader:wght@400;500;700', 'Archivo+Black'],
    labelFamily: '"Archivo Black", sans-serif',
  },
  riso: {
    name: 'Risograph',
    body: '"Work Sans", sans-serif',
    display: '"Big Shoulders Display", sans-serif',
    displayWeight: 900,
    googleFonts: ['Big+Shoulders+Display:wght@700;900', 'Work+Sans:wght@400;700'],
  },
  sticker: {
    name: 'Playful sticker',
    body: '"Nunito", sans-serif',
    display: '"Bowlby One", sans-serif',
    googleFonts: ['Bowlby+One', 'Nunito:wght@400;700;900'],
  },
  data: {
    name: 'Data-dense',
    body: '"JetBrains Mono", monospace',
    display: '"JetBrains Mono", monospace',
    displayWeight: 700,
    googleFonts: ['JetBrains+Mono:wght@400;500;700'],
  },
};

// Inject all Google Font links once.
(function loadFonts() {
  const families = new Set();
  for (const s of Object.values(FONT_SYSTEMS)) {
    for (const g of s.googleFonts || []) families.add(g);
  }
  if (!families.size) return;
  const link = document.createElement('link');
  link.rel = 'stylesheet';
  link.href = 'https://fonts.googleapis.com/css2?' +
    [...families].map(f => 'family=' + f).join('&') + '&display=swap';
  document.head.appendChild(link);
})();

// Off-DOM canvas for synchronous text measurement.
const __measureCanvas = document.createElement('canvas');
const __measureCtx = __measureCanvas.getContext('2d');

function measureText(value, size, weight, fontSystem) {
  const sys = fontSystem || FONT_SYSTEMS.firmware;
  const px = FONT_PX[size] || FONT_PX.body;
  const weightCss = weight === 'bold' ? 700 : 400;
  const isDisplay = size === 'h1' || size === 'h2';
  const family = isDisplay ? sys.display : sys.body;
  __measureCtx.font = `${weightCss} ${px}px ${family}`;
  return {
    w: Math.ceil(__measureCtx.measureText(value).width),
    h: FONT_HEIGHTS[size] || FONT_HEIGHTS.body,
    px,
    family,
    weightCss,
  };
}

window.RC = { INK, INK_SETS, FONT_HEIGHTS, FONT_PX, FONT_SYSTEMS, measureText };
