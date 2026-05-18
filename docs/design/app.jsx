// Main app — composes the design canvas with simulator + page artboards.

function ArtboardCard({ page, scale = 0.55 }) {
  const tree = React.useMemo(() => page.build(), [page]);
  // Device dims at scale=1: 456 × 746 (matches device.jsx)
  const DEV_W = 456, DEV_H = 746;
  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      justifyContent: 'flex-start',
      gap: 12,
      padding: '24px 16px 16px',
      width: DEV_W * scale + 56,
      height: DEV_H * scale + 100,
      background: 'transparent',
    }}>
      <PaperColorDevice
        tree={tree}
        fontKey={page.fontKey}
        inkKey={page.inkKey}
        scale={scale}
        showLabels={true}
      />
      <div style={{
        fontFamily: '"JetBrains Mono", monospace',
        fontSize: 10,
        color: '#5C5648',
        letterSpacing: 1,
        textAlign: 'center',
        maxWidth: DEV_W * scale,
      }}>
        {page.note}
      </div>
    </div>
  );
}

function App() {
  const claudePages = PAGES.filter((p) => p.group === 'Claude');
  const codexPages = PAGES.filter((p) => p.group === 'Codex');
  const newPages = PAGES.filter((p) => p.group === 'New');

  const cardW = 320;
  const cardH = 580;
  const SIM_W = 1340, SIM_H = 800;

  return (
    <DesignCanvas>
      <DCSection
        id="intro"
        title="rcpapercolor"
        subtitle="Battery-powered Spectra 6 color e-ink on M5Stack PaperColor. ESP32-S3R8 · 400×600 · 6 ink palette · 1250 mAh · SHT40 environment · 2× RGB LED · 15-minute Axiom fetch cycle."
      >
        <DCArtboard id="brand" label="Brand · wordmark" width={1080} height={460}>
          <BrandHero />
        </DCArtboard>
        <DCArtboard id="hardware" label="Hardware · M5Stack PaperColor" width={720} height={620}>
          <HardwareCard />
        </DCArtboard>
        <DCArtboard id="env" label="Environment · SHT40 live" width={340} height={620}>
          <EnvCard />
        </DCArtboard>
        <DCArtboard id="led" label="RGB LED · state machine" width={1080} height={460}>
          <RgbLedCard />
        </DCArtboard>
        <DCArtboard id="ink" label="Spectra 6 · ink set" width={720} height={460}>
          <PaletteCard />
        </DCArtboard>
        <DCArtboard id="readme" label="Read this first" width={340} height={460}>
          <Readme />
        </DCArtboard>
        <DCArtboard id="legend" label="Style directions" width={1080} height={300}>
          <Legend />
        </DCArtboard>
      </DCSection>

      <DCSection
        id="simulator"
        title="Live simulator"
        subtitle="Buttons flip pages, BtnC forces re-fetch, every change triggers the e-ink redraw flash. Edit the protocol JSON live or fiddle with sample data."
      >
        <DCArtboard id="sim" label="M5Stack PaperColor · interactive" width={SIM_W} height={SIM_H}>
          <Simulator />
        </DCArtboard>
      </DCSection>

      <DCSection
        id="claude"
        title="Claude · 3 directions"
        subtitle="Three takes on the existing Claude usage page. First is faithful to the current magazine implementation; the next two propose where the visual language could push."
      >
        {claudePages.map((p) => (
          <DCArtboard key={p.id} id={p.id} label={p.title} width={cardW} height={cardH}>
            <ArtboardCard page={p} />
          </DCArtboard>
        ))}
      </DCSection>

      <DCSection
        id="codex"
        title="Codex · 3 directions"
        subtitle="Three takes on Codex. Magazine matches the current implementation; editorial and sticker variants demonstrate where serif and chunky-display systems could go."
      >
        {codexPages.map((p) => (
          <DCArtboard key={p.id} id={p.id} label={p.title} width={cardW} height={cardH}>
            <ArtboardCard page={p} />
          </DCArtboard>
        ))}
      </DCSection>

      <DCSection
        id="new"
        title="New page kinds"
        subtitle="Six new producer scripts could each push a page kind into the rotation. The protocol stays unchanged; one variation introduces a `donut` and `grid` proposal."
      >
        {newPages.map((p) => (
          <DCArtboard key={p.id} id={p.id} label={p.title} width={cardW} height={cardH}>
            <ArtboardCard page={p} />
          </DCArtboard>
        ))}
      </DCSection>
    </DesignCanvas>
  );
}

// ─── Shared atoms ──────────────────────────────────────────────────────────

const APP_INK = {
  black:  '#1A1612',
  paper:  '#F4ECD8',
  cream:  '#FAF6E8',
  red:    '#C8312A',
  yellow: '#E5B72D',
  green:  '#3E8242',
  blue:   '#234B8E',
};

const appDisplay = '"Archivo Black", sans-serif';
const appBody = '"Work Sans", sans-serif';
const appMono = '"JetBrains Mono", monospace';
const display = appDisplay;
const body = appBody;
const mono = appMono;

