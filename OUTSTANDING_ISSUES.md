# SensorNetPro: Outstanding Bugs and Changes

Review of the codebase as of commit `a8375cf` (2026-10-24). The firmware has not been compiled or run on hardware for this review. Items marked **(verify)** depend on library behaviour that should be checked against the installed library version.

## Project snapshot

- **Firmware**: PlatformIO / Arduino on the Olimex ESP32-POE (`src/main.cpp`, `src/prefs.cpp`, `src/snmp.cpp`). It reads DS18B20 sensors over 1-Wire and exposes them through an SNMP table and an authenticated web UI.
- **Web UI**: jQuery/Bootstrap/DataTables single page served from SPIFFS (`data/`). It talks to one JSON endpoint, `POST /json-api`, using a `webAction` field.
- **Settings**: NVS Preferences. The `SensorApp` namespace holds the config. The `SensorSNMP` namespace holds serial→SNMP-index mappings (`S_<n>`) and friendly names (keyed by serial).
- **Hardware**: The KiCad project has been created but is empty (the schematic has no symbols).

---

## Critical

### 1. ✅ Fresh devices never load the factory defaults
> **Fixed:** `getConfigured()` now defaults to `false`, so a blank NVS triggers `resetToDefaults()` on first boot. Devices that have saved settings are not affected, because every save writes `confdone = true`.

`getConfigured()` returns `true` when the key is missing (`src/prefs.cpp:246`). As a result, `loadPreferences()` never calls `resetToDefaults()` on first boot (`src/main.cpp:90`). Every other value then falls back to the Preferences zero defaults:
- `dhcp` = false, so the static IP is set to `0.0.0.0/0.0.0.0`.
- `ap` = false, so the device starts in **station** mode and tries to join SSID `SensorNetPro` with an empty key.
- If Ethernet is not up within 60 s, it reboots (`src/main.cpp:792`). This repeats forever.

The only way out is the hardware reset button. **Fix:** change the default to `false`.

### 2. Buffer overflow when more than 10 sensors are on the bus
`countSensors = sensors.getDeviceCount()` (`src/main.cpp:806`) has no upper limit. These arrays are all sized at `__MAX_SENSOR_COUNT` or 10, and are written past their end when more sensors are present:
- `sensorAddress[]`, `sensorName[]`, `snmpTemperature[]`
- `nameOid[]` and `snmpIndex[]` in `snmp.cpp`
- `temperaturec[10]` in `updateSensorData()`

This corrupts memory or crashes the device. **Fix:** clamp `countSensors` to `__MAX_SENSOR_COUNT`.

### 3. Readings can be assigned to the wrong sensor
`updateSensorData()` reads each sensor by bus position with `getTempCByIndex(i)` (`src/main.cpp:215`). The SNMP OIDs and names, however, are tied to the serials found at boot. If a sensor drops off the bus or fails to respond, the remaining sensors' readings move up one position. They are then reported against the wrong rack position. Failed reads are also published without any check:
- `-127 °C` (`DEVICE_DISCONNECTED_C`) is published as `-12700`.
- `85 °C` (the power-on value) is published as a real reading.

**Fix:** read with `getTempC(sensorsUnique[i])`. Detect failed reads and report them in a defined way, for example by not answering, using a sentinel value, or adding a status column.

### 4. `printf("%s")` is given Arduino `String` objects (undefined behaviour)
A `String` object is passed where a `char*` is expected. This prints garbage and can crash the device. Affected code:
- Every `write*()` setter that takes a `String`: `src/prefs.cpp:62-108`
- `Serial.printf("Handling JSON action %s\n", webAction)`: `src/main.cpp:587`
- `src/main.cpp:779-780`: three `String`s are passed this way. There are also four arguments for three `%s` specifiers, so the IP is printed twice and the gateway is dropped.

**Fix:** use `.c_str()`.

### 5. Web handlers race with the main loop
ESPAsyncWebServer callbacks run on the `async_tcp` task, while `loop()` runs on the Arduino task. The API handlers change shared state with no locking:
- They call `snmp.removeHandler()`, `addReadOnlyStaticStringHandler()` and `sortHandlers()` (`apiSaveThermalSensorConfiguration` and `apiSaveSnmpConfiguration`). At the same time, `snmp.loop()` may be walking that same handler list.
- They call `prefs.end()`/`begin()` inside `commitToPrefs()`.
- They rewrite the `ch_*` buffers that the SNMP agent and the auth check read.

