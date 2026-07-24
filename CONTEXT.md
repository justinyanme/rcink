# rcink

Language for dashboard content, device interaction, and low-power operation.

## Language

**View**:
User-selected Dashboard or Deck presentation, preserved across wake cycles.
_Avoid_: UI mode, screen mode

**Runtime mode**:
Live RLCD power/interaction state: `HOST`, `ACTIVE`, or `ECO`; independent of
View.
_Avoid_: View, power source

**USB Host presence**:
USB data-host presence proven by SOF traffic.
_Avoid_: External power, charger present, VBUS present

**Wake cycle**:
Wake, sense, maybe fetch, render or skip, then deep sleep.

**Sleep proof**:
USB port de-enumeration after a wake cycle; meter-free deep-sleep evidence.

**Wedge**:
Chip parked in ROM download mode instead of running firmware or entering deep
sleep.

**`data_ts`**:
Page content-change time, carried forward while content is unchanged.