function Icon({ name, size = 18, color = '#1A1612' }) {
  // Re-uses the same chunky SVG icons from the protocol renderer so the
  // overview reads as the same visual language as what the firmware draws.
  return (
    <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} style={{ flexShrink: 0 }}>
      <IconGlyph name={name} x={0} y={0} size={size} color={color} ink={RC.INK} />
    </svg>
  );
}

function Eyebrow({ children, color = APP_INK.black, bg }) {
  return (
    <div style={{
      display: 'inline-block',
      fontFamily: display,
      fontSize: 10,
      letterSpacing: 2,
      color,
      background: bg,
      padding: bg ? '3px 8px' : 0,
    }}>{children}</div>
  );
}

// ─── BRAND HERO ────────────────────────────────────────────────────────────

function BrandHero() {
  return (
    <div style={{
      position: 'relative',
      width: '100%',
      height: '100%',
      background: APP_INK.paper,
      overflow: 'hidden',
      display: 'grid',
      gridTemplateColumns: '1.4fr 1fr',
      gap: 0,
    }}>
      {/* Left: wordmark + tagline */}
      <div style={{ padding: '36px 40px', display: 'flex', flexDirection: 'column', justifyContent: 'space-between', position: 'relative' }}>
        {/* top eyebrow strip with colored chips */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <Eyebrow bg={APP_INK.red} color={APP_INK.cream}>RC · PAPER · COLOR</Eyebrow>
          <div style={{ height: 1, flex: 1, background: APP_INK.black, opacity: 0.3 }} />
          <span style={{ fontFamily: mono, fontSize: 10, color: APP_INK.black, opacity: 0.6 }}>v0.1 · MAY 2026</span>
        </div>

        {/* huge wordmark, mixing inks per letter so the palette announces itself */}
        <div style={{
          fontFamily: display,
          fontSize: 116,
          lineHeight: 0.88,
          letterSpacing: -3,
          fontWeight: 900,
        }}>
          <span style={{ color: APP_INK.red }}>rc</span>
          <span style={{ color: APP_INK.black }}>paper</span>
          <br />
          <span style={{ color: APP_INK.blue }}>co</span>
          <span style={{ color: APP_INK.green }}>l</span>
          <span style={{ color: APP_INK.yellow, WebkitTextStroke: `2px ${APP_INK.black}` }}>o</span>
          <span style={{ color: APP_INK.black }}>r</span>
          <span style={{ color: APP_INK.red, fontSize: 88 }}>.</span>
        </div>

        {/* tagline */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <div style={{
            fontFamily: body, fontWeight: 700, fontSize: 18, lineHeight: 1.35, maxWidth: 540, color: APP_INK.black,
          }}>
            A battery-powered <span style={{ background: APP_INK.yellow, padding: '0 4px' }}>color e-ink dashboard</span> that wakes every 15 minutes, pulls a JSON layout tree from Axiom, and renders it in six reflective inks — then sleeps for days.
          </div>

          <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', marginTop: 4 }}>
            <FactChip icon="chip"    label="ESP32-S3R8"   sub="16MB · 8MB PSRAM" color={APP_INK.black} />
            <FactChip icon="eink"    label="Spectra 6"   sub="400 × 600 · E6"   color={APP_INK.red} />
            <FactChip icon="battery" label="1250 mAh"     sub="≈ 6.2 day cycle"  color={APP_INK.green} />
            <FactChip icon="refresh" label="15 min wake"   sub="deep-sleep btwn" color={APP_INK.blue} />
          </div>
        </div>
      </div>

      {/* Right: visual mark — 6 ink stripes meeting an EPD glyph */}
      <div style={{ position: 'relative', background: APP_INK.black, padding: '24px' }}>
        <svg viewBox="0 0 400 420" width="100%" height="100%" style={{ display: 'block' }}>
          {/* six-stripe paper unfurl */}
          {[APP_INK.red, APP_INK.yellow, APP_INK.green, APP_INK.blue, APP_INK.cream, APP_INK.black].map((c, i) => (
            <rect key={i} x={40 + i * 18} y={20} width={16} height={380} fill={c} stroke={APP_INK.cream} strokeWidth={i === 5 ? 1 : 0.5} />
          ))}
          {/* the EPD itself, tilted */}
          <g transform="translate(190 60) rotate(6)">
            <rect x="0" y="0" width="180" height="270" fill={APP_INK.cream} stroke={APP_INK.cream} strokeWidth="6" />
            {/* schematic page inside */}
            <rect x="12" y="14" width="156" height="22" fill={APP_INK.red} />
            <text x="20" y="32" fontFamily={display} fontSize="16" fill={APP_INK.cream}>CLAUDE</text>
            <rect x="12" y="46" width="80" height="6" fill={APP_INK.black} />
            <rect x="12" y="58" width="120" height="14" fill={APP_INK.blue} />
            <rect x="12" y="80" width="156" height="3" fill={APP_INK.black} />
            <rect x="12" y="92" width="60" height="6" fill={APP_INK.black} />
            <rect x="12" y="104" width="100" height="12" fill={APP_INK.green} />
            <rect x="12" y="124" width="156" height="3" fill={APP_INK.black} />
            <rect x="12" y="138" width="60" height="6" fill={APP_INK.black} />
            <polyline points="12,180 30,170 48,176 66,156 84,160 102,142 120,150 138,130 156,138" fill="none" stroke={APP_INK.blue} strokeWidth="2.5" />
            <rect x="12" y="195" width="156" height="1.5" fill={APP_INK.black} />
            <rect x="12" y="210" width="48" height="22" fill={APP_INK.yellow} stroke={APP_INK.black} />
            <text x="36" y="226" textAnchor="middle" fontFamily={display} fontSize="11" fill={APP_INK.black}>$47.50</text>
            <rect x="12" y="244" width="156" height="14" fill={APP_INK.red} />
          </g>
          {/* tagline corner */}
          <text x="40" y="412" fontFamily={mono} fontSize="11" fill={APP_INK.cream} letterSpacing="2">SIX INKS · NO RGB · NO GRADIENT</text>
        </svg>
      </div>
    </div>
  );
}

function FactChip({ icon, label, sub, color }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
      <div style={{
        width: 32, height: 32,
        background: color,
        display: 'grid', placeItems: 'center',
      }}>
        <Icon name={icon} size={18} color={APP_INK.cream} />
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.1 }}>
        <div style={{ fontFamily: display, fontSize: 12, letterSpacing: 0.5 }}>{label}</div>
        <div style={{ fontFamily: mono, fontSize: 9, color: APP_INK.black, opacity: 0.6 }}>{sub}</div>
      </div>
    </div>
  );
}

