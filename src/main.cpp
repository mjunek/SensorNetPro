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

#include <WiFi.h>              // ESP32 Core WiFi Library
#include <ETH.h>               // Ethernet library
#include <SNMP_Agent.h>        // SNMP routines
#include <SPIFFS.h>            // SPI Flash Filesystem for Web Server
#include <ESPAsyncWebServer.h> // WebServer
#include <AsyncJson.h>         // JSON Support for WebServer
#include <ArduinoJson.h>       // JSON Support for Program
#include <ESPmDNS.h>           // MDNS Responder

#include "prefs.h"       // Preferences Lib
#include "snmp.h"        // SNMP Lib
#include "sensor.h"      // Generic sensor interface
#include "ds18b20.h"     // DS18B20 temperature sensors

uint32_t uptime, prevPoll;
int snmpSensorValue[__MAX_SENSOR_COUNT], snmpSensorStatus[__MAX_SENSOR_COUNT], countSensors;
bool eth_connected, dhcp_on, ap_on, wifi_on, isConfigured, resetNeeded, wifiConnected;
bool statusLed = false;
bool settingsRebootNeeded = false;
uint32_t buttonPressedAt = 0; // millis() when the current runtime button press started, 0 if not pressed
uint32_t rebootRequestedAt = 0; // millis() when a reboot was requested from the web UI, 0 if not requested
uint32_t readingStartedAt = 0; // millis() when the sensors were asked for a reading, 0 if none is pending
uint32_t readingWaitTime = 0;  // ms to wait after readingStartedAt before collecting the readings
bool fallbackApActive = false;
char fallbackSsid[__PREF_SSID_LEN];
uint32_t lastWifiRetry = 0;

char *ch_httpAdminUser, *ch_httpAdminPass, *ch_systemHostname, *ch_wifiSSID, *ch_wpaPsk, *ch_snmpCommunity, *ch_snmpSysName, *ch_snmpLocation, *ch_snmpSysContact;

String firmwareVersion = __FIRMWARE_VERSION;
String dhcpLabel, sensorCountLabel, ethLabel, wifiLabel, etherSpeed;

// The supported sensor types. Sensors are discovered in this order, up to __MAX_SENSOR_COUNT in all.
Ds18b20Sensors thermalSensors(ONE_WIRE_BUS, __DS18B20_RESOLUTION);
SensorType *sensorTypes[] = {&thermalSensors};
Sensor sensorList[__MAX_SENSOR_COUNT];
AsyncWebServer server(80);

// Guards state shared between loop() and the async web / WiFi event tasks:
// the SNMP agent, preferences, the ch_* config buffers and the sensor arrays.
SemaphoreHandle_t stateMutex;

class StateLock
{
public:
  StateLock() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~StateLock() { xSemaphoreGive(stateMutex); }
};

// The API is limited to changing the admin password until the factory default is replaced
bool adminPasswordIsDefault()
{
  return strcmp(ch_httpAdminPass, __DEF_ADMIN_PASS) == 0;
}

bool isAuthenticated(AsyncWebServerRequest *request)
{
  StateLock lock;
  return request->authenticate(ch_httpAdminUser, ch_httpAdminPass);
}

void loadPreferences(bool bypassInit)
{
  Serial.println("Loading Preferences");
  if (!bypassInit)
  {
    initPreferences();
    isConfigured = getConfigured();

    delay(200);
    // Only a power-on or hardware reset can start a factory reset. A software restart (web UI or
    // the runtime button hold) never does, even if the button is still held as it reboots.
    if (digitalRead(KEY_RESET) == 0 && esp_reset_reason() == ESP_RST_SW)
    {
      Serial.println("Button held after a software restart. Ignoring it, not resetting settings");
    }
    else if (digitalRead(KEY_RESET) == 0)
    {
      digitalWrite(STATUS_LED, 0);
      digitalWrite(ERROR_LED, 0);
      Serial.println("Factory reset button held at startup. Resetting after 5 seconds");
      bool done = false;
      int count = 0;
      while (!done)
      {
        if (digitalRead(KEY_RESET) != 0)
        {
          done = true;
        }
        Serial.println("...keep holding button");
        bool led = count % 2;
        digitalWrite(STATUS_LED, led);
        digitalWrite(ERROR_LED, led);
        delay(500);
        if (++count >= 10)
        {
          digitalWrite(STATUS_LED, 1);
          digitalWrite(ERROR_LED, 1);
          resetToDefaults(true);
        }
      }
      Serial.println("Button was released. Not resetting");
      digitalWrite(STATUS_LED, 1);
      digitalWrite(ERROR_LED, 1);
    }
    if (!isConfigured)
    {
      Serial.println("No configuration stored - performing hard reset");
      digitalWrite(ERROR_LED, 1);
      digitalWrite(STATUS_LED, 0);
      resetToDefaults(true);
    }
  }
  dhcp_on = getDhcpMode();
  ap_on = getApMode();
  wifi_on = getWifiEnabled();
  getSnmpCommunity(ch_snmpCommunity);
  getHttpUser(ch_httpAdminUser);
  getHostname(ch_systemHostname);
  getHttpPassword(ch_httpAdminPass);
  getSSID(ch_wifiSSID);
  getWpaKey(ch_wpaPsk);
  getSnmpContact(ch_snmpSysContact);
  getSnmpSysName(ch_snmpSysName);
  getSnmpLocation(ch_snmpLocation);
}

