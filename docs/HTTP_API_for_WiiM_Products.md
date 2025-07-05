# HTTP API for WiiM PRODUCTS

# 1. Introduction

## 1.1. API format

It supports the https based API.

## 1.2. Request format

You can send 'HTTPs Get' request to the device, most of the response is in the JSON format.

Request format is https://x.x.x.x/httpapi.asp?command=********

x.x.x.x is the IP address of the device (Below, we assume the IP of the device is 10.10.10.254）

******* is the actual command.

Commands can contain both integers and strings, denoted as ‘%d’ for integers and ‘%s’ for
strings, respectively.

# 2. Open API list

## 2.1. Get device information

**Params:** getStatusEx

https://10.10.10.254/httpapi.asp?command=getStatusEx

**JSON response** ：

### {


"language": "en_us",

"ssid": "WiiM Mini-8FA2", // Name of the device

"hideSSID": "0",

"firmware": "Linkplay.4.6.425351", // firmware version

"build": "release",

"project": "Muzo_Mini",

"priv_prj": "Muzo_Mini",

"Release": "20220805", // data the firmware is released

"FW_Release_version": "", // Reserved

"group": "0", // 0 means it's a master speaker, 1 means a
slave speaker in a group

"wmrm_version": "4.2", // LinkPlay's MRM SDK version,
version 4.2 or above won't work with any version below
4.

"expired": "0", // Reserved

"internet": "1", // Is it connected to Internet

"uuid": "FF970016A6FE22C1660AB4D8", // The unique
ID of the device

"MAC": "08:E9:F6:8F:8F:A2", // The WiFi MAC address of
the device

"BT_MAC": "08:E9:F6:8F:8F:A3", // The BT MAC address
of the device

"AP_MAC": "0A:E9:F6:8F:8F:A2", // The MAC address of
the AP that the device is connected to

"date": "2022:08:09",


"time": "07:13:16",

"netstat": "2",

"essid": "4C6966656E674F66666963655F3547", // The
AP name in the HEX format

"apcli0": "192.168.4.62", // The IP v4 address of the
device

"eth0": "0.0.0.0",

"ETH_MAC": "00:00:00:00:00:00",

"hardware": "ALLWINNER-R328",

"VersionUpdate": "0", // 0: No new version; 1: new
version.

"NewVer": "0", // If there's new version, the new
firmware version number

"mcu_ver": "0",

"mcu_ver_new": "0",

"update_check_count": "102",

"ra0": "10.10.10.254",

"temp_uuid": "BEDA811FFC2F4D5C",

"cap1": "0x400", // Reserved

"capability": "0x20084000", // Reserved

"languages": "0x1ec",

"prompt_status": "1",

"alexa_ver": "20180604",

"alexa_beta_enable": "1",


"alexa_force_beta_cfg": "1",

"dsp_ver": "0",

"streams_all": "0x1edffbfd", // Reserved

"streams": "0x1edffbfd", // Reserved

"region": "unknown",

"volume_control": "0",

"external": "0x0",

"preset_key": "6", // Number of preset keys

"plm_support": "0x300006", // Reserved

"lbc_support": "0", // Reserved

"WifiChannel": "0",

"RSSI": "-30", // WiFi signal strength

"BSSID": "8c:25:05:1c:41:40", // The MAC address of
connected access point

"wlanFreq": "5805",

"wlanDataRate": "390",

"battery": "0",

"battery_percent": "0",

"securemode": "1",

"ota_interface_ver": "2.0",

"upnp_version": "1005",

"upnp_uuid": "uuid:FF 970016 - A6FE-22C1-660A-
B4D8FF970016",


```
"uart_pass_port": "0",
```
```
"communication_port": "8819",
```
```
"web_firmware_update_hide": "0",
```
```
"tidal_version": "2.0",
```
```
"service_version": "1.0",
```
```
"EQ_support": "Eq10HP_ver_1.0",
```
```
"HiFiSRC_version": "1.0",
```
```
"power_mode": "-1",
```
```
"security": "https\/2.0",
```
```
"security_version": "3.0",
```
```
"security_capabilities": { "ver": "1.0", "aes_ver": "1.0" },
```
```
"public_https_version": "1.0",
```
```
"privacy_mode": "0",
```
```
"DeviceName": "WiiM Mini-8FA2", // The device name
```
```
"GroupName": "WiiM Mini-8FA2" // The group name of
the device is belonged to
```
```
}
```
## 2.2 Network

2.2.1 Get the connection status

**Params:** wlanGetConnectState


https://10.10.10.254/httpapi.asp?command=wlanGetConnectState

Note the return result is not in json.

Return string：

```
Return string Description
PROCESS In progress
PAIRFAIL Wrong password
FAIL Connect fail
OK connected
```
## 2.3 Playback control

2.3.1 Get the playback status

**Params:** getPlayerStatus

https://10.10.10.254/httpapi.asp?command=getPlayerStatus

**JSON response** ：

### {

```
"type":"0",
```
```
"ch":"2",
```

```
"mode":"10",
```
```
"loop":"4",
```
```
"eq":"0",
```
```
"status":"play",
```
```
"curpos":"184919",
```
```
"offset_pts":"184919",
```
```
"totlen":"0",
```
```
"alarmflag":"0",
```
```
"plicount":"0",
```
```
"plicurr":"0",
```
```
"vol":"39",
```
```
"mute":"0"
```
```
}
```
Description：

```
Field Description
type 0: master or standalone device 1: slave
ch 0 stereo， 1 left， 2 right
```
```
mode
```
```
0 None
```
```
1 AirPlay or AirPlay 2
```
```
2 3rd party DLNA
```
```
10 ~ 19 Wiimu playlist
```
```
（ 10 ： default wiimu mode;
```

```
11 : USB disk playlist
```
```
16: TF card play list
```
```
)
```
```
20 ~ 30 Reserved
```
```
31 Spotify Connect
```
```
32 TIDAL Connect
```
```
40 AUX-In
```
```
41 BT
```
```
42 external storage
```
```
43 Optical-In
```
```
50 Mirror
```
```
60 Voice mail
```
```
99 Slave
```
loop

```
Loop mode:
```
```
0: loop all
```
```
1: single loop
```
```
2 ：shuffle loop
```
```
3 ：shuffle, no loop
```
```
4: no shuffle, no loop
```
eq The preset number of the Equalizer

status

```
"stop"
```
```
"play"
```

```
"loading"
```
```
"pause"
curpos Position, in ms
offset_pts
totlen Duration in ms
alarmflag
plicount The total number of tracks in the playlist
plicurr Current track index
vol Current volume
mute Current mute state
```
2.3.2 Play audio URL

**Params:** setPlayerCmd:play:url

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:play:url

Play the URL. URL points to an audio stream address.

Response is always 'OK' now.

2.3.3 Play audio playlist

**Params:** setPlayerCmd:playlist:url:<index>

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:playlist:url:<index>

Play the playlist with the URL (URL points to the m3u or ASX playlist link, index is the start
index).

Response is always 'OK' now.


[http://10.10.10.254/httpapi.asp?command=setPlayerCmd:hex_playlist:url:<index>](http://10.10.10.254/httpapi.asp?command=setPlayerCmd:hex_playlist:url:<index>)

Play the URl (URI is the m3u or ASX playlist link, index is the start index), here, url should be
hexed (please refer to 1.3)

2.3.4 Pause

**Params:** setPlayerCmd:pause

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:pause

2.3.5 Resume

**Params:** setPlayerCmd:resume

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:resume

2.3.6 Toggle pause/play

**Params:** setPlayerCmd:onepause

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:onepause

If the state is paused, resume it; otherwise, pause it.

2.3.7 Previous

**Params:** setPlayerCmd:prev

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:prev

2.3.8 Next

**Params:** setPlayerCmd:next


https://10.10.10.254/httpapi.asp?command=setPlayerCmd:next

2.3.9 Seek

**Params:** setPlayerCmd:seek:position

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:seek:position

Position is from 0 to duration in second.

2.3.10 Stop

**Params:** setPlayerCmd:stop

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:stop

2.3.11 Set volume

**Params:** setPlayerCmd:vol:value

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:vol:value

Value can be 0 to 100.

2.3.12 Mute

**Params:** setPlayerCmd:mute:n

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:mute:n

Mute: n=

Unmute: n=

The slave mute state will be set at the same time when it's in group play.


2.3.13 Loop mode set

**Params:** setPlayerCmd:loopmode:n

https://10.10.10.254/httpapi.asp?command=setPlayerCmd:loopmode:n

n

```
0 Sequence, no loop
1 Single loop
2 Shuffle loop
```
- 1 Sequence loop

## 2.4 EQ

2.4.1 Turn on the EQ

**Params:** EQOn
https://10.10.10.254/httpapi.asp?command=EQOn

**JSON Response:**

{"status":"OK"} or {"status":"Failed"}

2.4.2 Turn off the EQ setting

**Params:** EQOff
https://10.10.10.254/httpapi.asp?command=EQOff

**JSON Response:**
{"status":"OK"} or {"status":"Failed"}

2.4.3 Check if the EQ is ON or OFF


**Params:** EQGetStat

[http://10.10.10.254/httpapi.asp?command=EQGetStat](http://10.10.10.254/httpapi.asp?command=EQGetStat)

**JSON Response:**
{"EQStat":"On"} or {"EQStat":"Off"}

2.4.4 Check all the possible EQ settings

**Params:** EQGetList

[http://10.10.10.254/httpapi.asp?command=EQGetList](http://10.10.10.254/httpapi.asp?command=EQGetList)
**Response:**
["Flat", "Acoustic", "Bass Booster", "Bass Reducer", "Classical", "Dance", "Deep", "Electronic",
"Hip-Hop", "Jazz", "Latin", "Loudness", "Lounge", "Piano", "Pop", "R&B", "Rock", "Small
Speakers", "Spoken Word", "Treble Booster", "Treble Reducer", "Vocal Booster"]

2.4.5 Set the specific EQ with name

**Params:** EQLoad

[http://10.10.10.254/httpapi.asp?command=EQLoad:xxx](http://10.10.10.254/httpapi.asp?command=EQLoad:xxx)

**JSON Response:**
{"status":"OK"} or {"status":"Failed"}
Note: xxx is the one of the name in the list returned by EQGetList, i.e., EQLoad:Flat

## 2.5 Device control

2.5.1 Reboot

**Params:** reboot

[http://10.10.10.254/httpapi.asp?command=reboot](http://10.10.10.254/httpapi.asp?command=reboot)

**JSON Response:**
{"status":"OK"}


2.5.2 Shutdown

**Params:** setShutdown:sec

[http://10.10.10.254/httpapi.asp?command=setShutdown:sec](http://10.10.10.254/httpapi.asp?command=setShutdown:sec)

Shutdown device in sec

sec:

0: shutdown immediately

- 1: cancel the previous shutdown timer

**JSON Response:**

{"status":"OK"} or {"status":"Failed"}

2.5.3 Get the shutdown timer

**Params:** getShutdown

[http://10.10.10.254/httpapi.asp?command=getShutdown](http://10.10.10.254/httpapi.asp?command=getShutdown)

Return the seconds

## 2.6 Alarm clock

2.6.1 Get network time

If the device has no internet access, you need to sync its time with:

[http://10.10.10.254/httpapi.asp?command=timeSync:YYYYMMDDHHMMSS](http://10.10.10.254/httpapi.asp?command=timeSync:YYYYMMDDHHMMSS)

YYYY is year（such as 2015)，MM is month (01~12)，DD is day (01~31)，HH is hour (00~23)，
MM is minute (00~59)，SS is second (00~59)

In UTC


2.6.2 Set Alarm

[http://10.10.10.254/httpapi.asp?command=setAlarmClock:n:trig:op:time[:day][:url]](http://10.10.10.254/httpapi.asp?command=setAlarmClock:n:trig:op:time[:day][:url])

n: 0~2，currently support max 3 alarm

trig: the alarm trigger：

0 cancel the alarm, for example: setAlarmClock:n:

1 once，day should be YYYYMMDD

2 every day

3 every week，day should be 2 bytes (00”~“06”), means from Sunday to Saturday.

4 every week，day should be 2 bytes, the bit 0 to bit 6 means the effect，for example,
“7F” means every day in week, “01” means only Sunday

5 every month，day should be 2 bytes (“01”~“31”)

op: the action

0 shell execute

1 playback or ring

2 stop playback

time: should be HHMMSS, in UTC

day:

if trigger is 0 or 2, no need to set.

if trigger is 1, should be YYYYMMDD ( %04d%02d%02d)

if trigger is 3, day should be 2 bytes (“00”~“06”), means from Sunday to Saturday.

if trigger is 4, day should be 2 bytes, the bit 0 to bit 6 means the effect，for example,
“7F” means every day in week, “01” means only Sunday


if trigger is 5, day should be 2 bytes (“01”~“31”)

url：the shell path or playback url, should less than 256 bytes

2.6.3 Get alarm

[http://10.10.10.254/httpapi.asp?command=getAlarmClock:n](http://10.10.10.254/httpapi.asp?command=getAlarmClock:n)

n: 0~2，currently support max 3 alarm

{"enable":"1",

"trigger":"%d",

"operation":"%d",

"date"::"%02d:%02d:%02d", //if not a “every day” alarm, no this

"week_day":"%d", //if not a “every week” alarm, no this

"day":"%02d", //if not a “every month” alarm, no this

"time":"%02d:02d:%02d",

"path":"%s""}

2.6.4 Stop the current alarm

[http://10.10.10.254/httpapi.asp?command=alarmStop](http://10.10.10.254/httpapi.asp?command=alarmStop)

## 2.7 Source Input Switch

2.7.1 Switch the source input

[http://10.10.10.254/httpapi.asp?command=setPlayerCmd:switchmode:%s](http://10.10.10.254/httpapi.asp?command=setPlayerCmd:switchmode:%s)

the mode can be the text as below:


line-in (it refers to aux-in too)

bluetooth

optical

udisk

wifi

## 2.8 Presets

The WiiM Home App allows users to configure 12 presets for quick access to preferred radio
stations, playlists, mixes, albums, or artists. Each preset is accessible through its assigned
number.

2.8.1 Play preset with preset number

[http://10.10.10.254/httpapi.asp?command=MCUKeyShortClick:%d](http://10.10.10.254/httpapi.asp?command=MCUKeyShortClick:%d)

%d: Range is from 1 to 12

2.8. 2 Get Preset List

[http://10.10.10.254/httpapi.asp?command=getPresetInfo](http://10.10.10.254/httpapi.asp?command=getPresetInfo)

JSON Response

{

"preset_num": 3,

"preset_list": [{

"number": 1,

"name": "BBC Radio Norfolk",


"url": "http:\/\/as-hls-ww-
live.akamaized.net\/pool_904\/live\/ww\/bbc_radio_norfolk\/bbc_radio_norfolk.isml\/bbc_ra
dio_norfolk-audio%3d96000.norewind.m3u8",

"source": "Linkplay Radio",

"picurl": "http:\/\/cdn-
profiles.tunein.com\/s6852\/images\/logoq.jpg?t=638353933090000000"

}, {

"number": 6,

"name": "Radio Paradise",

"url": "http:\/\/stream.radioparadise.com\/flacm",

"source": "RadioParadise",

"picurl": "https:\/\/cdn-
profiles.tunein.com\/s13606\/images\/logod.png?t=637541039930000000"

}, {

"number": 8,

"name": "1. Country Heat",

"url": "unknow",

"source": "Prime",

"picurl": "https:\/\/m.media-amazon.com\/images\/I\/51uR6AJUAQL.jpg"

}]

}

Description

```
Field type Description
```
```
name string Playlist name
```

```
Field type Description
```
```
number int Preset index (Start from 1)
```
```
picurl string Cover picture url
```
```
preset_list json array Preset list information
```
```
preset_num int Total number of presets
```
```
source string Music source
```
```
url string Play url
```
## 2.9 Get Current Track Metadata

2. 9 .1 Command

[http://10.10.10.254/httpapi.asp?command=getMetaInfo](http://10.10.10.254/httpapi.asp?command=getMetaInfo)

2 .9.2 JSON Response

{

"metaData": {

"album": "Country Heat",

"title": "Old Dirt Roads",

"artist": "Owen Riegling",

"albumArtURI ": "https://m.media-amazon.com/images/I/51iU0odzJwL.jpg",

"sampleRate ": "44100",

"bitDepth": "16"

}

}


## 2. 10 Audio Output Control

2. 10 .1 Get audio output mode

https://10.10.10.254/httpapi.asp?command=getNewAudioOutputHardwareMode

**JSON Response** ：

```
{"hardware":"2","source":"0","audiocast":"0"}
```
Description：

```
Field Description
```
```
hardware Hardware Interface output mode:
```
```
1: AUDIO_OUTPUT_SPDIF_MODE
```
```
2: AUDIO_OUTPUT_AUX_MODE
```
```
3: AUDIO_OUTPUT_COAX_MODE
```
```
source BT source output mode:
```
```
0: disable
```
```
1: active
```
```
audiocast Audio cast output mode:
```
```
0: disable
```
```
1: active
```
2. 10. 2 Set audio output mode

https://10.10.10.254/httpapi.asp?command=setAudioOutputHardwareMode:%d

Description：


```
Field Description
```
```
outputmode Hardware Interface output mode:
```
```
1: AUDIO_OUTPUT_SPDIF_MODE
```
```
2: AUDIO_OUTPUT_AUX_MODE
```
```
3: AUDIO_OUTPUT_COAX_MODE
```
Response is 'OK'