// ─── HARDWARE BOARD ────────────────────────────────────────────────────────

function HardwareCard() {
  const blocks = [
    { icon: 'chip',        title: 'ESP32-S3R8',   sub: '16 MB Flash · 8 MB OPI PSRAM',   color: APP_INK.black, group: 'CORE' },
    { icon: 'eink',        title: '4″ E6 EPD',    sub: 'ED2208-DOA · 400 × 600',         color: APP_INK.red,   group: 'CORE' },
    { icon: 'battery',     title: '1250 mAh',     sub: 'M5PM1 power mgmt',               color: APP_INK.green, group: 'CORE' },
    { icon: 'thermometer', title: 'SHT40',        sub: 'Temp/RH · I²C 0x44',             color: APP_INK.blue,  group: 'SENSE' },
    { icon: 'clock',       title: 'RX8130CE',     sub: 'RTC · I²C 0x32',                 color: APP_INK.yellow,group: 'SENSE' },
    { icon: 'imu',         title: 'BMI270',       sub: '6-axis IMU · I²C',               color: APP_INK.blue,  group: 'SENSE' },
    { icon: 'led',         title: '2× RGB LED',   sub: 'GPIO 21 · NeoPixel',             color: APP_INK.red,   group: 'I/O' },
    { icon: 'button',      title: '3× User key',  sub: 'GPIO 1 / 9 / 10',                color: APP_INK.black, group: 'I/O' },
    { icon: 'ir',          title: 'IR TX',        sub: 'GPIO 48 · 940 nm',               color: APP_INK.red,   group: 'I/O' },
    { icon: 'sd',          title: 'microSD',      sub: 'SPI · GPIO 47 CS',               color: APP_INK.black, group: 'I/O' },
    { icon: 'mic',         title: 'ES7210',       sub: 'Mic ADC · I²C 0x40',             color: APP_INK.green, group: 'AUDIO' },
    { icon: 'speaker',     title: 'ES8311 + AW8737A', sub: 'Codec + amp · I²S',          color: APP_INK.green, group: 'AUDIO' },
  ];

  return (
    <div style={{
      width: '100%', height: '100%',
      background: APP_INK.paper,
      padding: 24,
      display: 'flex', flexDirection: 'column', gap: 16,
      fontFamily: body, color: APP_INK.black,
    }}>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 12, borderBottom: `2px solid ${APP_INK.black}`, paddingBottom: 8 }}>
        <Icon name="chip" size={28} color={APP_INK.black} />
        <div style={{ fontFamily: display, fontSize: 32, letterSpacing: -0.5, lineHeight: 1 }}>
          HARDWARE
        </div>
        <div style={{ flex: 1 }} />
        <div style={{ fontFamily: mono, fontSize: 10, color: APP_INK.black, opacity: 0.6, letterSpacing: 1 }}>
          M5STACK · PAPERCOLOR · C151
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 10 }}>
        {blocks.map((b, i) => (
          <div key={i} style={{
            background: APP_INK.cream,
            border: `1.5px solid ${APP_INK.black}`,
            padding: '12px 12px 10px 12px',
            display: 'flex', flexDirection: 'column', gap: 6,
            position: 'relative',
            overflow: 'hidden',
          }}>
            {/* corner color flag */}
            <div style={{ position: 'absolute', top: 0, right: 0, background: b.color, color: APP_INK.cream, fontFamily: display, fontSize: 9, letterSpacing: 1.5, padding: '2px 6px' }}>
              {b.group}
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <div style={{ width: 30, height: 30, background: b.color, display: 'grid', placeItems: 'center', flexShrink: 0 }}>
                <Icon name={b.icon} size={20} color={APP_INK.cream} />
              </div>
              <div style={{ fontFamily: display, fontSize: 14, lineHeight: 1, letterSpacing: -0.3 }}>{b.title}</div>
            </div>
            <div style={{ fontFamily: mono, fontSize: 10, color: APP_INK.black, opacity: 0.7, lineHeight: 1.3 }}>{b.sub}</div>
          </div>
        ))}
      </div>

      <div style={{ marginTop: 'auto', display: 'flex', gap: 16, alignItems: 'center', padding: '10px 12px', background: APP_INK.black, color: APP_INK.cream }}>
        <Icon name="bolt" size={16} color={APP_INK.yellow} />
        <div style={{ fontFamily: mono, fontSize: 10, letterSpacing: 1 }}>
          M5UNIFIED · M5GFX · M5PM1 · ARDUINO @ PIO 6.12.0
        </div>
        <div style={{ flex: 1 }} />
        <div style={{ display: 'flex', gap: 6 }}>
          {[APP_INK.red, APP_INK.yellow, APP_INK.green, APP_INK.blue, APP_INK.cream, APP_INK.black].map((c, i) => (
            <div key={i} style={{ width: 12, height: 12, background: c, border: c === APP_INK.black ? `1px solid ${APP_INK.cream}` : 'none' }} />
          ))}
        </div>
      </div>
    </div>
  );
}

