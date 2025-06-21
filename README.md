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

It uses Senheiser Sound Control Protocol (SSP) to control the volume of the speakers and reading and setting parameters.

## Hardware

- ESP 8266 Wemos D1 mini
- Display GMT130 240x240px, Driver IC ST7789, SPI interface, it needs HW SPI controller, needs init with parameter SPI_MODE3 (it does not have CS pin)
- Rotary encoder with push button KY-040

Suggest what esphome packages to use to control the aboe hardware. Suggest yaml configuration for the ESP8266 and provide custom componenets where packages are not available.

## Wiring

Display pins:
- GND -> GND
- VCC -> 3.3V
- SCL -> GPIO 14 (D5)  CLOCK
- SDA -> GPIO 13 (D7)  MOSI
- RES -> GPIO 12 (D6)  RESET, need for initialization
- DC  -> GPIO 15 (D8)  Display data/command selection
- BLK -> GPIO  2 (D4)  backlight (PWM)

Rotary encoder pins:
- GND -> GND
- VCC -> 3.3V
- CLK -> GPIO 5 (D1)
- DT -> GPIO 4 (D2)
- SW -> GPIO 0 (D3)

## Software

Based on ESP Home project. ESPhome is used to compile a firmware for ESP8266 and to control the display and rotary encoder. When ESP is powered on, it will connect to the WiFi network. It will connect to the speakers over IPv6 and control them. It will also natively integrate with Home Assistant and export services for volume control and speaker settings.

This project is based on the logic coded in source code of the KH Tool, see directory ```khtool```. The source code is not used directly, it is only for reference.
Refer to the ```Resources``` below for further documentation, however the khtool project shows practical implementation of the protocol, include discovery, volume control, speaker settings and parametric equalizer settings.

> Note: Commands send to the speakers are always sent to all speakers.

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

Rotary encoder will increase/decrease volume by 1 db step on each click. It will also toggle mute on push button click.

Diplay will show:
 - current volume level (big numbers) - this value must be retrieved from the speaker over network
 - mute state by showing a forbidden sign over the volume level
 - date and time (from NTP server) on top left of the display
 - standby auto_standby_time "/" standby auto_standby_time on top right of the display
 - indication of speaker connection state (connected/disconnected) on the bottom of the display. Red or green dot for each speaker known. This is useful because it takes some time to connect to the speaker after power on, and it is not possible to control volume before connection is established.

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
  7.3. Display timeout

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
```
