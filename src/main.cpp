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
#include <OneWire.h>           // Dallas OneWire interface
#include <DallasTemperature.h> // DS18b120 support
#include <SPIFFS.h>            // SPI Flash Filesystem for Web Server
#include <ESPAsyncWebServer.h> // WebServer
#include <AsyncJson.h>         // JSON Support for WebServer
#include <ArduinoJson.h>       // JSON Support for Program
#include <ESPmDNS.h>           // MDNS Responder

#include "ethsettings.h" // Ethernet settings
#include "prefs.h"       // Preferences Lib
#include "snmp.h"        // SNMP Lib

uint32_t uptime, prevPoll;
int snmpTemperature[__MAX_SENSOR_COUNT], countSensors;
bool eth_connected, dhcp_on, ap_on, wifi_on, isConfigured, resetNeeded, wifiConnected;
bool statusLed = false;
bool settingsRebootNeeded = false;

char *sensorAddress[__MAX_SENSOR_COUNT], *sensorName[__MAX_SENSOR_COUNT];
char *ch_httpAdminUser, *ch_httpAdminPass, *ch_systemHostname, *ch_wifiSSID, *ch_wpaPsk, *ch_snmpCommunity, *ch_snmpSysName, *ch_snmpLocation, *ch_snmpSysContact;

String firmwareVersion = __FIRMWARE_VERSION;
String dhcpLabel, sensorCountLabel, ethLabel, wifiLabel, etherSpeed;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress *sensorsUnique;
AsyncWebServer server(80);

void loadPreferences(bool bypassInit)
{
  Serial.println("Loading Preferences");
  if (!bypassInit)
  {
    initPreferences();
    isConfigured = getConfigured();

    delay(200);
    if (digitalRead(KEY_RESET) == 0)
    {
      digitalWrite(STATUS_LED, 0);
      digitalWrite(ERROR_LED, 0);
      Serial.println("Factory reset button held at startup. Resetting after 5 seconds");
      bool reset = false;
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
    wifiConnected = false;
    break;

  case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
    Serial.println("Wifi AP: Client Assigned IP");
    wifiConnected = true;
    break;

  default:
    break;
  }
  digitalWrite(WIFI_LED, (wifiConnected || eth_connected));
}

void updateSensorData()
{
  float temperaturec[10], temperaturef[10];

  Serial.print("Requesting sensor data...");
  sensors.requestTemperatures();
  while (!sensors.isConversionComplete())
  {
  };
  Serial.println("done, getting values");
  for (int i = 0; i < countSensors; i++)
  {
    temperaturec[i] = sensors.getTempCByIndex(i);
    snmpTemperature[i] = temperaturec[i] * 100;
    Serial.printf("%d=%.2fºC ", i, temperaturec[i]);
  }
  Serial.println();
}

void apiReboot(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  serializeJson(jsonDoc, *responseString);
  ESP.restart();
}

void apiSensorData(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  for (int i = 0; i < countSensors; i++)
  {
    jsonDoc["thermalSensorData"][i]["id"] = i + 1;
    jsonDoc["thermalSensorData"][i]["address"] = sensorAddress[i];
    jsonDoc["thermalSensorData"][i]["name"] = sensorName[i];
    jsonDoc["thermalSensorData"][i]["reading"] = (float)snmpTemperature[i] / 100.0f;
  }

  serializeJson(jsonDoc, *responseString);
}

void apiSystemStats(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
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

  serializeJson(jsonDoc, *responseString);
}

void apiGetConfiguration(DynamicJsonDocument *jsonDoc, bool network, bool snmp, bool admin)
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

  DynamicJsonDocument jsonDoc(2048);
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
  if (modifyAdminPass && not(*jsonRequest)["adminPassword"].is<String>())
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }
  if (modifyAdminPass)
  {
    String adminPassword = (*jsonRequest)["adminPassword"].as<String>();
    if (adminPassword == "" || adminPassword.length() < 8)
    {
      jsonDoc["error"] = true;
      jsonDoc["errorMessage"] = "Invalid Admin Password";
      serializeJson(jsonDoc, *responseString);
      return;
    }
    writeHttpPassword(adminPassword);
  }

  String adminUser = (*jsonRequest)["adminUser"].as<String>();
  writeHttpUser(adminUser);

  commitToPrefs(false);
  loadPreferences(true);
  apiGetConfiguration(&jsonDoc, false, false, true);
  serializeJson(jsonDoc, *responseString);
}

