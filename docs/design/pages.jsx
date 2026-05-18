// Page library — each page returns the rcpapercolor protocol JSON tree.
// Pages declare their preferred fontKey/inkKey, but the user can override
// from the Tweaks panel and the simulator.
//
// A few pages use proposed renderer extensions (donut, grid, pattern bg)
// which are clearly labeled. Everything else stays within the existing
// protocol primitives (vstack, hstack, text, hbar, sparkline, divider,
// spacer, icon, image, marker).

// ─── DSL helpers ───────────────────────────────────────────────────────────
const V = (props, ...children) => ({ type: 'vstack', ...props, children: children.flat().filter(Boolean) });
const H = (props, ...children) => ({ type: 'hstack', ...props, children: children.flat().filter(Boolean) });
const T = (value, props = {}) => ({ type: 'text', value, ...props });
const BAR = (props) => ({ type: 'hbar', ...props });
const SPK = (values, props = {}) => ({ type: 'sparkline', values, ...props });
const DIV = (props = {}) => ({ type: 'divider', thickness: 1, color: 'black', ...props });
const SPC = (size) => ({ type: 'spacer', ...(size != null ? { size } : {}) });
const ICN = (name, props = {}) => ({ type: 'icon', name, size: 14, color: 'black', ...props });
const DON = (props) => ({ type: 'donut', ...props });
const GRD = (props) => ({ type: 'grid', ...props });

// ─── Sample state ──────────────────────────────────────────────────────────

const SAMPLE = {
  claude: {
    five_h_pct: 42,
    five_h_reset: '2h 14m',
    seven_d_pct: 68,
    seven_d_reset: '4d 6h',
    opus: 91,
    sonnet: 34,
    plan: 'Max 20×',
    spark: [12, 18, 14, 22, 19, 28, 31],
    updated: '14:32',
  },
  codex: {
    primary_pct: 27,
    primary_label: '5H WINDOW',
    primary_reset: '34m',
    secondary_pct: 81,
    secondary_label: 'WEEKLY',
    secondary_reset: '3d 12h',
    credits: 47.5,
    plan: 'Plus',
    spark: [4, 7, 9, 5, 12, 8, 15],
    updated: '14:32',
  },
  calendar: {
    today: 'TUE · MAY 19',
    events: [
      { time: '10:00', dur: '30m', title: 'Standup', color: 'blue' },
      { time: '11:30', dur: '1h', title: 'Spectra 6 review', color: 'red' },
      { time: '14:00', dur: '45m', title: '1:1 — Rocky', color: 'green' },
      { time: '16:00', dur: '30m', title: 'Producer plan', color: 'blue' },
    ],
    next_label: 'next in 38m',
    free_after: '17:00',
  },
  todo: {
    list: 'TODAY · 5 / 8',
    items: [
      { done: true,  text: 'Fix layout pad bug',  prio: 'red' },
      { done: true,  text: 'NVS wear estimate',   prio: null },
      { done: true,  text: 'Riso variant',        prio: null },
      { done: false, text: 'Codex window math',   prio: 'red' },
      { done: false, text: 'Calendar producer',   prio: 'yellow' },
      { done: false, text: 'Solder helper jig',   prio: null },
    ],
  },
  github: {
    today_commits: 7,
    streak: 14,
    pr_open: 3,
    pr_review: 2,
    contrib: (() => {
      // 14 cols × 7 rows = 98 days. Most recent (today) is bottom-right.
      const arr = [];
      const seed = [0,0,12,40,5,0,20, 30,80,55,40,0,0,20, 70,40,20,0,5,15,90, 100,60,30,15,0,0,40, 50,30,0,10,25,55,80, 90,70,40,25,15,5,0, 0,5,15,40,60,80,30, 50,25,10,0,0,5,15, 30,55,70,90,40,20,0, 0,15,25,55,75,60,40, 60,30,15,0,5,25,50, 65,80,45,20,10,0,15, 35,50,70,55,30,15,5, 10,40,60,75,55,80,90];
      for (let i = 0; i < 98; i++) arr.push(seed[i] || 0);
      return arr;
    })(),
  },
  stocks: {
    indices: [
      { sym: 'SPX',  pct:  0.42, val: '4982.12', dir: 'up' },
      { sym: 'QQQ',  pct: -0.18, val:  '421.04', dir: 'down' },
      { sym: 'BTC',  pct:  3.74, val: '67,231',  dir: 'up' },
      { sym: 'AAPL', pct: -1.20, val:  '189.42', dir: 'down' },
      { sym: 'NVDA', pct:  5.81, val:  '894.50', dir: 'up' },
    ],
    spark: [100, 102, 98, 104, 101, 108, 112, 115, 113, 119],
  },
  habits: {
    week: ['M','T','W','T','F','S','S'],
    rows: [
      { name: 'READ',     marks: [1,1,1,1,0,1,0], color: 'blue' },
      { name: 'WORKOUT',  marks: [1,0,1,1,0,0,0], color: 'red' },
      { name: 'NO COFFEE',marks: [0,0,1,1,1,0,1], color: 'yellow' },
      { name: 'WRITE',    marks: [1,1,1,1,1,1,0], color: 'green' },
    ],
    streak: 4,
  },
  quote: {
    body_l1: 'BUILD',
    body_l2: 'PROTOCOLS,',
    body_l3: 'NOT',
    body_l4: 'PRODUCTS.',
    attrib: '— ALAN KAY',
  },
  weather: {
    temp: 64,
    cond: 'SUN · CLEAR',
  },
};

