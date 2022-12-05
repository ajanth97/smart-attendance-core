#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <EEPROM.h>
#include <base64.h>
#include <libb64/cdecode.h>
#include <WiFiUdp.h>
#include <bearssl/bearssl.h>
#include <bearssl/bearssl_hmac.h>
#include <libb64/cdecode.h>
// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>
//PubSubClient
#include <PubSubClient.h>
//Embedded libraries
#include "src/RFID/RFID.h"
#include "src/NTPClient/NTPClient.h"
#include "src/wifi/wifi.h"

#include "AZURE_CRED.h" // Contains Azure IOT HUB Credentials ! Included in gitignore

// please follow the format '(ard;<platform>)'. 
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp8266)"
// Publish 1 message every 2 seconds
#define TELEMETRY_FREQUENCY_MILLISECS 10000

static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const char* device_key = IOT_CONFIG_DEVICE_KEY;
static const int port = 8883;

#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define ONE_HOUR_IN_SECS 3600
#define MQTT_PACKET_SIZE 1024
#define NTP_SERVERS "0.jp.pool.ntp.org", "ntp.nict.jp"

// Memory allocated for the sample's variables and structures.
static WiFiClientSecure wifi_client;
static X509List cert((const char*)ca_pem);
static PubSubClient mqtt_client(wifi_client);
static az_iot_hub_client client;
static char sas_token[200];
static uint8_t signature[512];
static unsigned char encrypted_signature[32];
static char base64_decoded_device_key[32];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[100];
static uint32_t telemetry_send_count = 0;
static char send_user_data_topic[128];
static uint8_t user_data_payload[128];

RFID rfid(D8, D0);       //D8:pin of tag reader SDA. D0:pin of tag reader RST
unsigned char str[MAX_LEN]; //MAX_LEN is 16: size of the array

WiFiUDP ntpUDP;
//const long utcOffsetInSeconds = 19800; //(UTC+5:30)

//32400 offset for timezone of UTC+9:00
NTPClient timeClient(ntpUDP, "0.jp.pool.ntp.org", 32400);


//Variables
int i = 0;
const char* ssid = "text";
const char* passphrase = "text";

static uint32_t getSecondsSinceEpoch()
{
  return (uint32_t)time(NULL);
}

