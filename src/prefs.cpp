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

#include "prefs.h"
/*
 *
 * SETTERS
 *
 */
Preferences prefs;
Preferences snmpMap;

void initPreferences()
{
    prefs.begin("SensorApp", false);
    snmpMap.begin("SensorSNMP", false);
}

void writeIPAddress(int octet1, int octet2, int octet3, int octet4)
{
    prefs.putUChar(__PREF_IPADDR_1, octet1);
    prefs.putUChar(__PREF_IPADDR_2, octet2);
    prefs.putUChar(__PREF_IPADDR_3, octet3);
    prefs.putUChar(__PREF_IPADDR_4, octet4);
    Serial.printf("PREFS: writeIPAddress complete - %d.%d.%d.%d\n", octet1, octet2, octet3, octet4);
}
void writeSubnetMask(int octet1, int octet2, int octet3, int octet4)
{
    prefs.putUChar(__PREF_MASK_1, octet1);
    prefs.putUChar(__PREF_MASK_2, octet2);
    prefs.putUChar(__PREF_MASK_3, octet3);
    prefs.putUChar(__PREF_MASK_4, octet4);
    Serial.printf("PREFS: writeSubnetMask complete - %d.%d.%d.%d\n", octet1, octet2, octet3, octet4);
}
void writeDefaultGateway(int octet1, int octet2, int octet3, int octet4)
{
    prefs.putUChar(__PREF_GATEWAY_1, octet1);
    prefs.putUChar(__PREF_GATEWAY_2, octet2);
    prefs.putUChar(__PREF_GATEWAY_3, octet3);
    prefs.putUChar(__PREF_GATEWAY_4, octet4);
    Serial.printf("PREFS: writeDefaultGateway complete - %d.%d.%d.%d\n", octet1, octet2, octet3, octet4);
}

void writeHostname(String hostname)
{
    prefs.putString(__PREF_HOSTNAME, hostname);
    Serial.printf("PREFS: writeHostname complete - %s\n", hostname);
}
void writeSSID(String ssid)
{
    prefs.putString(__PREF_SSID, ssid);
    Serial.printf("PREFS: writeSSID complete - %s\n", ssid);
}
void writeWpaKey(String wpaKey)
{
    prefs.putString(__PREF_WPA_KEY, wpaKey);
    Serial.printf("PREFS: writeWpaKey complete - %s\n", wpaKey);
}

void writeSnmpCommunity(String community)
{
    prefs.putString(__PREF_COMMUNITY, community);
    Serial.printf("PREFS: writeSnmpCommunity complete - %s\n", community);
}

void writeSnmpContact(String contact)
{
    prefs.putString(__PREF_SNMP_SYSCONTACT, contact);
    Serial.printf("PREFS: writeSnmpContact complete - %s\n", contact);
}

void writeSnmpSysName(String sysName)
{
    prefs.putString(__PREF_SNMP_SYSNAME, sysName);
    Serial.printf("PREFS: writeSnmpSysName complete - %s\n", sysName);
}

void writeSnmpLocation(String location)
{
    prefs.putString(__PREF_SNMP_SYSLOCATION, location);
    Serial.printf("PREFS: writeSnmpLocation complete - %s\n", location);
}

void writeHttpUser(String login)
{
    prefs.putString(__PREF_ADMIN_USER, login);
    Serial.printf("PREFS: writeHttpUser complete - %s\n", login);
}

void writeHttpPassword(String password)
{
    prefs.putString(__PREF_ADMIN_PASS, password);
    Serial.printf("PREFS: writeHttpPassword complete - %s\n", password);
}

void writeDhcpMode(bool dhcpMode)
{
    prefs.putBool(__PREF_DHCP_MODE, dhcpMode);
    Serial.printf("PREFS: writeDhcpMode complete - %x\n", dhcpMode);
}

