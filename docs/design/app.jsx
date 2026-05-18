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
        title="rcpapercolor · dashboard explorations"
        subtitle="Battery-powered Spectra 6 color e-ink, 400×600. Six-ink palette: black / white / red / yellow / green / blue. All pages are JSON layout trees rendered by the device firmware."
      >
        <DCArtboard id="readme" label="Read me" width={520} height={420}>
          <Readme />
        </DCArtboard>
        <DCArtboard id="ink" label="Spectra 6 palette" width={300} height={420}>
          <Palette />
        </DCArtboard>
        <DCArtboard id="legend" label="Style directions" width={480} height={420}>
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

// ─── Intro artboards ───────────────────────────────────────────────────────

function Readme() {
  return (
    <div style={{
      padding: 24,
      fontFamily: '"Work Sans", sans-serif',
      fontSize: 13,
      lineHeight: 1.55,
      color: '#1A1612',
      height: '100%',
      background: '#FAF6E8',
      display: 'flex',
      flexDirection: 'column',
      gap: 10,
    }}>
      <div style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 20, letterSpacing: -0.5 }}>
        How to read this canvas
      </div>
      <div>
        Every artboard renders through the same protocol tree → flex layout → paint pipeline
        the firmware uses in <code style={mono}>src/render/engine.cpp</code>, against the
        Spectra 6 ink palette and FreeSans metrics from <code style={mono}>fonts_icons.cpp</code>.
      </div>
      <div>
        <strong>Within protocol</strong> means the page composes only the primitives in
        the design spec (vstack, hstack, text, hbar, sparkline, divider, spacer, icon, image,
        marker). <strong>Proposed extension</strong> notes appear under cards that would need
        a renderer addition — currently <code style={mono}>donut</code>,&nbsp;
        <code style={mono}>grid</code>, and <code style={mono}>pattern: 'halftone'</code> on
        text bg.
      </div>
      <div>
        Open the <strong>Live simulator</strong> for the full M5Stack PaperColor experience:
        button rotation, e-ink redraw flash, JSON live-edit, and a quick-tweak panel for
        every page's sample data.
      </div>
      <div style={{ marginTop: 'auto', fontFamily: '"JetBrains Mono", monospace', fontSize: 11, color: '#5C5648' }}>
        BtnA NEXT &nbsp; BtnB PREV &nbsp; BtnC FETCH
      </div>
    </div>
  );
}

function Palette() {
  const swatches = [
    ['black',  RC.INK.black,  '#000000'],
    ['white',  RC.INK.white,  'paper'],
    ['red',    RC.INK.red,    '#D92027'],
    ['yellow', RC.INK.yellow, '#F7C815'],
    ['green',  RC.INK.green,  '#1EA84B'],
    ['blue',   RC.INK.blue,   '#0F4D92'],
  ];
  return (
    <div style={{
      padding: 16,
      fontFamily: '"Work Sans", sans-serif',
      background: '#FAF6E8',
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      gap: 10,
    }}>
      <div style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 14, letterSpacing: 1 }}>
        SPECTRA 6 INKS
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {swatches.map(([name, hex, src]) => (
          <div key={name} style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            <div style={{
              width: 32, height: 32,
              background: hex,
              border: '1px solid #1A1612',
            }} />
            <div style={{ display: 'flex', flexDirection: 'column' }}>
              <div style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 11, letterSpacing: 1 }}>
                {name.toUpperCase()}
              </div>
              <div style={{ fontFamily: '"JetBrains Mono", monospace', fontSize: 10, color: '#5C5648' }}>
                {hex} <span style={{ opacity: 0.6 }}>· src {src}</span>
              </div>
            </div>
          </div>
        ))}
      </div>
      <div style={{
        marginTop: 'auto',
        fontFamily: '"JetBrains Mono", monospace',
        fontSize: 10,
        color: '#5C5648',
        lineHeight: 1.4,
      }}>
        No RGB · no gradients · no opacity.
        Producers never invent colors. Six tokens, that's it.
      </div>
    </div>
  );
}

function Legend() {
  const styles = [
    ['Magazine',  '"Archivo Black"', 'Work Sans',           'Knockout headers, KPI rows. Matches current spec.'],
    ['Swiss',     '"Space Grotesk"', 'Space Grotesk',       'Asymmetric, bold numerals, generous whitespace.'],
    ['Editorial', '"DM Serif Display"', 'Newsreader',       'Serif wordmark, hairline rules, all-caps labels.'],
    ['Risograph', '"Big Shoulders Display"', 'Work Sans',   'Halftone overprint, chunky blocks (proposed primitive).'],
    ['Sticker',   '"Bowlby One"', 'Nunito',                 'Pill badges, donut charts (proposed primitive).'],
    ['Data',      '"JetBrains Mono"', 'JetBrains Mono',     'Monospace tabular density.'],
  ];
  return (
    <div style={{
      padding: 16,
      fontFamily: '"Work Sans", sans-serif',
      background: '#FAF6E8',
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      gap: 8,
      overflow: 'hidden',
    }}>
      <div style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 14, letterSpacing: 1 }}>
        STYLE DIRECTIONS
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {styles.map(([name, display, body, note]) => (
          <div key={name} style={{ display: 'grid', gridTemplateColumns: '90px 1fr', gap: 10, alignItems: 'baseline' }}>
            <div style={{ fontFamily: display, fontSize: 18, lineHeight: 1 }}>{name}</div>
            <div style={{ fontSize: 11, color: '#3A352C', fontFamily: body, lineHeight: 1.4 }}>{note}</div>
          </div>
        ))}
      </div>
      <div style={{
        marginTop: 'auto',
        fontFamily: '"JetBrains Mono", monospace',
        fontSize: 10,
        color: '#5C5648',
        lineHeight: 1.4,
      }}>
        Firmware ships FreeSans by default. These directions
        assume a custom TTF or two could be baked into flash.
      </div>
    </div>
  );
}

const mono = { fontFamily: '"JetBrains Mono", monospace', fontSize: 12, background: 'rgba(0,0,0,0.06)', padding: '0 4px' };

window.App = App;
