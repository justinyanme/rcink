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

**Native refresh**:
Complete six-colour panel update driven by the controller's built-in (OTP)
waveform; the quality reference and the only vendor-qualified mode.
_Avoid_: full refresh, slow refresh, quality mode

**Fast mono**:
Two-tone panel update driven by a host-supplied waveform or an interrupted
native waveform; speed-first, residue tolerated until the next Cleanup. Only
changed pixels are driven.
_Avoid_: partial refresh, fast mode

**Draft color**:
Six-colour panel update that trades colour accuracy and residue for speed.
_Avoid_: fast color, low-quality color

**Cleanup**:
A Native refresh performed to remove residue accumulated by Fast mono or
Draft color updates.
_Avoid_: clear, flush

**Trial**:
One numbered, single-variable panel experiment with its serial log, command
timestamps and settled photo kept together.
_Avoid_: test, run, experiment

**Lab firmware**:
Minimal firmware whose only jobs are driving panel Trials over a serial console
and showing their conclusions in the Demo gallery; no network, storage, sleep or
product pages.
_Avoid_: dev build, debug firmware

**Demo gallery**:
Button-driven pages in the Lab firmware, one per proven capability, each naming
the update it uses; for people to judge results on the device.
_Avoid_: demo mode, showcase

**Own waveform**:
A waveform this project designed and measured itself, borrowing methods from
reference implementations but not their table bytes.
_Avoid_: custom LUT, our LUT

**Clean mono**:
Two-tone update with an Own waveform that drives every pixel regardless of the
previous image; slower than Fast mono, residue close to a Native refresh.
_Avoid_: full mono, slow mono