void WiFiEvent(WiFiEvent_t event)
{
  StateLock lock;
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    Serial.println("Ethernet Started");
    ETH.setHostname(ch_systemHostname);
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.print("Ethernet Link Up > MAC: ");
    Serial.print(ETH.macAddress());
    if (ETH.fullDuplex())
    {
      Serial.print(", FULL_DUPLEX");
    }
    Serial.print(", ");
    Serial.print(ETH.linkSpeed());
    Serial.println("Mbps");
    etherSpeed = String() + ETH.linkSpeed() + "/" + (ETH.fullDuplex() ? "Full" : "Half");
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    Serial.print("Ethernet IP Assigned > ");
    Serial.print("IPv4: ");
    Serial.println(ETH.localIP());
    eth_connected = true;
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED:
    Serial.println("Ethernet Link Down");
    etherSpeed = "NC";
    eth_connected = false;
    break;
  case ARDUINO_EVENT_ETH_STOP:
    Serial.println("Ethernet Stopped");
    etherSpeed = "NC";

    eth_connected = false;
    break;
  case ARDUINO_EVENT_WIFI_STA_START:
    Serial.println("Wifi Started: Station Mode");
    WiFi.setHostname(ch_systemHostname);
    break;
  case ARDUINO_EVENT_WIFI_STA_STOP:
    Serial.println("Wifi Stopped: Station Mode");
    wifiConnected = false;
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Serial.println("Wifi Associated to AP");
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    wifiConnected = false;
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.print("Wifi IP Assigned > ");
    Serial.print("IPv4: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
    break;

  case ARDUINO_EVENT_WIFI_AP_START:
    Serial.println("Wifi Started: Access Point Mode");
    WiFi.setHostname(ch_systemHostname);
    break;

  case ARDUINO_EVENT_WIFI_AP_STOP:
    Serial.println("Wifi Stopped: Access Point Mode");
    wifiConnected = false;
    break;

  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    Serial.println("Wifi AP: Client Associated");
    break;

  case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    Serial.println("Wifi AP: Client Disassociated");
    // Only drop the connected state when the last client has gone
    if (ap_on)
      wifiConnected = WiFi.softAPgetStationNum() > 0;
    break;

  case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
    Serial.println("Wifi AP: Client Assigned IP");
    if (ap_on)
      wifiConnected = true;
    break;

  default:
    break;
  }
  digitalWrite(WIFI_LED, (wifiConnected || eth_connected));
}

// Find the connected sensors of every type, and give each a row in its type's SNMP table
void discoverSensors()
{
  countSensors = 0;
  for (SensorType *type : sensorTypes)
  {
    int found = type->discover(__MAX_SENSOR_COUNT - countSensors);
    for (int j = 0; j < found; j++)
    {
      Sensor *sensor = &sensorList[countSensors];
      sensor->type = type;
      sensor->typeIndex = j;
      type->serial(j, sensor->serial, SENSOR_SERIAL_LEN);
      Serial.printf("Sensor: %s (%s)\n", sensor->serial, type->name());
      getFriendlyName(sensor->name, sensor->serial);
      snmpSensorStatus[countSensors] = SENSOR_STATUS_ERROR; // until the first good reading
      addSensorHandler(type->snmpTable(), sensor->serial, sensor->name, &snmpSensorValue[countSensors],
                       &snmpSensorStatus[countSensors], countSensors);
      countSensors++;
    }
  }
  Serial.printf("Using %d sensors\n", countSensors);
}

// Start a reading on every sensor type. The readings run in the background, and loop() collects
// them with updateSensorData() once the slowest type is ready.
void requestSensorData()
{
  Serial.println("Requesting sensor data");
  readingWaitTime = 0;
  for (SensorType *type : sensorTypes)
    readingWaitTime = max(readingWaitTime, type->startReading());
  readingStartedAt = millis() | 1; // never 0 while pending
}

void updateSensorData()
{
  readingStartedAt = 0;
  Serial.print("Sensor data: ");
  StateLock lock;
  for (int i = 0; i < countSensors; i++)
  {
    if (!sensorList[i].type->reading(sensorList[i].typeIndex, &snmpSensorValue[i]))
    {
      snmpSensorStatus[i] = SENSOR_STATUS_ERROR;
      Serial.printf("%d=ERR ", i);
      continue;
    }
    snmpSensorStatus[i] = SENSOR_STATUS_OK;
    Serial.printf("%d=%.2f ", i, snmpSensorValue[i] / 100.0f);
  }
  Serial.println();
}

/*
 * Input validation for the JSON API. The web UI applies the same rules, but the API can't
 * rely on it: everything saved here ends up in NVS, the SNMP agent and the web page.
 */
