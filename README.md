# ESP32 Temperature Monitor

Ethernet-based multi-bus DS18B20 temperature monitoring system with OLED display, web interface, SNMP agent and traps.

Built for ESP32 + W5500 Ethernet PHY. Supports up to 4 independent 1-Wire buses and multiple sensors per bus.

## Features

- **Ethernet (W5500)** — DHCP with automatic fallback to static IP
- **4× 1-Wire buses** — DS18B20 sensors (up to 5 per bus)
- **OLED SSD1306** (128×32) — live temperatures + status line
- **Web UI** — monitor, settings, sensor names, SNMP & trap configuration, user management
- **SNMP Agent** (v2c) — temperature OIDs + high-temperature / missing-sensor traps
- **Persistent settings** — stored in NVS (Preferences)
- **Watchdog** — 20 s task watchdog
- **Soft rescan** — detects disconnected / reconnected sensors every 60 s

## Hardware

| Component          | Pins / Notes                          |
|--------------------|---------------------------------------|
| ESP32              | —                                     |
| W5500 Ethernet     | CS=5, RST=4, SCK=18, MISO=19, MOSI=23 |
| OLED SSD1306       | I2C SDA=21, SCL=22, addr 0x3C         |
| 1-Wire Bus 1       | GPIO 26                               |
| 1-Wire Bus 2       | GPIO 25                               |
| 1-Wire Bus 3       | GPIO 33                               |
| 1-Wire Bus 4       | GPIO 32                               |

Default static IP (fallback): `192.168.1.177`  
Gateway / DNS: `192.168.1.1`

## Project Structure

```
├── config.h              # Pins, timeouts, defaults, SNMP OIDs
├── globals.h / .cpp      # Shared state, BusConfig, flags
├── main.cpp              # setup() / loop()
├── network_module.*      # Ethernet init, DHCP/static, link recovery
├── sensors.*             # DS18B20 init, non-blocking read, soft rescan
├── display.*             # SSD1306 status + scrolling sensor lines
├── webserver.*           # HTTP server, login, settings pages
├── snmp_module.*         # SNMP agent + high-temp / missing traps
├── settings.*            # NVS load/save, users, trap config
└── README.md
```

## Dependencies (Arduino / PlatformIO)

- `ETH` (ESP32 Arduino core)
- `Adafruit_SSD1306` + `Adafruit_GFX`
- `OneWire` + `DallasTemperature`
- `SNMP_Agent` + `SNMPTrap` (by Martin-Laclaustra or compatible)
- `Preferences` (built-in)

## Configuration

Most compile-time options live in `config.h`:

- Ethernet PHY type / SPI pins / frequency
- Network timeouts (`DHCP_TIMEOUT_MS`, `LINK_DOWN_REINIT_MS` …)
- Default IP / mask / gateway / DNS
- Max sensors per bus, SNMP community & OID prefix
- Trap specific codes (`TRAP_SPECIFIC_HIGH_TEMP = 1`, `TRAP_SPECIFIC_MISSING = 2`)

Runtime settings (DHCP flag, IP, SNMP communities, trap thresholds, sensor names, users) are stored in NVS and editable via the web UI.

## Web Interface

- **Login** required (default: `admin` / `admin`)
- **/** — live temperatures (auto-refresh 15 s) + JSON endpoint `/temp`
- **/settings** — network, SNMP, traps, per-sensor names & thresholds, user management
- Session cookie, 30 min timeout

## SNMP

- Agent listens on UDP/161
- OIDs: `<prefix>.<bus>.<sensor>.0` (temperature ×10 as integer, `-1270` = no data)
- Default prefix: `.1.3.6.1.4.1.99999`
- Traps (v2c) to configured manager IP:
  - High temperature (specific = 1) after configurable hold time
  - Missing sensor (specific = 2)

## Network Behaviour

1. Starts with DHCP (if enabled) or static IP.
2. On link-up without IP → waits `DHCP_TIMEOUT_MS` then applies static.
3. Link down for `LINK_DOWN_REINIT_MS` → full re-init (ETH + web + SNMP).
4. Periodic check every 15 s.

## Building & Flashing

### Arduino IDE
1. Install ESP32 board package.
2. Install required libraries.
3. Select your ESP32 board + correct COM port.
4. Upload.

### PlatformIO (recommended)
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    adafruit/Adafruit SSD1306
    adafruit/Adafruit GFX Library
    paulstoffregen/OneWire
    milesburton/DallasTemperature
    ; SNMP_Agent library
```

## Default Credentials

| Item       | Value          |
|------------|----------------|
| Web login  | `admin` / `admin` |
| SNMP read  | `public`       |
| SNMP write | `private`      |

**Change the password immediately after first boot.**

## Notes

- Temperature conversion is non-blocking (uses `setWaitForConversion(false)`).
- OLED shows IP + sensor count on the status line and scrolls when more than 3 lines of sensors are present.
- SNMP and web server are restarted cleanly on network re-init.
- All settings survive reboot (NVS).

## License

MIT.

---

*ESP32 + W5500 + DS18B20 multi-bus temperature monitor with web UI and SNMP.*