void writeApMode(bool apMode)
{
    prefs.putBool(__PREF_AP_MODE, apMode);
    Serial.printf("PREFS: writeApMode complete - %x\n", apMode);
}

void writeConfigured(bool configured)
{
    prefs.putBool(__PREF_CONFIGURED, configured);
    Serial.printf("PREFS: writeConfigured complete - %x\n", configured);
}

void writeWirelesEnabled(bool wifiMode)
{
    prefs.putBool(__PREF_WIFI_ENABLED, wifiMode);
    Serial.printf("PREFS: writeWirelesEnabled complete - %x\n", wifiMode);
}

/*
 *
 * GETTERS
 *
 */
IPAddress getIPAddress()
{
    IPAddress addr(prefs.getUChar(__PREF_IPADDR_1), prefs.getUChar(__PREF_IPADDR_2), prefs.getUChar(__PREF_IPADDR_3), prefs.getUChar(__PREF_IPADDR_4));
    Serial.printf("PREFS: getIPAddress() returns - %d.%d.%d.%d\n", addr[0], addr[1], addr[2], addr[3]);
    return addr;
}

String getIPString(int type)
{
    switch (type)
    {
    case TYPE_ADDR: // ip address
        return String() + prefs.getUChar(__PREF_IPADDR_1) + "." + prefs.getUChar(__PREF_IPADDR_2) + "." + prefs.getUChar(__PREF_IPADDR_3) + "." + prefs.getUChar(__PREF_IPADDR_4);
    case TYPE_MASK: // mask
        return String() + prefs.getUChar(__PREF_MASK_1) + "." + prefs.getUChar(__PREF_MASK_2) + "." + prefs.getUChar(__PREF_MASK_3) + "." + prefs.getUChar(__PREF_MASK_4);
    case TYPE_GWAY: // gateway
        return String() + prefs.getUChar(__PREF_GATEWAY_1) + "." + prefs.getUChar(__PREF_GATEWAY_2) + "." + prefs.getUChar(__PREF_GATEWAY_3) + "." + prefs.getUChar(__PREF_GATEWAY_4);
    default:
        return String() + "";
    }
}

IPAddress getSubnetMask()
{
    IPAddress addr(prefs.getUChar(__PREF_MASK_1), prefs.getUChar(__PREF_MASK_2), prefs.getUChar(__PREF_MASK_3), prefs.getUChar(__PREF_MASK_4));
    Serial.printf("PREFS: getSubnetMask() returns - %d.%d.%d.%d\n", addr[0], addr[1], addr[2], addr[3]);
    return addr;
}

IPAddress getDefaultGateway()
{
    IPAddress addr(prefs.getUChar(__PREF_GATEWAY_1), prefs.getUChar(__PREF_GATEWAY_2), prefs.getUChar(__PREF_GATEWAY_3), prefs.getUChar(__PREF_GATEWAY_4));
    Serial.printf("PREFS: getDefaultGateway() returns - %d.%d.%d.%d\n", addr[0], addr[1], addr[2], addr[3]);
    return addr;
}


void getHostname(char hostname[__PREF_HOSTNAME_LEN])
{
    if (!prefs.isKey(__PREF_HOSTNAME))
    {
        prefs.putString(__PREF_HOSTNAME, __DEF_HOSTNAME);
    }
    String returnString = prefs.getString(__PREF_HOSTNAME, __DEF_HOSTNAME);
    returnString.toCharArray(hostname, __PREF_HOSTNAME_LEN);
    Serial.printf("PREFS: getHostname() returns - %s\n", hostname);
}

void getSSID(char ssid[__PREF_SSID_LEN])
{
    String returnString = prefs.getString(__PREF_SSID, __DEF_SSID);
    returnString.toCharArray(ssid, __PREF_SSID_LEN);
    Serial.printf("PREFS: getSSID() returns - %s\n", ssid);
}

