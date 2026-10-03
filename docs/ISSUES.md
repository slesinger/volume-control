# Known issues & tech debt

Found by code inspection (not hardware-tested). Ordered by priority. Mark `[x]` when fixed.

## Correctness / stability

- [x] **1. Blocking network I/O in the main loop** (`network.cpp: send_ssc_command`). `connect()` has no timeout (SO_SNDTIMEO doesn't bound connect on lwIP), recv/send timeouts are 1 s, and each speaker is polled sequentially, so one powered-off speaker can stall `loop()` for seconds → watchdog resets, laggy encoder. Fix: non-blocking connect with `select` timeout (~200 ms), or a FreeRTOS task that does network I/O and publishes state via a queue/mutex.
- [x] **2. Null dereference with no devices** (`vol_ctrl.cpp:127` `last_state->requested_volume`, and `update_whole_screen()` uses `last_state` unguarded at every call). Crashes if no device is registered.
- [x] **3. Change flags overwritten, not OR-ed** (`vol_ctrl.cpp:113,115`): `standby_countdown_changed = …` / `mute_changed = …` only reflect the last speaker; `is_up_changed |=` is correct. Use `|=` everywhere.
- [x] **4. Single `recv()` for the reply** (`network.cpp`). TCP may deliver partial data and replies end in CRLF; larger replies (EQ queries) exceed the 512-byte buffer. Read until `\r\n` / buffer full.
- [x] **5. Mute toggles per-speaker from its own state** (`toggle_mute`). If speakers disagree they stay inverted. Derive a single target from the first/“any unmuted” speaker and apply to all.
- [x] **6. `volume_change_from_hass` is O(N²) and wrong**: for each device it calls `set_volume_from_hass`, which sets *all* devices. Compute once, send once per device.
- [x] **7. `volume_change()` hijacks menu navigation**: HA `set_volume` while the menu is open calls `menu_down()`. Separate "set volume" from "UI input"; ignore or queue HA volume while in menu (comment in code says to ignore).
- [x] **8. Error-path leaks / noise**: failed polls log at ERROR every 1.5 s per offline speaker; socket is closed on each path manually – wrap in an RAII guard.
- [x] **9. Unvalidated volume bounds**: 120 dB max is hard-coded in three places and there's no soft cap. Add configurable `max_volume` (and optionally max step per tick) – a stray encoder glitch should not be able to send 120 dB to monitors.
- [x] **10. Encoder start-up hack**: `fabs(diff) > 10` is used to swallow the first bogus tick. Use the sensor's `on_clockwise`/`on_anticlockwise` triggers instead of diffing against a global.
- [ ] **11. `WAIT_FOR_WIFI` loop state never changes**: `loop_state` is initialised to `WAIT_FOR_WIFI`, never set to `MAIN_LOOP`, and the main polling code runs under the `WAIT_FOR_WIFI` branch. Either remove the enum or implement the states (WIFI → DISCOVER → RUN → SLEEP). The `delay(1000)` inside `loop()` while waiting for WiFi also blocks the loop.

## Menu / UI

- [x] **12. Menu levels 2 and 3 are unreachable.** (fixed on `port-master-ideas`: data-driven menu in `menu.cpp`) All submenus set `menu_level_ = 1`; the switch cases `2`/`3` in `menu_select` are dead and the renderer disambiguates by `menu_items_count_` (4/6/7). Introduce a submenu id / data-driven menu.
- [x] **13. Item counts don't match rendered items**: EQ submenu count 4 but 3 rows drawn, speaker params 6 vs 5 rows, volume settings 7 vs 5 rows → highlight can land on nothing. "Deep sleep timeout" is drawn at y=90, overlapping "Backlight intensity".
- [x] **14. Menu dot Y positions** (`52 + 20*i` clear vs `58 + 20*i` draw) are magic numbers duplicated from text rows (`50 + 20*i`); derive from one `MENU_ROW_Y(i)`.
- [ ] **15. Button behaviour differs from spec**: README says long-press = 1 s and "volume 0 toggles mute"; code uses 300 ms and does not implement the volume-0 rule. Pick one and update README.
- [ ] **16. Rate limit from spec is not implemented**: README says commands are sent at most once per second; `last_volume_change_` is written but never read. (Current behaviour — send on every tick — works well per the latest commit, so update the README rather than add a limit; but do coalesce, see ROADMAP.)
- [ ] **17. Datetime region** `{240, 0, 100, …}` relies on TR_DATUM and is never cleared except by overdraw with background colour; a shorter string can leave artefacts. Datum is reset to `MC_DATUM` only inside `update_datetime`; other draw functions assume it silently.
- [ ] **18. (partly fixed: band cleared on digit-count change and on unmute) `update_mute_status` draws the mute icon over the volume digits without clearing** and the digits' region fill is commented out in `update_volume_display` → ghosting on unmute from 3-digit/2-digit transitions.

## Dead / duplicate code

- [ ] **19. (partly fixed: duplicate utils/, ROT_SYMBOLS, device_rot and unused members removed) Unused or never-defined symbols**: `display::draw_wifi_icon`, `draw_forbidden_icon`, `update_standby_status` (declared, no definition); `VolCtrl::get_device_states`, `volume_step_`, `backlight_level_`, `display_timeout_`, `last_interaction_`, `display_active_`, `user_adjusting_volume_`, `last_detail_check_`, `ROT_SYMBOLS`, `device_rot` (set but never used); `vol_ctrl.cpp: get_datetime_string()` duplicates `utils::get_datetime_string` (and `utils/datetime.*` exists too). `exit_menu`'s `uint32_t now` unused in `enter_menu`.
- [x] **20. `const_cast` of `get_device_states()`** is repeated in 5 places because the getter returns `const&` while callers mutate. Provide a non-const accessor (or have `network` own mutation APIs).
- [x] **21. `DeviceState::set_standby_countdown`** has an unused local `standby_countdown_change`; `get_requested_volume()` isn't `const`.

## Config / hygiene

- [ ] **22. Hard-coded speaker IPv6 addresses** in `network::init()` using a global prefix (`2a00:1028:8390:75ee::`). If the ISP prefix changes the device silently stops working. Prefer discovery (mDNS `_ssc._tcp`), persisted via preferences, or at least move to yaml substitutions/`secrets.yaml`.
- [ ] **23. Hand-rolled JSON string search** (`utils/json.cpp`) is fragile (e.g. key `level` could match inside another key; `countdown` vs other fields). Use ESPHome's bundled ArduinoJson (`esphome/components/json`).
- [ ] **24. Include path hack**: `-Isrc/esphome/components/...` build flags and manual `class TFT_eSPI` forward declaration are workarounds; `TFT_eSPI.h` is included in headers (`display.h`) anyway. Revisit once build is stable.
- [ ] **25. `README.md` is out of date and doubles as a scratchpad**: says ESP8266/EEPROM (target is ESP32), contains a pasted curl transcript in TODO, TODO items in mixed Czech/English. Split: user README / requirements / TODO (→ ROADMAP.md).
- [ ] **26. No CI, no tests.** Add `esphome compile` in GitHub Actions and host-side unit tests for `utils` JSON parsing (can be compiled with plain g++).

## Fixed on branch `review-fixes` (not in the list above)

- Failed polls no longer overwrite volume/mute/standby with zero defaults (`get_device_data` leaves output untouched, loop skips down speakers).
- Encoder volume is clamped before it is stored; out-of-range state (-1 / 121) can no longer freeze or skip ticks.
- Encoder uses `on_clockwise`/`on_anticlockwise`; the ±200 count saturation and start-up hack are gone.
- Speaker dots are drawn on the first poll even if all speakers start offline (`force_redraw_`).
- Polling is skipped while the menu is open; HA volume services are ignored in the menu.
- `max_volume` option (yaml: 100 dB) caps every volume sent.
- Fixed off-by-one in `extract_json_value`; removed duplicated `utils/json.*`, `utils/datetime.*`.

- Branch `port-master-ideas`: HA services/entities, WiiM control (own worker task, HTTP API only, no UPnP), brightness editor and deep sleep (settings persisted in NVS), "Deep sleep timeout" menu row no longer overlaps (#13 for the volume settings submenu, count 7 -> 5), extra push buttons. Compile-checked only.
- Branch `async`: all socket I/O moved to a FreeRTOS worker task (`network.cpp`). Writes are coalesced (latest volume/mute per speaker), polls back off to 5 s for unreachable speakers, and polls that race with a write are discarded. `loop()` and the encoder/button callbacks no longer touch the network.