// ─── SHT40 ENV CARD ────────────────────────────────────────────────────────

function EnvCard() {
  // Simulated SHT40 reading. Drifts subtly so the card feels live.
  const [t, setT] = React.useState(0);
  React.useEffect(() => {
    const id = setInterval(() => setT(x => x + 1), 1800);
    return () => clearInterval(id);
  }, []);
  const tempC = 22.4 + Math.sin(t * 0.3) * 0.6;
  const tempF = tempC * 9 / 5 + 32;
  const rh = 48 + Math.cos(t * 0.27) * 4;
  // synthetic 24h trace
  const trace = Array.from({ length: 24 }, (_, i) => 20 + Math.sin((i + t) * 0.5) * 2 + Math.sin(i * 0.3) * 1.5);
  const lo = Math.min(...trace), hi = Math.max(...trace);

  return (
    <div style={{ width: '100%', height: '100%', background: APP_INK.cream, padding: 18, display: 'flex', flexDirection: 'column', gap: 10, fontFamily: body, color: APP_INK.black, position: 'relative', overflow: 'hidden' }}>
      {/* top corner sticker */}
      <div style={{ position: 'absolute', top: 0, right: 0, background: APP_INK.blue, color: APP_INK.cream, padding: '4px 8px', fontFamily: display, fontSize: 10, letterSpacing: 1.5 }}>
        I²C · 0x44
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <Icon name="thermometer" size={22} color={APP_INK.red} />
        <div style={{ fontFamily: display, fontSize: 22, letterSpacing: -0.5 }}>SHT40</div>
      </div>
      <div style={{ fontFamily: mono, fontSize: 9, color: APP_INK.black, opacity: 0.6, letterSpacing: 1 }}>
        SENSIRION · TEMP · HUMIDITY
      </div>

      {/* big temp number */}
      <div style={{ display: 'flex', alignItems: 'flex-end', gap: 6, marginTop: 4 }}>
        <div style={{ fontFamily: display, fontSize: 76, lineHeight: 0.88, color: APP_INK.red, letterSpacing: -3 }}>
          {tempC.toFixed(1)}
        </div>
        <div style={{ fontFamily: display, fontSize: 26, lineHeight: 1, color: APP_INK.red }}>°C</div>
      </div>
      <div style={{ fontFamily: mono, fontSize: 11, color: APP_INK.black, opacity: 0.7 }}>
        {tempF.toFixed(1)} °F · ±0.2 °C accuracy
      </div>

      {/* humidity bar */}
      <div style={{ marginTop: 6, display: 'flex', flexDirection: 'column', gap: 4 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, fontFamily: display, fontSize: 11, letterSpacing: 1 }}>
          <Icon name="droplet" size={14} color={APP_INK.blue} />
          <span>HUMIDITY</span>
          <span style={{ marginLeft: 'auto', fontFamily: mono, fontSize: 13, color: APP_INK.blue, fontWeight: 700 }}>{rh.toFixed(0)}%</span>
        </div>
        <div style={{ height: 14, border: `1.5px solid ${APP_INK.black}`, background: APP_INK.cream, position: 'relative' }}>
          <div style={{ position: 'absolute', inset: 2, background: `repeating-linear-gradient(90deg, ${APP_INK.blue} 0 4px, transparent 4px 6px)`, width: `${rh}%` }} />
        </div>
      </div>

      {/* 24h micro trace */}
      <div style={{ marginTop: 8 }}>
        <div style={{ fontFamily: display, fontSize: 10, letterSpacing: 1, marginBottom: 4 }}>24H TRACE</div>
        <svg viewBox="0 0 240 60" width="100%" height="60" style={{ display: 'block', border: `1px solid ${APP_INK.black}` }}>
          <rect x="0" y="0" width="240" height="60" fill={APP_INK.cream} />
          {[0,1,2,3].map(i => <line key={i} x1="0" y1={i * 20} x2="240" y2={i * 20} stroke={APP_INK.black} strokeOpacity="0.08" />)}
          <polyline
            fill="none"
            stroke={APP_INK.red}
            strokeWidth="2"
            points={trace.map((v, i) => `${(i * 240) / 23},${60 - ((v - lo) / (hi - lo || 1)) * 50 - 5}`).join(' ')}
          />
          {trace.map((v, i) => i % 4 === 0 ? (
            <rect key={i} x={(i * 240) / 23 - 2} y={60 - ((v - lo) / (hi - lo || 1)) * 50 - 7} width="3" height="3" fill={APP_INK.red} />
          ) : null)}
        </svg>
      </div>

      <div style={{ marginTop: 'auto', display: 'flex', gap: 6, alignItems: 'center', fontFamily: mono, fontSize: 9, color: APP_INK.black, opacity: 0.6 }}>
        <Icon name="refresh" size={10} color={APP_INK.green} />
        <span>POLL · 60 S</span>
        <span style={{ marginLeft: 'auto' }}>SHOWN ON OVERVIEW PAGE</span>
      </div>
    </div>
  );
}

