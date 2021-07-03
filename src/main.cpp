// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. 

#include <WiFi.h>
#include "AzureIotHub.h"
#include "Esp32MQTTClient.h"
#include "ArduinoJson.h"
#include "ESP32httpUpdate.h"

#define INTERVAL 10000
#define DEVICE_ID "Esp32Device"
#define MESSAGE_MAX_LEN 256

#define redLED 27
#define greenLED 14
#define blueLED 16

#define redChannel 0
#define greenChannel 1
#define blueChannel 2

#define CURRENT_VERSION "<CurrentFirmwareVersion>"
#define BLOB_STORAGE_ROOT "<BlobStorageRoot>"

// Please input the SSID and password of WiFi
const char* ssid     = "agileMax_Guest";
const char* password = "WLAN_agileMax";

static const char* connectionString = "HostName=IoTGregorHub.azure-devices.net;DeviceId=GregorsDevice;SharedAccessKey=O1wkse6KQXIiGOT/738g4dwnDiHrqmyg3uC8xwhB17M=";

const char *messageData = "{\"deviceId\":\"%s\", \"messageId\":%d, \"Temperature\":%f, \"Humidity\":%f}";

int messageCount = 1;
static bool hasWifi = false;
static bool messageSending = true;
static uint64_t send_interval_ms;

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utilities

static void updateFromBLOB(String url) {
    Serial.print("Downloading new firmware from: ");
    Serial.println(url);
    
    t_httpUpdate_return ret = ESPhttpUpdate.update(url);
    
    switch(ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
            break;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;

        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
                Serial.println("Update done");
            break;
    }
    Serial.println();
}

static void InitWifi()
{
  Serial.println("Connecting...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  hasWifi = true;
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result)
{
  if (result == IOTHUB_CLIENT_CONFIRMATION_OK)
  {
    Serial.println("Send Confirmation Callback finished.");
  }
}

static void MessageCallback(const char* payLoad, int size)
{
  Serial.println("Message callback:");
  Serial.println(payLoad);
}

static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payLoad, int size)
{
  char *temp = (char *)malloc(size + 1);
  if (temp == NULL)
  {
    return;
  }
  memcpy(temp, payLoad, size);
  temp[size] = '\0';
  // Display Twin message.
  Serial.println(temp);

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, temp);
  
  ledcWrite(redChannel, doc["Red"]);

  Serial.print("Current Firmeware Version: ");
  Serial.println(CURRENT_VERSION);
  
  const char* desiredFirmwareVersion;

  if (!doc["desired"].isNull()) {
    desiredFirmwareVersion = doc["desired"]["firmwareversion"];
  }
  else {
    desiredFirmwareVersion = doc["firmwareversion"];
  }
  Serial.print("Desired Firmeware Version: ");
  Serial.println(desiredFirmwareVersion);

  free(temp);

  if (strcmp(desiredFirmwareVersion, CURRENT_VERSION) != 0) {
    Serial.println();
    Serial.println("Starting Firmware OTA update ...");
    char url [1000];
    sprintf(url, "%s/%s/firmware.bin", BLOB_STORAGE_ROOT, desiredFirmwareVersion);
    updateFromBLOB(url);
  }

  
}

static int  DeviceMethodCallback(const char *methodName, const unsigned char *payload, int size, unsigned char **response, int *response_size)
{
  LogInfo("Try to invoke method %s", methodName);
  const char *responseMessage = "\"Successfully invoke device method\"";
  int result = 200;

  if (strcmp(methodName, "start") == 0)
  {
    LogInfo("Start sending temperature and humidity data");
    messageSending = true;
  }
  else if (strcmp(methodName, "stop") == 0)
  {
    LogInfo("Stop sending temperature and humidity data");
    messageSending = false;
  }
  else
  {
    LogInfo("No method %s found", methodName);
    responseMessage = "\"No method found\"";
    result = 404;
  }

  *response_size = strlen(responseMessage) + 1;
  *response = (unsigned char *)strdup(responseMessage);

  return result;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arduino sketch
void setup()
{
  Serial.begin(115200);
  Serial.println("ESP32 Device");
  Serial.println("Initializing...");
  Serial.print("   Current Firmware Version: ");
  Serial.println(CURRENT_VERSION);

  Serial.println("Initializing GPIO Ports");
  ledcSetup(redChannel, 5000, 8);
  ledcSetup(greenChannel, 5000, 8);
  ledcSetup(blueChannel, 5000, 8);

  ledcAttachPin(redLED, redChannel);
  ledcAttachPin(greenLED, greenChannel);
  ledcAttachPin(blueLED, blueChannel);

  // Initialize the WiFi module
  Serial.println(" > WiFi");
  hasWifi = false;
  InitWifi();
  if (!hasWifi)
  {
    return;
  }
  randomSeed(analogRead(0));

  Serial.println(" > IoT Hub");
  Esp32MQTTClient_SetOption(OPTION_MINI_SOLUTION_NAME, "GetStarted");
  Esp32MQTTClient_Init((const uint8_t*)connectionString, true);

  Esp32MQTTClient_SetSendConfirmationCallback(SendConfirmationCallback);
  Esp32MQTTClient_SetMessageCallback(MessageCallback);
  Esp32MQTTClient_SetDeviceTwinCallback(DeviceTwinCallback);
  Esp32MQTTClient_SetDeviceMethodCallback(DeviceMethodCallback);

  send_interval_ms = millis();
}

void loop()
{
  if (hasWifi)
  {
    if (messageSending && 
        (int)(millis() - send_interval_ms) >= INTERVAL)
    {
      // Send teperature data
      char messagePayload[MESSAGE_MAX_LEN];
      float temperature = (float)random(0,50);
      float humidity = (float)random(0, 1000)/10;
      snprintf(messagePayload,MESSAGE_MAX_LEN, messageData, DEVICE_ID, messageCount++, temperature,humidity);
      Serial.println(messagePayload);
      EVENT_INSTANCE* message = Esp32MQTTClient_Event_Generate(messagePayload, MESSAGE);
      Esp32MQTTClient_Event_AddProp(message, "temperatureAlert", "true");
      Esp32MQTTClient_SendEventInstance(message);
      
      send_interval_ms = millis();
    }
    else
    {
      Esp32MQTTClient_Check();
    }
  }

  ledcWrite(greenChannel, 255);
  delay(20);
  ledcWrite(greenChannel, 0);
  delay(300);

  delay(10);
}
