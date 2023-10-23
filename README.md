# ESP SNMP Temperature Monitor #

## Oveview ##
This project was created as a cheaper alternative to commercial thermal sensors for data centre use.
The system is aimed at using the SNMP protocol for monitoring temperatures in server racks.

## Features ##
The system is based on the Olimex ESP32-POE ESP32 module, or alternatively the WT32-ETH01 ethernet ESP32 module. 
These both supply a hard-wired connection into the monitoring/management network where WiFi is not usually employed. The system does have the option for WiFi configuration, which is also used for initial setup.

The button is used to reset the settings. Pressing this button and holding it for 5 seconds at power on will reset the settings to default.
The default mode is set automatically on first boot within the Preferences librbary.

The platform supports the Dallas 1-wire DS18B20 sensor set (https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf)
It will discover up to 10 devices (configurable in ```prefs.h```), and the embedded 1-wire serial numbers are stored within the preferences to ensure consistent SNMP indexes within the table. Up to 20 devices are remembered. These can also be named with a friendly name, so that display shows something reasonable such as "Rack 1G, Front"

The SNMP OID consists of a prefix ```1.3.6.1.4.1.53165.1.1.1``` (enterprises.mortonLights.espThermalMonitor.sensorTable.sensorTableEntry) 
There are four entries under this table
* ```53165.1.1.1.1``` - SNMP Index for discovery
* ```53165.1.1.1.2``` - Serial number of sensor
* ```53165.1.1.1.3``` - Friendly name of sensor
* ```53165.1.1.1.4``` - Temperature in hundreths of Degrees Celsius -> This needs to be divided by 100 to get the actual temperature

## MIB ##
A MIB file is coming!

## License ##
The project is licensed under the terms of the General Public License v3 as detailed in the LICENSE file

## Copyright ##
Copyright © 2023 Michael Junek. 

## Building ##
Please see the BUILDING.md file (not yet written)

## Updates ##
_Update 2023-Oct-10:_ Initial commit to repository is NOT YET complete. There is some seriously broken code in here.