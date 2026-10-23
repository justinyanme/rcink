# KEYPOINT KP4 producer and rollout

2026-10-23. KEYPOINT keyboard protocol is separate from rcink's JSON display DSL.

## Contract

`KP4|GEN|IDX|TOTAL|ICON|LED|ROLES|L1|L2|L3|L4|L5|L6`, printable ASCII, maximum 88 bytes. Six lines, at most nine characters each. `ROLES` contains exactly six digits aligned with the six lines: `0` neutral, `1` positive, `2` attention, `3` warning, `4` error. Firmware owns palette/layout; producer sends meanings, never RGB values. Card LED hint remains independent. KP3 firmware rejects KP4 frames.

Authority: `~/w/zmk-config-KEYPOINT/config/boards/shields/lpm_view/widgets/live_data.h` and `live_data_core.c`; `rcink/kp4.py` and `kp4_contract.json` are vendored from that repo's `scripts/` directory. Upgrade/freeze both copies together.

`Row.role` accepts lowercase semantic names, default `neutral`. Rendering keeps roles aligned through timestamp insertion and empty-line padding. Usage bars represent **consumed quota**: below 75% positive, 75–<90% attention, ≥90% warning; each window is independent. Highest model-scoped limit uses the same thresholds. Reset countdown does not affect severity. Whole-card LED calculation still considers usage windows only, excluding the scoped-limit line.

## Working-tree deployment

The loaded LaunchAgent `com.rocry.rcink.keypoint` executes `uv run --directory /Users/rocry/w/_hw/rcink/producer keypoint-push` every 60 seconds. Editing this checkout changes the next scheduled send; a commit/restart is not required. `keypoint-push` now defaults to acknowledged writes (`response=True`), surfacing firmware rejection instead of treating write-without-response submission as acceptance.

**Current upgrade state:** sender was unloaded with `launchctl bootout` to prevent KP4 reaching an unupgraded keyboard. Keep it unloaded until KP4 firmware is flashed and an acknowledged deck succeeds. Keyboard USB is not currently attached; flashing and physical eight-color acceptance remain pending. The plist remains at `/Users/rocry/Library/LaunchAgents/com.rocry.rcink.keypoint.plist`.

## Resume after firmware upgrade

1. Flash the matching KEYPOINT firmware; reconnect keyboard BLE.
2. Regenerate cached cards. Existing `~/.cache/rcink/keypoint/*.json` rows without `role` load as neutral; they do not acquire usage roles merely by upgrading the sender. The existing `com.rocry.rcpaper.all` LaunchAgent refreshes cards via `fetch-all` every 900 seconds. To trigger its configured fetch environment after flashing:

   ```sh
   launchctl kickstart gui/501/com.rocry.rcpaper.all
   ```

   Inspect `/Users/rocry/Library/Logs/rcpaper.log` and cached card rows for successful regeneration. A failed source can retain old cached content; do not assume every card refreshed because the job ran.

3. Send one acknowledged deck only after the firmware upgrade:

   ```sh
   uv run --directory /Users/rocry/w/_hw/rcink/producer keypoint-push --response
   ```

4. After acknowledgement succeeds, restore the 60-second sender:

   ```sh
   launchctl bootstrap gui/501 /Users/rocry/Library/LaunchAgents/com.rocry.rcink.keypoint.plist
   ```

   `RunAtLoad` sends immediately. Check `/Users/rocry/Library/Logs/rcink-keypoint.log`; acknowledged delivery proves protocol acceptance, while visible colors, cold boot, wake, COM behavior and power require physical verification.
