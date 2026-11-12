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

**Page**:
One screen of the Deck View. KEY steps through the Pages and the Dashboard in
a single ring.
_Avoid_: board, 看板, screen

**Allowance**:
The unused share of a Provider's weekly subscription limit. It belongs to an
account, so it is never summed across machines.
_Avoid_: quota, limit, usage

**Token usage**:
Tokens a Provider's CLI consumed in a local day, summed across Sources.
_Avoid_: usage (alone), consumption, spend

**Source**:
A machine that uploads its own Token usage, such as the M4 or the MacBook.
_Avoid_: device, host

**Allowance page**:
The Page that shows each Provider's Allowance as a fuel road.
_Avoid_: usage page, usage dashboard

**Token page**:
The Page that shows each Provider's Token usage.
_Avoid_: tokens dashboard, usage page

**Provider**:
An AI service the device reports on, named the same on every Page: Claude,
Codex, Kimi or Gemini. Kimi has an Allowance but no Token usage.
_Avoid_: vendor, model, Claude Code, Antigravity, Gemini CLI

**Seven-day total**:
Token usage summed over today and the six days before it. It is not tied to
any Allowance reset.
_Avoid_: this week, weekly usage

**Usual day**:
The mean daily Token usage of the seven complete days before today. Today's
Token usage is reported as a share of it.
_Avoid_: average day, normal, baseline

**Lifetime**:
Token usage summed since the first record (2025-09-18). It only grows.
_Avoid_: total, all-time