// ─── RGB LED STATE INDICATOR ───────────────────────────────────────────────

function RgbLedCard() {
  // Cycle a "current state" so the LEDs feel alive on the canvas.
  const states = [
    { id: 'WAKE',    color: APP_INK.blue,   pulse: 'fast',   icon: 'bolt',     desc: 'Booting · WiFi associate · NTP sync', detail: 'BOTH · 200 ms pulse · 1.5 Hz' },
    { id: 'FETCH',   color: APP_INK.green,  pulse: 'fast',   icon: 'refresh',  desc: 'Fetching Axiom layout tree', detail: 'BOTH · breath · 0.5 Hz' },
    { id: 'RENDER',  color: APP_INK.yellow, pulse: 'steady', icon: 'eink',     desc: 'EPD redraw in progress', detail: 'LED1 steady · LED2 chase' },
    { id: 'OK',      color: APP_INK.green,  pulse: 'blip',   icon: 'check',    desc: 'Render done · entering deep-sleep', detail: 'Single 80 ms blip · then dark' },
    { id: 'ERROR',   color: APP_INK.red,    pulse: 'fast',   icon: 'warning',  desc: 'WiFi / Axiom / parse failure', detail: 'BOTH · 250 ms · 4 Hz · 5 s' },
    { id: 'LOWBAT',  color: APP_INK.yellow, pulse: 'slow',   icon: 'battery',  desc: 'Battery under 15% · degraded mode', detail: 'LED1 amber · 1 blink / 30 s' },
    { id: 'PAIR',    color: APP_INK.blue,   pulse: 'breathe',icon: 'wifi',     desc: 'Holding BtnC at boot · provisioning', detail: 'LED1 cyan breath · LED2 dark' },
    { id: 'SLEEP',   color: APP_INK.black,  pulse: 'off',    icon: 'sleep',    desc: 'Deep-sleep until next 15-min wake', detail: 'Both dark · < 35 µA quiescent' },
  ];
  const [activeIdx, setActiveIdx] = React.useState(1);
  React.useEffect(() => {
    const id = setInterval(() => setActiveIdx(i => (i + 1) % states.length), 2200);
    return () => clearInterval(id);
  }, []);
  const active = states[activeIdx];

  return (
    <div style={{ width: '100%', height: '100%', background: APP_INK.black, padding: '24px 28px', color: APP_INK.cream, fontFamily: body, display: 'grid', gridTemplateColumns: '1fr 1.4fr', gap: 24, position: 'relative', overflow: 'hidden' }}>
      {/* LEFT: live preview of the two LEDs on a small board silhouette */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
          <Icon name="led" size={22} color={active.color === APP_INK.black ? '#3A3A37' : active.color} />
          <div style={{ fontFamily: display, fontSize: 30, letterSpacing: -0.5 }}>RGB LED</div>
        </div>
        <div style={{ fontFamily: mono, fontSize: 10, opacity: 0.65, letterSpacing: 1 }}>
          GPIO 21 · NEOPIXEL · 2 PIXELS
        </div>

        {/* mini board sketch */}
        <div style={{
          background: '#11100E', border: `1px solid #2A2A28`, padding: 18, position: 'relative', flex: 1,
          display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 22,
        }}>
          {/* faint top label */}
          <div style={{ position: 'absolute', top: 8, left: 12, fontFamily: mono, fontSize: 9, opacity: 0.5, letterSpacing: 1 }}>
            FRONT BEZEL · TOP RIGHT
          </div>

          <div style={{ display: 'flex', gap: 28 }}>
            <Led color={active.color === APP_INK.black ? null : active.color} pulse={active.pulse} label="LED 1" />
            <Led color={active.color === APP_INK.black ? null : active.color} pulse={active.pulse === 'off' ? 'off' : (active.pulse === 'breathe' ? 'off' : active.pulse)} label="LED 2" />
          </div>

          <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '4px 10px', background: active.color === APP_INK.black ? '#1A1612' : active.color, color: active.color === APP_INK.yellow ? APP_INK.black : APP_INK.cream, fontFamily: display, fontSize: 12, letterSpacing: 2 }}>
            <Icon name={active.icon} size={14} color={active.color === APP_INK.yellow ? APP_INK.black : APP_INK.cream} />
            {active.id}
          </div>
          <div style={{ fontFamily: mono, fontSize: 10, opacity: 0.65, textAlign: 'center', maxWidth: 240, lineHeight: 1.45 }}>
            {active.detail}
          </div>
        </div>
      </div>

      {/* RIGHT: state legend grid */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10, minWidth: 0 }}>
        <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, borderBottom: `1px solid ${APP_INK.cream}40`, paddingBottom: 6 }}>
          <div style={{ fontFamily: display, fontSize: 14, letterSpacing: 2 }}>STATE MACHINE</div>
          <div style={{ flex: 1 }} />
          <div style={{ fontFamily: mono, fontSize: 10, opacity: 0.6 }}>8 STATES · CYCLING DEMO</div>
        </div>

        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 6, flex: 1 }}>
          {states.map((s, i) => {
            const isActive = i === activeIdx;
            const swatchColor = s.color === APP_INK.black ? '#2A2A28' : s.color;
            return (
              <div key={s.id} style={{
                display: 'flex', alignItems: 'center', gap: 10,
                padding: '8px 10px',
                background: isActive ? swatchColor : 'transparent',
                color: isActive && s.color === APP_INK.yellow ? APP_INK.black : (isActive ? APP_INK.cream : APP_INK.cream),
                border: `1px solid ${isActive ? swatchColor : '#2A2A28'}`,
                transition: 'background 200ms, border-color 200ms',
              }}>
                <div style={{
                  width: 18, height: 18,
                  borderRadius: '50%',
                  background: swatchColor,
                  boxShadow: isActive ? `0 0 14px 2px ${swatchColor}` : 'none',
                  transition: 'box-shadow 200ms',
                  flexShrink: 0,
                  border: s.color === APP_INK.black ? `1px solid ${APP_INK.cream}40` : 'none',
                }} />
                <div style={{ display: 'flex', flexDirection: 'column', minWidth: 0, lineHeight: 1.2 }}>
                  <div style={{ fontFamily: display, fontSize: 12, letterSpacing: 1.5 }}>{s.id}</div>
                  <div style={{ fontFamily: mono, fontSize: 9, opacity: isActive ? 0.85 : 0.55, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{s.desc}</div>
                </div>
              </div>
            );
          })}
        </div>

        <div style={{ display: 'flex', gap: 8, alignItems: 'center', fontFamily: mono, fontSize: 10, opacity: 0.6 }}>
          <Icon name="bolt" size={12} color={APP_INK.yellow} />
          <span>VISIBLE THROUGH FRONT BEZEL · SOFTWARE-DIMMABLE TO 6 LEVELS</span>
        </div>
      </div>

      <style>{`
        @keyframes pc-led-fast { 0%,100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.4; transform: scale(0.85); } }
        @keyframes pc-led-slow { 0%,90%,100% { opacity: 0.4; } 95% { opacity: 1; } }
        @keyframes pc-led-breathe { 0%,100% { opacity: 0.55; transform: scale(0.92); } 50% { opacity: 1; transform: scale(1.08); } }
        @keyframes pc-led-blip { 0%,40%,100% { opacity: 0.25; transform: scale(0.85); } 10%,30% { opacity: 1; transform: scale(1.08); } }
      `}</style>
    </div>
  );
}

