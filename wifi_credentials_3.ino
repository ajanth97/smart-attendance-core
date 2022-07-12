/**

   Written and Verified by the owner of techiesms
   Mr. Sachin Soni

   This code is open for fair use.
   If you are using it for commercial purpose, then it's mandatory to give credits

   Tutorial Video Link :-

*/

//#include <ESP8266WiFi.h>
#include <SPI.h>
#include <RFID.h>
#include "FirebaseESP8266.h"
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <WiFiUdp.h>

#define FIREBASE_HOST "smrt-atdnc-default-rtdb.asia-southeast1.firebasedatabase.app/" //Without http:// or https:// schemes
#define FIREBASE_AUTH "TzPzgZD9rRNW9WMfOT30OEADctj6djsGLOzun2Uu"
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
int statusCode;
const char* ssid = "text";
const char* passphrase = "text";
String st;
String content;


//Function Decalration
bool testWifi(void);
void launchWeb(void);
void setupAP(void);

//Establishing Local server at port 80 whenever required
ESP8266WebServer server(80);

void setup()
{

  Serial.begin(115200); //Initialising if(DEBUG)Serial Monitor
  Serial.println();
  Serial.println("Disconnecting previously connected WiFi");
  WiFi.disconnect();
  EEPROM.begin(512); //Initialasing EEPROM
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
    server.handleClient();
  }

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



void loop() {

//  timeClient.update();
//  time_t epochTime = timeClient.getEpochTime();
//  struct tm *ptm = gmtime ((time_t *)&epochTime);
//  int currentMonth = ptm->tm_mon + 1;
//  int monthDay = ptm->tm_mday;
//  int currentYear = ptm->tm_year + 1900;
//  String currentDate = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay);

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


  //  else
  //  {
  //  }

}


//----------------------------------------------- Fuctions used for WiFi credentials saving and connecting to it which you do not need to change
bool testWifi(void)
{
  int c = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( c < 50 ) {
    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    c++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}

void launchWeb()
{
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  createWebServer();
  // Start the server
  server.begin();
  Serial.println("Server started");
}

void setupAP(void)
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  st = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    st += "<li>";
    st += WiFi.SSID(i);
    st += " (";
    st += WiFi.RSSI(i);

    st += ")";
    st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    st += "</li>";
  }
  st += "</ol>";
  delay(100);
  WiFi.softAP("techiesms", "");
  Serial.println("softap");
  launchWeb();
  Serial.println("over");
}

void createWebServer()
{
  {
    server.on("/", []() {

      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 at ";
      content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
      content += ipStr;
      content += "<p>";
      content += st;
      content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
      content += "</html>";
      server.send(200, "text/html", content);
    });
    server.on("/scan", []() {
      //setupAP();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

      content = "<!DOCTYPE HTML>\r\n<html>go back";
      server.send(200, "text/html", content);
    });

    server.on("/setting", []() {
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      if (qsid.length() > 0 && qpass.length() > 0) {
        Serial.println("clearing eeprom");
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        Serial.println(qsid);
        Serial.println("");
        Serial.println(qpass);
        Serial.println("");

        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < qsid.length(); ++i)
        {
          EEPROM.write(i, qsid[i]);
          Serial.print("Wrote: ");
          Serial.println(qsid[i]);
        }
        Serial.println("writing eeprom pass:");
        for (int i = 0; i < qpass.length(); ++i)
        {
          EEPROM.write(32 + i, qpass[i]);
          Serial.print("Wrote: ");
          Serial.println(qpass[i]);
        }
        EEPROM.commit();

        content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
        statusCode = 200;
        ESP.reset();
      } else {
        content = "{\"Error\":\"404 not found\"}";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(statusCode, "application/json", content);

    });
  }
}