// ─── Page builders ─────────────────────────────────────────────────────────

// ── 1. Claude · Magazine (refined existing) ───
function buildClaudeMagazine(d = SAMPLE.claude) {
  const kpiRow = (label, pct, reset, color) => V({ gap: 2 },
    H({ align: 'center' },
      T(label, { size: 'sm', weight: 'bold' }),
      SPC(),
      T(reset ? `resets ${reset}` : '', { size: 'sm' }),
    ),
    H({ gap: 8, align: 'center' },
      T(pct != null ? `${pct}%` : '—', { size: 'h1', color, weight: 'bold' }),
      BAR({ pct: pct || 0, color, height: 18 }),
    ),
  );
  const modelCell = (label, pct) => V({ pad: 4, gap: 2 },
    T(label, { size: 'sm', weight: 'bold' }),
    T(`${pct}%`, { size: 'body', color: pct >= 80 ? 'red' : 'green', weight: 'bold' }),
  );
  return V({ pad: 14, gap: 6 },
    T('CLAUDE', { size: 'h1', color: 'white', bg: 'red', pad: 8, align: 'center', weight: 'bold' }),
    SPC(8),
    kpiRow('5H WINDOW', d.five_h_pct, d.five_h_reset, 'blue'),
    SPC(10),
    kpiRow('7D WINDOW', d.seven_d_pct, d.seven_d_reset, 'green'),
    SPC(12),
    T('BY MODEL · 7D', { size: 'sm', weight: 'bold' }),
    H({ gap: 6 }, modelCell('OPUS', d.opus), modelCell('SONNET', d.sonnet)),
    SPC(10),
    T('DAILY USAGE · LAST 7', { size: 'sm', weight: 'bold' }),
    SPK(d.spark, { color: 'blue', height: 56 }),
    SPC(),
    DIV(),
    H({ align: 'center', gap: 6 },
      ICN('claude', { color: 'red', size: 12 }),
      T(`${d.plan} · updated ${d.updated}`, { size: 'sm' }),
      SPC(),
      T('1/8', { size: 'sm' }),
    ),
  );
}

