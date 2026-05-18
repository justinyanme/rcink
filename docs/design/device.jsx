// M5Stack PaperColor bezel — white plastic enclosure around the 400×600
// e-ink panel. Three side buttons (A/B/C) wake the device. The bezel itself is
// purely cosmetic in the preview; clicks pass through to user-supplied handlers.

function PaperColorDevice({
  tree,
  fontKey = 'firmware',
  inkKey = 'default',
  flash = false,
  scale = 1,
  onPressA,
  onPressB,
  onPressC,
  highlight,        // 'A' | 'B' | 'C' | null — show a press-tween
  labelTop,         // optional caption above the screen ("M5STACK PaperColor")
  showLabels = true,
  staleBadge = false,
  variant = 'matte', // 'matte' | 'plastic-dark'
}) {
  // Dimensions chosen so the device frame mirrors the real PaperColor's
  // proportions (~110mm × 165mm portrait): screen 400×600, bezel 28px sides,
  // 36px top, 110px bottom (where the buttons sit).
  const PAD_X = 28;
  const PAD_T = 36;
  const PAD_B = 110;
  const W = SCREEN_W + PAD_X * 2;   // 456
  const H = SCREEN_H + PAD_T + PAD_B; // 746

  const cornerR = 18;

  const bodyFill = variant === 'plastic-dark' ? '#22221F' : '#EAE5DC';
  const bezelInner = variant === 'plastic-dark' ? '#0E0E0C' : '#D9D2C1';
  const screwColor = variant === 'plastic-dark' ? '#3A3A37' : '#C3BBA8';
  const labelColor = variant === 'plastic-dark' ? '#9A958A' : '#5E5546';

  return (
    <div
      className="pc-device"
      style={{
        width: W * scale,
        height: H * scale,
        position: 'relative',
        // outer device shadow for the canvas
        filter: 'drop-shadow(0 24px 32px rgba(20, 18, 14, 0.18)) drop-shadow(0 2px 4px rgba(20, 18, 14, 0.10))',
      }}
    >
      <svg width={W * scale} height={H * scale} viewBox={`0 0 ${W} ${H}`} style={{ position: 'absolute', inset: 0 }}>
        {/* body */}
        <rect x="0" y="0" width={W} height={H} rx={cornerR} ry={cornerR} fill={bodyFill} />
        {/* subtle body inner shadow */}
        <rect x="0.5" y="0.5" width={W - 1} height={H - 1} rx={cornerR - 0.5} ry={cornerR - 0.5}
          fill="none" stroke="rgba(0,0,0,0.15)" strokeWidth="1" />
        {/* screen recess */}
        <rect
          x={PAD_X - 8} y={PAD_T - 8}
          width={SCREEN_W + 16} height={SCREEN_H + 16}
          rx="3" ry="3"
          fill={bezelInner}
        />
        {/* corner screws */}
        {[[8, 8], [W - 8, 8], [8, H - 8], [W - 8, H - 8]].map(([cx, cy], i) => (
          <g key={i}>
            <circle cx={cx} cy={cy} r="3" fill={screwColor} />
            <line x1={cx - 2} y1={cy} x2={cx + 2} y2={cy} stroke="rgba(0,0,0,0.35)" strokeWidth="0.7" />
          </g>
        ))}
        {/* top label */}
        {showLabels && (
          <>
            <text
              x={PAD_X + 4}
              y={PAD_T / 2 + 5}
              fontFamily='"Archivo Black", sans-serif'
              fontSize="10"
              letterSpacing="2"
              fill={labelColor}
            >
              M5STACK · PaperColor
            </text>
            <text
              x={W - PAD_X - 4}
              y={PAD_T / 2 + 5}
              textAnchor="end"
              fontFamily='"JetBrains Mono", monospace'
              fontSize="9"
              fill={labelColor}
            >
              E6 · 400×600
            </text>
          </>
        )}
        {/* bottom button labels */}
        {showLabels && (
          <g>
            {['A', 'B', 'C'].map((b, i) => {
              const xc = PAD_X + (i + 0.5) * (SCREEN_W / 3);
              return (
                <text
                  key={b}
                  x={xc}
                  y={PAD_T + SCREEN_H + 78}
                  textAnchor="middle"
                  fontFamily='"Archivo Black", sans-serif'
                  fontSize="14"
                  fill={labelColor}
                >
                  {b}
                </text>
              );
            })}
          </g>
        )}
        {/* USB-C port hint on bottom edge */}
        <rect
          x={W / 2 - 12}
          y={H - 6}
          width={24}
          height={4}
          rx={2}
          fill="rgba(0,0,0,0.35)"
        />
      </svg>

      {/* Live screen */}
      <div
        style={{
          position: 'absolute',
          left: PAD_X * scale,
          top: PAD_T * scale,
          width: SCREEN_W * scale,
          height: SCREEN_H * scale,
          overflow: 'hidden',
          boxShadow: 'inset 0 2px 4px rgba(0,0,0,0.25)',
        }}
      >
        <PaperColorScreen tree={tree} fontKey={fontKey} inkKey={inkKey} flash={flash} scale={scale} />
        {staleBadge && (
          <div style={{
            position: 'absolute',
            top: 8 * scale, right: 8 * scale,
            background: RC.INK.yellow,
            border: `1px solid ${RC.INK.black}`,
            color: RC.INK.black,
            fontFamily: '"Archivo Black", sans-serif',
            fontSize: 10 * scale,
            padding: `${2 * scale}px ${6 * scale}px`,
            letterSpacing: 1,
          }}>STALE</div>
        )}
      </div>

      {/* Buttons (real, clickable). Positioned over the bottom plastic. */}
      <div
        style={{
          position: 'absolute',
          left: PAD_X * scale,
          top: (PAD_T + SCREEN_H + 18) * scale,
          width: SCREEN_W * scale,
          display: 'flex',
          justifyContent: 'space-around',
          pointerEvents: 'none',
        }}
      >
        {[
          ['A', onPressA],
          ['B', onPressB],
          ['C', onPressC],
        ].map(([b, fn]) => (
          <button
            key={b}
            onClick={fn}
            aria-label={'Btn' + b}
            className="pc-btn"
            style={{
              width: 44 * scale,
              height: 44 * scale,
              borderRadius: '50%',
              background: variant === 'plastic-dark' ? '#0E0E0C' : '#C8C0AE',
              border: variant === 'plastic-dark' ? '1px solid #2A2A28' : '1px solid #A89F89',
              boxShadow: highlight === b
                ? `inset 0 2px 4px rgba(0,0,0,0.45)`
                : `inset 0 -2px 3px rgba(0,0,0,0.18), 0 1px 2px rgba(0,0,0,0.08)`,
              transition: 'box-shadow 100ms ease',
              pointerEvents: 'auto',
              cursor: 'pointer',
              padding: 0,
              transform: highlight === b ? 'translateY(1px)' : 'none',
            }}
          />
        ))}
      </div>
    </div>
  );
}

window.PaperColorDevice = PaperColorDevice;
