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

#include "ethsettings.h" // Ethernet settings
#include "prefs.h"       // Preferences Lib
#include "snmp.h"        // SNMP Lib

uint32_t uptime, prevPoll;
int snmpTemperature[__MAX_SENSOR_COUNT], countSensors;
bool eth_connected, dhcp_on, ap_on, wifi_on, isConfigured, resetNeeded, wifiConnected;
bool statusLed = false;

char *sensorAddress[__MAX_SENSOR_COUNT], *sensorName[__MAX_SENSOR_COUNT];
char *ch_httpAdminUser, *ch_httpAdminPass, *ch_systemHostname, *ch_wifiSSID, *ch_wpaPsk, *ch_snmpCommunity, *ch_snmpSysName, *ch_snmpLocation, *ch_snmpSysContact;

String firmwareVersion = __FIRMWARE_VERSION;
String dhcpLabel, sensorCountLabel, ethLabel, wifiLabel, etherSpeed;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress *sensorsUnique;
AsyncWebServer server(80);

void loadPreferences()
{
  Serial.println("Loading Preferences");
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

  dhcp_on = getDhcpMode();
  ap_on = getApMode();
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

void portalAction()
{
  // if (portal.click())
  // {
  //   Serial.println("Processing web click action");
  //   if (portal.clickInt("dhcp", dhcp_on))
  //   {
  //     Serial.printf("Portal Change | DHCP | %d\n", dhcp_on);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickInt("wifiEnabled", wifi_on))
  //   {
  //     Serial.printf("Portal Change | Wifi On | %d\n", wifi_on);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickInt("ap", ap_on))
  //   {
  //     Serial.printf("Portal Change | Access Point Mode | %d\n", ap_on);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiHostname", uiHostname))
  //   {
  //     Serial.printf("Portal Change | Hostname | %s\n", uiHostname);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSSID", uiSSID))
  //   {
  //     Serial.printf("Portal Change | SSID | %s\n", uiSSID);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiWpaPsk", uiWpaPsk))
  //   {
  //     Serial.printf("Portal Change | WPA PSK | %s\n", uiWpaPsk);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiIPAddress", uiIPAddress))
  //   {
  //     Serial.printf("Portal Change | IP | %s\n", uiIPAddress);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSubnetMask", uiSubnetMask))
  //   {
  //     Serial.printf("Portal Change | Mask | %s\n", uiSubnetMask);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiGateway", uiGateway))
  //   {
  //     Serial.printf("Portal Change | Gateway | %s\n", uiGateway);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiPrimaryDNS", uiPrimaryDNS))
  //   {
  //     Serial.printf("Portal Change | PriDNS | %s\n", uiPrimaryDNS);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSecondaryDNS", uiSecondaryDNS))
  //   {
  //     Serial.printf("Portal Change | SecDNS | %s\n", uiSecondaryDNS);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSnmpCommunity", uiSnmpCommunity))
  //   {
  //     Serial.printf("Portal Change | SNMP Community | %s\n", uiSnmpCommunity);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSnmpContact", uiSnmpContact))
  //   {
  //     Serial.printf("Portal Change | SNMP Contact | %s\n", uiSnmpContact);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSystemName", uiSystemName))
  //   {
  //     Serial.printf("Portal Change | SNMP System Name | %s\n", uiSystemName);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiSnmpLocation", uiSnmpLocation))
  //   {
  //     Serial.printf("Portal Change | SNMP Location | %s\n", uiSnmpLocation);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiAdminUser", uiAdminUser))
  //   {
  //     Serial.printf("Portal Change | Admin User | %s\n", uiAdminUser);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (portal.clickString("uiAdminPass", uiAdminPass))
  //   {
  //     Serial.printf("Portal Change | Admin Pass | %s\n", uiAdminPass);
  //     resetNeeded = resetNeeded || true;
  //   }
  //   if (resetNeeded)
  //   {
  //     Serial.println("Will reboot after saving changes");
  //   }
  //   if (portal.click("btnSnmp"))
  //   {
  //     Serial.println("Processing SNMP Save Button");

  //     commitToPrefs(false);
  //   }

  //   if (portal.click("btnNetwork"))
  //   {
  //     Serial.println("Processing Network Save Button");

  //     portal.update("ap");
  //     portal.update("uiSSID");
  //     portal.update("uiWpaPsk");
  //     portal.update("uiHostname");
  //     portal.update("dhcp");
  //     portal.update("uiIPAddress");
  //     portal.update("uiSubnetMask");
  //     portal.update("uiGateway");
  //     portal.update("uiPrimaryDNS");
  //     portal.update("uiSecondaryDNS");

  //     IPAddress ip;
  //     ip.fromString(uiIPAddress);
  //     IPAddress mask;
  //     mask.fromString(uiSubnetMask);
  //     IPAddress gw;
  //     gw.fromString(uiGateway);
  //     IPAddress dns1;
  //     dns1.fromString(uiPrimaryDNS);
  //     IPAddress dns2;
  //     dns2.fromString(uiSecondaryDNS);

  //     writeDhcpMode(dhcp_on);
  //     writeApMode(ap_on);
  //     writeHostname(uiHostname);
  //     writeSSID(uiSSID);
  //     writeWpaKey(uiWpaPsk);
  //     writeIPAddress(ip[0], ip[1], ip[2], ip[3]);
  //     writeSubnetMask(mask[0], mask[1], mask[2], mask[3]);
  //     writeDefaultGateway(gw[0], gw[1], gw[2], gw[3]);
  //     writePrimaryDNS(dns1[0], dns1[1], dns1[2], dns1[3]);
  //     writeSecondaryDNS(dns2[0], dns2[1], dns2[2], dns2[3]);

  //     commitToPrefs(resetNeeded);
  //     resetNeeded = false;
  //   }
  //   if (portal.click("btnUser"))
  //   {
  //     Serial.println("Processing User Save Button");
  //     writeHttpUser(uiAdminUser);
  //     writeHttpPassword(uiAdminPass);
  //     commitToPrefs(resetNeeded);
  //     resetNeeded = false;
  //   }
  //   for (int i = 0; i < countSensors; i++)
  //   {
  //     String textBoxName = "txtSens" + String(sensorAddress[i]);
  //     const char *tbName = textBoxName.c_str();
  //     String newFriendlyName;
  //     if (portal.clickString(textBoxName, newFriendlyName))
  //     {
  //       const char *deviceName;
  //       if (newFriendlyName == "")
  //       {
  //         deviceName = sensorAddress[i];
  //       }
  //       else
  //       {
  //         deviceName = newFriendlyName.c_str();
  //       }
  //       Serial.printf("CLICKSTRING - Got %s from text box %s\n", deviceName, tbName);
  //       strcpy(sensorName[i], deviceName);
  //     }
  //   }

  //   getSnmpContact(ch_snmpSysContact);
  //   getSnmpSysName(ch_snmpSysName);
  //   getSnmpLocation(ch_snmpLocation);

  //   if (portal.click("btnSensors"))
  //   {
  //     Serial.println("Processing Sensor Save Button");
  //     for (int i = 0; i < countSensors; i++)
  //     {
  //       Serial.printf("SAVEBUTTON Updating sensor %s to name %s\n", sensorAddress[i], sensorName[i]);
  //       writeFriendlyName(sensorName[i], sensorAddress[i]);
  //       updateSensorName(sensorName[i], i);
  //     }
  //     completeSnmpSetup();
  //   }

  //   getHttpUser(ch_httpAdminUser);
  //   getHttpPassword(ch_httpAdminPass);
  // }
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
    jsonDoc["sensorData"][i]["id"] = i + 1;
    jsonDoc["sensorData"][i]["address"] = sensorAddress[i];
    jsonDoc["sensorData"][i]["name"] = sensorName[i];
    jsonDoc["sensorData"][i]["reading"] = (float)snmpTemperature[i] / 100.0f;
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
  String runningWifiIP = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
  String runningEthIP = String() + ETH.localIP()[0] + "." + ETH.localIP()[1] + "." + ETH.localIP()[2] + "." + ETH.localIP()[3];
  String configuredIP = getIPString(TYPE_ADDR);
  String configuredMask = getIPString(TYPE_MASK);
  String configuredGateway = getIPString(TYPE_GWAY);
  String configuredPrimaryDNS = getIPString(TYPE_DNS1);
  String configuredSecondaryDNS = getIPString(TYPE_DNS2);

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

void apiGetConfiguration(String *responseString, JsonObject *jsonRequest)
{
  DynamicJsonDocument jsonDoc(2048);
  jsonDoc["error"] = false;
  jsonDoc["errorMessage"] = "Success";
  jsonDoc["networkHostname"] = ch_systemHostname;
  jsonDoc["networkWifiEnabled"] = wifi_on;
  jsonDoc["networkDhcpEnabled"] = dhcp_on;
  jsonDoc["networkWifiApMode"] = ap_on;
  jsonDoc["networkIP"] = getIPString(TYPE_ADDR);
  jsonDoc["networkMask"] = getIPString(TYPE_MASK);
  jsonDoc["networkGateway"] = getIPString(TYPE_GWAY);
  jsonDoc["networkPriDNS"] = getIPString(TYPE_DNS1);
  jsonDoc["networkSecDNS"] = getIPString(TYPE_DNS2);
  jsonDoc["networkSSID"] = ch_wifiSSID;
  jsonDoc["adminUser"] = ch_httpAdminUser;
  jsonDoc["snmpLocation"] = ch_snmpLocation;
  jsonDoc["snmpSysName"] = ch_snmpSysName;
  jsonDoc["snmpContact"] = ch_snmpSysContact;

  serializeJson(jsonDoc, *responseString);
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
  
  serializeJson(jsonDoc, *responseString);
}

void apiSaveNetworkConfiguration(String *responseString, JsonObject *jsonRequest)
{
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
  getSnmpCommunity(ch_snmpSysContact);
  getSnmpContact(ch_snmpSysContact);
  getSnmpSysName(ch_snmpSysName);
  getSnmpLocation(ch_snmpLocation);
  completeSnmpSetup();
  jsonDoc["restartRequired"] = true;
  serializeJson(jsonDoc, *responseString);
}

void apiSaveSensorConfiguration(String *responseString, JsonObject *jsonRequest)
{
}

void apiFactoryReset(String *responseString, JsonObject *jsonRequest) {
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
    apiGetConfiguration(jsonResponse, &reqData);
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
  else if (webAction == "save-sensor")
  {
    Serial.println("Processing Save Sensor Settings Request");
    apiSaveSensorConfiguration(jsonResponse, &reqData);
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
            { request->send(SPIFFS, "/index.html", "text/html", false); });
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
            { request->send(SPIFFS, "/reboot.html", "text/html", false); });
  server.on("/reboot.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/reboot.js", "text/javascript", false); });
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

  loadPreferences();

  WiFi.onEvent(WiFiEvent);
  wifi_on = getWifiEnabled();
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
      WiFi.config(getIPAddress(), getDefaultGateway(), getSubnetMask(), getPrimaryDNS(), getSecondaryDNS());
    ETH.config(getIPAddress(), getDefaultGateway(), getSubnetMask(), getPrimaryDNS(), getSecondaryDNS());
    Serial.printf("Config > IP: %s | Mask: %s | Gateway: %s | DNS1: %s | DNS2: %s\n", getIPString(TYPE_ADDR),
                  getIPString(TYPE_ADDR), getIPString(TYPE_MASK), getIPString(TYPE_GWAY), getIPString(TYPE_DNS1), getIPString(TYPE_DNS2));
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
    addSensorHandler(sensorAddress[i], sensorName[i], snmpTemperature, i);
  }
  completeSnmpSetup();
  initPortal();
  digitalWrite(ERROR_LED, 0);
  digitalWrite(STATUS_LED, 1);
}

void loop()
{
  snmpAgentLoop();
  uptime = esp_timer_get_time() / 10000;
  // portal.tick();
  if (prevPoll == 0 || prevPoll + (SENSOR_TIME * 100) <= uptime)
  {
    updateSensorData();
    prevPoll = uptime;
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