// ── 2. Claude · Swiss/Poster ───
// Asymmetric: huge "42" set against generous whitespace; secondary stats
// tucked into a sidebar. No knockout header.
function buildClaudeSwiss(d = SAMPLE.claude) {
  return V({ pad: 24, gap: 0 },
    H({ align: 'start', gap: 4 },
      T('CLAUDE', { size: 'sm', weight: 'bold' }),
      SPC(),
      T(`${d.plan.toUpperCase()}`, { size: 'sm' }),
    ),
    SPC(8),
    DIV({ thickness: 2 }),
    SPC(16),
    // Huge stat
    T('5-HOUR WINDOW', { size: 'sm', weight: 'bold' }),
    SPC(8),
    H({ align: 'end', gap: 4 },
      T(`${d.five_h_pct}`, { size: 'h1', color: 'blue', weight: 'bold' }),
      T('%', { size: 'h2', color: 'blue', weight: 'bold' }),
      SPC(),
      V({ gap: 2 },
        T('RESETS IN', { size: 'sm' }),
        T(d.five_h_reset, { size: 'body', weight: 'bold' }),
      ),
    ),
    SPC(8),
    BAR({ pct: d.five_h_pct, color: 'blue', height: 8 }),
    SPC(28),
    H({ gap: 16, align: 'start' },
      V({ gap: 4 },
        T('7-DAY', { size: 'sm', weight: 'bold' }),
        T(`${d.seven_d_pct}%`, { size: 'h2', color: 'green', weight: 'bold' }),
        BAR({ pct: d.seven_d_pct, color: 'green', height: 6 }),
      ),
      V({ gap: 4 },
        T('OPUS', { size: 'sm', weight: 'bold' }),
        T(`${d.opus}%`, { size: 'h2', color: d.opus >= 80 ? 'red' : 'black', weight: 'bold' }),
        BAR({ pct: d.opus, color: 'red', height: 6 }),
      ),
      V({ gap: 4 },
        T('SONNET', { size: 'sm', weight: 'bold' }),
        T(`${d.sonnet}%`, { size: 'h2', color: 'black', weight: 'bold' }),
        BAR({ pct: d.sonnet, color: 'black', height: 6 }),
      ),
    ),
    SPC(28),
    T('LAST 7 DAYS', { size: 'sm', weight: 'bold' }),
    SPC(6),
    SPK(d.spark, { color: 'blue', height: 72, dots: true }),
    SPC(),
    DIV(),
    SPC(8),
    H({ align: 'center' },
      T(d.updated, { size: 'sm', weight: 'bold' }),
      SPC(),
      T('1/8 · CLAUDE', { size: 'sm' }),
    ),
  );
}

// ── 3. Claude · Risograph ───
// Heavy overprinted blocks of red and blue. Halftone bg achieved via the
// proposed `bg_pattern` prop (renderer extension).
function buildClaudeRiso(d = SAMPLE.claude) {
  return V({ pad: 0, gap: 0 },
    // Red overprint header
    V({ pad: 18 },
      T('CLAUDE', { size: 'h1', color: 'white', bg: 'red', pattern: 'halftone', pad: 4, weight: 'bold' }),
      SPC(4),
      T('5-HOUR · 7-DAY USAGE', { size: 'sm', color: 'white', weight: 'bold' }),
    ),
    SPC(0),
    V({ pad: 18, gap: 12 },
      // Big number block, blue ink
      H({ align: 'end', gap: 6 },
        T(`${d.five_h_pct}`, { size: 'h1', color: 'blue', weight: 'bold' }),
        V({ gap: 0 },
          T('%', { size: 'h2', color: 'blue', weight: 'bold' }),
          T(d.five_h_reset, { size: 'sm', weight: 'bold' }),
        ),
        SPC(),
        T('5H', { size: 'h2', color: 'white', bg: 'blue', pad: 8, weight: 'bold' }),
      ),
      BAR({ pct: d.five_h_pct, color: 'blue', height: 16 }),
      H({ align: 'end', gap: 6 },
        T(`${d.seven_d_pct}`, { size: 'h1', color: 'green', weight: 'bold' }),
        V({ gap: 0 },
          T('%', { size: 'h2', color: 'green', weight: 'bold' }),
          T(d.seven_d_reset, { size: 'sm', weight: 'bold' }),
        ),
        SPC(),
        T('7D', { size: 'h2', color: 'white', bg: 'green', pad: 8, weight: 'bold' }),
      ),
      BAR({ pct: d.seven_d_pct, color: 'green', height: 16 }),
      SPC(6),
      H({ gap: 6 },
        T(`OPUS ${d.opus}%`, { size: 'body', color: 'black', bg: 'yellow', pattern: 'halftone', pad: 8, weight: 'bold' }),
        T(`SONNET ${d.sonnet}%`, { size: 'body', color: 'white', bg: 'black', pad: 8, weight: 'bold' }),
      ),
      SPK(d.spark, { color: 'red', height: 60, fill: true }),
      SPC(),
      H({ align: 'center' },
        T(`${d.plan} · ${d.updated}`, { size: 'sm', weight: 'bold' }),
      ),
    ),
  );
}

