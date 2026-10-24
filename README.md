# ESP SNMP Temperature Monitor #

## Overview ##
This project was created as a cheaper alternative to commercial thermal sensors for data centre use.
The system is aimed at using the SNMP protocol for monitoring temperatures in server racks.

## Features ##
The system is based on the Olimex ESP32-POE ESP32 module, or alternatively the WT32-ETH01 ethernet ESP32 module. 
These both supply a hard-wired connection into the monitoring/management network where WiFi is not usually employed. The system does have the option for WiFi configuration, which is also used for initial setup.

The button is used to reset the settings. Pressing this button and holding it for 5 seconds at power on will reset the settings to default.
While the device is running, holding the button for 5 seconds reboots it (the error LED flashes while the button is held), and shorter presses are ignored. A reboot started this way, or from the web UI, never triggers the factory reset, even if the button is still held as the device restarts.
The default mode is set automatically on first boot within the Preferences library.

The platform supports the Dallas 1-wire DS18B20 sensor set (https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf)
It will discover up to 10 devices (configurable in ```prefs.h```), and the embedded 1-wire serial numbers are stored within the preferences to ensure consistent SNMP indexes within the table. Up to 20 devices are remembered. These can also be named with a friendly name, so that display shows something reasonable such as "Rack 1G, Front"

Sensors are discovered once, at startup. After adding or replacing a sensor, reboot the device (from the web UI, or by holding the button for 5 seconds) to pick it up. A sensor that is removed while running stays in the SNMP table with a read error status until the next reboot.

The SNMP OID consists of a prefix ```1.3.6.1.4.1.53165.1.1.1``` (enterprises.mortonLights.sensorNetPro.thermalSensorTable.thermalSensorTableEntry)
There are five columns in this table, each indexed by the sensor's SNMP index (the same value as column 1)
* ```53165.1.1.1.1``` - SNMP Index for discovery
* ```53165.1.1.1.2``` - Friendly name of sensor
* ```53165.1.1.1.3``` - Serial number of sensor (the 1-wire ROM address, as 16 hex digits)
* ```53165.1.1.1.4``` - Temperature in hundredths of degrees Celsius -> This needs to be divided by 100 to get the actual temperature
* ```53165.1.1.1.5``` - Sensor status: 1 = OK, 2 = read error (the temperature column then holds the last good reading)

### Sensor types ###
DS18B20 temperature sensors are the only type supported so far, but the firmware is built around a generic sensor interface (```include/sensor.h```) so that others can be added. Each type gets its own SNMP table under ```1.3.6.1.4.1.53165.1``` with the same five columns (index, name, serial, value, status), with values scaled by 100. The thermal table is ```.1```, and ```.2``` to ```.99``` are reserved for further types. To add a type:
1. Implement ```SensorType``` for it (see ```src/ds18b20.cpp```), and add an instance to ```sensorTypes[]``` in ```src/main.cpp```.
2. Give it the next free table number, and add its table to the MIB.
3. Show it in the web UI. The JSON API already lists each type's sensors under ```<type>SensorData```, but the page only displays ```thermalSensorData``` today.


The MIB is in ```mibs/SENSORNETPRO-MIB.txt```. Copy it into your SNMP manager's MIB directory to see named objects (for example ```thermalSensorTemperature```) instead of numeric OIDs. With net-snmp:

```
snmpwalk -v2c -c public -M +./mibs -m +SENSORNETPRO-MIB <device> SENSORNETPRO-MIB::thermalSensorTable
```

The device's sysObjectID is ```1.3.6.1.4.1.53165.0.1``` (```sensorNetProDevice```).

## License ##
The project is licensed under the terms of the General Public License v3 as detailed in the LICENSE file

## Copyright ##
Copyright © 2023 Michael Junek. 

## Building ##
Please see the BUILDING.md file (not yet written)

Each supported board has its own PlatformIO environment. `esp32poe` is the default:
* Olimex ESP32-POE: ```pio run -e esp32poe```
* WT32-ETH01: ```pio run -e wt32-eth01``` (provisional pin map, see ```platformio.ini```)

## Updates ##
_Update 2023-Oct-10:_ Initial commit to repository is NOT YET complete. There is some seriously broken code in here.