This can cause intermittent crashes, especially when a sensor is renamed while it is being polled. **Fix:** have the handler set a "pending change" flag or queue an item, and apply the change in `loop()`. The other option is to guard the SNMP and prefs access with a mutex.

---

## High

### 6. Pages are served even when authentication fails
In the `/`, `/reboot.html` and `/reboot.js` routes (`src/main.cpp:660-690`), `requestAuthentication()` is not followed by `return`. The code goes straight on to `request->send(SPIFFS, …)`, so each request gets two responses. The page may end up being served without authentication. The API data itself is still protected, because `jsonApiHandler` returns correctly.

### 7. The device is insecure by default
The device starts as an **open** Wi-Fi AP (empty WPA key), with web login `admin`/`admin` over plain HTTP and SNMP community `public`. Anyone within Wi-Fi range can reconfigure it. Suggested fixes:
- Generate a WPA key from the MAC address or efuse, or print one on the label.
- Force a password change on first login.
- Turn the AP off once Ethernet is configured.

### 8. Secrets are written to the serial console
These are printed in clear text:
- WPA key: `getWpaKey`, `writeWpaKey`, `src/main.cpp:751,759`
- Admin password: `getHttpPassword`, `writeHttpPassword`
- SNMP community string

Mask these values or remove the logging.

### 9. A Wi-Fi station failure puts the device into a reboot loop
With Wi-Fi in station mode and Ethernet not up within 60 s, the device reboots every minute (`src/main.cpp:787-799`). It can never be reached to fix the configuration. **Fix:** fall back to AP mode, or keep running without Wi-Fi.

### 10. Static IP is applied to both interfaces
When DHCP is off, `WiFi.config()` and `ETH.config()` are given the same IP, mask and gateway (`src/main.cpp:776-778`). This causes an address conflict and broken routing whenever both interfaces are up.

AP mode has a related problem. `WiFi.config()` sets the station interface, not the soft-AP, so the configured address is ignored in AP mode. That would need `softAPConfig()`. Network settings probably need to be set per interface, for example Ethernet static/DHCP plus AP on a fixed `192.168.4.1`.

---

## Medium

### Firmware

#### 11. No server-side input validation
- **Network settings**: `IPAddress::fromString()` failures are ignored, so `0.0.0.0` is saved. Hostname characters and length are not checked.
- **SNMP and admin settings**: string lengths are not checked. Values are silently truncated to 30 or 13 characters by `toCharArray`.
- **Sensor names**: not validated at all. This also enables the XSS issue in item 20.

#### 12. Validation limits differ between the UI and the firmware

| Field | UI | Firmware |
|---|---|---|
| Admin password min length | 6 | 8. A 6–7 character password passes the UI and then fails with a generic "error saving" message. |
| Admin username | regex `^[a-z]{3,10}` applied to the lowercased value. The un-lowercased value is sent. | buffer of 13 characters |
| WPA key max length | 25 | 30. WPA allows 63. |

The UI also never shows the server's `errorMessage`.

#### 13. `sysObjectID` is registered with the wrong type
It is registered as an OctetString (`src/snmp.cpp:57`). It should be an OBJECT IDENTIFIER (`addOIDHandler`).

#### 14. SysDescr serial format
`ESP.getEfuseMac()` returns a `uint64_t`, but it is formatted with `%x` (`src/snmp.cpp:54`). Use `%llx`.

#### 15. SNMP_Agent API usage needs checking **(verify)**
- `addReadOnlyStaticStringHandler(oid, sensorName, 40)` (`src/snmp.cpp:91,92,106`): the `40` looks like a buffer length left over from the v1 API. In 2.x it may bind to a `bool` parameter instead.
- `setReadWriteCommunity()` is given a stack array of length 1.
- `SysDescr` is `free()`d right after it is registered.

Each of these is only safe if the library copies the strings. Confirm against SNMP_Agent 2.1.

#### 16. Ethernet pin definitions are not used
`include/ethsettings.h` is included **after** `<ETH.h>`, so `ETH.begin()` uses the board variant's defaults. It also uses `ETH_TYPE` where the library expects `ETH_PHY_TYPE`. This works on `esp32-poe` only because that board variant has the correct pins.

