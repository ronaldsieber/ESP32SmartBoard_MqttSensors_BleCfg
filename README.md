# ESP32SmartBoard_MqttSensors_BleCfg

This Arduino project is the symbiosis of the two autonomous projects [ESP32SmartBoard_MqttSensors](https://github.com/ronaldsieber/ESP32SmartBoard_MqttSensors) and [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) to a common project. As a result of the integration of the Bluetooth Configuration Framework from the [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) project, the adjustments in the source code described in [ESP32SmartBoard_MqttSensors](https://github.com/ronaldsieber/ESP32SmartBoard_MqttSensors) for the following areas are not necessary:

 - WLAN configuration
 - MQTT configuration
 - Application configuration

These configuration settings are now made via Bluetooth using the graphical configuration tool.

![\[Project Overview\]](Documentation/ESP32SmartBoard_MqttSensors_BleCfg.png)

This project illustrates the integration of the Bluetooth Configuration Framework implemented in [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) into a real ESP32/Arduino application. The hardware configuration (Port definitions) are matched to the *ESP32SmartBoard* (see hardware project [ESP32SmartBoard_PCB](https://github.com/ronaldsieber/ESP32SmartBoard_PCB)).


## Used Third Party Components 

1. **MQTT Library**
For the MQTT communication the library "Arduino Client for MQTT" is used: https://github.com/knolleary/pubsubclient. The installation is done with the Library Manager of the Arduino IDE.

2. **MH-Z19 CO2 Sensor**
The following driver libraries are used for the MH-Z19 CO2 sensor:
https://www.arduino.cc/reference/en/libraries/mh-z19/ and https://www.arduino.cc/en/Reference/SoftwareSerial. The installation is done with the Library Manager of the Arduino IDE.

3. **DHT Sensor**
The driver library from Adafruit is used for the DHT sensor (temperature, humidity). The installation is done with the Library Manager of the Arduino IDE.

No third-party components are used for the Bluetooth Configuration Framework from the [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) project. Both BLE and EEPROM support are installed along with the Arduino ESP32 add-on.



