# SensorNetPro hardware #

## Status ##
**Not started.** The KiCad project (`SensorNetPro.kicad_pro`) has been created, but the schematic and PCB are empty. Until it is designed, build the circuit below by hand on either supported board.

The plan is a carrier board for the Olimex ESP32-POE or the WT32-ETH01, with sensor connectors, a button and status LEDs. The WT32-ETH01 pin map in `platformio.ini` is provisional and may change with the PCB.

## Circuit ##
The firmware expects:

| Function | Signal | Wiring |
|---|---|---|
| 1-wire sensor bus | `ONE_WIRE_BUS` | DS18B20 DQ lines in parallel, with one 4.7 kΩ pull-up to 3.3 V for the whole bus. Power the sensors from 3.3 V and ground (parasite power also works) |
| Button | `KEY_RESET` | Active low: a push button to ground, with a 10 kΩ pull-up to 3.3 V. The firmware enables no internal pull-up, and the input-only pins (34–39) have none |
| Error LED | `ERROR_LED` | Active high: GPIO → resistor (about 330 Ω–1 kΩ) → LED → ground |
| Status LED | `STATUS_LED` | As above. Flashes once a second while running |
| Network LED | `WIFI_LED` | As above. On while Ethernet or Wi-Fi is connected |

### Pins ###

| Signal | Olimex ESP32-POE | WT32-ETH01 (provisional) |
|---|---|---|
| `ONE_WIRE_BUS` | GPIO4 | GPIO4 |
| `KEY_RESET` | GPIO34 (the on-board BUT1, already pulled up) | GPIO39 (needs the external pull-up and button) |
| `ERROR_LED` | GPIO14 | GPIO14 |
| `STATUS_LED` | GPIO15 | GPIO2 |
| `WIFI_LED` | GPIO16 | GPIO15 |

Notes:
* The Ethernet PHY pins are fixed by each board and set by its PlatformIO board variant. Don't use them: on the ESP32-POE that includes GPIO12 (PHY power) and GPIO17 (PHY clock), and on the WT32-ETH01 GPIO0 (PHY clock) and GPIO16 (PHY oscillator enable).
* GPIO2 and GPIO15 are strapping pins. An LED with its resistor to ground keeps them low at reset, which is safe for normal booting.
* WROVER-based ESP32-POE variants use GPIO16 for PSRAM, so `WIFI_LED` would need another pin on those.
* For long sensor runs, keep the bus as one daisy chain rather than a star. Lowering the pull-up toward 2.2 kΩ can help on long cables.