// ── 4. Codex · Magazine ───
function buildCodexMagazine(d = SAMPLE.codex) {
  const kpiRow = (label, pct, reset, color) => V({ gap: 2 },
    H({ align: 'center' },
      T(label, { size: 'sm', weight: 'bold' }),
      SPC(),
      T(`resets ${reset}`, { size: 'sm' }),
    ),
    H({ gap: 8, align: 'center' },
      T(`${pct}%`, { size: 'h1', color, weight: 'bold' }),
      BAR({ pct, color, height: 18 }),
    ),
  );
  return V({ pad: 14, gap: 6 },
    T('CODEX', { size: 'h1', color: 'white', bg: 'green', pad: 8, align: 'center', weight: 'bold' }),
    SPC(8),
    kpiRow(d.primary_label, d.primary_pct, d.primary_reset, 'blue'),
    SPC(12),
    kpiRow(d.secondary_label, d.secondary_pct, d.secondary_reset, 'blue'),
    SPC(12),
    T('DAILY USAGE · LAST 7', { size: 'sm', weight: 'bold' }),
    SPK(d.spark, { color: 'blue', height: 56 }),
    SPC(12),
    T(`CREDITS  $${d.credits.toFixed(2)}`, { size: 'body', weight: 'bold', bg: 'yellow', pad: 6, align: 'center' }),
    SPC(),
    DIV(),
    H({ align: 'center', gap: 6 },
      ICN('codex', { color: 'green', size: 12 }),
      T(`${d.plan} · updated ${d.updated}`, { size: 'sm' }),
      SPC(),
      T('2/8', { size: 'sm' }),
    ),
  );
}

// ── 5. Codex · Editorial newspaper ───
// Serif wordmark, hairline rules, all-caps Archivo labels, drop-cap primary.
function buildCodexEditorial(d = SAMPLE.codex) {
  return V({ pad: 20, gap: 0 },
    // Masthead
    H({ align: 'center' },
      T('VOL. 1', { size: 'sm' }),
      SPC(),
      T(d.updated, { size: 'sm' }),
      SPC(),
      T('NO. 47', { size: 'sm' }),
    ),
    SPC(4),
    DIV({ thickness: 2 }),
    SPC(4),
    T('Codex', { size: 'h1', weight: 'bold', align: 'center' }),
    SPC(2),
    H({ align: 'center', gap: 6 },
      DIV({ thickness: 0.5 }),
      T('USAGE DAILY', { size: 'sm', weight: 'bold' }),
      DIV({ thickness: 0.5 }),
    ),
    SPC(4),
    DIV({ thickness: 2 }),
    SPC(14),
    // Lead story — primary window
    T(d.primary_label, { size: 'sm', weight: 'bold' }),
    SPC(4),
    H({ align: 'end', gap: 8 },
      T(`${d.primary_pct}%`, { size: 'h1', color: 'blue', weight: 'bold' }),
      V({ gap: 2 },
        T('RESETS', { size: 'sm', weight: 'bold' }),
        T(d.primary_reset, { size: 'body', weight: 'bold' }),
      ),
    ),
    SPC(6),
    BAR({ pct: d.primary_pct, color: 'blue', height: 8 }),
    SPC(14),
    DIV(),
    SPC(8),
    // Secondary column
    H({ gap: 14, align: 'start' },
      V({ gap: 2 },
        T(d.secondary_label, { size: 'sm', weight: 'bold' }),
        T(`${d.secondary_pct}%`, { size: 'h2', color: d.secondary_pct >= 80 ? 'red' : 'black', weight: 'bold' }),
        BAR({ pct: d.secondary_pct, color: 'red', height: 6 }),
      ),
      V({ gap: 2 },
        T('CREDITS', { size: 'sm', weight: 'bold' }),
        T(`$${d.credits.toFixed(2)}`, { size: 'h2', weight: 'bold' }),
        T('AVAILABLE', { size: 'sm' }),
      ),
    ),
    SPC(12),
    DIV(),
    SPC(8),
    T('USAGE · 7-DAY TREND', { size: 'sm', weight: 'bold' }),
    SPC(4),
    SPK(d.spark, { color: 'blue', height: 70, dots: true }),
    SPC(),
    DIV({ thickness: 2 }),
    SPC(6),
    H({ align: 'center' },
      T(`${d.plan} · PAGE 2`, { size: 'sm', weight: 'bold' }),
    ),
  );
}

