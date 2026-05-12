# Wireless Sign
ESP32 Matrix Display (WiFi-Controlled)

> [!NOTE]  
> New Updates: Added OTA firmware updates.

A WiFi-connected LED matrix display built using ESP32 + MAX7219, controllable through a Flask-based web server.
It supports real-time messages, clock display, OTA firmware updates, remote configuration, sound notifications, and multi-device management.

⸻

## Features

* 📡 WiFi setup portal (captive portal)
* 💾 Multiple saved WiFi networks (automatic reconnect)
* 🧠 Persistent configuration using NVS
* 🌐 Remote configuration via HTTP API
* 💬 Display messages (scroll/static)
* 😀 Emoji support
* 🕒 Live clock (NTP synchronized)
* 🌍 Timezone + daylight saving support
* 🔆 Adjustable brightness
* 🔄 Display flip mode (upside down)
* 🔊 Piezo buzzer notifications and tones
* ⚡ Heartbeat system (device monitoring)
* 📟 Multi-device support via unique device ID
* 🎞️ Custom display animations
* 🚀 OTA firmware updates
* 🖥️ Web dashboard for fleet management

⸻

## Hardware

Tested Boards

* ESP32-C3
* ESP32
* Other ESP32 variants should also work

Display

* MAX7219 LED matrix
* 4 chained modules (32x8 display)

Buzzer

* Passive piezo buzzer
* Connected to GPIO 10

SPI Connections

MAX7219	ESP32
DIN	GPIO 6
CLK	GPIO 4
CS	GPIO 7

⸻

## Software Architecture

The project is split into modular components:

File	Purpose
main.c	Application logic, tasks, shared state
wifi_manager.c	WiFi connection management
portal.c	Captive portal and setup server
time_manager.c	NTP synchronization and time formatting
max7219.c	LED matrix driver
buzzer.c	Piezo buzzer driver

⸻

## Boot Flow

Boot
│
├─ Load saved WiFi credentials from NVS
│    ├─ Success → try all saved WiFi networks
│    │             ├─ Connected → start normal mode
│    │             └─ Failed → start AP mode
│    └─ No credentials → start AP mode
│
└─ AP Mode (Setup)
     ├─ Start captive portal
     ├─ Show WiFi animation on matrix
     ├─ User connects to AP
     ├─ User submits WiFi credentials
     └─ ESP32 restarts

⸻

## Main Features

### Message Display

Supports:

* Scrolling messages
* Static messages
* Adjustable scroll speed
* Configurable display duration
* Emoji rendering

Example

{
  "message": "Hello :heart:",
  "has_message": true,
  "message_mode": "scroll",
  "message_seconds": 10,
  "scroll_speed_ms": 60
}

⸻

### Clock Display

Features:

* NTP synchronized time
* 12h or 24h format
* UTC offset support
* Daylight saving support
* AM/PM indicators
* WiFi disconnected indicator

⸻

### Display Flip

The display can be flipped upside down remotely.

{
  "flip_display": true
}

Useful for:

* Ceiling-mounted displays
* Reverse installations
* Different orientations

⸻

### Sound System

The device supports buzzer tones and notifications.

Built-in Tones

* success
* error
* warning
* boot
* wifi
* message
* notification

Example

{
  "tone_command": "success",
  "tone_id": 12
}

⸻

### OTA Firmware Updates

The system supports OTA firmware updates through the web dashboard.

OTA Flow

1. Upload .bin firmware to server
2. Select target device(s)
3. Queue OTA update
4. Device downloads firmware
5. Device reboots automatically

During OTA:

* Matrix displays OTA animation
* Display task pauses
* Device reboots automatically after success

Example OTA JSON

{
  "ota_available": true,
  "ota_version": "1.0.1",
  "ota_url": "http://server/static/firmware/matrix_ap.bin"
}

⸻

### Multi-WiFi Support

The ESP32 can store multiple WiFi credentials.

Features:

* Automatic reconnect
* Retry saved networks
* Roaming between known WiFi networks
* Captive portal fallback if all fail

⸻

### Server API

Fetch Device Configuration

GET /api/device/<device_id>/config

Example Response

{
  "message": "Hello World",
  "has_message": true,
  "brightness": 8,
  "show_clock": true,
  "message_id": 12,
  "message_seconds": 10,
  "utc_offset_hours": 1,
  "time_format_24h": true,
  "scroll_speed_ms": 60,
  "flip_display": false,
  "sound_enabled": true,
  "tone_command": "success",
  "tone_id": 20,
  "ota_available": false,
  "ota_url": "",
  "ota_version": ""
}

⸻

### Message ACK

POST /api/device/<device_id>/ack
{
  "message_id": 12
}

⸻

### Tone ACK

POST /api/device/<device_id>/tone_ack
{
  "tone_id": 20
}

⸻

### Heartbeat

POST /alive
{
  "device_id": "matrix-XXXX",
  "free_mem": 123456,
  "fw_version": "1.0.0"
}

⸻

### FreeRTOS Tasks

Task	Purpose
clock_task	Updates current time
fetch_task	Polls server configuration
display_task	Controls matrix display
heartbeat_task	Sends device heartbeat
ota_animation_task	OTA animation while updating
wifi_animation_task	WiFi setup animation

⸻

### Web Dashboard

The Flask dashboard supports:

* Multi-device management
* Live device status
* Message sending
* Brightness control
* Clock configuration
* Timezone selection
* Display flip
* Sound controls
* OTA firmware management
* Firmware upload page

⸻

### Device Identification

Each device generates a unique ID from its MAC address:

matrix-XXXXXXXXXXXX

Used for:

* API communication
* Database mapping
* OTA targeting
* Multi-user management

⸻

## Setup

1. Build Firmware

Using ESP-IDF:

idf.py build

⸻

2. Flash Device

idf.py flash monitor

⸻

3. First Boot

Connect to:

Matrix_Config_AP

Password:

12345678

⸻

4. Open Setup Portal

http://192.168.4.1

⸻

5. Enter WiFi Credentials

The ESP32 will:

* Save credentials
* Restart automatically
* Connect to WiFi
* Sync time
* Start polling server

⸻

## OTA Partition Table

OTA requires a partition table with at least:

* factory app
* ota_0
* ota_1

Example:
```csv
# Name, Type, SubType, Offset, Size
nvs,data,nvs,0x9000,0x6000
otadata,data,ota,0xf000,0x2000
phy_init,data,phy,0x11000,0x1000
factory,app,factory,0x20000,1M
ota_0,app,ota_0,,1M
ota_1,app,ota_1,,1M
```

⸻

## Future Improvements

* MQTT support instead of polling
* Better emoji rendering
* Mobile app
* HTTPS OTA updates
* Display zones
* Audio playback
* Power-saving modes
* Sensor integrations
* WebSocket live updates

⸻

License

Personal/open-source project for learning embedded systems, networking, and IoT device management.