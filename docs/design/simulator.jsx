// Interactive simulator artboard: device on the left, control surface on the
// right (page picker, e-ink stats, JSON editor that re-parses on every keystroke,
// quick data tweaks for the active page).

const FLASH_MS = 800;            // simulated e-ink redraw flash
const FETCH_INTERVAL_SEC = 900;  // matches firmware default

function useFlash() {
  const [flash, setFlash] = React.useState(false);
  const trigger = React.useCallback(() => {
    setFlash(true);
    setTimeout(() => setFlash(false), FLASH_MS);
  }, []);
  return [flash, trigger];
}

function Simulator() {
  const [pageIdx, setPageIdx] = React.useState(0);
  const [overrideFontKey, setOverrideFontKey] = React.useState(null);
  const [showStale, setShowStale] = React.useState(false);
  const [flash, triggerFlash] = useFlash();
  const [highlight, setHighlight] = React.useState(null);
  const [lastFetch, setLastFetch] = React.useState(Date.now());
  const [now, setNow] = React.useState(Date.now());
  const [scale, setScale] = React.useState(0.7);

  // Editable copy of the sample data for the active page only.
  const [editedData, setEditedData] = React.useState({});

  // Live editor state — string of JSON. Parsed on every keystroke.
  const [jsonText, setJsonText] = React.useState('');
  const [jsonError, setJsonError] = React.useState(null);
  const [editMode, setEditMode] = React.useState(false);

  const page = PAGES[pageIdx];
  const dataKey = page.id.split('-')[0]; // claude / codex / calendar / etc.
  const liveData = { ...(SAMPLE[dataKey] || {}), ...(editedData[dataKey] || {}) };

  const fontKey = overrideFontKey || page.fontKey;
  const inkKey = page.inkKey;

  // Build the tree. If user is editing JSON, parse that; else build from data.
  const builtTree = React.useMemo(() => {
    if (editMode) {
      try {
        const parsed = JSON.parse(jsonText);
        setJsonError(null);
        return parsed;
      } catch (e) {
        setJsonError(e.message);
        return null;
      }
    }
    try {
      return page.build(liveData);
    } catch (e) {
      console.error('build error', e);
      return null;
    }
  }, [editMode, jsonText, pageIdx, JSON.stringify(liveData)]);

  // When entering edit mode, seed the JSON from the current built tree.
  React.useEffect(() => {
    if (editMode) {
      try { setJsonText(JSON.stringify(page.build(liveData), null, 2)); } catch {}
    }
  }, [editMode]);

  // Tick the clock for "since last fetch" display.
  React.useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(t);
  }, []);

  // Use the latest tree we successfully built (avoid blanking on a typo).
  const lastGoodRef = React.useRef(null);
  if (builtTree) lastGoodRef.current = builtTree;
  const treeToRender = builtTree || lastGoodRef.current;

  const press = (which) => {
    setHighlight(which);
    setTimeout(() => setHighlight(null), 180);
    triggerFlash();
    if (which === 'A') setPageIdx((i) => (i + 1) % PAGES.length);
    if (which === 'B') setPageIdx((i) => (i - 1 + PAGES.length) % PAGES.length);
    if (which === 'C') {
      // force re-fetch
      setLastFetch(Date.now());
      setShowStale(false);
    }
  };

  const sinceFetch = Math.floor((now - lastFetch) / 1000);
  const fetchAge = sinceFetch < 60 ? `${sinceFetch}s` : `${Math.floor(sinceFetch / 60)}m ${sinceFetch % 60}s`;

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: 'auto 1fr',
      gap: 24,
      padding: 20,
      background: 'transparent',
      width: 1320,
      minHeight: 760,
    }}>
      {/* DEVICE COLUMN */}
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 12 }}>
        <PaperColorDevice
          tree={treeToRender || { type: 'vstack', pad: 14, children: [{ type: 'text', value: 'rendering…', size: 'body' }] }}
          fontKey={fontKey}
          inkKey={inkKey}
          flash={flash}
          scale={scale}
          highlight={highlight}
          staleBadge={showStale}
          onPressA={() => press('A')}
          onPressB={() => press('B')}
          onPressC={() => press('C')}
        />
        <div style={{ fontFamily: '"JetBrains Mono", monospace', fontSize: 11, color: '#5C5648', letterSpacing: 1 }}>
          BtnA · NEXT &nbsp; · &nbsp; BtnB · PREV &nbsp; · &nbsp; BtnC · FETCH
        </div>
      </div>

      {/* CONTROL COLUMN */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 14, fontFamily: '"Work Sans", sans-serif', color: '#1A1612' }}>
        {/* Header strip */}
        <div style={{
          display: 'flex', alignItems: 'baseline', gap: 16, paddingBottom: 12,
          borderBottom: '2px solid #1A1612',
        }}>
          <div style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 22, letterSpacing: -0.5 }}>
            SIMULATOR
          </div>
          <div style={{ fontFamily: '"JetBrains Mono", monospace', fontSize: 11, color: '#5C5648' }}>
            {page.id.toUpperCase()} · {fontKey.toUpperCase()} · {pageIdx + 1}/{PAGES.length}
          </div>
          <div style={{ marginLeft: 'auto', display: 'flex', gap: 12 }}>
            <Stat label="LAST FETCH" value={fetchAge} />
            <Stat label="NEXT WAKE" value={`${Math.max(0, FETCH_INTERVAL_SEC - sinceFetch)}s`} />
            <Stat label="BATTERY" value="6.2d" />
          </div>
        </div>

        {/* Page picker grid */}
        <div>
          <Label>PAGE ROTATION ({PAGES.length})</Label>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 6, marginTop: 6 }}>
            {PAGES.map((p, i) => (
              <button
                key={p.id}
                onClick={() => { setPageIdx(i); triggerFlash(); }}
                style={{
                  border: '1px solid #1A1612',
                  background: i === pageIdx ? '#1A1612' : '#ECE5D2',
                  color: i === pageIdx ? '#ECE5D2' : '#1A1612',
                  padding: '8px 10px',
                  textAlign: 'left',
                  cursor: 'pointer',
                  fontFamily: '"Work Sans", sans-serif',
                  fontSize: 11,
                  letterSpacing: 0.3,
                  display: 'flex',
                  flexDirection: 'column',
                  gap: 2,
                }}
              >
                <span style={{ fontFamily: '"JetBrains Mono", monospace', fontSize: 9, opacity: 0.7 }}>
                  {String(i + 1).padStart(2, '0')} · {p.group.toUpperCase()}
                </span>
                <span style={{ fontWeight: 700 }}>{p.title.split(' · ').slice(-1)[0]}</span>
              </button>
            ))}
          </div>
        </div>

        {/* Quick controls strip */}
        <div style={{ display: 'flex', gap: 16, alignItems: 'center', flexWrap: 'wrap' }}>
          <FontPicker value={fontKey} onChange={setOverrideFontKey} pageDefault={page.fontKey} />
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <Label inline>SCALE</Label>
            <input
              type="range"
              min={0.5} max={1} step={0.05}
              value={scale}
              onChange={(e) => setScale(parseFloat(e.target.value))}
              style={{ width: 100 }}
            />
            <span style={{ fontFamily: '"JetBrains Mono", monospace', fontSize: 10 }}>{Math.round(scale * 100)}%</span>
          </div>
          <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 11, letterSpacing: 0.5 }}>
            <input type="checkbox" checked={showStale} onChange={(e) => setShowStale(e.target.checked)} />
            STALE BADGE
          </label>
          <button
            onClick={() => triggerFlash()}
            style={btnStyle()}>
            FORCE FLASH
          </button>
          <button
            onClick={() => setEditMode((v) => !v)}
            style={{ ...btnStyle(), background: editMode ? '#1A1612' : '#ECE5D2', color: editMode ? '#ECE5D2' : '#1A1612' }}>
            {editMode ? 'EXIT JSON' : 'EDIT JSON'}
          </button>
        </div>

        {/* Either: data tweaks OR JSON editor */}
        {!editMode && (
          <DataTweaks
            dataKey={dataKey}
            data={liveData}
            onChange={(patch) => setEditedData((prev) => ({
              ...prev,
              [dataKey]: { ...(prev[dataKey] || {}), ...patch },
            }))}
            onReset={() => setEditedData((prev) => ({ ...prev, [dataKey]: {} }))}
          />
        )}
        {editMode && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <Label>PROTOCOL JSON · LIVE</Label>
            <textarea
              value={jsonText}
              onChange={(e) => setJsonText(e.target.value)}
              spellCheck={false}
              style={{
                fontFamily: '"JetBrains Mono", monospace',
                fontSize: 11,
                lineHeight: 1.5,
                padding: 10,
                minHeight: 280,
                border: '1px solid #1A1612',
                background: '#FAF6E8',
                color: '#1A1612',
                resize: 'vertical',
                outline: 'none',
              }}
            />
            <div style={{
              fontFamily: '"JetBrains Mono", monospace', fontSize: 10,
              color: jsonError ? '#C8312A' : '#3E8242', minHeight: 14,
            }}>
              {jsonError ? `✕ ${jsonError}` : '✓ valid · rendering live'}
            </div>
          </div>
        )}

        {/* Note for this page */}
        <div style={{
          marginTop: 'auto',
          background: '#F5EFE0',
          border: '1px solid #C8C0AE',
          padding: 10,
          fontSize: 11,
          lineHeight: 1.5,
          color: '#3A352C',
        }}>
          <span style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 10, letterSpacing: 1, marginRight: 8 }}>
            NOTE
          </span>
          {page.note}
        </div>
      </div>
    </div>
  );
}

