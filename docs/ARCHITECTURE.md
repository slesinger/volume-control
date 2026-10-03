# Architecture

## Runtime overview

```
 KY-040 encoder ──(ESPHome rotary_encoder sensor, yaml lambda)──► VolCtrl::process_encoder_change(diff)
 KY-040 button  ──(binary_sensor, 50ms debounce)────────────────► button_pressed()/button_released()
 Home Assistant ──(API services)────────────────────────────────► set_volume_from_hass / volume_change_from_hass / toggle_mute

 VolCtrl::loop()  every 1500 ms:
     for each DeviceState: network::get_device_data()  ──► DeviceState setters (return "changed")
     if !in_menu_: display::update_* (partial redraws)

 network::send_ssc_command(): socket → connect [ipv6]:45 → send JSON+CRLF → single recv → close
```

## Components

| File | Role |
|---|---|
| `vol_ctrl.cpp` | Component lifecycle, polling loop, encoder/button logic, menu state machine (`menu_level_`, `menu_position_`, `menu_items_count_`) |
| `network.cpp` | Blocking SSC client; `device_map` (name→ipv6) and `device_states` (ipv6→`DeviceState`) are file-static globals |
| `display.cpp` | Stateless drawing functions taking `TFT_eSPI*`; screen regions hard-coded for 240x240 |
| `device_state.cpp` | `is_up`, `requested_volume`, `muted`, `standby_countdown` with change-detecting setters |
| `utils*.cpp` | String-search JSON helpers (`extract_json_number`, `check_json_boolean`), datetime formatting |

## Data flow for a volume change

1. Encoder tick → yaml lambda computes `diff` against `last_encoder_value` → `process_encoder_change(diff)`.
2. For each device: `requested_volume += diff`, immediately `network::set_device_volume()` (one new TCP connection per speaker per tick), display shows the number in **blue**.
3. `last_device_check_` is reset so the next `loop()` polls speakers and redraws in **yellow** (= confirmed by device).

## Menu

Main menu has 7 items (Exit, List speakers, Speaker details, Parametric EQ, Discover, Speaker params, Volume settings). Only Exit and navigation into three submenus are implemented; every other item is a TODO. Submenus are drawn by switching on `menu_items_count_` (4/6/7), not on a submenu id.

## Protocol notes (SSC)

- TCP port 45 on the speaker's IPv6 address; messages are JSON + `\r\n`.
- `null` value = query, value = set. Example poll: `{"device":{"standby":{"countdown":null}},"audio":{"out":{"level":null,"mute":null}}}`.
- Discovery in the reference tool (`khtool`/`pyssc`) uses mDNS/zeroconf service `_ssc._tcp`; speakers also publish name, serial, product in `device.identity.*`.
- Full spec: `docs/TI_1093_v2.0_Sennheiser_Sound_Control_Protocol_ew_D1_EN.md`.

## Target architecture (suggested direction)

- A `SpeakerClient` class per speaker owning a persistent (or lazily reconnecting) non-blocking socket and a small command queue; `loop()` only pumps queues.
- Volume writes coalesced: keep `target_volume`, send at most every ~100 ms, last value wins.
- Menu as data: a tree of `MenuItem{label, kind, action/children}` rendered generically, replacing the hard-coded `draw_menu_screen` switch and `menu_select` if-chains.
- Settings (speaker list, max volume, step, timeouts) persisted via ESPHome `preferences` and editable from the menu / HA.