function Led({ color, pulse, label }) {
  const anim =
    pulse === 'fast'    ? 'pc-led-fast 0.6s ease-in-out infinite' :
    pulse === 'slow'    ? 'pc-led-slow 2.4s linear infinite' :
    pulse === 'breathe' ? 'pc-led-breathe 1.6s ease-in-out infinite' :
    pulse === 'blip'    ? 'pc-led-blip 1.8s ease-in-out infinite' :
    pulse === 'steady'  ? 'none' :
    'none';
  const off = !color || pulse === 'off';
  const c = off ? '#1A1612' : color;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6 }}>
      <div style={{ position: 'relative', width: 56, height: 56, display: 'grid', placeItems: 'center' }}>
        {/* outer corona */}
        {!off && (
          <div style={{
            position: 'absolute', inset: -10,
            background: `radial-gradient(circle, ${c}66 0%, ${c}22 40%, transparent 70%)`,
            animation: anim,
          }} />
        )}
        {/* LED dome */}
        <div style={{
          width: 28, height: 28, borderRadius: '50%',
          background: off ? 'radial-gradient(circle at 35% 30%, #2A2A28 0%, #0E0E0C 70%)' :
            `radial-gradient(circle at 35% 30%, ${APP_INK.cream}cc 0%, ${c} 35%, ${c} 100%)`,
          boxShadow: off ? 'inset 0 1px 2px rgba(0,0,0,0.6)' :
            `0 0 10px ${c}, 0 0 22px ${c}aa, inset 0 1px 2px rgba(255,255,255,0.4)`,
          animation: off ? 'none' : anim,
        }} />
      </div>
      <div style={{ fontFamily: mono, fontSize: 9, opacity: 0.6, letterSpacing: 1 }}>{label}</div>
    </div>
  );
}