function btnStyle() {
  return {
    border: '1px solid #1A1612',
    background: '#ECE5D2',
    color: '#1A1612',
    padding: '6px 10px',
    fontFamily: '"Archivo Black", sans-serif',
    fontSize: 10,
    letterSpacing: 1,
    cursor: 'pointer',
  };
}

function Stat({ label, value }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-end' }}>
      <div style={{ fontFamily: '"JetBrains Mono", monospace', fontSize: 9, color: '#5C5648', letterSpacing: 1 }}>{label}</div>
      <div style={{ fontFamily: '"Archivo Black", sans-serif', fontSize: 14 }}>{value}</div>
    </div>
  );
}

function Label({ children, inline }) {
  return (
    <div style={{
      fontFamily: '"Archivo Black", sans-serif',
      fontSize: 10,
      letterSpacing: 1.5,
      color: '#5C5648',
      display: inline ? 'inline-block' : 'block',
    }}>{children}</div>
  );
}

function FontPicker({ value, onChange, pageDefault }) {
  const options = Object.entries(RC.FONT_SYSTEMS).map(([k, v]) => [k, v.name]);
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
      <Label inline>FONT</Label>
      <select
        value={value}
        onChange={(e) => onChange(e.target.value === pageDefault ? null : e.target.value)}
        style={{
          border: '1px solid #1A1612',
          background: '#ECE5D2',
          color: '#1A1612',
          padding: '4px 6px',
          fontFamily: '"Work Sans", sans-serif',
          fontSize: 11,
        }}
      >
        {options.map(([k, name]) => (
          <option key={k} value={k}>{name}{k === pageDefault ? ' · default' : ''}</option>
        ))}
      </select>
    </div>
  );
}

