# Roadmap

Ordered by value/effort. References like (#12) point to `docs/ISSUES.md`.

## Phase 0 – Stabilise (do first)
- Fix issues #1–#3, #5–#7, #9 (non-blocking network, null guards, consistent mute, safe max volume).
- Remove dead code (#19–#21), add `esphome compile` CI (#26).
- Refresh README (#25).

## Phase 1 – Core UX
- **Max volume & step settings** stored in NVS (ESPHome `preferences`), exposed in menu 7.1 and as HA `number` entities.
- **Display timeout / backlight dimming** using `last_interaction_` (fields already exist); wake on encoder/button. Menu 7.2/7.3.
- **Standby indicator**: show "Zzz" when speaker is in standby (spec in README, not implemented); expose countdown.
- **Persistent speaker list + discovery** (menu 5): mDNS query for `_ssc._tcp`, store IPv6 in NVS, drop hard-coded addresses (#22). Show list/details screens (menu 2/3: name, product, serial, firmware, level, input).
- **Volume coalescing**: keep `target_volume`, send at most every ~50–100 ms per speaker; display blue until confirmed.

## Phase 2 – Speaker features
- Speaker parameter editing (menu 6): logo brightness (`ui.logo.brightness`), delay (`audio.out.delay`), standby timeout (`device.standby.auto_standby_time`), standby enable. Add a generic "numeric editor" screen (encoder = value, press = save).
- Input selection / status display if the model exposes it.
- Parametric EQ (menu 4; the read-only list of eq2/eq3 bands is done, see Param EQ in the menu): read `audio.out.eq2` arrays (enabled/type/frequency/q/gain), list bands, edit/enable/disable, curve plot (biquad magnitude response drawn on 240x240). Needs the multi-line reply handling from #4. Keep a "flat" reset action.
- Settings backup/restore via HA service (dump speaker JSON like `khtool --backup`).

## Phase 3 – Power & integration
- Deep sleep when idle/speakers asleep (README TODO): switch display VCC (GPIO via transistor or high-side switch), backlight off, wake on encoder button (ext0 wake on GPIO25). Define policy: sleep after N minutes with all speakers in standby.
- Home Assistant entities, not just services: `number` (volume), `switch` (mute), `sensor` (standby countdown, per-speaker online), `text_sensor` (speaker state). Per-speaker control optional.
- WiiM integration (README TODO): query `https://<wiim>/httpapi.asp?command=getMetaInfo` (self-signed cert, `-k`) to show title/artist/input on the idle screen; also play/pause on double-press. Make the WiiM address a yaml substitution.
- OTA-friendly: version string shown in menu 7; safe-mode behaviour documented.

## Phase 4 – Code structure
- Data-driven menu tree (#12–#14) so adding screens is declarative.
- `SpeakerClient` class with persistent non-blocking socket + queue (see ARCHITECTURE.md).
- Host-side unit tests for JSON parsing, menu navigation state machine and EQ math (g++ + simple assert harness).
- Optionally split `vol_ctrl` into smaller ESPHome components (`ssc_speaker`, `vol_display`) with proper yaml config (speakers list, max_volume, step) instead of constants in C++.

## Hardware / mechanical (non-code)
- Mount electronics in top-case (`cad/case_v1.FCStd`); verify rotary pins after the recent pin change; consider hardware debounce for KY-040.
- Display VCC switching for deep sleep.

## Ideas / open questions
- Multiple speaker groups (e.g. stereo pair + sub) with relative trim per speaker?
- Volume curve: current 1 dB per tick, consider acceleration when turned fast.
- Behaviour when a speaker is offline: apply to the reachable one only (currently yes) and re-sync levels on reconnect?
