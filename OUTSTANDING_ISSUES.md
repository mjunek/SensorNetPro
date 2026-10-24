# SensorNetPro: Outstanding Bugs and Changes

Review of the codebase as of commit `a8375cf` (2026-10-24). Items marked ✅ have since been fixed. Each was fixed in its own commit, and the description of the fix is quoted under the item.

**Build status:** the repo builds as committed against Arduino-ESP32 v3 (3.3.12), with every dependency pinned (see item 19). Items 1–5 were first compiled against Arduino 2.0.17 in a scratch copy, and then again as part of the v3 build.

None of the fixes has been tested on hardware.

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

### 2. ✅ Buffer overflow when more than 10 sensors are on the bus
> **Fixed:** `setup()` now limits `countSensors` to `__MAX_SENSOR_COUNT` and logs a warning. `temperaturec[]` is now sized from `__MAX_SENSOR_COUNT`, and the unused `temperaturef[]` has been removed. Any sensors beyond the limit are ignored.

`countSensors = sensors.getDeviceCount()` (`src/main.cpp:806`) has no upper limit. These arrays are all sized at `__MAX_SENSOR_COUNT` or 10, and are written past their end when more sensors are present:
- `sensorAddress[]`, `sensorName[]`, `snmpTemperature[]`
- `nameOid[]` and `snmpIndex[]` in `snmp.cpp`
- `temperaturec[10]` in `updateSensorData()`

This corrupts memory or crashes the device. **Fix:** clamp `countSensors` to `__MAX_SENSOR_COUNT`.

### 3. ✅ Readings can be assigned to the wrong sensor
> **Fixed:**
> - Sensors are now read by address with `getTempC(sensorsUnique[i])`.
> - A read of `-127` or `85` °C is treated as a failure. A new status column is set to 2 (read error), and the temperature OID keeps the last good reading.
> - Status column: `…53165.1.1.1.5.<index>`, 1 = OK, 2 = read error. It starts at 2 until the first good read.
> - The web API returns an `ok` flag, and the status table shows "Read error".
> - The README OID list has been updated. Monitoring should alert on status ≠ 1.

`updateSensorData()` reads each sensor by bus position with `getTempCByIndex(i)` (`src/main.cpp:215`). The SNMP OIDs and names, however, are tied to the serials found at boot. If a sensor drops off the bus or fails to respond, the remaining sensors' readings move up one position. They are then reported against the wrong rack position. Failed reads are also published without any check:
- `-127 °C` (`DEVICE_DISCONNECTED_C`) is published as `-12700`.
- `85 °C` (the power-on value) is published as a real reading.

**Fix:** read with `getTempC(sensorsUnique[i])`. Detect failed reads and report them in a defined way, for example by not answering, using a sentinel value, or adding a status column.

### 4. ✅ `printf("%s")` is given Arduino `String` objects (undefined behaviour)
> **Fixed:** all `String` arguments to `printf` now go through `.c_str()`. The `Config > IP` line now has the correct three arguments. The `SysDescr` format bug (item 14) was fixed at the same time.

A `String` object is passed where a `char*` is expected. This prints garbage and can crash the device. Affected code:
- Every `write*()` setter that takes a `String`: `src/prefs.cpp:62-108`
- `Serial.printf("Handling JSON action %s\n", webAction)`: `src/main.cpp:587`
- `src/main.cpp:779-780`: three `String`s are passed this way. There are also four arguments for three `%s` specifiers, so the IP is printed twice and the gateway is dropped.

**Fix:** use `.c_str()`.

### 5. ✅ Web handlers race with the main loop
> **Fixed:** a FreeRTOS mutex (`stateMutex`, taken through the RAII `StateLock` in `src/main.cpp`) now guards the SNMP agent, Preferences, the `ch_*` buffers and the sensor arrays. It is taken by:
> - `loop()`, around `snmp.loop()` and while it stores sensor results. It is *not* held during the 1-Wire conversion or the Wi-Fi reconnect.
> - The whole JSON API dispatch.
> - The auth checks (`isAuthenticated()`).
> - `WiFiEvent()`.
>
> Nothing called while the lock is held waits on the Wi-Fi event task, so the event handler cannot deadlock. This still needs a soak test on hardware: rename sensors repeatedly while an `snmpwalk` loop is running.