void apiSaveNetworkConfiguration(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
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

  if ((!networkDhcp && (networkIP == "" || networkMask == "" || networkGateway == "")) || networkWifi && (networkSSID == ""))
  {
    jsonDoc["error"] = true;
    jsonDoc["errorMessage"] = "Invalid request, not all parameters supplied.";
    serializeJson(jsonDoc, *responseString);
    return;
  }

  if (modifyWpaKey)
  {
    writeWpaKey(networkWpaKey);
  }
  writeApMode(networkApMode);
  writeDhcpMode(networkDhcp);
  writeHostname(networkHostname);
  writeSSID(networkSSID);
  writeWirelesEnabled(networkWifi);

  IPAddress ip;
  ip.fromString(networkIP);
  writeIPAddress(ip[0], ip[1], ip[2], ip[3]);

  IPAddress mask;
  mask.fromString(networkMask);
  writeSubnetMask(mask[0], mask[1], mask[2], mask[3]);

  IPAddress gw;
  gw.fromString(networkGateway);
  writeDefaultGateway(gw[0], gw[1], gw[2], gw[3]);

  commitToPrefs(false);
  loadPreferences(true);
  apiGetConfiguration(&jsonDoc, true, false, false);
  jsonDoc["restartRequired"] = true;
  settingsRebootNeeded = true;
  serializeJson(jsonDoc, *responseString);
}

void apiSaveSnmpConfiguration(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
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
  if (modifyCommunity)
  {
    String snmpCommunity = (*jsonRequest)["snmpCommunity"].as<String>();
    if (snmpCommunity == "")
    {
      jsonDoc["error"] = true;
      jsonDoc["errorMessage"] = "Invalid SNMP community string";
      serializeJson(jsonDoc, *responseString);
      return;
    }
    writeSnmpCommunity(snmpCommunity);
  }

  String snmpSysName = (*jsonRequest)["snmpSysName"].as<String>();
  String snmpLocation = (*jsonRequest)["snmpLocation"].as<String>();
  String snmpContact = (*jsonRequest)["snmpContact"].as<String>();

  writeSnmpContact(snmpContact);
  writeSnmpSysName(snmpSysName);
  writeSnmpLocation(snmpLocation);

  commitToPrefs(false);

  jsonDoc["restartRequired"] = true;
  settingsRebootNeeded = true;
  loadPreferences(true);
  completeSnmpSetup();

  apiGetConfiguration(&jsonDoc, false, true, false);
  serializeJson(jsonDoc, *responseString);
}

void apiSaveThermalSensorConfiguration(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
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
    Serial.printf("UpdateSensorName: Processing serial: %s with new name %s\n", sensAddr.c_str(), sensName.c_str());
    int sensNum = 0;
    bool found = false;
    while (sensNum < countSensors)
    {
      if (!strcmp(sensAddr.c_str(), sensorAddress[sensNum]))
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
    getFriendlyName(sensorName[sensNum], sensAddr.c_str());
    updateThermalSensorName(sensorName[sensNum], sensNum);
  }
  completeSnmpSetup();
  jsonDoc["restartRequired"] = false;
  serializeJson(jsonDoc, *responseString);
}

void apiFactoryReset(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  resetToDefaults(false);
  jsonDoc["restartRequired"] = true;
  serializeJson(jsonDoc, *responseString);
}

void apiClearSensorConfiguration(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  clearSnmpMap();
  jsonDoc["restartRequired"] = true;
  serializeJson(jsonDoc, *responseString);
}

