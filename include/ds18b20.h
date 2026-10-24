/*
 *  Sensor Net Pro
 *  Copyright © 2023-2026 Michael Junek
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

#ifndef DS18B20_H
#define DS18B20_H

#include <OneWire.h>           // Dallas OneWire interface
#include <DallasTemperature.h> // DS18B20 support
#include "prefs.h"
#include "sensor.h"

// DS18B20 temperature sensors on one 1-wire bus. Readings are in hundredths of a degree Celsius.
class Ds18b20Sensors : public SensorType
{
public:
  Ds18b20Sensors(uint8_t pin, uint8_t resolution);

  const char *name() const override { return "thermal"; }
  int snmpTable() const override { return 1; } // thermalSensorTable
  int discover(int maxCount) override;
  void serial(int i, char *buffer, size_t length) override;
  uint32_t startReading() override;
  bool reading(int i, int *value) override;

private:
  OneWire oneWire;
  DallasTemperature bus;
  uint8_t resolution;
  int count = 0;
  DeviceAddress addresses[__MAX_SENSOR_COUNT];
};

#endif // DS18B20_H