The README also promises WT32-ETH01 support, but there is no environment or pin set for it. **Fix:** add a `[env:wt32-eth01]` and pass the PHY settings to `ETH.begin()` explicitly.

#### 17. LED pins
`STATUS_LED` and `ERROR_LED` are both on GPIO15 (`platformio.ini`), so the error and status indications overwrite each other. The heartbeat in `loop()` also overwrites the error LED state. Assign separate pins once the PCB is designed.

#### 18. The reset button reboots during normal operation
Pressing the button for 1 s at runtime reboots the device (`src/main.cpp:866-875`). This is undocumented, and an accidental press in a rack will drop monitoring.

#### 19. Dependencies are not pinned
- `espressif32` has no version. Arduino-ESP32 v3 changed the `ETH.begin()` signature and the event API.
- `me-no-dev/ESPAsyncWebServer` is unmaintained and taken from git HEAD. Consider `mathieucarbou/ESPAsyncWebServer` or `ESP32Async`.
- `ArduinoJson` is unpinned. v7 deprecates `DynamicJsonDocument`.
- `lib_extra_dirs = /lib` is an absolute path and is probably a typo.

### Web UI

#### 20. Stored XSS through sensor names
Sensor names and serials are concatenated into HTML without escaping (`data/sensorNetPro.js:89-111`). The status table also renders `name` as HTML. A name containing `"` breaks the input field. The risk is limited because the API requires a login.

#### 21. Wrong element ID
`setWifiStatus()` targets `#networkWifiModeAp`, but the element is `#networkWifiModeAP` (`data/sensorNetPro.js:272`). The AP/Station radio buttons are therefore never disabled when Wi-Fi is turned off.

#### 22. Undeclared variables
`dhcpModeValue`, `wifiModeValue` and the loop variable `i` are implicit globals (`data/sensorNetPro.js:336,560,649,729`). If no radio button is checked, `validateIpSettings()` throws a `ReferenceError`.

#### 23. Status refresh stops permanently after one error
A single failed status poll clears the interval for good (`data/sensorNetPro.js:225`). The dashboard stops updating until the page is reloaded.

---

## Low / tidy-up

- **Reboot response:** `apiReboot()` restarts before the response is sent. `reboot.js` still works through its error/poll fallback, but deferring the restart (set a flag and act on it in `loop()`) would give a clean response.
- **Unused code:**
  - The static `sensorCount` in `snmp.cpp` is never used.
  - `apiSaveSnmpConfiguration` calls `completeSnmpSetup()` for no effect, because SNMP values only change after a reboot.
- **Wi-Fi AP status:** `wifiConnected` is cleared when any AP client disconnects, even if other clients are still connected.
- **Busy-wait in `updateSensorData()`:** `requestTemperatures()` already blocks until conversion completes, so the `while (!isConversionComplete())` loop is redundant. Polling blocks `snmp.loop()` for about 190 ms. Consider `setWaitForConversion(false)` and collecting the results on the next pass.
- **Sensor discovery:** new sensors are only found at boot. Document this, or add a "rescan" action.
- **Typos:**
  - `writeWirelesEnabled`
  - "librbary", "Oveview", "hundreths" (README)
  - "Gateway address be the broadcast address" (JS)

---

## Incomplete features and documentation

- **README OID table does not match the code.** The README lists `.2` as the serial and `.3` as the name. The code registers `.2` as the **name** and `.3` as the **serial** (`src/snmp.cpp:78-79`). Decide which is correct before releasing a MIB.
- **MIB file:** not written yet. The README says "coming".
- **`BUILDING.md`:** referenced in the README but missing. It should cover the PlatformIO build and the `uploadfs` step for SPIFFS.
- **Other sensor types:** the 6c4966d commit message describes "pre-work for other sensor types", but none is visible in the code beyond the "thermal" naming.
- **KiCad hardware:** the project was created in a8375cf, but the schematic and PCB are empty.
- **README status:** still says "Initial commit … NOT YET complete. There is some seriously broken code in here" (2023-Oct-10). Update it once the critical items above are fixed.
- **No firmware update path:** there is no OTA update (ArduinoOTA or a web upload). A device in a rack has to be removed to update it.
