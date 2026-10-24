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

#ifndef SNMP_H
#define SNMP_H

#include <SNMP_Agent.h>
#include <SNMPTrap.h>
#include "prefs.h"
#include <WiFi.h>

void initialiseSnmp(char* ch_snmpCommunity, uint32_t *uptime, char *ch_snmpSysContact, char *ch_snmpSysName, char *ch_snmpLocation);
// Values for the sensor status column (.5) of every sensor table
#define SENSOR_STATUS_OK 1
#define SENSOR_STATUS_ERROR 2

// Register the five columns of one sensor's row in table sensorNetPro.<table>. value and statusValue
// are read live by the agent. sensNum identifies the sensor in updateSensorName().
void addSensorHandler(int table, char *sensorSerial, char *sensorName, int *value, int *statusValue, int sensNum);
void completeSnmpSetup();
void snmpAgentLoop();
void updateSensorName(char *sensorName, int sensNum);

#endif // SNMP_H