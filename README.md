# Wireless Sign
ESP32 Matrix Display (WiFi-Controlled)

A WiFi-connected LED matrix display built using ESP32 + MAX7219, controllable through a web server.
It supports real-time messages, clock display, and remote configuration.


## Features

* 📡 WiFi setup portal (captive portal)
* 🧠 Persistent credentials using NVS
* 🌐 Remote configuration via HTTP API
* 💬 Display messages (scroll/static)
* 🕒 Live clock (NTP synchronized)
* 🔆 Adjustable brightness
* ⚡ Heartbeat system (device monitoring)
* 📟 Multi-device support via unique device ID
* 🎞️ Custom animations (WiFi setup mode)

⸻

## Hardware

* ESP32 (tested on ESP32-C3 / ESP32 variants)
* MAX7219 LED matrix (4x chained = 32x8)
* SPI connections:

| MAX7219 | ESP32 |
|--------|--------|
|DIN | GPIO 6|
|CLK |	GPIO 4|
|CS	| GPIO 7|

⸻

## Software Architecture

The project is split into modular components:

* **main.c**              → Application logic (tasks, state)
* **wifi_manager.c**      → WiFi connection + events
* **portal.c**            → Captive portal + config server
* **time_manager.c**      → NTP + time utilities
* **max7219.c**           → Display driver

⸻

## How It Works

Boot Flow

```
Boot
│
├─ Load WiFi credentials from NVS
│    ├─ Success → connect to WiFi
│    │             ├─ OK → start tasks
│    │             └─ Fail → start AP mode
│    └─ No creds → start AP mode
│
└─ AP Mode (Setup)
     ├─ Start captive portal
     ├─ Show animation on display
     ├─ User connects and submits WiFi
     └─ ESP32 restarts
```
⸻

## API (Server Side)

Fetch configuration

```
GET /api/device/<device_id>/config
```

Example response:

```json
{
  "message": "Hello World",
  "has_message": true,
  "brightness": 8,
  "show_clock": true,
  "message_id": 12,
  "message_seconds": 10,
  "utc_offset_hours": 1,
  "time_format_24h": true,
  "scroll_speed_ms": 60
}
```

⸻

Acknowledge message

```json
POST /api/device/<device_id>/ack
{
  "message_id": 12
}
```

⸻

Heartbeat

```json
POST /alive
{
  "device_id": "matrix-XXXX",
  "ip": "...",
  "free_mem": 123456
}
```

⸻

## FreeRTOS Tasks

|Task	| Purpose|
|--------|--------|
|clock_task|	Updates time every second|
|fetch_task	|Polls server config|
|display_task	| Controls LED output|
|heartbeat_task	Sends| device status|
|wifi_animation_task|	Runs during setup mode|

⸻

## Display Modes

1. Message (priority)

    * Scroll mode
    * Static mode
    * Controlled by:
        * message_mode
        * message_seconds
        * scroll_speed_ms

2. Clock

    * Displayed when no message
    * Format:
        * 24h or 12h (future support)
        * timezone offset support

⸻

## Device Identification

Each device generates a unique ID using its MAC address:

matrix-XXXXXXXXXXXX

Used for:

* API communication
* Database mapping
* Multi-user control

⸻

## Setup

1. Flash firmware

    Using ESP-IDF:

    idf.py build
    idf.py flash monitor

2. First boot

3. Connect to:

    Matrix_Config_AP
    password: 12345678

4. Open:
    http://192.168.4.1

5. Enter WiFi credentials

6. Device connects automatically

    * Syncs time
    * Starts polling server
    * Begins display updates


## Future Improvements

* OTA firmware updates
* Better font rendering
* Emoji/icon support
* Mobile-friendly dashboard
* MQTT instead of polling
* Power-saving modes
* Multi-zone display support
