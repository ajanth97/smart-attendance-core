#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>
#include <ESP8266WiFi.h>


// Function prototypes
bool testWifi(void);
void setupAP(void);
void launchWeb();
void handleWebClient();

//Variables
extern String st;
extern String content;

#endif