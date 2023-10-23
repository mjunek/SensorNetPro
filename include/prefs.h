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

#ifndef PREFS_H
#define PREFS_H

#include <WiFi.h>  
#include <Preferences.h>

#define __FIRMWARE_VERSION "1.0.0"


// Maximum number of sensors supported by the system concurrently
#define __MAX_SENSOR_COUNT 10

// Maximum number of sensors that can be configured (but not connected) 
#define __SENSOR_CONFIG_LIMIT 20

// Resolution for the DS18B20 sensors, can be 9, 10, 11 or 12
// Conversion Rate - important when considering max sensor count and the cycle time to ensure they can all be read
// 9 bit = 94ms, 10 bit = 188ms, 11 bit = 375ms, 12 bit = 750ms
// ºC resolution of each bit setting
// 9 bit = 0.5ºC, 10 bit = 0.25ºC, 11 bit = 0.125ºC, 12 bit = 0.0625ºC
#define __DS18B20_RESOLUTION 10


// Default values

// TCPIP Parmaters
// Addresses should be specified using commas as they get passed to the writeXXXX functions as parameters
#define __DEF_IPADDR 192, 168, 4, 1
#define __DEF_MASK 255, 255, 255, 0
#define __DEF_GATEWAY 192, 168, 4, 1
#define __DEF_DNS1 8, 8, 8, 8
#define __DEF_DNS2 8, 8, 4, 4

// Hostname
#define __DEF_HOSTNAME "SensorNetPro"

// Wifi AP SSID
#define __DEF_SSID "SensorNetPro"

// AP Mode = 1, Station Mode = 0
#define __DEF_MODE 1

// SNMP Community
#define __DEF_COMMUNITY "public"

// SNMP Location
#define __DEF_SYSLOCATION "SNMP Location"

// Admin Credentials
#define __DEF_ADMIN_USER "admin"
#define __DEF_ADMIN_PASS "admin"

// true = Enable DHCP, false = Manual IP
#define __DEF_DHCP_MODE true

// true = Wirelress AP Mode, false = Wireless Client mode
#define __DEF_AP_MODE true

// true = Wifi Enabled, 0 = Wifi Disabled
#define __DEF_WIFI_ENABLED true

/*
    Preference Location Names
    For string based items, there is also the maximum length. This includes the terminating null and is also used elsewhere
    in the program to define the array sizes needed
*/

#define __PREF_DHCP_MODE "dhcp"
#define __PREF_AP_MODE "ap"
#define __PREF_CONFIGURED "confdone"
#define __PREF_WIFI_ENABLED "wifi"

#define __PREF_IPADDR_1 "ip1"
#define __PREF_IPADDR_2 "ip2"
#define __PREF_IPADDR_3 "ip3"
#define __PREF_IPADDR_4 "ip4"

#define __PREF_MASK_1 "sn1"
#define __PREF_MASK_2 "sn2"
#define __PREF_MASK_3 "sn3"
#define __PREF_MASK_4 "sn4"

#define __PREF_GATEWAY_1 "gw1"
#define __PREF_GATEWAY_2 "gw2"
#define __PREF_GATEWAY_3 "gw3"
#define __PREF_GATEWAY_4 "gw4"

#define __PREF_DNS1_1 "dnsp1"
#define __PREF_DNS1_2 "dnsp2"
#define __PREF_DNS1_3 "dnsp3"
#define __PREF_DNS1_4 "dnsp4"
 
#define __PREF_DNS2_1 "dnss1"
#define __PREF_DNS2_2 "dnss2"
#define __PREF_DNS2_3 "dnss3"
#define __PREF_DNS2_4 "dnss4"

#define __PREF_HOSTNAME "host"
#define __PREF_HOSTNAME_LEN 31

#define __PREF_SSID "ssid"
#define __PREF_SSID_LEN 31

#define __PREF_WPA_KEY "wpa"
#define __PREF_WPA_KEY_LEN 31

#define __PREF_COMMUNITY "comm"
#define __PREF_COMMUNITY_LEN 31

#define __PREF_SNMP_SYSCONTACT "contact"
#define __PREF_SNMP_SYSCONTACT_LEN 31

#define __PREF_SNMP_SYSNAME "sysname"
#define __PREF_SNMP_SYSNAME_LEN 31

#define __PREF_SNMP_SYSLOCATION "sysloc"
#define __PREF_SNMP_SYSLOCATION_LEN 31

#define __PREF_ADMIN_USER "user"
#define __PREF_ADMIN_USER_LEN 31

#define __PREF_ADMIN_PASS "pass"
#define __PREF_ADMIN_PASS_LEN 31

#define TYPE_ADDR 1
#define TYPE_MASK 2
#define TYPE_GWAY 3
#define TYPE_DNS1 4
#define TYPE_DNS2 5


void writeIPAddress(int octet1, int octet2, int octet3, int octet4);
void writeSubnetMask(int octet1, int octet2, int octet3, int octet4);
void writeDefaultGateway(int octet1, int octet2, int octet3, int octet4);
void writePrimaryDNS(int octet1, int octet2, int octet3, int octet4);
void writeSecondaryDNS(int octet1, int octet2, int octet3, int octet4);
void writeHostname(String hostname);
void writeSSID(String ssid);
void writeWpaKey(String wpaKey);
void writeSnmpCommunity(String community);
void writeSnmpContact(String contact);
void writeSnmpSysName(String sysName);
void writeSnmpLocation(String location);
void writeHttpUser(String login);
void writeHttpPassword(String password);
void writeDhcpMode(bool dhcpMode);
void writeApMode(bool apMode);
void writeConfigured(bool configured);
void writeWirelesEnabled(bool wifiMode);

IPAddress getIPAddress();
IPAddress getSubnetMask();
IPAddress getDefaultGateway();
IPAddress getPrimaryDNS();
IPAddress getSecondaryDNS();

void getHostname(char hostname[__PREF_HOSTNAME_LEN]);
void getSSID(char ssid[__PREF_SSID_LEN]);
void getWpaKey(char wpakey[__PREF_WPA_KEY_LEN]);
void getSnmpCommunity(char community[__PREF_COMMUNITY_LEN]);
void getSnmpContact(char snmpcontact[__PREF_SNMP_SYSCONTACT_LEN]);
void getSnmpSysName(char snmpsysname[__PREF_SNMP_SYSNAME_LEN]);
void getSnmpLocation(char snmplocation[__PREF_SNMP_SYSLOCATION_LEN]);
void getHttpUser(char httpuser[__PREF_ADMIN_USER_LEN]);
void getHttpPassword (char httppassword[__PREF_ADMIN_PASS_LEN]);


bool getConfigured();
bool getDhcpMode();
bool getApMode();
bool getWifiEnabled();
String getIPString(int type);

void resetToDefaults(bool reboot);
void initPreferences();
void commitToPrefs(bool reboot);


int getSnmpIndex(char* serialNum);
int getNextId();
void clearSnmpMap();
void getFriendlyName(char* friendlyName, String serial);
void writeFriendlyName(char *friendlyname, char *serial);


#endif //PREFS_H