void getWpaKey(char wpakey[__PREF_WPA_KEY_LEN])
{
    String returnString = prefs.getString(__PREF_WPA_KEY, "");
    returnString.toCharArray(wpakey, __PREF_WPA_KEY_LEN);
    Serial.printf("PREFS: getWpaKey() returns - %s\n", wpakey);
}

void getSnmpCommunity(char community[__PREF_COMMUNITY_LEN])
{
    String returnString = prefs.getString(__PREF_COMMUNITY, __DEF_COMMUNITY);
    returnString.toCharArray(community, __PREF_COMMUNITY_LEN);
    Serial.printf("PREFS: getSnmpCommunity() returns - %s\n", community);
}

void getSnmpContact(char snmpcontact[__PREF_SNMP_SYSCONTACT_LEN])
{
    String returnString = prefs.getString(__PREF_SNMP_SYSCONTACT, __DEF_ADMIN_USER);
    returnString.toCharArray(snmpcontact, __PREF_SNMP_SYSCONTACT_LEN);
    Serial.printf("PREFS: getSnmpContact() returns - %s\n", snmpcontact);
}

void getSnmpSysName(char snmpsysname[__PREF_SNMP_SYSNAME_LEN])
{
    String returnString = prefs.getString(__PREF_SNMP_SYSNAME, __DEF_HOSTNAME);
    returnString.toCharArray(snmpsysname, __PREF_SNMP_SYSNAME_LEN);
    Serial.printf("PREFS: getSnmpSysName() returns - %s\n", snmpsysname);
}

void getSnmpLocation(char snmplocation[__PREF_SNMP_SYSLOCATION_LEN])
{
    String returnString = prefs.getString(__PREF_SNMP_SYSLOCATION, __DEF_SYSLOCATION);
    returnString.toCharArray(snmplocation, __PREF_SNMP_SYSLOCATION_LEN);
    Serial.printf("PREFS: getSnmpLocation() returns - %s\n", snmplocation);
}

void getHttpUser(char httpuser[__PREF_ADMIN_USER_LEN])
{
    String returnString = prefs.getString(__PREF_ADMIN_USER, __DEF_ADMIN_USER);
    returnString.toCharArray(httpuser, __PREF_ADMIN_USER_LEN);
    Serial.printf("PREFS: getHttpUser() returns - %s\n", httpuser);
}

void getHttpPassword(char httppassword[__PREF_ADMIN_PASS_LEN])
{
    String returnString = prefs.getString(__PREF_ADMIN_PASS, __DEF_ADMIN_PASS);
    returnString.toCharArray(httppassword, __PREF_ADMIN_PASS_LEN);
    Serial.printf("PREFS: getHttpPassword() returns - %s\n", httppassword);
}

bool getConfigured()
{
    bool ret = prefs.getBool(__PREF_CONFIGURED, true);
    Serial.printf("PREFS: getConfigured() returns - %d\n", ret);
    return ret;
}

bool getDhcpMode()
{
    bool ret = prefs.getBool(__PREF_DHCP_MODE);
    Serial.printf("PREFS: getDhcpMode() returns - %d\n", ret);

    return ret;
}

bool getApMode()
{
    bool ret = prefs.getBool(__PREF_AP_MODE);
    //  Serial.printf("PREFS: getApMode() returns - %d\n", ret);
    return ret;
}

bool getWifiEnabled()
{
    bool ret = prefs.getBool(__PREF_WIFI_ENABLED, true);
    Serial.printf("PREFS: getWifiEnabled() returns - %d\n", ret);
    return ret;
}
/*
 *
 * DEFAULT WRITING
 *
 */