ESPAsyncWebServer callbacks run on the `async_tcp` task, while `loop()` runs on the Arduino task. The API handlers change shared state with no locking:
- They call `snmp.removeHandler()`, `addReadOnlyStaticStringHandler()` and `sortHandlers()` (`apiSaveThermalSensorConfiguration` and `apiSaveSnmpConfiguration`). At the same time, `snmp.loop()` may be walking that same handler list.
- They call `prefs.end()`/`begin()` inside `commitToPrefs()`.
- They rewrite the `ch_*` buffers that the SNMP agent and the auth check read.

This can cause intermittent crashes, especially when a sensor is renamed while it is being polled. **Fix:** have the handler set a "pending change" flag or queue an item, and apply the change in `loop()`. The other option is to guard the SNMP and prefs access with a mutex.

---

## High

### 6. ✅ Pages are served even when authentication fails
> **Fixed:** the `/`, `/reboot.html` and `/reboot.js` handlers now `return` after `requestAuthentication()`, so an unauthenticated request gets only the 401 challenge.

In the `/`, `/reboot.html` and `/reboot.js` routes (`src/main.cpp:660-690`), `requestAuthentication()` is not followed by `return`. The code goes straight on to `request->send(SPIFFS, …)`, so each request gets two responses. The page may end up being served without authentication. The API data itself is still protected, because `jsonApiHandler` returns correctly.

### 7. ✅ The device is insecure by default
> **Fixed (forced password change):** while the admin password is still the factory default, the device is locked to changing it.
> - **API:** `/json-api` refuses every action except `config` and `save-admin` with a 403 (`passwordChangeRequired: true`). `save-admin` rejects any request that doesn't set a new password.
> - **Web UI:** the page opens on the Admin tab with a notice. The other tabs and the configuration-reset buttons are disabled, and the password fields are mandatory. After any password change, the page reloads so the browser asks for the new credentials.
> - **"Default" means** the stored password equals `__DEF_ADMIN_PASS`. A factory reset therefore locks the UI again.
>
> **Deliberately unchanged:** the setup AP is still open by default, and SNMP still defaults to `public`. Anyone in Wi-Fi range can still reach the login page.

The device starts as an **open** Wi-Fi AP (empty WPA key), with web login `admin`/`admin` over plain HTTP and SNMP community `public`. Anyone within Wi-Fi range can reconfigure it. Suggested fixes:
- Generate a WPA key from the MAC address or efuse, or print one on the label.
- Force a password change on first login.
- Turn the AP off once Ethernet is configured.

### 8. ✅ Secrets are written to the serial console
> **Fixed:** the WPA key, admin password and SNMP community are now logged through `maskSecret()` (`src/prefs.cpp`). It prints `<set>` or `<empty>`, so the console still shows whether the AP is open, but not the value.

These are printed in clear text:
- WPA key: `getWpaKey`, `writeWpaKey`, `src/main.cpp:751,759`
- Admin password: `getHttpPassword`, `writeHttpPassword`
- SNMP community string

Mask these values or remove the logging.

### 9. ✅ A Wi-Fi station failure puts the device into a reboot loop
> **Fixed:** the device no longer reboots. If station mode can't connect within the 60 s boot wait and Ethernet isn't up, `startFallbackAp()` switches to AP+STA and brings up an open setup AP named `SensorNetPro-XXXX` (the last two MAC bytes) on 192.168.4.1. The station connection keeps retrying in `loop()`.
> - **Retry rate:** while the fallback AP is up, retries happen every `FALLBACK_RETRY_TIME` (60 s) instead of every sensor poll, because each retry scan briefly drops AP clients.
> - **AP lifetime:** the AP stays up until the next reboot.
> - **Status:** the status page shows the fallback SSID and IP. AP client events no longer mark the Wi-Fi link as connected in station mode.

With Wi-Fi in station mode and Ethernet not up within 60 s, the device reboots every minute (`src/main.cpp:787-799`). It can never be reached to fix the configuration. **Fix:** fall back to AP mode, or keep running without Wi-Fi.

### 10. ✅ Static IP is applied to both interfaces
> **Fixed (Ethernet only):** the DHCP/static IP settings now apply to Ethernet only.
> - Wi-Fi station mode always uses DHCP.
> - The AP, whether configured or the fallback from item 9, always uses the soft-AP default of 192.168.4.1/24.
> - No settings, NVS keys or API fields changed. The Network tab and the status page now label these settings as Ethernet.
> - **Watch out:** the factory static defaults (`__DEF_IPADDR`, `__DEF_GATEWAY` = 192.168.4.1) are in the AP's subnet. They are only used when DHCP is turned off, but they should be edited before switching Ethernet to static.