// ── 6. Codex · Playful sticker ───
function buildCodexSticker(d = SAMPLE.codex) {
  return V({ pad: 16, gap: 8 },
    H({ gap: 6 },
      T('CODEX', { size: 'h1', color: 'white', bg: 'black', pad: 8, weight: 'bold' }),
      SPC(),
      T(d.plan.toUpperCase(), { size: 'sm', color: 'black', bg: 'yellow', pad: 8, weight: 'bold' }),
    ),
    SPC(2),
    // Big donut
    H({ gap: 12, align: 'center' },
      DON({ pct: d.primary_pct, color: 'blue', track: 'white', size: 150, stroke: 22, label: `${d.primary_pct}%`, label_size: 36 }),
      V({ gap: 4 },
        T(d.primary_label, { size: 'sm', weight: 'bold' }),
        T(d.primary_reset, { size: 'body', color: 'blue', weight: 'bold' }),
        SPC(8),
        T(d.secondary_label, { size: 'sm', weight: 'bold' }),
        T(`${d.secondary_pct}%`, { size: 'h2', color: d.secondary_pct >= 80 ? 'red' : 'black', weight: 'bold' }),
        BAR({ pct: d.secondary_pct, color: 'red', height: 10 }),
      ),
    ),
    SPC(8),
    H({ gap: 6 },
      T('CREDITS', { size: 'sm', weight: 'bold', bg: 'green', color: 'white', pad: 6 }),
      T(`$${d.credits.toFixed(2)}`, { size: 'h2', color: 'black', bg: 'yellow', pad: 6, weight: 'bold' }),
    ),
    SPC(4),
    T('7-DAY USAGE', { size: 'sm', weight: 'bold' }),
    SPK(d.spark, { color: 'red', height: 56, fill: true, dots: true }),
    SPC(),
    H({ align: 'center', gap: 6 },
      ICN('codex', { color: 'green' }),
      T(`UPDATED ${d.updated}`, { size: 'sm', weight: 'bold' }),
    ),
  );
}

// ── 7. Calendar (data-dense) ───
function buildCalendar(d = SAMPLE.calendar) {
  return V({ pad: 18, gap: 6 },
    H({ align: 'center' },
      T('TODAY', { size: 'sm', weight: 'bold' }),
      SPC(),
      T(d.today, { size: 'sm', weight: 'bold' }),
    ),
    SPC(2),
    DIV({ thickness: 2 }),
    SPC(8),
    // Next event hero
    T('NEXT', { size: 'sm', weight: 'bold', color: 'white', bg: 'red', pad: 4 }),
    SPC(4),
    H({ align: 'end', gap: 6 },
      T(d.events[0].time, { size: 'h1', color: 'red', weight: 'bold' }),
      V({ gap: 2 },
        T(d.events[0].title, { size: 'body', weight: 'bold' }),
        T(`${d.events[0].dur} · ${d.next_label}`, { size: 'sm' }),
      ),
    ),
    SPC(10),
    DIV(),
    SPC(6),
    T('UP NEXT', { size: 'sm', weight: 'bold' }),
    SPC(2),
    // List of events
    ...d.events.slice(1).map(ev =>
      H({ gap: 8, align: 'center' },
        T(ev.time, { size: 'body', weight: 'bold', color: ev.color }),
        BAR({ pct: 100, color: ev.color, height: 4 }),
        T(ev.title, { size: 'sm', weight: 'bold' }),
      ),
    ),
    SPC(8),
    DIV(),
    SPC(6),
    H({ align: 'center', gap: 6 },
      T('FREE AFTER', { size: 'sm', weight: 'bold' }),
      T(d.free_after, { size: 'body', color: 'green', weight: 'bold', bg: 'yellow', pad: 4 }),
    ),
    SPC(),
    DIV({ thickness: 2 }),
    SPC(4),
    H({ align: 'center' },
      T('CALENDAR · 3/8', { size: 'sm' }),
    ),
  );
}