#define __SENSOR_NAME_MAX_LEN 35

void apiError(JsonDocument *jsonDoc, String *responseString, const char *message)
{
  (*jsonDoc)["error"] = true;
  (*jsonDoc)["errorMessage"] = message;
  serializeJson(*jsonDoc, *responseString);
}

bool isLengthBetween(const String &value, unsigned int minLen, unsigned int maxLen)
{
  return value.length() >= minLen && value.length() <= maxLen;
}

// Printable ASCII only, so stored values can't carry control characters into logs, SNMP or the UI
bool isPrintableAscii(const String &value)
{
  for (unsigned int i = 0; i < value.length(); i++)
  {
    if (value[i] < 0x20 || value[i] > 0x7e)
      return false;
  }
  return true;
}

// Single RFC 1123 label: letters, digits and hyphens, not starting or ending with a hyphen
bool isValidHostname(const String &hostname)
{
  if (!isLengthBetween(hostname, 1, __PREF_HOSTNAME_LEN - 1) || hostname[0] == '-' || hostname[hostname.length() - 1] == '-')
    return false;
  for (unsigned int i = 0; i < hostname.length(); i++)
  {
    if (!isalnum(hostname[i]) && hostname[i] != '-')
      return false;
  }
  return true;
}

// 3 to 10 lowercase letters, matching the web UI
bool isValidAdminUser(const String &user)
{
  if (!isLengthBetween(user, 3, 10))
    return false;
  for (unsigned int i = 0; i < user.length(); i++)
  {
    if (user[i] < 'a' || user[i] > 'z')
      return false;
  }
  return true;
}

// Letters and digits at both ends, with @ - / _ = + . and spaces allowed in between, matching the web UI
bool isValidSensorName(const String &name)
{
  if (!isLengthBetween(name, 2, __SENSOR_NAME_MAX_LEN) || !isalnum(name[0]) || !isalnum(name[name.length() - 1]))
    return false;
  for (unsigned int i = 0; i < name.length(); i++)
  {
    if (!isalnum(name[i]) && !strchr("@-/_=+. ", name[i]))
      return false;
  }
  return true;
}

uint32_t ipToUint(const IPAddress &ip)
{
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}

// Checks the Ethernet static IP settings. An empty gateway is allowed and stored as 0.0.0.0.
// Returns nullptr if valid, otherwise the error message.
const char *validateStaticIp(const String &ipString, const String &maskString, const String &gatewayString,
                             IPAddress &ip, IPAddress &mask, IPAddress &gateway)
{
  if (!ip.fromString(ipString) || ip[0] == 0)
    return "Invalid IP address";
  uint32_t maskValue;
  if (!mask.fromString(maskString) || (maskValue = ipToUint(mask)) == 0 || (~maskValue & (~maskValue + 1)) != 0)
    return "Invalid subnet mask";
  uint32_t ipValue = ipToUint(ip);
  if ((ipValue & ~maskValue) == 0 || (ipValue | maskValue) == 0xFFFFFFFF)
    return "IP address cannot be the subnet or broadcast address";
  gateway = IPAddress(0, 0, 0, 0);
  if (gatewayString == "")
    return nullptr;
  if (!gateway.fromString(gatewayString))
    return "Invalid gateway";
  uint32_t gatewayValue = ipToUint(gateway);
  if ((gatewayValue & maskValue) != (ipValue & maskValue))
    return "Gateway must be on the same subnet as the IP address";
  if ((gatewayValue & ~maskValue) == 0 || (gatewayValue | maskValue) == 0xFFFFFFFF || gatewayValue == ipValue)
    return "Gateway cannot be the subnet, broadcast or device address";
  return nullptr;
}

void apiReboot(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  serializeJson(jsonDoc, *responseString);
  // Restart from loop() once the response has gone out, rather than from inside the request handler
  rebootRequestedAt = millis() | 1; // never 0 once requested
}

void apiSensorData(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  // Each type's sensors are listed under "<type>SensorData", such as thermalSensorData
  for (int i = 0; i < countSensors; i++)
  {
    JsonObject sensor = jsonDoc[String(sensorList[i].type->name()) + "SensorData"].add<JsonObject>();
    sensor["id"] = i + 1;
    sensor["address"] = sensorList[i].serial;
    sensor["name"] = sensorList[i].name;
    sensor["reading"] = (float)snmpSensorValue[i] / 100.0f;
    sensor["ok"] = (snmpSensorStatus[i] == SENSOR_STATUS_OK);
  }

  serializeJson(jsonDoc, *responseString);
}