// ─── Data tweaks panel (per-page) ──────────────────────────────────────────

function DataTweaks({ dataKey, data, onChange, onReset }) {
  const tweaks = TWEAK_DEFS[dataKey] || [];
  if (!tweaks.length) {
    return (
      <div style={{ fontSize: 11, color: '#5C5648', padding: 8, fontStyle: 'italic' }}>
        No quick tweaks defined for this page kind. Switch to EDIT JSON for full control.
      </div>
    );
  }
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <Label>DATA TWEAKS · {dataKey.toUpperCase()}</Label>
        <button onClick={onReset} style={{ ...btnStyle(), padding: '2px 6px', fontSize: 9 }}>RESET</button>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 10, padding: 10, background: '#F5EFE0', border: '1px solid #C8C0AE' }}>
        {tweaks.map((t) => (
          <TweakField key={t.key} def={t} value={data[t.key]} onChange={(v) => onChange({ [t.key]: v })} />
        ))}
      </div>
    </div>
  );
}

function TweakField({ def, value, onChange }) {
  if (def.kind === 'pct' || def.kind === 'num') {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, fontFamily: '"Work Sans", sans-serif' }}>
          <span style={{ letterSpacing: 0.5 }}>{def.label}</span>
          <span style={{ fontFamily: '"JetBrains Mono", monospace' }}>{value ?? '—'}</span>
        </div>
        <input
          type="range"
          min={def.min ?? 0}
          max={def.max ?? 100}
          step={def.step ?? 1}
          value={value ?? def.min ?? 0}
          onChange={(e) => onChange(parseFloat(e.target.value))}
          style={{ width: '100%' }}
        />
      </div>
    );
  }
  if (def.kind === 'text') {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
        <div style={{ fontSize: 10, letterSpacing: 0.5 }}>{def.label}</div>
        <input
          type="text"
          value={value ?? ''}
          onChange={(e) => onChange(e.target.value)}
          style={{
            fontFamily: '"Work Sans", sans-serif',
            fontSize: 11,
            border: '1px solid #1A1612',
            background: '#ECE5D2',
            padding: '4px 6px',
          }}
        />
      </div>
    );
  }
  if (def.kind === 'enum') {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
        <div style={{ fontSize: 10, letterSpacing: 0.5 }}>{def.label}</div>
        <select
          value={value ?? def.options[0]}
          onChange={(e) => onChange(e.target.value)}
          style={{
            fontFamily: '"Work Sans", sans-serif',
            fontSize: 11,
            border: '1px solid #1A1612',
            background: '#ECE5D2',
            padding: '4px 6px',
          }}
        >
          {def.options.map((o) => <option key={o} value={o}>{o}</option>)}
        </select>
      </div>
    );
  }
  return null;
}