// ── 8. Todo (sticker style) ───
function buildTodo(d = SAMPLE.todo) {
  return V({ pad: 16, gap: 8 },
    H({ align: 'center' },
      T('TODO', { size: 'h1', color: 'white', bg: 'black', pad: 6, weight: 'bold' }),
      SPC(),
      T(d.list, { size: 'sm', weight: 'bold', bg: 'yellow', pad: 6 }),
    ),
    SPC(6),
    // List items
    ...d.items.map(item => H({ gap: 8, align: 'center' },
      // Checkbox
      item.done
        ? T('✓', { size: 'h2', color: 'green', weight: 'bold' })
        : T('☐', { size: 'h2', color: 'black' }),
      // Priority dot via colored badge
      item.prio
        ? T('!', { size: 'sm', color: 'white', bg: item.prio, pad: 3, weight: 'bold' })
        : SPC(8),
      T(item.text, { size: 'body', weight: item.done ? 'regular' : 'bold', color: item.done ? 'black' : 'black' }),
    )),
    SPC(),
    DIV({ thickness: 2 }),
    SPC(4),
    H({ gap: 6, align: 'center' },
      T('DONE', { size: 'sm', weight: 'bold', bg: 'green', color: 'white', pad: 4 }),
      BAR({ pct: 62, color: 'green', height: 12 }),
      T('62%', { size: 'sm', weight: 'bold' }),
    ),
    SPC(),
    H({ align: 'center' },
      T('TODO · 4/8', { size: 'sm' }),
    ),
  );
}

// ── 9. GitHub activity (Swiss, contribution grid) ───
function buildGithub(d = SAMPLE.github) {
  return V({ pad: 18, gap: 4 },
    H({ align: 'start' },
      T('GITHUB', { size: 'h1', weight: 'bold' }),
      SPC(),
      T('@rocry', { size: 'sm' }),
    ),
    SPC(2),
    DIV({ thickness: 2 }),
    SPC(14),
    // 3-stat strip
    H({ gap: 12, align: 'start' },
      V({ gap: 2 },
        T('TODAY', { size: 'sm', weight: 'bold' }),
        T(String(d.today_commits), { size: 'h1', color: 'green', weight: 'bold' }),
        T('COMMITS', { size: 'sm' }),
      ),
      V({ gap: 2 },
        T('STREAK', { size: 'sm', weight: 'bold' }),
        T(`${d.streak}d`, { size: 'h1', color: 'red', weight: 'bold' }),
        T('DAILY', { size: 'sm' }),
      ),
      V({ gap: 2 },
        T('PRs', { size: 'sm', weight: 'bold' }),
        T(`${d.pr_open}/${d.pr_review}`, { size: 'h1', color: 'blue', weight: 'bold' }),
        T('OPEN / REVIEW', { size: 'sm' }),
      ),
    ),
    SPC(16),
    T('CONTRIBUTIONS · 14W', { size: 'sm', weight: 'bold' }),
    SPC(6),
    GRD({ cols: 14, rows: 7, gap: 3, color: 'green', values: d.contrib, width: 360, height: 192 }),
    SPC(6),
    H({ gap: 4, align: 'center' },
      T('LESS', { size: 'sm' }),
      T(' ', { size: 'sm', bg: 'white', pad: 5 }),
      T(' ', { size: 'sm', bg: 'green', pad: 5 }),
      T(' ', { size: 'sm', bg: 'green', pad: 5 }),
      T(' ', { size: 'sm', bg: 'green', pad: 5 }),
      T(' ', { size: 'sm', bg: 'green', pad: 5 }),
      T('MORE', { size: 'sm' }),
      SPC(),
      T('5/8', { size: 'sm' }),
    ),
  );
}