void apiSystemStats(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";

  int uptimeSec = uptime / 100;
  int i_days = (uptimeSec / 86400);
  int remainder = uptimeSec - (i_days * 86400);
  int i_hours = remainder / 3600;
  remainder = remainder - (i_hours * 3600);
  int i_minutes = remainder / 60;
  int i_seconds = remainder - (i_minutes * 60);

  String runTime = String() + i_days + "d " + i_hours + "h " + i_minutes + "m " + i_seconds + "s";
  String runningWifiIP;
  if (ap_on)
  {
    runningWifiIP = String() + WiFi.softAPIP()[0] + "." + WiFi.softAPIP()[1] + "." + WiFi.softAPIP()[2] + "." + WiFi.softAPIP()[3];
  }
  else
  {
    runningWifiIP = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
  }

  String runningEthIP = String() + ETH.localIP()[0] + "." + ETH.localIP()[1] + "." + ETH.localIP()[2] + "." + ETH.localIP()[3];
  String configuredIP = getIPString(TYPE_ADDR);
  String configuredMask = getIPString(TYPE_MASK);
  String configuredGateway = getIPString(TYPE_GWAY);

  String RSSI = String() + WiFi.RSSI() + " dB";
  String hostname = String(ch_systemHostname);
  if (dhcp_on)
    dhcpLabel = "ON";
  else
    dhcpLabel = "OFF";
  if (eth_connected)
    ethLabel = "Connected";
  else
    ethLabel = "Disconnected";
  if (wifiConnected)
    wifiLabel = "Connected";
  else
    wifiLabel = "Disconnected";

  jsonDoc["uptime"] = runTime;
  jsonDoc["version"] = __FIRMWARE_VERSION;
  jsonDoc["wifiIp"] = runningWifiIP;
  jsonDoc["ethIp"] = runningEthIP;
  jsonDoc["rssi"] = RSSI;
  jsonDoc["hostname"] = hostname;
  jsonDoc["wifiStatus"] = wifiConnected;
  jsonDoc["ethStatus"] = eth_connected;
  jsonDoc["wifiEnabled"] = wifi_on;
  jsonDoc["dhcpEnabled"] = dhcp_on;
  jsonDoc["wifiApMode"] = ap_on;
  jsonDoc["etherSpeed"] = etherSpeed;
  jsonDoc["wifiFallbackAp"] = fallbackApActive;
  if (fallbackApActive)
  {
    jsonDoc["wifiFallbackSsid"] = fallbackSsid;
    jsonDoc["wifiFallbackIp"] = WiFi.softAPIP().toString();
  }

  serializeJson(jsonDoc, *responseString);
}

void apiGetConfiguration(JsonDocument *jsonDoc, bool network, bool snmp, bool admin)
{
  (*jsonDoc)["error"] = false;
  (*jsonDoc)["errorMessage"] = "Success";
  if (network)
  {
    (*jsonDoc)["networkHostname"] = ch_systemHostname;
    (*jsonDoc)["networkWifiEnabled"] = wifi_on;
    (*jsonDoc)["networkDhcpEnabled"] = dhcp_on;
    (*jsonDoc)["networkWifiApMode"] = ap_on;
    (*jsonDoc)["networkIP"] = getIPString(TYPE_ADDR);
    (*jsonDoc)["networkMask"] = getIPString(TYPE_MASK);
    (*jsonDoc)["networkGateway"] = getIPString(TYPE_GWAY);
    (*jsonDoc)["networkSSID"] = ch_wifiSSID;
  }
  if (admin)
  {
    (*jsonDoc)["adminUser"] = ch_httpAdminUser;
    (*jsonDoc)["adminPasswordChangeRequired"] = adminPasswordIsDefault();
  }
  if (snmp)
  {
    (*jsonDoc)["snmpLocation"] = ch_snmpLocation;
    (*jsonDoc)["snmpSysName"] = ch_snmpSysName;
    (*jsonDoc)["snmpContact"] = ch_snmpSysContact;
  }
}

