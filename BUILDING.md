# Building SensorNetPro #

The firmware is built with [PlatformIO](https://platformio.org/), either the `pio` command line tool or the PlatformIO extension for VS Code. Everything else (the ESP32 toolchain, Arduino-ESP32 3.3 and the libraries) is downloaded by PlatformIO at the exact versions pinned in `platformio.ini`.

## Boards ##
Each supported board has its own environment in `platformio.ini`, which also holds its pin map:

| Board | Environment | Notes |
|---|---|---|
| Olimex ESP32-POE | `esp32poe` (default) | USB connector on the board |
| WT32-ETH01 | `wt32-eth01` | Provisional pin map. Needs a 3.3V USB-serial adapter, and has no user button |

Add `-e <environment>` to every `pio` command below to choose the board. Without it, PlatformIO uses `esp32poe`.

## Building ##
```
pio run -e esp32poe
```

The firmware image is written to `.pio/build/<environment>/firmware.bin`.

Build-time settings:
* `platformio.ini` build flags: `SENSOR_TIME` (seconds between sensor readings), `FALLBACK_RETRY_TIME` (seconds between Wi-Fi reconnection attempts while the fallback access point is up), `RUNTIME_REBOOT_HOLD_TIME` (seconds the button must be held to reboot), and each board's pins.
* `include/prefs.h`: the maximum number of sensors, the DS18B20 resolution, and the factory default settings.

## Web interface ##
The web interface lives in `data/` and is stored in the SPIFFS partition, separately from the firmware. Build its image with:
```
pio run -e esp32poe -t buildfs
```

The image is built from a gzipped copy of `data/` (made by `scripts/gzip_data.py`), so edit the files in `data/` as normal. The web server sends the compressed files to the browser as they are. The result is `.pio/build/<environment>/spiffs.bin`.

## First flash (USB) ##
A new board, or one running firmware from before network updates were added, has to be flashed over USB once. Upload both the firmware and the web interface:
```
pio run -e esp32poe -t upload
pio run -e esp32poe -t uploadfs
```

PlatformIO picks the serial port automatically. If it chooses the wrong one, add `--upload-port /dev/ttyUSB0` (or `COM7` on Windows).

* **Olimex ESP32-POE:** connect the board's USB port. The plain ESP32-POE is not isolated from its PoE supply, so Olimex advises against connecting it to a computer over USB while it is powered over PoE. The ESP32-POE-ISO is isolated.
* **WT32-ETH01:** connect a 3.3V USB-serial adapter to TX0 (GPIO1), RX0 (GPIO3) and ground, and hold IO0 to ground while the board powers up or resets to enter the bootloader.

Older firmware used a different partition table. The new table keeps the settings area where it was, so saved settings survive, but the web interface must be uploaded again with `uploadfs`.

To watch the serial log:
```
pio device monitor
```

## First boot ##
With no saved settings, the device starts with:
* An open Wi-Fi access point called `SensorNetPro`, at `http://192.168.4.1`
* DHCP on Ethernet
* Web login `admin` / `admin`. The web interface only allows the admin password to be changed until it is.
* SNMP read-only community `public`

To return a device to these defaults, hold the button while it powers up. The LEDs flash, and after about 5 seconds it resets its settings and restarts. Releasing the button sooner cancels the reset.

## Network updates ##
Once the default admin password has been changed, later updates don't need USB.

From the web interface: open Admin → Firmware Update, choose **Firmware** with `firmware.bin` or **Web interface** with `spiffs.bin`, and upload it. The device restarts once the image is written, and the page reloads when it is back. If the web interface is missing or broken, for example after an interrupted upload, a basic upload page is built into the firmware at `http://<device>/update`.

From PlatformIO, authenticating with the admin password:
```
PLATFORMIO_UPLOAD_FLAGS="--auth=<admin password>" pio run -e esp32poe -t upload --upload-port <device IP>
PLATFORMIO_UPLOAD_FLAGS="--auth=<admin password>" pio run -e esp32poe -t uploadfs --upload-port <device IP>
```

PlatformIO switches to network upload when the upload port is an IP address or a `.local` hostname. To make it permanent for a device, add `upload_protocol = espota`, `upload_port` and `upload_flags = --auth=...` to a local copy of the environment, but don't commit the password.

## SNMP MIB ##
`mibs/SENSORNETPRO-MIB.txt` describes the device's SNMP objects. It isn't part of the build. Load it into your SNMP manager (see the README).