void resetToDefaults(bool reboot)
{
    Serial.println("Performing hard reset!");
    prefs.clear();
    clearSnmpMap();

    Serial.println("Resetting TCP/IP parameters");
    // IP Octets
    writeIPAddress(__DEF_IPADDR);

    // Subnet mask
    writeSubnetMask(__DEF_MASK);

    // Gateway
    writeDefaultGateway(__DEF_GATEWAY);

    Serial.println("Resetting hostname");

    // Host Name
    writeHostname(__DEF_HOSTNAME);

    Serial.println("Resetting Wifi parameters");

    // SSID
    writeSSID(__DEF_SSID);

    // WPA PSK
    writeWpaKey("");

    Serial.println("Resetting SNMP parameters");
    // Community String
    writeSnmpCommunity(__DEF_COMMUNITY);

    // SNMP Contact
    writeSnmpContact(__DEF_ADMIN_USER);

    // SNMP System Name
    writeSnmpSysName(__DEF_HOSTNAME);

    // SNMP Location
    writeSnmpLocation(__DEF_SYSLOCATION);

    Serial.println("Resetting authentication");

    // HTTP Credentials
    writeHttpPassword(__DEF_ADMIN_PASS);
    writeHttpUser(__DEF_ADMIN_USER);

    Serial.println("Setting boolean flags");

    // Boolean values
    writeApMode(__DEF_AP_MODE);
    writeDhcpMode(__DEF_DHCP_MODE);
    writeWirelesEnabled(__DEF_WIFI_ENABLED);

    // Save and reboot
    commitToPrefs(reboot);
}

void commitToPrefs(bool reboot)
{

    Serial.println("Committing to Preferences");
    writeConfigured(true);
    prefs.end();
    snmpMap.end();
    if (reboot)
    {
        Serial.println("Delay 3 sec before rebooting");
        delay(3000);
        Serial.println("Rebooting ESP");
        ESP.restart();
    }
    else
    {
        Serial.println("...Not rebooting, reloading preferences");
        initPreferences();
    }
}

int getSnmpIndex(char *serialNum)
{
    Serial.printf("Getting ID %s from preferences\n", serialNum);
    int id = 0;

    for (int i = 1; i <= __SENSOR_CONFIG_LIMIT; i++)
    {
        char *c = (char *)malloc(5);
        sprintf(c, "S_%d", i);
        Serial.printf("%s\n", c);
        String s = snmpMap.getString(c, "_NOTFOUND_");
        free(c);
        if (s.compareTo(serialNum) == 0)
        {
            Serial.printf("Found ID %d\n", i);
            id = i;
            break;
        }
    }
    if (id == 0)
    {
        Serial.println("ID not found, generating new");
        id = getNextId();
    }

    Serial.printf("Mapped %s to %d\n", serialNum, id);
    char *c = (char *)malloc(5);
    sprintf(c, "S_%d", id);
    snmpMap.putString(c, serialNum);
    free(c);
    return id;
}

void clearSnmpMap()
{
    Serial.println("Clearing all entries from SNMP map");
    snmpMap.clear();
}

int getNextId()
{
    for (int i = 1; i <= __SENSOR_CONFIG_LIMIT; i++)
    {
        char *c = (char *)malloc(5);
        sprintf(c, "S_%d", i);
        Serial.printf("%s\n", c);
        String s = snmpMap.getString(c, "_NOTFOUND_");
        free(c);
        if (s.compareTo("_NOTFOUND_") == 0)
        {
            Serial.printf("  Found next spare id: %d\n", i);
            return i;
        }
    }
    Serial.println("  All positions allocated, not allocating SNMP OID");
    return -1;
}

void getFriendlyName(char *friendlyName, String serial)
{
    char keyname[15];
    strncpy(keyname, serial.c_str() + 2, 15);

    if (!snmpMap.isKey(keyname))
    {
        snmpMap.putString(keyname, serial.c_str());
    }
    String s = snmpMap.getString(keyname, serial);
    s.toCharArray(friendlyName, 40);
}

void writeFriendlyName(char *friendlyname, char *serial)
{
    char keyname[15];
    strncpy(keyname, serial + 2, 15);
    snmpMap.putString(keyname, friendlyname);
    Serial.printf("PREFS: writeFriendlyName complete - %s\n", friendlyname);
}