When DHCP is off, `WiFi.config()` and `ETH.config()` are given the same IP, mask and gateway (`src/main.cpp:776-778`). This causes an address conflict and broken routing whenever both interfaces are up.

AP mode has a related problem. `WiFi.config()` sets the station interface, not the soft-AP, so the configured address is ignored in AP mode. That would need `softAPConfig()`. Network settings probably need to be set per interface, for example Ethernet static/DHCP plus AP on a fixed `192.168.4.1`.

---

## Medium

### Firmware

#### 11. ✅ No server-side input validation
> **Fixed:** every save handler now validates the whole request before writing anything, and returns a specific `errorMessage`. The rules match the web UI. The validators are in the "Input validation" block in `src/main.cpp`, and a host-side unit test of them passes.
>
> | Field | Rule |
> |---|---|
> | Hostname | A single RFC 1123 label of 1–30 characters |
> | SSID | 1–30 printable ASCII characters (may be empty when Wi-Fi is off) |
> | WPA key | Empty (open network) or 8 to the buffer limit, printable ASCII |
> | Ethernet static IP | Parsed and validated only when DHCP is off. The mask must be contiguous. The IP cannot be the subnet or broadcast address. The gateway may be empty; if set, it must be in the subnet. With DHCP on, values that don't parse are not saved. |
> | Admin user | 3–10 lowercase letters |
> | Admin password | 8–30 printable ASCII characters, not equal to the user name |
> | SNMP community | 1–30 printable ASCII characters |
> | SNMP name, location, contact | Up to 30 printable ASCII characters |
> | Sensor names | The UI's rule: 2–35 characters, alphanumeric at both ends, with `@-/_=+.` and spaces in between |
> | Sensor count | Limited to `__MAX_SENSOR_COUNT` |
>
> **Note:** the README's example name "Rack 1G, Front" has a comma, which both the UI and the firmware reject.

- **Network settings**: `IPAddress::fromString()` failures are ignored, so `0.0.0.0` is saved. Hostname characters and length are not checked.
- **SNMP and admin settings**: string lengths are not checked. Values are silently truncated to 30 or 13 characters by `toCharArray`.
- **Sensor names**: not validated at all. This also enables the XSS issue in item 20.

#### 12. ✅ Validation limits differ between the UI and the firmware
> **Fixed:** the UI and the firmware now apply the same limits.
> - **Admin password minimum:** 8 in both. This was changed with item 7.
> - **Text fields:** every `maxlength="25"` is now 30, matching the 30-character firmware buffers.
> - **WPA key:** the buffer (`__PREF_WPA_KEY_LEN`) is now 64, and the UI's `maxlength` is 63, so the full WPA2 passphrase range is accepted.
> - **Admin user name:** sent lowercased, as it is validated.
> - **Hostname:** the UI now has the same check as the firmware.
> - **Gateway:** the firmware now accepts an empty gateway, as the UI does (item 11).
> - **Error messages:** when a save is rejected, the UI now shows the firmware's `errorMessage`, including on HTTP errors.


| Field | UI | Firmware |
|---|---|---|
| Admin password min length | 6 | 8. A 6–7 character password passes the UI and then fails with a generic "error saving" message. |
| Admin username | regex `^[a-z]{3,10}` applied to the lowercased value. The un-lowercased value is sent. | buffer of 13 characters |
| WPA key max length | 25 | 30. WPA allows 63. |

The UI also never shows the server's `errorMessage`.

#### 13. ✅ `sysObjectID` is registered with the wrong type
> **Fixed:** `sysObjectID` is now registered with `addOIDHandler()`, so it is returned as an OBJECT IDENTIFIER (`.1.3.6.1.4.1.53165.0.1`). SNMP_Agent's OID encoder requires the `.1.3.` prefix, which this value has.

It is registered as an OctetString (`src/snmp.cpp:57`). It should be an OBJECT IDENTIFIER (`addOIDHandler`).

#### 14. ✅ SysDescr serial format
> **Fixed** together with item 4: the value is now cast to `unsigned long long` and printed with `%llx`.

`ESP.getEfuseMac()` returns a `uint64_t`, but it is formatted with `%x` (`src/snmp.cpp:54`). Use `%llx`.

