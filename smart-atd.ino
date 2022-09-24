#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include "src/RFID/RFID.h"
#include "src/FirebaseESP8266/FirebaseESP8266.h"
#include "src/NTPClient/NTPClient.h"
#include "src/wifi/wifi.h"
#include "FB_CRED.h" //Contains firebase credentials ! Included in gitignore

RFID rfid(D8, D0);       //D8:pin of tag reader SDA. D0:pin of tag reader RST
unsigned char str[MAX_LEN]; //MAX_LEN is 16: size of the array

WiFiUDP ntpUDP;
//const long utcOffsetInSeconds = 19800; //(UTC+5:30)

NTPClient timeClient(ntpUDP, "0.jp.pool.ntp.org", 32400);

//Week Days
String weekDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

//Month names
String months[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

//timeClient.setTimeOffset(19800);

String uidPath = "/";
//Define FirebaseESP8266 data object
FirebaseJson json;
FirebaseData firebaseData;

//Variables
int i = 0;
const char* ssid = "text";
const char* passphrase = "text";

void setup()
{
  Serial.begin(115200); //Initialising if(DEBUG)Serial Monitor
  Serial.println();
  Serial.println("Disconnecting previously connected WiFi");
  WiFi.disconnect();
  EEPROM.begin(512); //Initialasing EEPROM using 512 Bytes
  delay(10);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D3, OUTPUT);
  Serial.println();
  Serial.println();
  Serial.println("Startup");
  timeClient.begin();
  SPI.begin();
  rfid.init();

  // connect();
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);

  //---------------------------------------- Read eeprom for ssid and pass
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
    delay(1000);
    handleWebClient();
  }
}

void loop() {
  if ((WiFi.status() == WL_CONNECTED))
  {
    digitalWrite(D2, HIGH);
  }
  else
  {
    digitalWrite(D2, HIGH);
    delay(100);
    digitalWrite(D2, LOW);
    delay(100);
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
      pushUser (temp);     //run pushuser function
    }
    rfid.selectTag(str); //Lock card to prevent a redundant read, removing the line will make the sketch read cards continually
  }
  rfid.halt();
}

//----------------------------------------------- Fuctions used for WiFi credentials saving and connecting to it which you do not need to change

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

  Firebase.pushString(firebaseData, uidPath + "users/" + temp, String(timeClient.getFormattedTime())+" / "+String (currentDate));
 // Firebase.pushString(firebaseData, uidPath + "users/" + temp, String (currentDate));
  //   Firebase.pushString(firebaseData, uidPath+"users/"+temp,1000);
  //      json.add("id", "fgf");
  //      json.add("uid", temp);
  //      json.add("time", String(timeClient.getFormattedTime()));

  digitalWrite(D3, HIGH);
  delay(500);
  digitalWrite(D3, LOW);
}