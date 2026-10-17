# ESP32 Temperature Monitor

Ethernet-based multi-bus **DS18B20** temperature monitoring system with OLED display, web interface, SNMP agent and traps.

Built for **ESP32 + W5500** Ethernet PHY. Supports up to **4 independent 1-Wire buses** and multiple sensors per bus.

---

## Features

- **Ethernet (W5500)** — DHCP with automatic fallback to static IP
- **4× 1-Wire buses** — DS18B20 sensors (up to 5 per bus)
- **OLED SSD1306** (128×32) — live temperatures + status line with IP and sensor count
- **Web UI** — monitor, settings, sensor names, SNMP & trap configuration, user management
- **SNMP Agent** (v2c) — temperature OIDs + high-temperature / missing-sensor traps
- **Persistent settings** — stored in NVS (Preferences)
- **Watchdog** — 20 s task watchdog
- **Soft rescan** — detects disconnected / reconnected sensors every 60 s
- **Ready-to-use** MIB file and Zabbix template included

---

## Hardware

| Component          | Pins / Notes                          |
|--------------------|---------------------------------------|
| ESP32              | —                                     |
| W5500 Ethernet     | CS=5, RST=4, SCK=18, MISO=19, MOSI=23 |
| OLED SSD1306       | I2C SDA=21, SCL=22, addr `0x3C`       |
| 1-Wire Bus 1       | GPIO 26                               |
| 1-Wire Bus 2       | GPIO 25                               |
| 1-Wire Bus 3       | GPIO 33                               |
| 1-Wire Bus 4       | GPIO 32                               |

**Default static IP** (fallback): `192.168.1.177`  
**Gateway / DNS**: `192.168.1.1`

---

## Project Structure

```
├── src/                          # Firmware source
│   ├── config.h                  # Pins, timeouts, defaults, SNMP OIDs
│   ├── globals.h / .cpp          # Shared state, BusConfig, flags
│   ├── main.cpp                  # setup() / loop()
│   ├── network_module.*          # Ethernet init, DHCP/static, link recovery
│   ├── sensors.*                 # DS18B20 init, non-blocking read, soft rescan
│   ├── display.*                 # SSD1306 status + scrolling sensor lines
│   ├── webserver.*               # HTTP server, login, settings pages
│   ├── snmp_module.*             # SNMP agent + high-temp / missing traps
│   └── settings.*                # NVS load/save, users, trap config
├── MIB/
│   └── TEMP-MONITOR-MIB.mib      # SMIv2 MIB for SNMP managers
├── Zabbix/
│   └── zabbix_template_temp_monitor.yaml  # Zabbix 6.0+ template
├── platformio.ini
└── README.md
```

---

## Building & Flashing

### PlatformIO (recommended)

```bash
git clone https://github.com/fvvalfa/tempmon.git
cd tempmon
pio run -t upload
pio device monitor
```

`platformio.ini` already contains all required libraries:

```ini
[env:stable]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps = 
    adafruit/Adafruit SSD1306@^2.5.17
    adafruit/Adafruit GFX Library@^1.12.6
    paulstoffregen/OneWire@^2.3.8
    milesburton/DallasTemperature@^4.0.6
    0neblock/SNMP_Agent@^2.1.0
```

### Arduino IDE

1. Install ESP32 board package.
2. Install libraries: Adafruit SSD1306, Adafruit GFX, OneWire, DallasTemperature, SNMP_Agent.
3. Copy files from `src/` into a sketch folder.
4. Select your board and upload.

---

## Configuration

### Compile-time (`src/config.h`)

- Ethernet PHY type / SPI pins / frequency
- Network timeouts (`DHCP_TIMEOUT_MS`, `LINK_DOWN_REINIT_MS` …)
- Default IP / mask / gateway / DNS
- Max sensors per bus, SNMP community & OID prefix
- Trap specific codes (`TRAP_SPECIFIC_HIGH_TEMP = 1`, `TRAP_SPECIFIC_MISSING = 2`)

### Runtime (Web UI → Settings)

DHCP flag, static IP, SNMP communities, trap thresholds, sensor names, users — all stored in NVS and survive reboot.

---

## Web Interface

| Path         | Description                                      |
|--------------|--------------------------------------------------|
| `/login`     | Login page (default: `admin` / `admin`)          |
| `/`          | Live temperatures (auto-refresh 15 s)            |
| `/temp`      | JSON endpoint (`Access-Control-Allow-Origin: *`) |
| `/settings`  | Network, SNMP, traps, sensor names, users        |
| `/logout`    | Clear session                                    |

Session cookie, 30 min timeout.

---

## SNMP

- **Agent**: UDP/161
- **OID layout**: `.1.3.6.1.4.1.99999.<bus>.<sensor>.0`  
  (bus = 1…4, sensor = 1…5)
- **Value**: temperature × 10 as `Integer32`  
  Example: `235` → 23.5 °C  
  Special value: `-1270` = no data / sensor missing
- **Traps** (SNMPv2c) to configured manager IP:
  - High temperature — specific = **1** (after configurable hold time)
  - Missing sensor — specific = **2**

### MIB

File: [`MIB/TEMP-MONITOR-MIB.mib`](MIB/TEMP-MONITOR-MIB.mib)

Load it into any SNMP manager (snmpwalk, LibreNMS, Observium, PRTG, etc.).

Example:
```bash
snmpwalk -v2c -c public 192.168.1.177 1.3.6.1.4.1.99999
```

### Zabbix

Ready-made template for **Zabbix 6.0+**:  
[`Zabbix/zabbix_template_temp_monitor.yaml`](Zabbix/zabbix_template_temp_monitor.yaml)

- 20 items (4 buses × 5 sensors)
- Preprocessing: ×0.1 + filter for missing sensors (`-1270`)
- Units: °C
- History 90 d, trends 365 d

Import the YAML in **Configuration → Templates → Import**.

---

## Network Behaviour

1. Starts with DHCP (if enabled) or static IP.
2. On link-up without IP → waits `DHCP_TIMEOUT_MS` then applies static.
3. Link down longer than `LINK_DOWN_REINIT_MS` → full re-init (ETH + web + SNMP).
4. Periodic health check every 15 s.

---

## Default Credentials

| Item        | Value            |
|-------------|------------------|
| Web login   | `admin` / `admin`|
| SNMP read   | `public`         |
| SNMP write  | `private`        |

**Change the password immediately after first boot.**

---

## Notes

- Temperature conversion is non-blocking (`setWaitForConversion(false)`).
- OLED shows IP + sensor count on the status line and scrolls when more than 3 lines of sensors are present.
- SNMP agent and web server are restarted cleanly on network re-init.
- All settings survive reboot (NVS).

---

## License

MIT

---

*ESP32 + W5500 + DS18B20 multi-bus temperature monitor with web UI, SNMP and Zabbix template.*