#### 15. ✅ SNMP_Agent API usage needs checking
> **Fixed:** the API usage has been checked against SNMP_Agent 2.1.0 and corrected.
> - **String handling (already safe):** `setReadOnlyCommunity`, `setReadWriteCommunity`, `addReadOnlyStaticStringHandler` and `addOIDHandler` all take `const std::string&` and copy the value. Freeing `SysDescr` and passing a stack buffer to `setReadWriteCommunity()` are both safe.
> - **The trailing `40`:** in `addReadOnlyStaticStringHandler(oid, value, 40)` it bound to `bool overwritePrefix`. It has been removed.
> - **Leak on rename:** `updateThermalSensorName()` leaked one `ValueCallback` per rename, because `removeHandler()` doesn't free it. It now deletes the old callback. `ValueCallback`'s destructor isn't virtual, so the delete goes through the concrete `ReadOnlyStringCallback` type. The state mutex (item 5) is held during the delete, so `snmp.loop()` can't be using it.
> - **Rename of a skipped sensor:** a sensor that got no SNMP index at boot used to register a name OID at index 0 when renamed. It is now skipped.

#### 16. ✅ Ethernet pin definitions are not used
> **Fixed:**
> - **Removed `ethsettings.h`.** Each board variant already defines the PHY settings.
> - **`ETH.begin()` is called with the variant's PHY macros explicitly**, so a board that lacks them fails at compile time instead of using library defaults. The result is also checked and logged.
> - **`platformio.ini` has a shared `[env]` section and one environment per board.** `esp32poe` is the default.
> - **New `[env:wt32-eth01]` environment.** Ethernet uses the variant's settings: LAN8720 at address 1, power on GPIO16, clock in on GPIO0. The pin map is **provisional** until the PCB exists:
>   - `ONE_WIRE_BUS=4`
>   - `KEY_RESET=39`. This pin is input-only and needs an external pull-up and button.
>   - `STATUS_LED=2`, `ERROR_LED=14`, `WIFI_LED=15`
> - Both environments build. WT32-ETH01 has not been tested on hardware.

> **Update (v3 migration):** on Arduino-ESP32 v3, the no-argument `ETH.begin()` takes `ETH_PHY_TYPE`, `ETH_PHY_ADDR`, `ETH_PHY_MDC`, `ETH_PHY_MDIO`, `ETH_PHY_POWER` and `ETH_CLK_MODE` from the board variant. The `esp32-poe` variant defines all of them correctly. It also selects GPIO0 for the clock when PSRAM is enabled. `ethsettings.h` is still redundant, and WT32-ETH01 still needs its own environment.

`include/ethsettings.h` is included **after** `<ETH.h>`, so `ETH.begin()` uses the board variant's defaults. It also uses `ETH_TYPE` where the library expects `ETH_PHY_TYPE`. This works on `esp32-poe` only because that board variant has the correct pins.

The README also promises WT32-ETH01 support, but there is no environment or pin set for it. **Fix:** add a `[env:wt32-eth01]` and pass the PHY settings to `ETH.begin()` explicitly.

#### 17. ✅ LED pins
> **Fixed:**
> - **Separate pins on the ESP32-POE:** `ERROR_LED` is now GPIO14, `STATUS_LED` stays on GPIO15, and `WIFI_LED` stays on GPIO16. The `loop()` heartbeat only drives `STATUS_LED`, so the two indications no longer overwrite each other.
> - **Filesystem-failure blink:** the "filesystem failed" error loop timed its blink from `uptime`, which never advances before `loop()`. The error LED went off on the first pass and stayed off. It now blinks with `delay()`.
> - WT32-ETH01 uses the separate pins from item 16.

`STATUS_LED` and `ERROR_LED` are both on GPIO15 (`platformio.ini`), so the error and status indications overwrite each other. The heartbeat in `loop()` also overwrites the error LED state. Assign separate pins once the PCB is designed.

#### 18. ✅ The reset button reboots during normal operation
> **Fixed:**
> - **Deliberate hold:** a runtime reboot now needs a hold of `RUNTIME_REBOOT_HOLD_TIME` (5 s). It is timed with `millis()` and doesn't block, so SNMP and the web server keep running. The error LED flashes while the button is held, and releasing it early cancels.
> - **No accidental factory reset:** the boot-time factory reset is now skipped after a software restart (`esp_reset_reason() == ESP_RST_SW`). Holding the button through the reboot it triggers therefore can't wipe the settings. A power cycle or a hardware reset with the button held still works.
> - **Documented:** the README describes both behaviours.

Pressing the button for 1 s at runtime reboots the device (`src/main.cpp:866-875`). This is undocumented, and an accidental press in a rack will drop monitoring.

