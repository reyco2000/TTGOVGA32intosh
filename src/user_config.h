#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// MQTT Configuration
#define MQTT_BROKER_URL      "mqtt://192.168.1.100:1883"
#define MQTT_USERNAME        "YOUR_MQTT_USERNAME"
#define MQTT_PASSWORD        "YOUR_MQTT_PASSWORD"
#define MQTT_TOPIC_BASE      "home/weather"
#define MQTT_TOPIC_BASE_DEVICE "home/cyd-mac"
#define MQTT_TOPIC_SUBSCRIBE "home/weather/#"
#define MQTT_QOS             1

// HomeAssistant MQTT Discovery Configuration
#define HA_DISCOVERY_PREFIX  "homeassistant"
#define HA_DEVICE_ID         "cydintosh"

// Weather display units (Mac Roman encoding, use hex escapes for special chars)
// ° = \xA1, use string concatenation: "\xA1" "C" means "°C"
// Examples: "\xA1" "C" = °C, "\xA1" "F" = °F, "K" = Kelvin
// NOTE: These are display labels; values are not converted.
#define WEATHER_TEMP_UNIT \
    "\xA1"                \
    "C"
#define WEATHER_WIND_UNIT "km/h"

// Macintosh Emulation Settings
#ifndef UMAC_MEMSIZE
#define UMAC_MEMSIZE 1024
#endif

#ifndef DISP_WIDTH
#define DISP_WIDTH 512
#endif

#ifndef DISP_HEIGHT
#define DISP_HEIGHT 342
#endif

#ifndef ENABLE_DASM
#define ENABLE_DASM 0
#endif

// Multiplier applied to raw PS/2 mouse deltas before they reach the emulator
#ifndef MOUSE_SENSITIVITY
#define MOUSE_SENSITIVITY 2.0
#endif

// Max queued mouse-quadrature steps per axis. Measured drain rate on this
// hardware is only ~75-150 px/sec/axis (umac_task can't sustain real-time
// 68k emulation), so a big backlog from a fast swipe visibly "traces" to
// its destination for a long time. Lower = snappier catch-up but less
// reach per swipe.
#ifndef MOUSE_MAX_PENDING_PIX
#define MOUSE_MAX_PENDING_PIX 24
#endif

#endif