// ── 10. Stocks (data-dense / editorial) ───
function buildStocks(d = SAMPLE.stocks) {
  return V({ pad: 16, gap: 4 },
    H({ align: 'center' },
      T('PORTFOLIO', { size: 'sm', weight: 'bold' }),
      SPC(),
      T('14:32 ET', { size: 'sm' }),
    ),
    SPC(2),
    DIV({ thickness: 2 }),
    SPC(8),
    // Big composite move
    H({ align: 'end', gap: 4 },
      T('+1.42%', { size: 'h1', color: 'green', weight: 'bold' }),
      V({ gap: 2 },
        T('TODAY', { size: 'sm', weight: 'bold' }),
        T('$124,820', { size: 'body', weight: 'bold' }),
      ),
    ),
    SPC(4),
    SPK(d.spark, { color: 'green', height: 56, fill: true }),
    SPC(8),
    DIV(),
    SPC(4),
    // Header row
    H({ gap: 8, align: 'center' },
      T('SYMBOL', { size: 'sm', weight: 'bold' }),
      SPC(),
      T('LAST', { size: 'sm', weight: 'bold' }),
      SPC(8),
      T('CHG', { size: 'sm', weight: 'bold' }),
    ),
    DIV({ thickness: 0.5 }),
    ...d.indices.map(s =>
      H({ gap: 8, align: 'center' },
        T(s.sym, { size: 'body', weight: 'bold' }),
        SPC(),
        T(s.val, { size: 'body', weight: 'bold' }),
        SPC(8),
        T(`${s.pct >= 0 ? '+' : ''}${s.pct.toFixed(2)}%`, {
          size: 'body',
          weight: 'bold',
          color: 'white',
          bg: s.dir === 'up' ? 'green' : 'red',
          pad: 4,
        }),
      ),
    ),
    SPC(),
    DIV({ thickness: 2 }),
    SPC(4),
    H({ align: 'center' },
      T('STOCKS · 6/8', { size: 'sm' }),
    ),
  );
}

// ── 11. Habit tracker (riso) ───
function buildHabits(d = SAMPLE.habits) {
  return V({ pad: 18, gap: 8 },
    H({ align: 'end' },
      T('HABITS', { size: 'h1', color: 'white', bg: 'red', pattern: 'halftone', pad: 6, weight: 'bold' }),
      SPC(),
      V({ gap: 2 },
        T('STREAK', { size: 'sm', weight: 'bold' }),
        T(`${d.streak}d`, { size: 'h2', color: 'red', weight: 'bold' }),
      ),
    ),
    SPC(2),
    // Day header row
    H({ gap: 4, align: 'center' },
      SPC(60),
      ...d.week.map(day => T(day, { size: 'sm', weight: 'bold', align: 'center', pad: 4 })),
    ),
    DIV({ thickness: 1 }),
    SPC(2),
    // Habit rows
    ...d.rows.map(row => H({ gap: 4, align: 'center' },
      T(row.name, { size: 'sm', weight: 'bold' }),
      SPC(),
      ...row.marks.map((m, i) =>
        m
          ? T('●', { size: 'h2', color: row.color, weight: 'bold' })
          : T('·', { size: 'h2', color: 'black' }),
      ),
    )),
    SPC(),
    DIV({ thickness: 2 }),
    SPC(4),
    H({ gap: 6, align: 'center' },
      T('WEEK', { size: 'sm', weight: 'bold' }),
      BAR({ pct: 72, color: 'green', height: 10 }),
      T('72%', { size: 'sm', weight: 'bold' }),
    ),
    SPC(4),
    H({ align: 'center' },
      T('HABITS · 7/8', { size: 'sm' }),
    ),
  );
}

