# CLAUDE.md

SensorNetPro is ESP32 firmware that reads DS18B20 temperature sensors for rack monitoring. It reports them over SNMP and has a web UI with digest authentication. User-facing docs: `README.md`, `BUILDING.md`, `electronics/README.md`. Bug and change history: `OUTSTANDING_ISSUES.md`. All of its items are fixed, and its quoted "Fixed:" notes explain why the code is the way it is.

## Build and check

- `pio` is at `~/.platformio/penv/bin/pio`, and may not be on `PATH`.
- Build both boards after every firmware change: `pio run -e esp32poe -e wt32-eth01`. Treat any new warning from `src/` as a failure. The `SNMP_Agent` `BERDecode.cpp` warning comes from the library and is expected.
- Build the web UI image with `pio run -e esp32poe -t buildfs`. It is built from `.pio/build/<env>/data_gz`, a gzipped copy of `data/` made by `scripts/gzip_data.py`.
- Never run `pkill -f "pio run"`. It also matches, and kills, the shell running the command.
- Nothing has been tested on hardware. Say so when reporting results, and don't claim on-device behaviour.
- There is no test suite in the repo. Web UI changes were checked with jsdom (Node) driving `data/index.html` against a mock `/json-api` and `/update` server. Validators were checked by compiling them with g++ on the host. Rebuild a harness like that in the scratchpad for UI or logic changes, and check the new tests fail against the old code.

## Platform and dependencies

- The platform is pinned to `espressif32 @ 8.0.0`, which ships Arduino-ESP32 3.3.x. Platform 7.x still ships Arduino 2.x.
- Every `lib_deps` entry is pinned to an exact version. Use `esp32async/ESPAsyncWebServer` and `esp32async/AsyncTCP`, not the deprecated me-no-dev forks. ArduinoJson is v7, so use `JsonDocument`.
- `lib_ldf_mode = chain`. Don't add `lib_extra_dirs` pointing at system paths: that is what broke the original build.
- Each board has its own environment, with its pins set as build flags in `platformio.ini`. The WT32-ETH01 pin map is provisional. Ethernet PHY settings come from the board variant.
- `partitions.csv` has two 1.75 MB OTA slots, a 448 KB SPIFFS partition, and NVS at `0x9000`. NVS must keep that offset, or saved settings are lost. The firmware uses about 86% of a slot, so check the flash usage when adding features.

## Firmware architecture (`src/main.cpp` and friends)

- **Shared state:** `loop()`, the async web task and the WiFi event task share state. That includes the SNMP agent, preferences, the `ch_*` config buffers and the sensor arrays. Wrap access in `StateLock` (an RAII wrapper on `stateMutex`). Never hold it across anything that blocks for long.
- **API gating:** `/json-api` dispatches on `webAction`. While `adminPasswordIsDefault()` is true, everything except `config` and `save-admin` returns 403, and the same applies to `/update` and ArduinoOTA. The API validates every input itself (`isValidHostname` and the other `isValid*` helpers, and `validateStaticIp`). Don't rely on the UI's checks.
- **Reboots:** handlers never restart the device directly. They set `rebootRequestedAt`, and `loop()` restarts a second later, after the response has gone out.
- **Sensors:** every sensor type implements `SensorType` (`include/sensor.h`). `Ds18b20Sensors` is the only one. Readings are non-blocking: `requestSensorData()` starts them, and `loop()` collects them with `updateSensorData()` once the conversion time has passed. Values are ints scaled by 100. A DS18B20 reading of -127 or 85 °C counts as a read error. Sensors are only discovered at boot.
- **SNMP:**
  - Each sensor type has its own table at `1.3.6.1.4.1.53165.1.<type>.1.<column>.<index>`, where thermal is type 1.
  - The columns are 1 index, 2 name, 3 serial, 4 value and 5 status (1 = OK, 2 = error).
  - Indexes are stored per serial in the `SensorSNMP` NVS namespace.
  - Keep `mibs/SENSORNETPRO-MIB.txt` in sync with any OID change. It was validated with pysmi (`mibdump`).
  - `updateSensorName()` deletes the old handler with a `static_cast` inside a `-Wdelete-non-virtual-dtor` pragma, because the library's destructor isn't virtual.
- **Networking:**
  - Static IP applies to Ethernet only. Wi-Fi station mode always uses DHCP.
  - If station mode can't connect and Ethernet is down, the device starts an open fallback access point, `SensorNetPro-XXXX`, while it keeps retrying.
- **Button:** holding it for 5 s at power-on is a factory reset. Holding it for 5 s while running reboots, without blocking. A software restart (`ESP_RST_SW`) never triggers the factory reset.
- **Updates:**
  - `POST /update?target=firmware|filesystem` streams the upload into `Update`.
  - A recovery upload page built into the firmware is served at `/update`. `/` also falls back to it when the web UI files are missing.
  - ArduinoOTA uses the admin password. Its password is only changed from `loop()`, which owns ArduinoOTA.
- **Logging:** log secrets only through `maskSecret()`.

## Web UI (`data/`)

- The UI is jQuery, Bootstrap 4 and DataTables, with `"use strict"`. Declare every variable.
- **Escaping:** insert device-supplied text with `escapeHtml()` or `.text()`. Read serials with `.attr("data-serial")`, not `.data()`, which turns all-digit serials into numbers.
- **Errors:** show them in the `#jsError` banner, never with `alert()`. DataTables' `errMode` is `"none"` for this reason. Status polling must survive errors.
- **Forced password change:** `lockToPasswordChange()` hides everything except the admin password form. New admin-only sections must be hidden there as well.
- **Validation:** the UI's rules must match the firmware's validators.

## Working conventions with this user

- Commit directly to `master`, one commit per issue or item, with an explanatory message.
- When fixing an item tracked in `OUTSTANDING_ISSUES.md`, mark it ✅ and add a `> **Fixed:**` note in the same commit.
- Don't push unless asked. Pushing needs the user's credentials, so they run it themselves with `! git push origin master`.
- Ask before making design decisions with real trade-offs, such as behaviour changes, compatibility, or scope.
- When a script edits files, assert that each replacement matched. A silent miss once dropped a README heading.
- Copyright headers read `Copyright © 2023-2026 Michael Junek` and are followed by the GPLv3 notice. New source files get the same header.
