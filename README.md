# Volume Control for Neumann KH Speakers

For DSP enabled speakers that can be controlled over network IPv6.

- KH-120 II
- KH-150
- ??

## Features
- Automatic speaker discovery
- Volume control
- Mute control
- Display of current volume level
- Display speaker settings
- Set parametric equalizer settings
- Works with Home Assistant
- 3 configurable buttons (e.g. input select, pause/play, next song)
- works with Wii Pro
- deep sleep (rotary push button to wake up)

It uses Senheiser Sound Control Protocol (SSP) to control the volume of the speakers and reading and setting parameters.

## TODO

deep sleep - switch of display vcc
make display off during seep sleep
zabudovat hw do top-case
deepsleep kdyz repraky usnou
discover wiim and display input

curl -X GET "https://192.168.1.245/httpapi.asp?command=getMetaInfo" -k | jq
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100   297  100   297    0     0   3483      0 --:--:-- --:--:-- --:--:--  3494
{
  "metaData": {
    "album": "Best of the Best",
    "title": "Červená řeka",
    "subtitle": "unknow",
    "artist": "Helena Vondrackova",
    "albumArtURI": "https://static.qobuz.com/images/covers/4a/hh/clin57z27hh4a_600.jpg",
    "sampleRate": "44100",
    "bitDepth": "16",
    "bitRate": "unknow",
    "trackId": "unknow"
  }
}


## Hardware

- ESP32 Wemos Lolin32
- Display GMT130 240x240px, Driver IC ST7789, SPI interface, it needs HW SPI controller, needs init with parameter SPI_MODE2 (it does not have CS pin)
- Rotary encoder with push button KY-040

The hardware is controlled using ESPHome with custom components for display, network communication, and speaker control.

## Wiring

Display pins:
- GND -> GND
- VCC -> 3.3V
- SCL -> GPIO18 (SCK)
- SDA -> GPIO23 (MOSI)
- RES -> GPIO4 (RESET)
- DC  -> GPIO16 (DC)
- BLK -> GPIO17 (backlight, PWM)

Rotary encoder pins:
- GND -> GND
- VCC -> 3.3V
- CLK -> GPIO32
- DT  -> GPIO33
- SW  -> GPIO25

## Software

Based on ESP Home project. ESPhome is used to compile a firmware for ESP8266 and to control the display and rotary encoder. When ESP is powered on, it will connect to the WiFi network. It will connect to the speakers over IPv6 and control them. It will also natively integrate with Home Assistant and export services for volume control and speaker settings.

This project is based on the logic coded in source code of the KH Tool, see directory ```khtool```. The source code is not used directly, it is only for reference.
Refer to the ```Resources``` below for further documentation, however the khtool project shows practical implementation of the protocol, include discovery, volume control, speaker settings and parametric equalizer settings.

> Note: Commands send to the speakers are always sent to all speakers.

### Initialization

When the ESP8266 is powered on, it will render the default screen. It will connect to the WiFi network and then it will start connecting to the configured speakers on the network and fetch their settings. It will also fetch date and time from NTP server. The display will show status messages during this process. This is useful because it takes some time to connect to the speaker after power on, and it is not possible to control volume before connection is established.

### Default Screen Layout