// ─── PALETTE CARD ──────────────────────────────────────────────────────────

function PaletteCard() {
  const swatches = [
    { name: 'BLACK',  hex: '#1A1612', token: 'INK_BLACK',  use: 'text · borders · rules',         icon: 'square',     fg: APP_INK.cream },
    { name: 'WHITE',  hex: '#ECE5D2', token: 'INK_WHITE',  use: 'paper · negative space',         icon: 'circle',     fg: APP_INK.black },
    { name: 'RED',    hex: '#C8312A', token: 'INK_RED',    use: 'alerts · primary headers',       icon: 'warning',    fg: APP_INK.cream },
    { name: 'YELLOW', hex: '#E5B72D', token: 'INK_YELLOW', use: 'highlights · low-batt warn',     icon: 'bolt',       fg: APP_INK.black },
    { name: 'GREEN',  hex: '#3E8242', token: 'INK_GREEN',  use: 'success · positive deltas',      icon: 'check',      fg: APP_INK.cream },
    { name: 'BLUE',   hex: '#234B8E', token: 'INK_BLUE',   use: 'links · usage windows · charts', icon: 'chart',      fg: APP_INK.cream },
  ];

  return (
    <div style={{
      width: '100%', height: '100%',
      background: APP_INK.cream,
      padding: 22,
      display: 'flex', flexDirection: 'column', gap: 14,
      fontFamily: body, color: APP_INK.black,
    }}>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 10 }}>
        <Icon name="palette" size={26} color={APP_INK.red} />
        <div style={{ fontFamily: display, fontSize: 28, letterSpacing: -0.5 }}>SPECTRA 6</div>
        <div style={{ fontFamily: mono, fontSize: 10, opacity: 0.6, letterSpacing: 1 }}>6 INKS · NO RGB · NO GRADIENT · NO ALPHA</div>
      </div>

      {/* 2x3 grid of color tiles, each tile uses ITS OWN color so the page is what it documents */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 10, flex: 1 }}>
        {swatches.map((s) => (
          <div key={s.name} style={{
            background: s.hex,
            color: s.fg,
            padding: '14px 14px 12px 14px',
            border: `1.5px solid ${APP_INK.black}`,
            display: 'flex', flexDirection: 'column', gap: 6,
            position: 'relative', minHeight: 130,
            overflow: 'hidden',
          }}>
            {/* corner index */}
            <div style={{ position: 'absolute', top: 8, right: 10, fontFamily: mono, fontSize: 10, opacity: 0.6, letterSpacing: 1 }}>
              0{swatches.indexOf(s) + 1}
            </div>
            <Icon name={s.icon} size={24} color={s.fg} />
            <div style={{ fontFamily: display, fontSize: 22, lineHeight: 1, letterSpacing: -0.5 }}>{s.name}</div>
            <div style={{ fontFamily: mono, fontSize: 10, opacity: 0.85 }}>{s.hex}</div>
            <div style={{ marginTop: 'auto', fontFamily: body, fontSize: 11, fontWeight: 500, lineHeight: 1.25, opacity: 0.92 }}>
              {s.use}
            </div>
          </div>
        ))}
      </div>

      <div style={{
        display: 'flex', gap: 8, alignItems: 'center',
        padding: '8px 12px',
        background: APP_INK.black, color: APP_INK.cream,
        fontFamily: mono, fontSize: 10, letterSpacing: 1,
      }}>
        <Icon name="sparkles" size={12} color={APP_INK.yellow} />
        <span>PRODUCERS NEVER INVENT COLORS — SIX TOKENS, THAT'S IT.</span>
      </div>
    </div>
  );
}

// ─── README & LEGEND ───────────────────────────────────────────────────────