static void initializeTime()
{
  Serial.print("Setting time using SNTP");

  configTime(32400, 0, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < 1668270114)
  {
    delay(300);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println("Initialized time !");
}

static void connectToWifi(){
  Serial.println("Disconnecting previously connected WiFi");
  WiFi.disconnect();
  EEPROM.begin(512); //Initialasing EEPROM using 512 Bytes
  delay(10);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D3, OUTPUT);
  Serial.println("Reading EEPROM ssid");

  String esid;
  for (int i = 0; i < 32; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  Serial.println();
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");

  String epass = "";
  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(epass);


  WiFi.begin(esid.c_str(), epass.c_str());
  if (testWifi())
  {
    Serial.println("Succesfully Connected!!!");
    return;
  }
  else
  {
    Serial.println("Turning the HotSpot On");
    launchWeb();
    setupAP();// Setup HotSpot
  }

  Serial.println();
  Serial.println("Waiting.");

  while ((WiFi.status() != WL_CONNECTED))
  {
    Serial.print(".");
    delay(100);
    handleWebClient();
  }
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static int generateSasToken(char* sas_token, size_t size)
{
  az_span signature_span = az_span_create((uint8_t*)signature, sizeofarray(signature));
  az_span out_signature_span;
  az_span encrypted_signature_span
      = az_span_create((uint8_t*)encrypted_signature, sizeofarray(encrypted_signature));

  uint32_t expiration = getSecondsSinceEpoch() + (ONE_HOUR_IN_SECS * 12);

  // Get signature
  if (az_result_failed(az_iot_hub_client_sas_get_signature(
          &client, expiration, signature_span, &out_signature_span)))
  {
    Serial.println("Failed getting SAS signature");
    return 1;
  }

  // Base64-decode device key
  int base64_decoded_device_key_length
      = base64_decode_chars(device_key, strlen(device_key), base64_decoded_device_key);

  if (base64_decoded_device_key_length == 0)
  {
    Serial.println("Failed base64 decoding device key");
    return 1;
  }

  // SHA-256 encrypt
  br_hmac_key_context kc;
  br_hmac_key_init(
      &kc, &br_sha256_vtable, base64_decoded_device_key, base64_decoded_device_key_length);

  br_hmac_context hmac_ctx;
  br_hmac_init(&hmac_ctx, &kc, 32);
  br_hmac_update(&hmac_ctx, az_span_ptr(out_signature_span), az_span_size(out_signature_span));
  br_hmac_out(&hmac_ctx, encrypted_signature);

  // Base64 encode encrypted signature
  String b64enc_hmacsha256_signature = base64::encode(encrypted_signature, br_hmac_size(&hmac_ctx));

  az_span b64enc_hmacsha256_signature_span = az_span_create(
      (uint8_t*)b64enc_hmacsha256_signature.c_str(), b64enc_hmacsha256_signature.length());

  // URl-encode base64 encoded encrypted signature
  if (az_result_failed(az_iot_hub_client_sas_get_password(
          &client,
          expiration,
          b64enc_hmacsha256_signature_span,
          AZ_SPAN_EMPTY,
          sas_token,
          size,
          NULL)))
  {
    Serial.println("Failed getting SAS token");
    return 1;
  }

  return 0;
}

static int connectToAzureIoTHub()
{
  size_t client_id_length;
  char mqtt_client_id[128];
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Serial.println("Failed getting client id");
    return 1;
  }

  mqtt_client_id[client_id_length] = '\0';

  char mqtt_username[128];
  // Get the MQTT user name used to connect to IoT Hub
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    printf("Failed to get MQTT clientId, return code\n");
    return 1;
  }

  Serial.print("Client ID: ");
  Serial.println(mqtt_client_id);

  Serial.print("Username: ");
  Serial.println(mqtt_username);

  Serial.print("Password: ");
  Serial.println(sas_token);

  mqtt_client.setBufferSize(MQTT_PACKET_SIZE);

  while (!mqtt_client.connected())
  {
    time_t now = time(NULL);

    Serial.print("MQTT connecting ... ");

    if (mqtt_client.connect(mqtt_client_id, mqtt_username, sas_token))
    {
      Serial.println("connected to Azure IOT Hub !");
    }
    else
    {
      Serial.print("failed, status code =");
      Serial.print(mqtt_client.state());
      Serial.println(". Trying again in 5 seconds.");
      // Wait 5 seconds before retrying
      delay(100);
    }
  }

  mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  return 0;
}

static void initializeClients()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  wifi_client.setTrustAnchors(&cert);
  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          &options)))
  {
    Serial.println("Failed initializing Azure IoT Hub client");
    return;
  }

  mqtt_client.setServer(host, port);
  mqtt_client.setCallback(receivedCallback);
}

static void establishConnection(){
  initializeTime();
  initializeClients();
  // The SAS token is valid for 1 hour by default in this sample.
  // After one hour the sample must be restarted, or the client won't be able
  // to connect/stay connected to the Azure IoT Hub.
  if (generateSasToken(sas_token, sizeofarray(sas_token)) != 0)
  {
    Serial.println("Failed generating MQTT password");
  }
  else
  {
    connectToAzureIoTHub();
  }

}

static char* getTelemetryPayload()
{
  az_span temp_span = az_span_create(telemetry_payload, sizeof(telemetry_payload));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("{ \"message\": "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" Pinging Azure IOT HUB "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" , "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" \"count\": "));
  (void)az_span_u32toa(temp_span, telemetry_send_count++, &temp_span);
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" }"));
  temp_span = az_span_copy_u8(temp_span, '\0');

  return (char*)telemetry_payload;
}

