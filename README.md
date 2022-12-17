# Smart Attendance Core

## Wiring
![Wiring Diagram](./wiring.png)

## Getting started

1. Clone the repo
2. Create a file named `AZURE_CRED.h` and place your azure credentials like follows :
 ```
#define IOT_CONFIG_IOTHUB_FQDN "<IOT_CONFIG_IOTHUB_FQDN>"
#define IOT_CONFIG_DEVICE_ID "<IOT_CONFIG_DEVICE_ID>"
#define IOT_CONFIG_DEVICE_KEY "<IOT_CONFIG_DEVICE_KEY>" 
 ```
3. Install and configure Arduino IDE
4. Preferably install the following Arduino vscode extension : https://github.com/microsoft/vscode-arduino
5. Upload the sketch !   