// ── 12. Quote of the day (editorial / poster) ───
function buildQuote(d = SAMPLE.quote) {
  return V({ pad: 24, gap: 0 },
    H({ align: 'start' },
      T('QUOTE', { size: 'sm', weight: 'bold' }),
      SPC(),
      T('TUE · MAY 19', { size: 'sm' }),
    ),
    SPC(8),
    DIV({ thickness: 2 }),
    SPC(20),
    T('"', { size: 'h1', color: 'red', weight: 'bold' }),
    T(d.body_l1, { size: 'h1', weight: 'bold' }),
    T(d.body_l2, { size: 'h1', color: 'blue', weight: 'bold' }),
    T(d.body_l3, { size: 'h1', weight: 'bold' }),
    T(d.body_l4, { size: 'h1', color: 'red', weight: 'bold' }),
    SPC(20),
    DIV(),
    SPC(8),
    H({ align: 'end' },
      SPC(),
      T(d.attrib, { size: 'body', weight: 'bold' }),
    ),
    SPC(),
    DIV({ thickness: 2 }),
    SPC(4),
    H({ align: 'center' },
      T('QUOTE · 8/8', { size: 'sm' }),
    ),
  );
}

// ─── Registry ──────────────────────────────────────────────────────────────

const PAGES = [
  // Existing pages — 3 variations each
  { id: 'claude-magazine', title: 'Claude · Magazine', group: 'Claude', fontKey: 'firmware', inkKey: 'default', build: buildClaudeMagazine, note: 'Faithful to the existing implementation. Knockout red header, KPI rows with hbars, sparkline. Within protocol.' },
  { id: 'claude-swiss',    title: 'Claude · Swiss',    group: 'Claude', fontKey: 'swiss',    inkKey: 'default', build: buildClaudeSwiss,    note: 'Asymmetric, generous whitespace, big numeral hero. Within protocol.' },
  { id: 'claude-riso',     title: 'Claude · Risograph', group: 'Claude', fontKey: 'riso',    inkKey: 'punchy',  build: buildClaudeRiso,     note: 'Halftone overprint header + chunky blocks. Needs `pattern` prop on text (proposed extension).' },

  { id: 'codex-magazine',  title: 'Codex · Magazine',   group: 'Codex',  fontKey: 'firmware', inkKey: 'default', build: buildCodexMagazine,  note: 'Green knockout, KPIs, credits sticker. Within protocol.' },
  { id: 'codex-editorial', title: 'Codex · Editorial',  group: 'Codex',  fontKey: 'editorial', inkKey: 'muted', build: buildCodexEditorial, note: 'Newspaper masthead, serif wordmark, hairline rules. Within protocol (needs serif TTF baked).' },
  { id: 'codex-sticker',   title: 'Codex · Sticker',    group: 'Codex',  fontKey: 'sticker',  inkKey: 'punchy', build: buildCodexSticker,   note: 'Chunky display + donut chart. Needs `donut` primitive (proposed extension).' },

  // New page kinds
  { id: 'calendar',  title: 'Calendar · Next meetings', group: 'New', fontKey: 'data',      inkKey: 'default', build: buildCalendar, note: 'Lead event + list. Within protocol.' },
  { id: 'todo',      title: 'Todo · Today',            group: 'New', fontKey: 'sticker',    inkKey: 'punchy',  build: buildTodo,     note: 'Sticker style. Uses Unicode glyphs for checkboxes; could become an `icon` registry entry.' },
  { id: 'github',    title: 'GitHub · Activity',       group: 'New', fontKey: 'swiss',      inkKey: 'default', build: buildGithub,   note: 'Heatmap. Needs `grid` primitive (proposed extension).' },
  { id: 'stocks',    title: 'Stocks · Portfolio',      group: 'New', fontKey: 'data',       inkKey: 'default', build: buildStocks,   note: 'Tabular dense data. Within protocol.' },
  { id: 'habits',    title: 'Habits · Weekly',         group: 'New', fontKey: 'riso',       inkKey: 'punchy',  build: buildHabits,   note: 'Riso. Uses Unicode dot/circle glyphs.' },
  { id: 'quote',     title: 'Quote of the day',        group: 'New', fontKey: 'editorial',  inkKey: 'default', build: buildQuote,    note: 'Editorial poster. Within protocol.' },
];

window.PAGES = PAGES;
window.SAMPLE = SAMPLE;
window.PAGE_BUILDERS = {
  buildClaudeMagazine, buildClaudeSwiss, buildClaudeRiso,
  buildCodexMagazine, buildCodexEditorial, buildCodexSticker,
  buildCalendar, buildTodo, buildGithub, buildStocks, buildHabits, buildQuote,
};