static void sendTelemetry()
{
  Serial.print(millis());
  Serial.print(" ESP8266 Sending telemetry . . . ");
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Serial.println("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  mqtt_client.publish(telemetry_topic, getTelemetryPayload(), false);
  Serial.println("OK");
}

static char* getUserDataPayload(String rfid){
  //Storing string data into uint8_t buffer
  int rfidLength = sizeof(rfid);
  uint8_t rfid_buffer[rfidLength];
  for (int i = 0; i < rfidLength; i++){
    rfid_buffer[i] = rfid[i];
  }  
  az_span temp_span = az_span_create(user_data_payload, sizeof(user_data_payload));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("{ \"message\": "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" Sending User RFID Data "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" , "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" \"RFID\": "));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("}ddddd"));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_BUFFER(rfid_buffer));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("}ddddd"));
  Serial.print("Current span size :");
  Serial.println(az_span_size(temp_span));
  Serial.println(temp_span[54]);
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" }"));
  temp_span = az_span_copy_u8(temp_span, '\0');
  return (char*)user_data_payload;
}

static void sendUserData(String rfid){
  Serial.println("Sending user RFID data to Azure IOT Hub");
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, send_user_data_topic, sizeof(send_user_data_topic), NULL)))
  {
    Serial.println("Failed az_iot_hub_client_send_user_data_publish_topic");
    return;
  }
  mqtt_client.publish(send_user_data_topic, getUserDataPayload(rfid), false);
}

void setup()
{
  Serial.begin(9600); //Initialising if(DEBUG)Serial Monitor
  Serial.println("Startup");
  timeClient.begin();
  SPI.begin();
  rfid.init();
  connectToWifi();
  establishConnection();
}

void loop() {
  if ((WiFi.status() == WL_CONNECTED))
  {
    digitalWrite(D2, HIGH);
  }
  else
  {
    digitalWrite(D2, LOW);
    establishConnection();
  }

  // Check if connected, reconnect if needed.
  if(!mqtt_client.connected())
  {
      establishConnection();
  }

  if (rfid.findCard(PICC_REQIDL, str) == MI_OK)   //Wait for a tag to be placed near the reader
  {
    Serial.println("Card found");
    String temp = "";                             //Temporary variable to store the read RFID number
    if (rfid.anticoll(str) == MI_OK)              //Anti-collision detection, read tag serial number
    {
      Serial.print("The card's ID number is : ");
      for (int i = 0; i < 4; i++)                 //Record and display the tag serial number
      {
        temp = temp + (0x0F & (str[i] >> 4));
        temp = temp + (0x0F & str[i]);
      }
      Serial.println (temp);
      sendUserData(temp);     //run pushuser function
    }
    rfid.selectTag(str); //Lock card to prevent a redundant read, removing the line will make the sketch read cards continually
  }
  rfid.halt();

  if (millis() > next_telemetry_send_time_ms)
  {
    sendTelemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }

  // MQTT loop must be called to process Device-to-Cloud and Cloud-to-Device.
  mqtt_client.loop();

}

void pushUser (String temp)    //defining pushuser function
{
  Serial.println("PUSHING USER ID: " + temp);
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime);
  int currentMonth = ptm->tm_mon + 1;
  int monthDay = ptm->tm_mday;
  int currentYear = ptm->tm_year + 1900;
  String currentDate = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay);
  Serial.println(currentDate);
  Serial.println(epochTime);
//  Firebase.pushString(firebaseData, uidPath + "users/" + temp, String(timeClient.getFormattedTime())+" / "+String (currentDate));
 // Firebase.pushString(firebaseData, uidPath + "users/" + temp, String (currentDate));
  //   Firebase.pushString(firebaseData, uidPath+"users/"+temp,1000);
  //      json.add("id", "fgf");
  //      json.add("uid", temp);
  //      json.add("time", String(timeClient.getFormattedTime()));
}