function Readme() {
  const codeStyle = { fontFamily: mono, fontSize: 11, background: APP_INK.yellow, color: APP_INK.black, padding: '0 4px' };
  return (
    <div style={{
      padding: 18,
      fontFamily: body,
      fontSize: 12,
      lineHeight: 1.5,
      color: APP_INK.black,
      height: '100%',
      background: APP_INK.cream,
      display: 'flex',
      flexDirection: 'column',
      gap: 10,
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <Icon name="sparkles" size={16} color={APP_INK.red} />
        <div style={{ fontFamily: display, fontSize: 18, letterSpacing: -0.5 }}>
          How to read this
        </div>
      </div>
      <div>
        Every artboard renders through the same protocol-tree → layout → paint pipeline the
        firmware runs in <span style={codeStyle}>src/render/engine.cpp</span>.
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
        <div style={{ display: 'flex', gap: 6, alignItems: 'flex-start' }}>
          <Icon name="check" size={14} color={APP_INK.green} />
          <div><b>Within protocol</b> — uses only existing primitives.</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'flex-start' }}>
          <Icon name="sparkles" size={14} color={APP_INK.blue} />
          <div><b>Proposed extension</b> — needs a renderer addition (<span style={codeStyle}>donut</span>, <span style={codeStyle}>grid</span>, <span style={codeStyle}>halftone</span>).</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'flex-start' }}>
          <Icon name="button" size={14} color={APP_INK.red} />
          <div>Open the simulator below to interact.</div>
        </div>
      </div>
      <div style={{
        marginTop: 'auto',
        display: 'grid',
        gridTemplateColumns: 'repeat(3, 1fr)',
        gap: 6,
        background: APP_INK.black,
        color: APP_INK.cream,
        padding: 8,
      }}>
        {[
          { btn: 'A', label: 'NEXT',  color: APP_INK.blue },
          { btn: 'B', label: 'PREV',  color: APP_INK.yellow },
          { btn: 'C', label: 'FETCH', color: APP_INK.green },
        ].map(b => (
          <div key={b.btn} style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2 }}>
            <div style={{
              width: 22, height: 22, borderRadius: '50%',
              background: b.color, color: APP_INK.cream,
              display: 'grid', placeItems: 'center',
              fontFamily: display, fontSize: 11,
            }}>{b.btn}</div>
            <div style={{ fontFamily: mono, fontSize: 9, letterSpacing: 1 }}>{b.label}</div>
          </div>
        ))}
      </div>
    </div>
  );
}

function Legend() {
  const styles = [
    { name: 'Magazine',  family: '"Archivo Black"',         note: 'Knockout headers · KPI rows', color: APP_INK.red,    icon: 'eink' },
    { name: 'Swiss',     family: '"Space Grotesk"',         note: 'Asymmetric · big numerals',   color: APP_INK.black,  icon: 'square' },
    { name: 'Editorial', family: '"DM Serif Display"',      note: 'Hairlines · serif wordmark',  color: APP_INK.blue,   icon: 'flask' },
    { name: 'Risograph', family: '"Big Shoulders Display"', note: 'Halftone overprint',          color: APP_INK.yellow, icon: 'sparkles' },
    { name: 'Sticker',   family: '"Bowlby One"',            note: 'Donut + pill badges',         color: APP_INK.green,  icon: 'circle' },
    { name: 'Data',      family: '"JetBrains Mono"',        note: 'Tabular monospace',           color: APP_INK.black,  icon: 'chart' },
  ];
  return (
    <div style={{
      padding: 22,
      fontFamily: body,
      background: APP_INK.cream,
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      gap: 14,
      overflow: 'hidden',
    }}>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 10 }}>
        <Icon name="flask" size={22} color={APP_INK.blue} />
        <div style={{ fontFamily: display, fontSize: 22, letterSpacing: -0.5 }}>STYLE DIRECTIONS</div>
        <div style={{ flex: 1, height: 1, background: APP_INK.black, opacity: 0.3 }} />
        <div style={{ fontFamily: mono, fontSize: 10, opacity: 0.6, letterSpacing: 1 }}>6 SYSTEMS · 1 PROTOCOL</div>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(6, 1fr)', gap: 10, flex: 1 }}>
        {styles.map(s => (
          <div key={s.name} style={{
            display: 'flex', flexDirection: 'column',
            border: `1.5px solid ${APP_INK.black}`,
            background: APP_INK.cream,
            position: 'relative',
            overflow: 'hidden',
          }}>
            <div style={{
              padding: '8px 10px 6px 10px',
              background: s.color,
              color: s.color === APP_INK.yellow ? APP_INK.black : APP_INK.cream,
              display: 'flex', alignItems: 'center', gap: 6,
            }}>
              <Icon name={s.icon} size={14} color={s.color === APP_INK.yellow ? APP_INK.black : APP_INK.cream} />
              <div style={{ fontFamily: display, fontSize: 11, letterSpacing: 1.5 }}>{s.name.toUpperCase()}</div>
            </div>
            <div style={{ padding: '8px 10px', display: 'flex', flexDirection: 'column', gap: 4, flex: 1 }}>
              <div style={{ fontFamily: s.family, fontSize: 24, lineHeight: 1, color: APP_INK.black }}>{s.name}</div>
              <div style={{ fontFamily: mono, fontSize: 9, color: APP_INK.black, opacity: 0.65, lineHeight: 1.35, marginTop: 'auto' }}>{s.note}</div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

window.App = App;
