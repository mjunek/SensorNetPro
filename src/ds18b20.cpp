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

#include "ds18b20.h"

Ds18b20Sensors::Ds18b20Sensors(uint8_t pin, uint8_t resolution)
    : oneWire(pin), bus(&oneWire), resolution(resolution)
{
}

int Ds18b20Sensors::discover(int maxCount)
{
  Serial.println("Starting Dallas 1-wire interface");
  bus.begin();
  bus.setResolution(resolution);
  // Conversions run in the background: startReading() returns at once, and the caller waits
  bus.setWaitForConversion(false);
  int found = bus.getDeviceCount();
  Serial.printf("Found %d DS18B20 sensors\n", found);
  if (found > maxCount)
  {
    Serial.printf("Too many sensors, only the first %d will be used\n", maxCount);
    found = maxCount;
  }
  count = 0;
  for (int i = 0; i < found; i++)
  {
    if (!bus.getAddress(addresses[count], i))
    {
      Serial.printf("Could not read the address of sensor %d, skipping it\n", i);
      continue;
    }
    bus.setResolution(addresses[count], resolution);
    count++;
  }
  return count;
}

void Ds18b20Sensors::serial(int i, char *buffer, size_t length)
{
  // The 64-bit ROM address as lower-case hex, in bus order
  String hexString = "";
  for (int j = 0; j < 8; j++)
  {
    byte y = addresses[i][j];
    if (y < 0x10)
    {
      hexString += "0";
    }
    hexString += String(y, HEX);
  }
  hexString.toCharArray(buffer, length);
}

uint32_t Ds18b20Sensors::startReading()
{
  bus.requestTemperatures();
  // A fixed wait, rather than polling the bus, also works with parasite-powered sensors
  return bus.millisToWaitForConversion();
}

bool Ds18b20Sensors::reading(int i, int *value)
{
  // Read by address so a missing sensor can't shift readings onto other sensors
  float temperature = bus.getTempC(addresses[i]);
  // -127 = disconnected/CRC failure, 85 = power-on reset value (conversion never ran)
  if (temperature == DEVICE_DISCONNECTED_C || temperature == 85.0f)
    return false;
  *value = temperature * 100;
  return true;
}