void apiSaveAdminConfiguration(String *responseString, JsonObject *jsonRequest)
{

  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";

  if (not(*jsonRequest)["adminUser"].is<String>() || not(*jsonRequest)["modifyAdminPass"].is<bool>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  bool modifyAdminPass = (*jsonRequest)["modifyAdminPass"].as<bool>();
  if (!modifyAdminPass && adminPasswordIsDefault())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "The default admin password must be changed";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  if (modifyAdminPass && not(*jsonRequest)["adminPassword"].is<String>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  String adminUser = (*jsonRequest)["adminUser"].as<String>();
  if (!isValidAdminUser(adminUser))
    return apiError(&jsonDoc, responseString, "User name must be 3 to 10 lowercase letters");

  String adminPassword = (*jsonRequest)["adminPassword"].as<String>();
  if (modifyAdminPass && (!isLengthBetween(adminPassword, 8, __PREF_ADMIN_PASS_LEN - 1) || !isPrintableAscii(adminPassword)))
    return apiError(&jsonDoc, responseString, "Password must be 8 to 30 printable ASCII characters");
  if (modifyAdminPass && adminPassword.equalsIgnoreCase(adminUser))
    return apiError(&jsonDoc, responseString, "Password cannot match the user name");

  if (modifyAdminPass)
    writeHttpPassword(adminPassword);
  writeHttpUser(adminUser);

  commitToPrefs(false);
  loadPreferences(true);
  apiGetConfiguration(&jsonDoc, false, false, true);
  serializeJson(jsonDoc, *responseString);
}

void apiSaveNetworkConfiguration(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";

  if (
      not(*jsonRequest)["networkHostname"].is<String>() || not(*jsonRequest)["networkIP"].is<String>() || not(*jsonRequest)["networkMask"].is<String>() || not(*jsonRequest)["networkGateway"].is<String>() || not(*jsonRequest)["networkSSID"].is<String>() || not(*jsonRequest)["networkWpaKey"].is<String>() || not(*jsonRequest)["networkWifi"].is<bool>() || not(*jsonRequest)["networkApMode"].is<bool>() || not(*jsonRequest)["networkDhcp"].is<bool>() || not(*jsonRequest)["modifyWpaKey"].is<bool>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  bool modifyWpaKey = (*jsonRequest)["modifyWpaKey"].as<bool>();
  bool networkWifi = (*jsonRequest)["networkWifi"].as<bool>();
  bool networkDhcp = (*jsonRequest)["networkDhcp"].as<bool>();
  bool networkApMode = (*jsonRequest)["networkApMode"].as<bool>();
  String networkHostname = (*jsonRequest)["networkHostname"].as<String>();
  String networkIP = (*jsonRequest)["networkIP"].as<String>();
  String networkMask = (*jsonRequest)["networkMask"].as<String>();
  String networkGateway = (*jsonRequest)["networkGateway"].as<String>();
  String networkSSID = (*jsonRequest)["networkSSID"].as<String>();
  String networkWpaKey = (*jsonRequest)["networkWpaKey"].as<String>();

  if (!isValidHostname(networkHostname))
    return apiError(&jsonDoc, responseString, "Hostname must be 1 to 30 letters, digits or hyphens, and cannot start or end with a hyphen");
  if (!isLengthBetween(networkSSID, networkWifi ? 1 : 0, min(32, __PREF_SSID_LEN - 1)) || !isPrintableAscii(networkSSID))
    return apiError(&jsonDoc, responseString, "Wi-Fi SSID must be 1 to 30 printable ASCII characters");
  if (modifyWpaKey && networkWpaKey != "" && (!isLengthBetween(networkWpaKey, 8, min(63, __PREF_WPA_KEY_LEN - 1)) || !isPrintableAscii(networkWpaKey)))
    return apiError(&jsonDoc, responseString, "Invalid WPA key length or characters");

  // With DHCP on, the static settings are kept for later but only saved if they parse
  IPAddress ip, mask, gw;
  const char *ipError = validateStaticIp(networkIP, networkMask, networkGateway, ip, mask, gw);
  if (!networkDhcp && ipError != nullptr)
    return apiError(&jsonDoc, responseString, ipError);

  if (modifyWpaKey)
  {
    writeWpaKey(networkWpaKey);
  }
  writeApMode(networkApMode);
  writeDhcpMode(networkDhcp);
  writeHostname(networkHostname);
  writeSSID(networkSSID);
  writeWirelessEnabled(networkWifi);

  if (ipError == nullptr)
  {
    writeIPAddress(ip[0], ip[1], ip[2], ip[3]);
    writeSubnetMask(mask[0], mask[1], mask[2], mask[3]);
    writeDefaultGateway(gw[0], gw[1], gw[2], gw[3]);
  }

  commitToPrefs(false);
  loadPreferences(true);
  apiGetConfiguration(&jsonDoc, true, false, false);
  jsonDoc["restartRequired"] = true;
  settingsRebootNeeded = true;
  serializeJson(jsonDoc, *responseString);
}

void apiSaveSnmpConfiguration(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";

  if (not(*jsonRequest)["snmpSysName"].is<String>() || not(*jsonRequest)["snmpLocation"].is<String>() || not(*jsonRequest)["snmpContact"].is<String>() || not(*jsonRequest)["modifyCommunity"].is<bool>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  bool modifyCommunity = (*jsonRequest)["modifyCommunity"].as<bool>();
  if (modifyCommunity && not(*jsonRequest)["snmpCommunity"].is<String>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  String snmpCommunity = (*jsonRequest)["snmpCommunity"].as<String>();
  String snmpSysName = (*jsonRequest)["snmpSysName"].as<String>();
  String snmpLocation = (*jsonRequest)["snmpLocation"].as<String>();
  String snmpContact = (*jsonRequest)["snmpContact"].as<String>();

  if (modifyCommunity && (!isLengthBetween(snmpCommunity, 1, __PREF_COMMUNITY_LEN - 1) || !isPrintableAscii(snmpCommunity)))
    return apiError(&jsonDoc, responseString, "Community string must be 1 to 30 printable ASCII characters");
  if (!isLengthBetween(snmpSysName, 0, __PREF_SNMP_SYSNAME_LEN - 1) || !isPrintableAscii(snmpSysName))
    return apiError(&jsonDoc, responseString, "System name must be at most 30 printable ASCII characters");
  if (!isLengthBetween(snmpLocation, 0, __PREF_SNMP_SYSLOCATION_LEN - 1) || !isPrintableAscii(snmpLocation))
    return apiError(&jsonDoc, responseString, "Location must be at most 30 printable ASCII characters");
  if (!isLengthBetween(snmpContact, 0, __PREF_SNMP_SYSCONTACT_LEN - 1) || !isPrintableAscii(snmpContact))
    return apiError(&jsonDoc, responseString, "Contact must be at most 30 printable ASCII characters");

  if (modifyCommunity)
    writeSnmpCommunity(snmpCommunity);
  writeSnmpContact(snmpContact);
  writeSnmpSysName(snmpSysName);
  writeSnmpLocation(snmpLocation);

  commitToPrefs(false);

  jsonDoc["restartRequired"] = true;
  settingsRebootNeeded = true;
  loadPreferences(true);

  apiGetConfiguration(&jsonDoc, false, true, false);
  serializeJson(jsonDoc, *responseString);
}

void apiSaveThermalSensorConfiguration(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";

  if (not(*jsonRequest)["sensorCount"].is<int>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  int sensorCount = (*jsonRequest)["sensorCount"].as<int>();
  if (sensorCount < 0 || sensorCount > __MAX_SENSOR_COUNT)
    return apiError(&jsonDoc, responseString, "Invalid sensor count");
  for (int i = 1; i <= sensorCount; i++)
  {
    if (not(*jsonRequest)["sensorName_" + String(i)].is<String>() || not(*jsonRequest)["sensorSerial_" + String(i)].is<String>())
    {
      jsonDoc["error"] = true;
      jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
      continue;
    }
    String sensAddr = (*jsonRequest)["sensorSerial_" + String(i)].as<String>();
    String sensName = (*jsonRequest)["sensorName_" + String(i)].as<String>();
    if (!isValidSensorName(sensName))
    {
      jsonDoc["error"] = true;
      jsonDoc["errorMessage"] = "Sensor names must be 2 to 35 characters: letters and digits, with @ - / _ = + . and spaces in between";
      continue;
    }
    Serial.printf("UpdateSensorName: Processing serial: %s with new name %s\n", sensAddr.c_str(), sensName.c_str());
    int sensNum = 0;
    bool found = false;
    while (sensNum < countSensors)
    {
      if (!strcmp(sensAddr.c_str(), sensorList[sensNum].serial))
      {
        found = true;
        break;
      }
      sensNum++;
    }

    if (!found)
    {
      jsonDoc["error"] = true;
      jsonDoc["errorMessage"] = "Sensor does not exist, not updating";
      continue;
    }
    writeFriendlyName(sensName.c_str(), sensAddr.c_str());
    getFriendlyName(sensorList[sensNum].name, sensAddr.c_str());
    updateSensorName(sensorList[sensNum].name, sensNum);
  }
  completeSnmpSetup();
  jsonDoc["restartRequired"] = false;
  serializeJson(jsonDoc, *responseString);
}

void apiFactoryReset(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  resetToDefaults(false);
  jsonDoc["restartRequired"] = true;
  serializeJson(jsonDoc, *responseString);
}

void apiClearSensorConfiguration(String *responseString, JsonObject *jsonRequest)
{
  JsonDocument jsonDoc;
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  clearSnmpMap();
  jsonDoc["restartRequired"] = true;
  serializeJson(jsonDoc, *responseString);
}

void jsonApiHandler(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (!isAuthenticated(request))
    return request->requestAuthentication("SensorNetPro", true);

  if (not json.is<JsonObject>())
  {
    request->send(400, "text/plain", "Not an object");
    return;
  }
  auto &&reqData = json.as<JsonObject>();
  if (not reqData["webAction"].is<String>())
  {
    request->send(400, "application/json", "{ \"errorMessage\": \"webAction is not specified\", \"error\": true }");
    return;
  }

  StateLock lock;
  String webAction = reqData["webAction"].as<String>();
  if (adminPasswordIsDefault() && webAction != "config" && webAction != "save-admin")
  {
    request->send(403, "application/json", "{ \"errorMessage\": \"The default admin password must be changed first\", \"error\": true, \"passwordChangeRequired\": true }");
    return;
  }
  String *jsonResponse = new String();

  Serial.printf("Handling JSON action %s\n", webAction.c_str());
  if (webAction == "reboot")
  {
    Serial.println("Processing Reboot Request");
    apiReboot(jsonResponse, &reqData);
  }
  else if (webAction == "sensor-data")
  {
    Serial.println("Processing Get Sensor Data Request");
    apiSensorData(jsonResponse, &reqData);
  }
  else if (webAction == "status")
  {
    Serial.println("Processing Get Status Request");
    apiSystemStats(jsonResponse, &reqData);
  }
  else if (webAction == "config")
  {
    Serial.println("Processing Get Config Request");
    JsonDocument jsonDoc;
    apiGetConfiguration(&jsonDoc, true, true, true);
    serializeJson(jsonDoc, *jsonResponse);
  }
  else if (webAction == "save-network")
  {
    Serial.println("Processing Save Network Settings Request");
    apiSaveNetworkConfiguration(jsonResponse, &reqData);
  }
  else if (webAction == "save-admin")
  {
    Serial.println("Processing Save Admin Settings Request");
    apiSaveAdminConfiguration(jsonResponse, &reqData);
  }
  else if (webAction == "save-snmp")
  {
    Serial.println("Processing Save SNMP Settings Request");
    apiSaveSnmpConfiguration(jsonResponse, &reqData);
  }
  else if (webAction == "save-thermal-sensor")
  {
    Serial.println("Processing Save Thermal Sensor Settings Request");
    apiSaveThermalSensorConfiguration(jsonResponse, &reqData);
  }
  else if (webAction == "reset-sensor-config")
  {
    Serial.println("Processing Reset Sensor Config Request");
    apiClearSensorConfiguration(jsonResponse, &reqData);
  }
  else if (webAction == "factory-reset")
  {
    Serial.println("Processing Factory Reset Request");
    apiFactoryReset(jsonResponse, &reqData);
  }
  else
  {
    Serial.println("Unknown action");
    JsonDocument jsonDoc;
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "webAction not implemented";
    serializeJson(jsonDoc, *jsonResponse);
    request->send(400, "application/json", *jsonResponse);
    delete (jsonResponse);
    return;
  }
  request->send(200, "application/json", *jsonResponse);
  Serial.println(*jsonResponse);
  delete (jsonResponse);
}

// Station mode couldn't connect and there's no Ethernet: bring up an open setup AP
// alongside the station so the device can still be reached and reconfigured.
// The station keeps retrying in loop(); the AP stays up until the next reboot.
void startFallbackAp()
{
  uint64_t mac = ESP.getEfuseMac();
  snprintf(fallbackSsid, sizeof(fallbackSsid), "%s-%02X%02X", __DEF_SSID, (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  Serial.printf("Cannot connect to WiFi and no Ethernet. Starting fallback AP (SSID: %s)\n", fallbackSsid);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(fallbackSsid);
  fallbackApActive = true;
}

void initPortal()
{
  Serial.println("Initialising web portal - setting up routing");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (!isAuthenticated(request))
                return request->requestAuthentication("SensorNetPro", true);
              request->send(SPIFFS, "/index.html", "text/html", false); });
  server.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/bootstrap.min.css", "text/css", false); });
  server.on("/bootstrap.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/bootstrap.min.js", "text/javascript", false); });
  server.on("/datatables.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/datatables.min.css", "text/css", false); });
  server.on("/datatables.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/datatables.min.js", "text/javascript", false); });
  server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/favicon.png", "image/png", false); });
  server.on("/jquery.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/jquery.min.js", "text/javascript", false); });
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/logo.png", "image/png", false); });
  server.on("/sensorNetPro.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/sensorNetPro.css", "text/css", false); });
  server.on("/sensorNetPro.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/sensorNetPro.js", "text/javascript", false); });
  server.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (!isAuthenticated(request))
                return request->requestAuthentication("SensorNetPro", true);
              request->send(SPIFFS, "/reboot.html", "text/html", false); });
  server.on("/reboot.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              if (!isAuthenticated(request))
                return request->requestAuthentication("SensorNetPro", true);
              request->send(SPIFFS, "/reboot.js", "text/javascript", false); });
  AsyncCallbackJsonWebHandler *apiHandler = new AsyncCallbackJsonWebHandler("/json-api", jsonApiHandler);

  server.addHandler(apiHandler);
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(404, "text/plain", "Not found"); });

  Serial.println("Booting web portal");
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  stateMutex = xSemaphoreCreateMutex();
  pinMode(KEY_RESET, INPUT);
  pinMode(WIFI_LED, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(WIFI_LED, 0);
  digitalWrite(ERROR_LED, 1);
  digitalWrite(STATUS_LED, 1);

  Serial.printf("**********************\nSensorNetPro v%s\n**********************\n\n", __FIRMWARE_VERSION);

  ch_httpAdminUser = (char *)malloc(__PREF_ADMIN_USER_LEN);
  ch_httpAdminPass = (char *)malloc(__PREF_ADMIN_PASS_LEN);
  ch_systemHostname = (char *)malloc(__PREF_HOSTNAME_LEN);
  ch_wifiSSID = (char *)malloc(__PREF_SSID_LEN);
  ch_wpaPsk = (char *)malloc(__PREF_WPA_KEY_LEN);
  ch_snmpCommunity = (char *)malloc(__PREF_COMMUNITY_LEN);
  ch_snmpSysName = (char *)malloc(__PREF_SNMP_SYSNAME_LEN);
  ch_snmpLocation = (char *)malloc(__PREF_SNMP_SYSLOCATION_LEN);
  ch_snmpSysContact = (char *)malloc(__PREF_SNMP_SYSCONTACT_LEN);

  // Initialise Filesystem
  if (!SPIFFS.begin(true))
  {
    Serial.println("Unable to access filesystem. Cannot continue");
    digitalWrite(WIFI_LED, 0);
    digitalWrite(ERROR_LED, 1);
    digitalWrite(STATUS_LED, 0);
    // Blink the error LED forever. uptime isn't updated until loop(), so time this with delay()
    bool errorLed = true;
    while (true)
    {
      delay(500);
      errorLed = !errorLed;
      digitalWrite(ERROR_LED, errorLed);
    }
  }

  loadPreferences(false);

  WiFi.onEvent(WiFiEvent);
  if (wifi_on)
  {
    Serial.println("Setting up Wi-Fi");
    if (ap_on == 0)
    {
      Serial.printf("Starting Wifi, connecting to AP (Hostname: %s | SSID: %s | Key: %s)\n", ch_systemHostname, ch_wifiSSID, maskSecret(ch_wpaPsk));
      WiFi.hostname(ch_systemHostname);
      WiFi.mode(WIFI_STA);
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
      WiFi.begin(ch_wifiSSID, ch_wpaPsk);
    }
    else
    {
      Serial.printf("Starting Wifi, Creating SoftAP (SSID: %s | Key: %s)\n", ch_wifiSSID, maskSecret(ch_wpaPsk));
      WiFi.softAP(ch_wifiSSID, ch_wpaPsk);
    }
  }
  else
  {
    Serial.println("Wifi not enabled");
    wifiConnected = false;
  }

  Serial.println("Starting ethernet");
  // PHY settings come from the board variant. Passing them explicitly means a board without
  // them fails to compile, rather than silently using the library defaults.
  if (!ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE))
    Serial.println("Ethernet failed to start");

  // The static IP settings belong to Ethernet only. WiFi station mode always uses DHCP,
  // and the AP (configured or fallback) uses the soft-AP default of 192.168.4.1/24.
  if (dhcp_on == 0)
  {
    Serial.println("Not using DHCP on Ethernet");
    Serial.println("Setting Ethernet IP parameters");
    ETH.config(getIPAddress(), getDefaultGateway(), getSubnetMask());
    Serial.printf("Config > IP: %s | Mask: %s | Gateway: %s\n", getIPString(TYPE_ADDR).c_str(),
                  getIPString(TYPE_MASK).c_str(), getIPString(TYPE_GWAY).c_str());
  }
  int count = 0;

  if (wifi_on)
    Serial.println("Attempting to connect to WiFi");

  while (wifi_on && !ap_on && WiFi.status() != WL_CONNECTED && ++count < 60 && !eth_connected)
  {
    Serial.printf(".");
    delay(1000);
  }
  if (wifi_on && !ap_on && WiFi.status() != WL_CONNECTED && !eth_connected)
  {
    startFallbackAp();
  }

  uptime = 0;
  prevPoll = 0;
  initialiseSnmp(ch_snmpCommunity, &uptime, ch_snmpSysContact, ch_snmpSysName, ch_snmpLocation);

  Serial.println("Discovering sensors and setting up their OIDs");
  discoverSensors();
  completeSnmpSetup();
  initPortal();
  MDNS.begin(ch_systemHostname);
  MDNS.addService("http", "tcp", 80);
  digitalWrite(ERROR_LED, 0);
  digitalWrite(STATUS_LED, 1);
}