#### 19. ✅ Dependencies are not pinned
> **Fixed:** the project now targets Arduino-ESP32 v3, and every dependency is pinned in `platformio.ini`:
> - `espressif32 @ 8.0.0` (Arduino-ESP32 3.3.12). This is the first PlatformIO platform release with v3; 7.1.3 still ships 2.0.17.
> - `esp32async/ESPAsyncWebServer @ 3.12.1` and `esp32async/AsyncTCP @ 3.5.0`. These are the maintained forks and replace the unmaintained `me-no-dev` git dependency.
> - `ArduinoJson @ 7.4.3`, `SNMP_Agent @ 2.1.0`, `OneWire @ 2.3.8`, `DallasTemperature @ 3.11.0`.
>
> Other changes:
> - `lib_extra_dirs = /lib` has been removed. It pulled Linux system headers into the build.
> - `lib_ldf_mode` is now `chain`. Under `deep+`, the library finder missed the `SPI` and `Network` libraries that v3 requires.
> - `DynamicJsonDocument` has been replaced with `JsonDocument`.
> - **Partition table:** this is now `huge_app.csv` (3 MB app, 896 KB SPIFFS, no OTA slot). The v3 image is about 1.5 MB and no longer fits the 1.25 MB app partition in `default.csv`. `min_spiffs.csv` keeps OTA but has only 128 KB of SPIFFS, which is too small for the ~500 KB web UI.
>   - **Existing boards:** NVS keeps its offset, so saved settings survive. SPIFFS moves, so run `pio run -t uploadfs` again after flashing.
>
> The v3 runtime paths have been checked against the framework source, but not yet on hardware:
> - `ETH.begin()` with no arguments takes the LAN8720 pins and clock from the `esp32-poe` variant.
> - `ETH.config()` is called after `begin()`, as v3 requires.
> - The JSON API response shapes are unchanged under ArduinoJson 7 (checked with a host-side test).

- `espressif32` has no version. Arduino-ESP32 v3 changed the `ETH.begin()` signature and the event API.
- `me-no-dev/ESPAsyncWebServer` is unmaintained and taken from git HEAD. Consider `mathieucarbou/ESPAsyncWebServer` or `ESP32Async`.
- `ArduinoJson` is unpinned. v7 deprecates `DynamicJsonDocument`.
- `lib_extra_dirs = /lib` is an absolute path and is probably a typo.

### Web UI

#### 20. ✅ Stored XSS through sensor names
> **Fixed:** sensor names and serials now go through `escapeHtml()` wherever the web UI inserts them into HTML: the status table, the config table labels and inputs, and the `data-serial` attribute. The firmware also rejects such names (item 11), so this is defence in depth.
> - **Serial read-back:** the serial is now read back with `.attr("data-serial")`. jQuery's `.data()` turned all-digit serials into numbers, which the firmware then rejected.
> - **Testing:** the UI test now feeds hostile names and serials. The old code fails the injection checks, and the fixed code passes.

Sensor names and serials are concatenated into HTML without escaping (`data/sensorNetPro.js:89-111`). The status table also renders `name` as HTML. A name containing `"` breaks the input field. The risk is limited because the API requires a login.

#### 21. ✅ Wrong element ID
> **Fixed:** `setWifiStatus()` now targets `#networkWifiModeAP`, so the AP/Station radios are disabled when Wi-Fi is off. The selected mode is still saved, because disabled radios still match `:checked`.

`setWifiStatus()` targets `#networkWifiModeAp`, but the element is `#networkWifiModeAP` (`data/sensorNetPro.js:272`). The AP/Station radio buttons are therefore never disabled when Wi-Fi is turned off.

#### 22. ✅ Undeclared variables
> **Fixed:**
> - **Declarations:** all loop variables and the `dhcpModeValue`/`wifiModeValue` values are now declared. When no radio is checked, they default to what `saveSection()` sends (DHCP off, Wi-Fi off).
> - **Strict mode:** `sensorNetPro.js` and `reboot.js` now start with `"use strict"`, so a future implicit global throws. Both pages run clean under the UI test.

`dhcpModeValue`, `wifiModeValue` and the loop variable `i` are implicit globals (`data/sensorNetPro.js:336,560,649,729`). If no radio button is checked, `validateIpSettings()` throws a `ReferenceError`.