Top line is a status line:
 - it shows date and time aligned to the right in format HH:MM MMM DD (example 12:34 Dec 24) and auto standby time on the left. Text color will be white.
 - wifi status as a signal icon on the left side of the datetime. Color of the icon will indicate connection state (green for connected, red for disconnected).
  - speaker connection states as a 3D dot. Each speaker device is represented by one dot. They are aligned to the left. Color of the dot will indicate connection state (Green for connected, gray for disconnected.

Bottom line:
 - display status text during initialization, like "Connecting to WiFi", "Connecting to speakers", "Discovering speakers" etc. Text color will be white.
 - during normal operation it will show "Press button to enter menu" text, which will be centered horizontally on the display. Text color will be white.

Middle area:
 - display current volume level in big numbers, aligned to the center of the display. Text color will be yellow.
 - if speaker is muted, display a forbidden sign over the volume level.
 - if speaker is in standby mode, display a "Zzz" icon over the volume level.

### Menu Screen Layout
When the menu is entered, the display will show a list of menu items. The menu items will be navigated using the rotary encoder. The push button will select the item. The top line will show two dots ".." to indicate return to parent menu.

## Information gathering
Most of information displayed will be fetched straight from the speakers over network, except of the date and time, which will be fetched from NTP server and except IPv6 addresses of the speakers and own settings, which will be stored in EEPROM of the ESP8266 like this:

```cpp
#include <EEPROM.h>

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512); // Allocate 512 bytes for EEPROM emulation

  // Write a value
  int address = 0;
  int value = 123;
  EEPROM.put(address, value);
  EEPROM.commit(); // Save changes to flash

  // Read a value
  int readValue;
  EEPROM.get(address, readValue);
  Serial.print("Read value from EEPROM: ");
  Serial.println(readValue);
}

void loop() {
  // Your main code
}
```

## Installation

pip install esphome
esphome dashboard .

# Requirements specification

## Normal operation (outside of menu)

Rotary encoder will increase/decrease volume by 1 db step on each click. On every click it will also update the display with current volume level in blue color. The number will change back to yellow when new reading, as confirmation, from the device will read real value of volume. When user changes volume level by rotating the volume knob, it will send a command to the speaker to set the new volume level, but it will not be more requent than once a second.
If the volume is set to 0, it will toggle mute on the speaker. If the volume is set to non-zero value, it will toggle unmute on the speaker. It will also toggle mute on push button click.

## Menu operation

Entering menu by long press (1 second) of rotary encoder push button.
Rotary encoder will navigate through menu items, push button will select item.
Two dots will indicate return to parent menu. It will be displayed on top of menu item list, except of the root menu.

### Menu items

This section shows menu structure and how to navigate through it.

1. Exit menu
2. Show devices
3. Show settings
4. List Parametric eq (this shows a list of eqs in charge. On each item in the list apply edit and delete actions)
  4.0. .. (go back to parent menu)
  4.1. Show curve (this shows a graph of the eq curve compared to a virtual flat line)
  4.2. Add
  ... here comes the list of eqs
    a. Edit  then save
    b. Delete
5. Disvoer devices (this will scan for devices on the network and update the list of known devices, IPv6 addresses of the speakers will be stored to persistent memory of the ESP8266)
6. Set speakers paraameters
  6.1. Set logo brightness
  6.2. Set delay
  6.3. Set standby timeout
  6.4. Set speaker auto_standby_time
7. Volume control settings
  7.1. Set volume step
  7.2. Set backlight
  7.3. Display timeout to stop backlight
  7.4. Set ESP deep sleep timeout (to save power)

# Home Assistant Integration

## Invoking services

### Set Volume Level

```yaml
action: esphome.volume_control_set_volume
data:
  level: 40
```

### Toggle Mute

```yaml
action: esphome.volume_control_toggle_mute

### Volume Up/Down

```yaml
action: esphome.volume_control_volume_up
```

# Resources

## Senheiser Sound Control Protocol (SSP)

There is a text version of the protocol specification available in the ```docs/TI_1093_v2.0_Sennheiser_Sound_Control_Protocol_ew_D1_EN.md```

## KH Tool

[KHTool](https://github.com/schwinn/khtool) is based on [Pyssc](https://github.com/schwinn/pyssc.git) library, which is a Python library for controlling Sennheiser and Neumann DSP loudspeakers over network using the Sennheiser Sound Control Protocol (SSP).

Usage of th KH Tool to control the speakers over network.
```bash
usage: khtool.py [-h] [--scan] [-q] [--backup BACKUP] [--restore RESTORE] [--comment COMMENT] [--save] [--brightness BRIGHTNESS]
                 [--delay DELAY] [--dimm DIMM] [--level LEVEL] [--mute] [--unmute] [--expert EXPERT] -i INTERFACE
                 [-t {all,0,1,2,3,4,5,6,7,8}] [-v]

options:
  -h, --help            show this help message and exit
  --scan                scan for devices and ignore the khtool.json file
  -q, --query           query loudspeaker(s)
  --backup BACKUP       generate json backup of loudspeaker(s) and save it to [filename]
  --restore RESTORE     restore configuration from [filename]
  --comment COMMENT     comment for backup file
  --save                performs a save_settings command to the devices (only for KH 80/KH 150/KH 120 II)
  --brightness BRIGHTNESS
                        set logo brightness [0-100] (only for KH 80/KH 150/KH 120 II)
  --delay DELAY         set delay in 1/48khz samples [0-3360]
  --dimm DIMM           set dimm in dB [-120-0]
  --level LEVEL         set level in dB [0-120]
  --mute                mute speaker(s)
  --unmute              unmute speaker(s)
  --expert EXPERT       send a custom command
  -i INTERFACE, --interface INTERFACE
                        network interface to use (e.g. en0)
  -t {all,0,1,2,3,4,5,6,7,8}, --target {all,0,1,2,3,4,5,6,7,8}
                        use all speakers or only the selected one
  -v, --version         show program's version number and exit
```

Example volume control

```bash
python3 ./khtool.py -i en1 --level 40.0
```

Example getting speaker settings and output

```bash
python3 ./khtool.py -i en1 -q
```

```json
Used Device:  Left
IPv6 address: 2a00:1028:8390:75ee:2a36:38ff:fe61:25b9
*** query device settings ***
{"device":{"name":"Left"}}
{"device":{"identity":{"vendor":"Georg Neumann GmbH"}}}
{"device":{"identity":{"product":"KH 150"}}}
{"device":{"identity":{"serial":"6473470117"}}}
{"device":{"identity":{"version":"1_1_11"}}}
{"device":{"standby":{"enabled":true}}}
{"device":{"standby":{"auto_standby_time":90}}}
{"device":{"standby":{"level":-65}}}
{"device":{"standby":{"countdown":90}}}
{"ui":{"logo":{"brightness":50}}}
{"audio":{"in":{"interface":"DIGITAL DISCARDS ANALOG"}}}
{"audio":{"in1":{"label":"DIGITAL AES3 INPUT CHANNEL A"}}}
{"audio":{"in2":{"label":"DIGITAL AES3 INPUT CHANNEL B"}}}
{"audio":{"out":{"level":40.0}}}
{"audio":{"out":{"mute":false}}}
{"audio":{"out":{"delay":0}}}
{"audio":{"out":{"solo":false}}}
{"audio":{"out":{"phaseinversion":false}}}
{"audio":{"out":{"mixer":{"levels":[0.0,-100.0]}}}}
{"audio":{"out":{"mixer":{"inputs":["../../in1","../../in2"]}}}}
{"audio":{"out":{"eq2":{"enabled":[false,false,false,false,false,false,false,false,false,false]}}}}
{"audio":{"out":{"eq2":{"type":["PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC"]}}}}
{"audio":{"out":{"eq2":{"frequency":[100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000]}}}}
{"audio":{"out":{"eq2":{"q":[0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700]}}}}
{"audio":{"out":{"eq2":{"gain":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq2":{"boost":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq2":{"desc":"post EQ"}}}}
{"audio":{"out":{"eq3":{"enabled":[false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false]}}}}
{"audio":{"out":{"eq3":{"type":["PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC"]}}}}
{"audio":{"out":{"eq3":{"frequency":[100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000]}}}}
{"audio":{"out":{"eq3":{"q":[0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700]}}}}
{"audio":{"out":{"eq3":{"gain":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq3":{"boost":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq3":{"desc":"calibration EQ"}}}}

Used Device:  Right
IPv6 address: 2a00:1028:8390:75ee:2a36:38ff:fe61:279e
*** query device settings ***
{"device":{"name":"Right"}}
{"device":{"identity":{"vendor":"Georg Neumann GmbH"}}}
{"device":{"identity":{"product":"KH 150"}}}
{"device":{"identity":{"serial":"6194478038"}}}
{"device":{"identity":{"version":"1_1_11"}}}
{"device":{"standby":{"enabled":true}}}
{"device":{"standby":{"auto_standby_time":90}}}
{"device":{"standby":{"level":-65}}}
{"device":{"standby":{"countdown":90}}}
{"ui":{"logo":{"brightness":50}}}
{"audio":{"in":{"interface":"DIGITAL DISCARDS ANALOG"}}}
{"audio":{"in1":{"label":"DIGITAL AES3 INPUT CHANNEL A"}}}
{"audio":{"in2":{"label":"DIGITAL AES3 INPUT CHANNEL B"}}}
{"audio":{"out":{"level":40.0}}}
{"audio":{"out":{"mute":false}}}
{"audio":{"out":{"delay":0}}}
{"audio":{"out":{"solo":false}}}
{"audio":{"out":{"phaseinversion":false}}}
{"audio":{"out":{"mixer":{"levels":[-100.0,0.0]}}}}
{"audio":{"out":{"mixer":{"inputs":["../../in1","../../in2"]}}}}
{"audio":{"out":{"eq2":{"enabled":[false,false,false,false,false,false,false,false,false,false]}}}}
{"audio":{"out":{"eq2":{"type":["PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC"]}}}}
{"audio":{"out":{"eq2":{"frequency":[100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000]}}}}
{"audio":{"out":{"eq2":{"q":[0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700]}}}}
{"audio":{"out":{"eq2":{"gain":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq2":{"boost":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq2":{"desc":"post EQ"}}}}
{"audio":{"out":{"eq3":{"enabled":[false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false]}}}}
{"audio":{"out":{"eq3":{"type":["PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC","PARAMETRIC"]}}}}
{"audio":{"out":{"eq3":{"frequency":[100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000,100.000]}}}}
{"audio":{"out":{"eq3":{"q":[0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700,0.700]}}}}
{"audio":{"out":{"eq3":{"gain":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq3":{"boost":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}}}}
{"audio":{"out":{"eq3":{"desc":"calibration EQ"}}}}

## Implementation

This project is implemented using the following components:

1. **ESPHome** as the base framework
2. **TFT_eSPI library** for display control
3. **Rotary encoder** for volume control
4. **Custom C++ components** for:
   - Speaker discovery and communication
   - UI rendering
   - Menu navigation
   - Network communication using IPv6
   - Integration with Home Assistant

### ESPHome Configuration

The main configuration is located in `volctrl/volume_control.yaml` and includes:

- ESP32 hardware settings
- WiFi and network configuration with IPv6 support
- SPI display configuration
- Rotary encoder and button handling
- Custom component for volume control

### Custom Components

The project includes these custom components:

1. **vol_ctrl** - Main component controlling the UI and speaker interactions
2. **network** - Handles IPv6 communication with speakers using Sennheiser Sound Control Protocol
3. **display** - UI rendering and screen layout management
4. **device_state** - State tracking for connected speaker devices
5. **utils** - Helper functions for parsing JSON responses and other utilities

### Home Assistant Integration

The controller automatically integrates with Home Assistant and provides these services:

- `set_volume` - Set volume level (0-120)
- `toggle_mute` - Toggle speaker mute state
- `volume_up` - Increase volume by 1dB
- `volume_down` - Decrease volume by 1dB

### Building and Installation

1. Install ESPHome:
   ```bash
   pip install esphome
   ```

2. Build and flash:
   ```bash
   cd volctrl
   esphome run volume_control.yaml
   ```

3. After flashing, the device will automatically connect to your WiFi network and begin speaker discovery.