void loop()
{
  {
    StateLock lock;
    snmpAgentLoop();
  }
  uptime = esp_timer_get_time() / 10000;
  if (prevPoll == 0 || prevPoll + (SENSOR_TIME * 100) <= uptime)
  {
    requestSensorData();
    prevPoll = uptime;

    if (wifi_on && !ap_on && !settingsRebootNeeded && (WiFi.status() != WL_CONNECTED) &&
        (!fallbackApActive || lastWifiRetry + (FALLBACK_RETRY_TIME * 100) <= uptime))
    {
      lastWifiRetry = uptime;
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
  if (readingStartedAt != 0 && millis() - readingStartedAt >= readingWaitTime)
  {
    updateSensorData();
  }

  if (rebootRequestedAt != 0 && millis() - rebootRequestedAt >= 1000)
  {
    Serial.println("Rebooting ESP as requested");
    ESP.restart();
  }

  bool status = ((uptime / 50) % 2 == 1);
  if (statusLed != status)
  {
    statusLed = status;
    digitalWrite(STATUS_LED, statusLed);
  }

  // Runtime button: a deliberate hold of RUNTIME_REBOOT_HOLD_TIME reboots, and shorter presses are
  // ignored. Timed without blocking so SNMP keeps answering. The error LED flashes while held.
  if (digitalRead(KEY_RESET) == 0)
  {
    uint32_t now = millis();
    if (buttonPressedAt == 0)
    {
      buttonPressedAt = now | 1; // never 0 while pressed
      Serial.printf("Button pressed, hold for %d seconds to reboot\n", RUNTIME_REBOOT_HOLD_TIME);
    }
    digitalWrite(ERROR_LED, (now / 125) % 2);
    if (now - buttonPressedAt >= RUNTIME_REBOOT_HOLD_TIME * 1000)
    {
      Serial.println("Button held, rebooting ESP");
      digitalWrite(ERROR_LED, 1);
      ESP.restart();
    }
  }
  else if (buttonPressedAt != 0)
  {
    Serial.println("Button released, not rebooting");
    buttonPressedAt = 0;
    digitalWrite(ERROR_LED, 0);
  }
}
