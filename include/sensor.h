/*
 *  Sensor Net Pro
 *  Copyright © 2023 Michael Junek
 *
 *  This file is part of SensorNetPro.
 *
 *  SensorNetPro is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation, either
 *  version 3 of the License, or (at your option) any later version.
 *
 *  SensorNetPro is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with SensorNetPro in a file called LICENSE.
 *  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

// Includes the terminating null
#define SENSOR_SERIAL_LEN 20
#define SENSOR_NAME_LEN 40

/*
 * One kind of sensor, such as DS18B20 temperature sensors. To add a sensor type, implement this
 * class and add an instance to sensorTypes[] in main.cpp.
 *
 * Each type has its own SNMP table, sensorNetPro.<snmpTable()>, with the same five columns as the
 * thermal table: index, name, serial, value and status. Values are integers scaled by 100, so a
 * type's unit is set out in the MIB (thermal: hundredths of a degree Celsius).
 */
class SensorType
{
public:
  virtual ~SensorType() {}

  // Short lower-case name, such as "thermal". The web API lists this type's sensors under
  // "<name>SensorData".
  virtual const char *name() const = 0;

  // Sub-identifier of this type's table under sensorNetPro (1.3.6.1.4.1.53165.1). 1 is thermal,
  // and 2 to 99 are reserved for further types. Must match the MIB.
  virtual int snmpTable() const = 0;

  // Find the connected sensors, at most maxCount, and return how many were found. Called once at
  // startup.
  virtual int discover(int maxCount) = 0;

  // Serial number of sensor i (0 to discover() - 1), at most SENSOR_SERIAL_LEN - 1 characters. It
  // must be unique across all types, because it keys the stored SNMP index and friendly name.
  virtual void serial(int i, char *buffer, size_t length) = 0;

  // Start reading every sensor of this type, without blocking. Returns how many milliseconds to
  // wait before calling reading().
  virtual uint32_t startReading() = 0;

  // Fetch the result of the last startReading() for sensor i, scaled by 100. Returns false if the
  // sensor couldn't be read, leaving value unchanged.
  virtual bool reading(int i, int *value) = 0;
};

// A sensor found at startup
struct Sensor
{
  SensorType *type;
  int typeIndex; // index of this sensor within its type
  char serial[SENSOR_SERIAL_LEN];
  char name[SENSOR_NAME_LEN];
};

#endif // SENSOR_H