void jsonApiHandler(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (!request->authenticate(ch_httpAdminUser, ch_httpAdminPass))
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

  String webAction = reqData["webAction"].as<String>();
  String *jsonResponse = new String();

  Serial.printf("Handling JSON action %s\n", webAction);
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
    DynamicJsonDocument jsonDoc(2048);
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
    DynamicJsonDocument jsonDoc(2048);
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

void initPortal()
{
  Serial.println("Initialising web portal - setting up routing");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { 
              if(!request->authenticate(ch_httpAdminUser, ch_httpAdminPass))
                request->requestAuthentication("SensorNetPro", true);
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
            { if(!request->authenticate(ch_httpAdminUser, ch_httpAdminPass))
                request->requestAuthentication("SensorNetPro", true);
                 request->send(SPIFFS, "/reboot.html", "text/html", false); });
  server.on("/reboot.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {  if(!request->authenticate(ch_httpAdminUser, ch_httpAdminPass))
                request->requestAuthentication("SensorNetPro", true);
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
    bool errorLed = true;
    while (true)
    {
      bool status = ((uptime / 50) % 2 == 1);
      if (errorLed != status)
      {
        errorLed = status;
        digitalWrite(ERROR_LED, errorLed);
      }
    }
  }

  loadPreferences(false);

  WiFi.onEvent(WiFiEvent);
  if (wifi_on)
  {
    Serial.println("Setting up Wi-Fi");
    if (ap_on == 0)
    {
      Serial.printf("Starting Wifi, connecting to AP (Hostname: %s | SSID: %s | Key: %s)\n", ch_systemHostname, ch_wifiSSID, ch_wpaPsk);
      WiFi.hostname(ch_systemHostname);
      WiFi.mode(WIFI_STA);
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
      WiFi.begin(ch_wifiSSID, ch_wpaPsk);
    }
    else
    {
      Serial.printf("Starting Wifi, Creating SoftAP (SSID: %s | Key: %s)\n", ch_wifiSSID, ch_wpaPsk);
      WiFi.softAP(ch_wifiSSID, ch_wpaPsk);
    }
  }
  else
  {
    Serial.println("Wifi not enabled");
    wifiConnected = false;
  }

  Serial.println("Starting ethernet");
  ETH.begin();

  if (dhcp_on == 0)
  {
    Serial.println("Not using DHCP");
    Serial.println("Setting IP parameters");
    if (wifi_on)
      WiFi.config(getIPAddress(), getDefaultGateway(), getSubnetMask());
    ETH.config(getIPAddress(), getDefaultGateway(), getSubnetMask());
    Serial.printf("Config > IP: %s | Mask: %s | Gateway: %s\n", getIPString(TYPE_ADDR),
                  getIPString(TYPE_ADDR), getIPString(TYPE_MASK), getIPString(TYPE_GWAY));
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
    Serial.println("Still cannot connect. Rebooting");
    digitalWrite(ERROR_LED, 1);
    digitalWrite(STATUS_LED, 0);
    delay(3000);
    ESP.restart();
  }

  uptime = 0;
  prevPoll = 0;
  Serial.println("Starting Dallas 1-wire interface");
  sensors.begin();
  sensors.setResolution(__DS18B20_RESOLUTION);
  countSensors = sensors.getDeviceCount();
  Serial.println("Found " + String(countSensors) + " sensors");
  sensorsUnique = new DeviceAddress[countSensors];

  initialiseSnmp(ch_snmpCommunity, &uptime, ch_snmpSysContact, ch_snmpSysName, ch_snmpLocation);

  Serial.println("Setting up OIDs for discovered sensors");
  for (int i = 0; i < countSensors; i++)
  {
    sensors.getAddress(sensorsUnique[i], i);
    sensors.setResolution(sensorsUnique[i], __DS18B20_RESOLUTION);
    Serial.print("Sensor: ");
    String hexString = "";
    for (int j = 0; j < 8; j++)
    {
      byte y = sensorsUnique[i][j];
      if (y < 0x10)
      {
        hexString += "0";
      }
      hexString += String(y, HEX); // Lower byte first);
    }
    Serial.println(hexString);
    sensorAddress[i] = (char *)malloc(20);
    hexString.toCharArray(sensorAddress[i], 20);
    sensorName[i] = (char *)malloc(40);
    getFriendlyName(sensorName[i], hexString);
    addThermalSensorHandler(sensorAddress[i], sensorName[i], snmpTemperature, i);
  }
  completeSnmpSetup();
  initPortal();
  MDNS.begin(ch_systemHostname);
  MDNS.addService("http", "tcp", 80);
  digitalWrite(ERROR_LED, 0);
  digitalWrite(STATUS_LED, 1);
}

void loop()
{
  snmpAgentLoop();
  uptime = esp_timer_get_time() / 10000;
  if (prevPoll == 0 || prevPoll + (SENSOR_TIME * 100) <= uptime)
  {
    updateSensorData();
    prevPoll = uptime;

    if (wifi_on && !ap_on && !settingsRebootNeeded && (WiFi.status() != WL_CONNECTED))
    {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
  bool status = ((uptime / 50) % 2 == 1);
  if (statusLed != status)
  {
    statusLed = status;
    digitalWrite(STATUS_LED, statusLed);
  }

  if (digitalRead(KEY_RESET) == 0)
  {
    Serial.println("Reset pressed, waiting");
    delay(1000);
    if (digitalRead(KEY_RESET) == 0)
    {
      Serial.println("Reset pressed, rebooting ESP");
      ESP.restart();
    }
  }
}
