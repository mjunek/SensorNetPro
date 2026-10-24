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

#include "snmp.h"

const char *oidSysDescr = ".1.3.6.1.2.1.1.1.0";    // OctetString SysDescr
const char *oidSysObjectID = ".1.3.6.1.2.1.1.2.0"; // OID SysObjectID
const char *oidSysUptime = ".1.3.6.1.2.1.1.3.0";   // TimeTicks sysUptime (hundredths of seconds)
const char *oidSysContact = ".1.3.6.1.2.1.1.4.0";  // OctetString SysContact
const char *oidSysName = ".1.3.6.1.2.1.1.5.0";     // OctetString SysName
const char *oidSysLocation = ".1.3.6.1.2.1.1.6.0"; // OctetString SysLocation
const char *oidSysServices = ".1.3.6.1.2.1.1.7.0"; // Integer sysServices
const char *sysObject = ".1.3.6.1.4.1.53165.0.1";  // Default ObjectId for device = enterprises.mortonLights.sensorNetPro.objectIds.1
const char *thermalBaseOid = ".1.3.6.1.4.1.53165.1.1.1."; // Base table oid for Thermal monitored objects =  enterprises.mortonLights.sensorNetPro.thermalSensorTable.thermalSensorTableEntry


SNMPAgent snmp = SNMPAgent();
WiFiUDP udp;
int sensorCount = 0;
ValueCallback *nameOid[__MAX_SENSOR_COUNT];
int snmpIndex[__MAX_SENSOR_COUNT];

void snmpAgentLoop()
{
    snmp.loop();
}

void initialiseSnmp(char* ch_snmpCommunity, uint32_t *uptime, char *ch_snmpSysContact, char *ch_snmpSysName, char *ch_snmpLocation)
{
    Serial.println("Starting SNMP");
    snmp.setUDP(&udp);
    snmp.begin();
    snmp.setReadOnlyCommunity(ch_snmpCommunity);
    char n[1];
    n[0]='\0';
    snmp.setReadWriteCommunity(n);
    Serial.println("Setting SNMP mib-2 system attributes");
    char *SysDescr = (char *)malloc(100);
    sprintf(SysDescr, "SensorNetPro v%s HW:%s@%d SN:%llx", __FIRMWARE_VERSION, ESP.getChipModel(), ESP.getChipRevision(), (unsigned long long)ESP.getEfuseMac());
    snmp.addReadOnlyStaticStringHandler(oidSysDescr, SysDescr);
    free(SysDescr);
    snmp.addOIDHandler(oidSysObjectID, sysObject);
    snmp.addTimestampHandler(oidSysUptime, uptime);
    snmp.addReadOnlyStaticStringHandler(oidSysContact, ch_snmpSysContact);
    snmp.addReadOnlyStaticStringHandler(oidSysName, ch_snmpSysName);
    snmp.addReadOnlyStaticStringHandler(oidSysLocation, ch_snmpLocation);
}

void addThermalSensorHandler(char *sensorSerial, char *sensorName, int *temperatureValue, int *statusValue, int sensNum)
{
    String oid_deviceIndex, oid_deviceName, oid_deviceSerial, oid_temp, oid_status;
    char ch_oid_deviceIndex[40], ch_oid_deviceName[40], ch_oid_deviceSerial[40], ch_oid_temp[40], ch_oid_status[40];
    sensorCount++;
    Serial.println("++ Adding SNMP Thermal Sensor handler");
    int index = getSnmpIndex(sensorSerial);
    if (index < 1)
    {
        Serial.printf("Error: skipping this sensor, no ID allocated: %s\n", sensorSerial);
        return;
    }
    snmpIndex[sensNum] = index;
    oid_deviceIndex = String(thermalBaseOid) + "1." + String(index);
    oid_deviceName = String(thermalBaseOid) + "2." + String(index);
    oid_deviceSerial = String(thermalBaseOid) + "3." + String(index);
    oid_temp = String(thermalBaseOid) + "4." + String(index);
    oid_status = String(thermalBaseOid) + "5." + String(index);
    oid_deviceIndex.toCharArray(ch_oid_deviceIndex, 40);
    oid_deviceName.toCharArray(ch_oid_deviceName, 40);
    oid_deviceSerial.toCharArray(ch_oid_deviceSerial, 40);
    oid_temp.toCharArray(ch_oid_temp, 40);
    oid_status.toCharArray(ch_oid_status, 40);
    Serial.printf("++ (%s) >> Index OID:        %s\n", sensorSerial, ch_oid_deviceIndex);
    Serial.printf("++ (%s) >> Device Name  OID: %s\n", sensorSerial, ch_oid_deviceName);
    Serial.printf("++ (%s) >> Serial OID:       %s\n", sensorSerial, ch_oid_deviceSerial);
    Serial.printf("++ (%s) >> Temperature OID:  %s\n", sensorSerial, ch_oid_temp);
    Serial.printf("++ (%s) >> Status OID:       %s\n", sensorSerial, ch_oid_status);

    snmp.addReadOnlyIntegerHandler(ch_oid_deviceIndex, index);
    nameOid[sensNum] = snmp.addReadOnlyStaticStringHandler(ch_oid_deviceName, sensorName);
    snmp.addReadOnlyStaticStringHandler(ch_oid_deviceSerial, sensorSerial);
    snmp.addIntegerHandler(ch_oid_temp, &temperatureValue[sensNum]);
    snmp.addIntegerHandler(ch_oid_status, &statusValue[sensNum]);
}

void updateThermalSensorName(char *sensorName, int sensNum)
{
    int index = snmpIndex[sensNum];
    ValueCallback *vc = nameOid[sensNum];
    if (vc == nullptr)
    {
        // No SNMP index was allocated for this sensor at boot, so there's no name OID to update
        Serial.printf("No SNMP handler for sensor %d, not updating name\n", sensNum);
        return;
    }
    char ch_oid_deviceName[40];
    String oid_deviceName = String(thermalBaseOid) + "2." + String(index);
    oid_deviceName.toCharArray(ch_oid_deviceName, 40);
    Serial.printf("Removing existing snmp handler for index %d", sensNum);
    snmp.removeHandler(vc);
    // removeHandler() doesn't free the callback, and ValueCallback's destructor isn't virtual,
    // so delete it as its concrete type to release its string as well. The handler was created
    // by addReadOnlyStaticStringHandler(), so that is exactly its dynamic type.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
    delete static_cast<ReadOnlyStringCallback *>(vc);
#pragma GCC diagnostic pop
    Serial.printf("..and adding value %s to OID %s\n", sensorName, ch_oid_deviceName);
    nameOid[sensNum] = snmp.addReadOnlyStaticStringHandler(ch_oid_deviceName, sensorName);
}
void completeSnmpSetup()
{
    Serial.println("Sorting SNMP handlers for walk");
    snmp.sortHandlers();
}