const TWEAK_DEFS = {
  claude: [
    { key: 'five_h_pct', label: '5H %', kind: 'pct', min: 0, max: 100 },
    { key: 'seven_d_pct', label: '7D %', kind: 'pct', min: 0, max: 100 },
    { key: 'opus', label: 'OPUS %', kind: 'pct', min: 0, max: 100 },
    { key: 'sonnet', label: 'SONNET %', kind: 'pct', min: 0, max: 100 },
    { key: 'plan', label: 'PLAN', kind: 'enum', options: ['Free', 'Pro', 'Max 5×', 'Max 20×'] },
    { key: 'five_h_reset', label: 'RESETS', kind: 'text' },
  ],
  codex: [
    { key: 'primary_pct', label: 'PRIMARY %', kind: 'pct', min: 0, max: 100 },
    { key: 'secondary_pct', label: 'SECONDARY %', kind: 'pct', min: 0, max: 100 },
    { key: 'credits', label: 'CREDITS $', kind: 'num', min: 0, max: 500, step: 0.5 },
    { key: 'plan', label: 'PLAN', kind: 'enum', options: ['Free', 'Plus', 'Pro', 'Team'] },
  ],
  github: [
    { key: 'today_commits', label: 'TODAY', kind: 'num', min: 0, max: 50 },
    { key: 'streak', label: 'STREAK', kind: 'num', min: 0, max: 365 },
    { key: 'pr_open', label: 'PRs OPEN', kind: 'num', min: 0, max: 20 },
    { key: 'pr_review', label: 'IN REVIEW', kind: 'num', min: 0, max: 20 },
  ],
  habits: [
    { key: 'streak', label: 'STREAK', kind: 'num', min: 0, max: 365 },
  ],
};

window.Simulator = Simulator;