#### 23. ✅ Status refresh stops permanently after one error
> **Fixed:**
> - **Polling continues through errors.** While the device is unreachable, a "Lost contact with the device. Retrying..." banner stays up. The next successful status or table refresh clears it.
> - **Popups suppressed.** Table refresh errors used DataTables' default `alert()` error mode, which would pop up every 10 s during an outage. `errMode` is now `none`, and table errors go to the same banner.
> - **Testing:** the UI test runs a simulated outage and recovery. The old code fails it, and the fixed code passes.

A single failed status poll clears the interval for good (`data/sensorNetPro.js:225`). The dashboard stops updating until the page is reloaded.

---

## Low / tidy-up

- ✅ **Reboot response:** `apiReboot()` restarts before the response is sent. `reboot.js` still works through its error/poll fallback, but deferring the restart (set a flag and act on it in `loop()`) would give a clean response.
  > **Fixed:** `apiReboot()` now only records the request, and `loop()` restarts a second later, once the response has gone out. On a successful response `reboot.js` now waits and polls `status` until the device is back, instead of going straight to `/` while the device is still restarting.
- ✅ **Unused code:**
  - The static `sensorCount` in `snmp.cpp` is never used.
  - `apiSaveSnmpConfiguration` calls `completeSnmpSetup()` for no effect, because SNMP values only change after a reboot.
  > **Fixed:** removed both, plus an unused `reset` flag in the boot-time factory reset loop that the compiler warned about. `completeSnmpSetup()` only re-sorts the handlers, and that SNMP save adds none.
- ✅ **Wi-Fi AP status:** `wifiConnected` is cleared when any AP client disconnects, even if other clients are still connected.
  > **Fixed:** on a client disconnect, `wifiConnected` (and the Wi-Fi LED) now stays set while `WiFi.softAPgetStationNum()` still reports other clients.
- ✅ **Busy-wait in `updateSensorData()`:** `requestTemperatures()` already blocks until conversion completes, so the `while (!isConversionComplete())` loop is redundant. Polling blocks `snmp.loop()` for about 190 ms. Consider `setWaitForConversion(false)` and collecting the results on the next pass.
  > **Fixed:** conversions now run in the background (`setWaitForConversion(false)`). `loop()` starts one every `SENSOR_TIME` with `requestSensorData()`, keeps serving SNMP, the button and the LEDs, and collects the readings with `updateSensorData()` once `millisToWaitForConversion()` (188 ms at 10-bit) has passed. Waiting a fixed time, rather than polling the bus, also works with parasite-powered sensors.
- ✅ **Sensor discovery:** new sensors are only found at boot. Document this, or add a "rescan" action.
  > **Fixed (documented):** the README and the sensor configuration page now say that sensors are found at startup, and to reboot after adding or replacing one. A live rescan would still need the SNMP table rebuilt, which the agent only supports cleanly at boot, so a reboot is the simpler path.
- ✅ **Typos:**
  - `writeWirelesEnabled`
  - "librbary", "Oveview", "hundreths" (README)
  - "Gateway address be the broadcast address" (JS)
  > **Fixed:** renamed to `writeWirelessEnabled`, corrected the README spellings, and the gateway message now reads "Gateway address cannot be the broadcast address".

---

## Incomplete features and documentation

- ✅ **README OID table does not match the code.** The README lists `.2` as the serial and `.3` as the name. The code registers `.2` as the **name** and `.3` as the **serial** (`src/snmp.cpp:78-79`). Decide which is correct before releasing a MIB.
  > **Fixed:** kept the firmware's order, so existing SNMP polling keeps working. The README now lists `.2` as the name and `.3` as the serial, and uses the same object names as the code (`sensorNetPro.thermalSensorTable.thermalSensorTableEntry`).
- **MIB file:** not written yet. The README says "coming".
- **`BUILDING.md`:** referenced in the README but missing. It should cover the PlatformIO build and the `uploadfs` step for SPIFFS.
- **Other sensor types:** the 6c4966d commit message describes "pre-work for other sensor types", but none is visible in the code beyond the "thermal" naming.
- **KiCad hardware:** the project was created in a8375cf, but the schematic and PCB are empty.
- **README status:** still says "Initial commit … NOT YET complete. There is some seriously broken code in here" (2023-Oct-10). Update it once the critical items above are fixed.
- **No firmware update path:** there is no OTA update (ArduinoOTA or a web upload). A device in a rack has to be removed to update it.
  - `huge_app.csv` has no second app slot (see item 19). Adding OTA will need either a custom partition table or smaller web assets. Two ~1.6 MB app slots leave about 640 KB for SPIFFS. Gzipping the UI would shrink it from ~500 KB, and ESPAsyncWebServer serves `.gz` files automatically.
