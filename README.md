# ESP32SmartBoard_MqttSensors_BleCfg

This Arduino project was developed as firmware for the *ESP32SmartBoard* (see hardware project [ESP32SmartBoard_PCB](https://github.com/ronaldsieber/ESP32SmartBoard_PCB)). It reads the values of the temperature and humidity sensor (DHT22), the CO2 sensor (MH-Z19) and the other peripherals of the board and transmits the information as MQTT messages to an MQTT broker. Incoming MQTT messages can be used to set outputs and configure the board.

This project also demonstrates the integration of the Bluetooth configuration framework implemented in [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) into a real ESP32/Arduino application.

The hardware configuration (port definitions) are adapted to the *ESP32SmartBoard* (see hardware project [ESP32SmartBoard_PCB](https://github.com/ronaldsieber/ESP32SmartBoard_PCB)).


## History

**Version 1**

Initial MQTT-based firmware [ESP32SmartBoard_MqttSensors](https://github.com/ronaldsieber/ESP32SmartBoard_MqttSensors) for the *ESP32SmartBoard* (see hardware project [ESP32SmartBoard_PCB](https://github.com/ronaldsieber/ESP32SmartBoard_PCB)).

**Version 2**

This Arduino project is the symbiosis of the two autonomous projects [ESP32SmartBoard_MqttSensors](https://github.com/ronaldsieber/ESP32SmartBoard_MqttSensors) and [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) to a common project. As a result of the integration of the Bluetooth Configuration Framework from the [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) project, the adjustments in the source code described in [ESP32SmartBoard_MqttSensors](https://github.com/ronaldsieber/ESP32SmartBoard_MqttSensors) for the following areas are not necessary:

 - WLAN configuration
 - MQTT configuration
 - Application configuration

These configuration settings are now made via Bluetooth using the graphical configuration tool from the [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) project.

![\[Project Components\]](Documentation/ESP32SmartBoard_MqttSensors_BleCfg.png)

**Version 3**

In the current version, the software has been revised and expanded in important areas:

- At system startup, the board sends a bootup message in JSON format with the current configuration (see section *"Bootup data block"* below)

- Instead of separate messages for the respective sensors, all sensor data is now sent in a single message in JSON format (see section *"Sensor data block"* below)

- The execution of the software is monitored by a watchdog monitor, optionally the acknowledgement of the MQTT packet numbers can also be included here (see section *"Watchdog monitor"* below)

It has also been shown that the DHT22 1-wire sensor reacts with slight noise to industrial switching power supplies for supplying the board. For this reason, the measured values for temperature and humidity are now averaged ("smoothed") using the Simple Moving Average filter from the [SimpleMovingAverage](https://github.com/ronaldsieber/SimpleMovingAverage) project over the number `APP_SMA_DHT_SAMPLE_WINDOW_SIZE` resp. `APP_SMA_DHT_SAMPLE_WINDOW_SIZE`.


## Project Overview

At system startup, the *ESP32SmartBoard* connects to an MQTT broker. The board then transmits ("publishes") the values of the temperature and humidity sensor (DHT22) and the CO2 sensor (MH-Z19) cyclically. The two buttons *KEY0* and *KEY1* are transmitted event-controlled. The board receives data from the broker via "Subscribe" and can thus be controlled and configured at runtime. Further details are described in the section *"MQTT messages of the ESP32SmartBoard"* below.

The configuration of the board (WLAN setup, MQTT broker, runtime configuration) is done via Bluetooth using the graphical configuration tool *"Esp32ConfigUwp"* from the [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) project (see section *"Board configuration"* below).

In a typical scenario, several *ESP32SmartBoards* are installed in different rooms of an apartment or in several offices or classrooms. All these boards transmit their data via MQTT to a common broker, which provides all sensor values in a central instance. This is then accessed by other clients which, for example, display the current temperature or CO2 level values of different rooms in the form of dashboards (e.g. Node-RED) or collect the sensor values in a database (e.g. InfluxDB). The measurement data saved as time series can then be graphically displayed and analyzed as time courses of the sensor values in charts (e.g. Grafana).

![\[Project Overview\]](Documentation/ESP32SmartBoard_MqttSensors.png)

The *ESP32SmartBoard_MqttSensors* project only contains the MQTT-based firmware for the *ESP32SmartBoard* from the setup described. Broker, database, dashboards etc. are not part of this project. In addition to this Arduino project, [ESP32SmartBoard_NodeRED](https://github.com/ronaldsieber/ESP32SmartBoard_NodeRED) implements a Node-RED-based dashboard for displaying sensor data and for runtime configuration of the board.

The open source tool *'MQTT Explorer'* (https://mqtt-explorer.com/) is suitable for commissioning and diagnostics.


## Board Configuration

The board is configured via Bluetooth (WLAN setup, MQTT broker, runtime configuration) using the graphical configuration tool *"Esp32ConfigUwp"* from the [ESP32BleConfig](https://github.com/ronaldsieber/ESP32BleConfig) .

To activate the Bluetooth configuration mode on the *ESP32SmartBoard*, the following steps are necessary:

1. press *BLE_CFG* on the *ESP32SmartBoard* and hold it down continuously until step 2
2. press and release Reset on the ESP32DevKit
3. release *BLE_CFG*

The *ESP32SmartBoard* can then be connected to the configuration tool *"Esp32ConfigUwp"* and configured by it. The necessary steps are described in detail at <https://github.com/ronaldsieber/ESP32BleConfig/blob/main/README.md>.

![\[Esp32ConfigUwp\]](Documentation/Esp32ConfigUwp_Customizing.png)


## Application configuration section

The following configuration section with default settings is located at the beginning of the sketch:

    const int  DEFAULT_CFG_ENABLE_NETWORK_SCAN  = 1;
    const int  DEFAULT_CFG_ENABLE_DI_DO         = 1;
    const int  DEFAULT_CFG_ENABLE_DHT_SENSOR    = 1;
    const int  DEFAULT_CFG_ENABLE_MHZ_SENSOR    = 1;
    const int  DEFAULT_CFG_ENABLE_STATUS_LED    = 1;
    const int  DEFAULT_CFG_ENABLE_HW_WDT        = 1;
    const int  DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV = 1;

This can be used to enable (=1) or disable (=0) the execution of the associated code sections at runtime. This avoids the occurrence of runtime errors on boards on which not all components are fitted (especially if the DHT22 or the MH-Z19 are not present).

These default settings can be overwritten and adjusted as required using the Bluetooth-based configuration described above.


## Bootup Data Block

The bootup data block is sent once after system startup and contains basic information about the system:

    {
      "PacketNum": 0,
      "FirmwareVer": "3.00",
      "BootReason": 3,
      "IP": "192.168.69.251",
      "ChipID": "240AC460D9A4",
      "DataPackCycleTm": 60,
      "CfgNetworkScan": 0,
      "CfgDiDo": 1,
      "CfgTmpHumSensor": 1,
      "CfgCO2Sensor": 1,
      "CfgStatusLed": 1,
      "CfgHwWdt": 1,
      "CfgSrvWdtOnMqtt": 1
    }

**PacketNum** *(Integer)*: Consecutive packet number since system startup (0 = bootup)
**FirmwareVer** *(String)*: Firmware version number in the format "Ver.Rev"
**BootReason** *(Integer)*: Reason for the last system boot (return value of `esp_reset_reason()`)
1 = `ESP_RST_POWERON` (Power-on)
3 = `ESP_RST_SW` (software reset by calling the esp_restart() function)
4 = `ESP_RST_PANIC` (software reset due to exception/panic)
7 = `ESP_RST_WDT` (watchdog reset)
9 = `ESP_RST_BROWNOUT` (brownout reset)
(complete overview: see esp_reset_reason_t in *"esp_system.h"*)
**IP** *(string)*: The IP address assigned by the DHCP server during WLAN Connect
**ChipID** *(String)*: Chip ID of the ESP32 (based on the return value of `ESP.getEfuseMac()`)
**DataPackCycleTm** *(Integer)*: Configured transmission interval for sensor data block in [sec].
**CfgNetworkScan** *(Integer)*: Configuration option `CFG_ENABLE_NETWORK_SCAN`
**CfgDiDo** *(integer)*: Configuration option `CFG_ENABLE_DI_DO`
**CfgTmpHumSensor** *(Integer)*: Configuration option `CFG_ENABLE_DHT_SENSOR`
**CfgCO2Sensor** *(integer)*: Configuration option `CFG_ENABLE_MHZ_SENSOR`
**CfgStatusLed** *(integer)*: Configuration option `CFG_ENABLE_STATUS_LED`
**CfgHwWdt** *(Integer)*: Configuration option `CFG_ENABLE_HW_WDT`
**CfgSrvWdtOnMqtt** *(integer)*: Configuration option `CFG_SRV_WDT_ON_MQTT_RECV`


## Sensor Data Block

The packet is transmitted cyclically with the transmission interval defined at runtime by the variable `ui32DataPackCycleTm_g` (default value: `DEFAULT_DATA_PACKET_PUB_CYCLE_TIME`). The interval for cyclical transmission can be modified at runtime via the topic *"SmBrd/\<DevID\>/Settings/DataPackPubCycleTime"*. The packet is also transmitted event-controlled when one of the two KEY0 and KEY1 buttons is changed.

    {
      "PacketNum": 168,
      "MainLoopCycle": 19779,
      "Uptime": 10356,
      "NetErrorLevel": 0,
      "Key0": 0,
      "Key0Chng": 0,
      "Key1": 0,
      "Key1Chng": 0,
      "Temperature": 21.1,
      "Humidity": 51.1,
      "Co2Value": 826,
      "Co2SensTemp": 26
    }

**PacketNum** *(Integer)*: Consecutive packet number since system startup (0 = bootup)
**MainLoopCycle** *(Integer)*: Number of `main()` loops since system startup
**Uptime** *(Integer)*: Uptime of the system in [sec]
**NetErrorLevel** *(Integer)*: Diagnosis, error value for MQTT transmission
In the event of an MQTT transmission error (`PubSubClient_g.publish() != true`), the error value is increased by the amount 2; after an error-free MQTT transmission error, the value is reduced again by the amount 1
**Key0** *(Integer)*: [0|1] - current status of the KEY0 button
**Key0Chng** *(integer)*: [0|1] - status of button KEY0 changed or unchanged
**Key1** *(integer)*: [0|1] - current status of the KEY1 button
**Key1Chng** *(integer)*: [0|1] - status of the KEY1 button changed or unchanged
**Temperature** *(float)*: Ambient temperature in [°C]
**Humidity** *(float)*: Relative humidity of the environment in [%]
**Co2Value** *(integer)*: CO2 measured value of the environment in [ppm]
**Co2SensTemp** *(integer)*: Temperature within the CO2 sensor


## MQTT Client Basics

The *ESP32SmartBoard* Sketch uses the *"Arduino Client for MQTT"* library (https://github.com/knolleary/pubsubclient) for MQTT communication with the broker. This library encapsulates the entire protocol implementation and is therefore very easy and convenient for the user to use:

The runtime object is created directly via the constructor:

    PubSubClient  PubSubClient(WiFiClient);

In order to inform the *ESP32SmartBoard* of the broker to be used, the IP address and port of the server must be set:

    PubSubClient.setServer(pszMqttServer, iMqttPort);

A corresponding callback handler must be registered so that the client on the *ESP32SmartBoard* can receive messages from the broker via subscribe:

    PubSubClient.setCallback(pfnMqttSubCallback);

In order to connect the local client on the *ESP32SmartBoard* to the broker, at least one unique ClientID and the login data in the form of a username and password are required. The ClientID is derived from the MAC address of the board, which ensures that each board uses its unique ID. Username and password must be specified on the client side, but whether these are actually evaluated depends on the configuration of the broker. Many brokers are operated openly, so that the login data used can be selected freely. For brokers with active authentication, the user data assigned by the server operator must be used.

Optionally, the topic and payload of a "LastWill" message can be defined when establishing the connection. The broker first saves this message and then publishes it later when the connection to the client is lost. When establishing the connection, the client can thus determine the message with which the broker informs all other participants about the termination of the connection between client and broker:

    PubSubClient.connect(strClientId.c_str(),            // ClientID
                         pszMqttUser,                    // Username
                         pszMqttPassword,                // Password
                         pszSupervisorTopic,             // LastWillTopic
                         0,                              // LastWillQoS
                         1,                              // LastWillRetain
                         strPayloadMsgGotLost.c_str(),   // LastWillMessage
                         true);                          // CleanSession

To publish a message, its topic and payload must be specified. A message marked as "Retain" is temporarily stored by the broker. This means that it can still be sent to a client if the client subscribes to the relevant topic only after the message has been published. This means that other clients can be synchronized with the current status of their partners when they log in to the broker:

    PubSubClient.publish(pszSupervisorTopic,                       // Topic
                         strPayloadMsgConnect.c_str(),             // Payload
                         1);                                       // Retain

Since the library does not use its own scheduler, it depends on being regularly supplied with CPU time:

    PubSubClient.loop();

The current connection status to the broker can be queried at runtime:

    PubSubClient.state();


## Individualization of the MQTT Topics at Runtime

In a typical scenario, several *ESP32SmartBoards* connects to a common broker. In order to be able to assign the data to the individual boards, each board uses individualized topics. This customization only takes place at runtime, so that the same sketch source can be used for all boards.

The generic topic templates are defined in static string arrays:

    const char*  MQTT_SUBSCRIBE_TOPIC_LIST_TEMPLATE[] =
    {
        "SmBrd/<%>/Settings/Heartbeat",            // "<%>" will be replaced by DevID
        "SmBrd/<%>/Settings/LedBarIndicator",      // "<%>" will be replaced by DevID
        "SmBrd/<%>/Settings/DataPackPubCycleTime", // "<%>" will be replaced by DevID
        "SmBrd/<%>/Settings/PrintSensorVal",       // "<%>" will be replaced by DevID
        "SmBrd/<%>/Settings/PrintMqttDataProc",    // "<%>" will be replaced by DevID
        "SmBrd/<%>/OutData/LedBar",                // "<%>" will be replaced by DevID
        "SmBrd/<%>/OutData/LedBarInv",             // "<%>" will be replaced by DevID
        "SmBrd/<%>/OutData/Led"                    // "<%>" will be replaced by DevID
        "SmBrd/<%>/Ack/PacketNum"                  // "<%>" will be replaced by DevID
    };
    
    const char*  MQTT_PUBLISH_TOPIC_LIST_TEMPLATE[] =
    {
        "SmBrd/<%>/Data/Bootup",                   // "<%>" will be replaced by DevID
        "SmBrd/<%>/Data/StData",                   // "<%>" will be replaced by DevID
    };

The board-specific customization is implemented by the `MqttBuildTopicFromTemplate()` function. The individualized stings are assigned to the following two arrays:

    String  astrMqttSubscribeTopicList_g[ARRAY_SIZE(MQTT_SUBSCRIBE_TOPIC_LIST_TEMPLATE)];
    String  astrMqttPublishTopicList_g[ARRAY_SIZE(MQTT_PUBLISH_TOPIC_LIST_TEMPLATE)];

For example, the topic for publishing the sensor data block has the following generic format:

    "SmBrd/Bedroom/Data/StData"

If no device name was specified via the configuration tool (empty string), the name defined using the `MQTT_DEVICE_ID` constant is used instead. If this is set to `NULL`, the individual board ID derived from the MAC address is ultimately used, e.g:

    "SmBrd/246F2822A4B8/Data/StData"


## MQTT Messages of the ESP32SmartBoard

The *ESP32SmartBoards* communicates with the broker via the MQTT messages described below. For both topics and payload only Strings are used as data type.

    Type:    Publish
    Topic:   SmBrd_Supervisor
    Payload: "CI=<ClientId>, IP=<LocalIP>, RC=[N][I][T][C][L][W][A], ST=[Connected|GotLost]"

This message is a diagnostic message. It is sent once as the first message after the connection has been successfully established to the broker. It contains the ClientID, the IP Address and the Runtime Configuration of the board. The same message is also used as "LastWill". The status "Connected" or "GotLost" indicates the establishment or termination of the connection.

The term used to describe the current runtime configuration ("RC=") contains a symbol for each code section activated at runtime:

    N : CFG_ENABLE_NETWORK_SCAN  == 1  // (N)etworkScan
    I : CFG_ENABLE_DI_DO         == 1  // (I)/O 
    T : CFG_ENABLE_DHT_SENSOR    == 1  // (T)emperature+Humidity
    C : CFG_ENABLE_MHZ_SENSOR    == 1  // (C)O2 
    L : CFG_ENABLE_STATUS_LED    == 1  // Status(L)ed
    W : CFG_ENABLE_HW_WDT        == 1  // (W)atchdog
    A : CFG_SRV_WDT_ON_MQTT_RECV == 1  // Serve Watchdog on (A)CK 

---

    Typ:     Publish
    Topic:   SmBrd/<DevID>/Data/Bootup
    Payload: <JsonBootupPacket>

The *JsonBootupPacket* is described in the section *"Bootup data block"*. The package is transferred once after system startup.

---

    Typ:     Publish
    Topic:   SmBrd/<DevID>/Data/StData
    Payload: <JsonDataPacket>

The *JsonDataPacket* is described in the section *"Sensor data block"*. The packet is transmitted cyclically with the transmission interval defined at runtime by the variable `ui32DataPackCycleTm_g` (default value: `DEFAULT_DATA_PACKET_PUB_CYCLE_TIME`). The transmission interval can be modified at runtime via the topic *"SmBrd/\<DevID\>/Settings/DataPackPubCycleTime"* (see below).

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/Settings/Heartbeat
    Payload: Payload=['0'|'1']

Sets the Heartbeat Mode (flashing of the blue LED on the ESP32DevKit):
0 = off
1 = on

The constant `DEFAULT_STATUS_LED_HEARTBEAT` defines the default value for this.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/Settings/LedBarIndicator
    Payload: Payload=['0'|'1'|'2'|'3']

Selects the data source for the LED Bar Indicator:
0 = kLedBarNone
1 = kLedBarDht22Temperature
2 = kLedBarDht22Humidity
3 = kLedBarMhz19Co2Level

The constant `DEFAULT_LED_BAR_INDICATOR` defines the default value for this.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/Settings/DataPackPubCycleTime
    Payload: Payload=<INT32>

Defines the send interval for the sensor data block (see topic *"SmBrd/\<DevID\>/Data/StData"* above):
\> 0 = transmission interval in seconds (limit: `MAX_SET_DATA_PACKET_PUB_CYCLE_TIME`)
   0 = send the sensor data block once without changing the send interval
< 0 = reset to the default value (`DEFAULT_DATA_PACKET_PUB_CYCLE_TIME`)

The default value is defined by the constant `DEFAULT_DATA_PACKET_PUB_CYCLE_TIME` in the sketch.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/Settings/PrintSensorVal
    Payload: Payload=['0'|'1']

Specifies whether the values read from the sensors are also displayed in the serial terminal window (115200Bd)
0 = off
1 = on

The constant `DEFAULT_PRINT_SENSOR_VALUES` defines the default value for this.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/Settings/PrintMqttDataProc
    Payload: Payload=['0'|'1']

Specifies whether the published and received MQTT messages are also displayed in the serial terminal window (115200Bd)
0 = off
1 = on

The constant `DEFAULT_PRINT_MQTT_DATA_PROC` defines the default value for this.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/OutData/LedBar
    Payload: Payload=['0'-'9']

Displays the specified value on the LED Bar Indicator. The data source for the LED Bar Indicator is set to 0 = kLedBarNone.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/OutData/LedBarInv
    Payload: Payload=['0'-'9']

Displays the specified value inversely on the LED Bar Indicator. The data source for the LED Bar Indicator is set to 0 = kLedBarNone.

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/OutData/Led
    Payload: Payload=['0'-'9']=['0'|'1']

Turns a single LED on the LED Bar Indicator on or off. The data source for the LED Bar Indicator is set to 0 = kLedBarNone.
0 = off
1 = on

---

    Typ:     Subscribe
    Topic:   SmBrd/<DevID>/Ack/PacketNum
    Payload: Payload=<INT32>

Is used as an optional trigger condition for the watchdog monitor.
A server acknowledges the packet number of the last received "Sensor Data Block" (variable `PacketNum` in *JsonDataPacket*) to the *ESP32SmartBoards*.

If the configuration option `CFG_SRV_WDT_ON_MQTT_RECV` is active, the watchdog monitor is operated depending on the last acknowledged packet number (see the following section *"Watchdog monitor"*).


## Watchdog Monitor

The Watchdog Monitor continuously monitors the time-based program sequence of the *ESP32SmartBoard*. If the system no longer operates the watchdog timer for a specified period of time, it triggers a reset and restarts the system.

**Watchdog Timer:**

The Watchdog Timer is only active if it has been enabled via the configuration option `CFG_ENABLE_HW_WDT`. This setting can be set via Bluetooth using the graphical configuration tool *"Esp32ConfigUwp"* (see section *"Board configuration"* above). If the watchdog timer has been activated, it must be triggered within the time interval defined by `APP_WDT_TIMEOUT`. This takes place during the cyclical program sequence within the main function `loop()`.

**Acknowledge Packet**

If the configuration option `CFG_SRV_WDT_ON_MQTT_RECV` is active, the Watchdog Monitor is operated depending on the last acknowledged packet number. This setting can be set via Bluetooth using the graphical configuration tool *"Esp32ConfigUwp"* (see section *"Board configuration"* above). The server, which processes the data sent by the *ESP32SmartBoards*, acknowledges the packet number of the last received *"Sensor Data Block"* (variable `PacketNum` in *JsonDataPacket*) via the topic *"SmBrd/\<DevID\>/Ack/PacketNum"*. If the last acknowledged packet number is more than `APP_WINSIZE_NON_ACK_MQTT_PACKETS` packets behind the last packet sent, this is interpreted as a loss of the WLAN connection and the watchdog timer is no longer operated. This leads to a reset and thus a restart of the *ESP32SmartBoard* with subsequent reestablishment of the connection.


## Calibration of the CO2 sensor MH-Z19

**An incorrectly performed calibration can brick the sensor. It is therefore important to understand how calibration works.**

The sensor is designed for use in 24/7 continuous operation. It supports the modes AutoCalibration, calibration via Hardware Signal (triggered manually) and calibration via Software Command (also triggered manually). The *ESP32SmartBoard* uses the AutoCalibration and manual calibration modes via software commands. Regardless of the method used, the calibration sets the sensor-internal reference value of 400 ppm CO2. A concentration of 400 ppm is considered to be the normal CO2 value in the earth's atmosphere, i.e. a typical value for the outside air in rural areas.

**(1) AutoCalibration:**

With AutoCalibration, the sensor permanently monitors the measured CO2 values. The lowest value measured within 24 hours is interpreted as the reference value of 400 ppm CO2. This method is recommended by the sensor manufacturer for use of the sensor in normal living rooms or in offices that are regularly well ventilated. It is implicitly assumed that the air inside the roorm is completely exchanged during ventilation and thus the CO2 concentration in the room falls down to the normal value of the earth's atmosphere / outside air.

However, the sensor manufacturer explicitly states in the datasheet that the AutoCalibration method cannot be used for use in agricultural greenhouses, farms, refrigerators, etc. AutoCalibration should be disabled here.

In the *ESP32SmartBoard* Sketch the constant `DEFAULT_MHZ19_AUTO_CALIBRATION` is used to activate (true) or deactivate (false) the AutoCalibration method. The AutoCalibration method is set on system startup. If necessary, the AutoCalibration method can be inverted using a push button. The following steps are required for this:

1. Press KEY0 and keep it pressed until step 3
2. Press and release Reset on the ESP32DevKit
3. Hold down KEY0 for another 2 seconds
4. Release KEY0

**Note:** The AutoCalibration mode can lead to sudden changes in the CO2 value, especially in the first few days after powering on the sensor. After some time in 24/7 continuous operation, this effect decreases more and more.

![\[CO2 Sensor AutoCalibration Discontinuity\]](Documentation/CO2Sensor_Autocalibration_Discontinuity.png)

The spontaneous discontinuities caused by the AutoCalibration mode can be avoided by manually calibrating the sensor.

**(2) Manually Calibration:**

Before a manual calibration, the sensor must be operated for at least 20 minutes in a stable reference environment with 400 ppm CO2. This requirement can only be approximated in the amateur and hobby area without a defined calibration environment. For this purpose, the *ESP32SmartBoard* can be operated outdoors in a shady place or inside a room near an open window, also in the shadow. In this environment, the *ESP32SmartBoard* must work for at least 20 minutes before the calibration can be triggered.

* (2a) Direct calibration:

If the *ESP32SmartBoard* has been working in the 400 ppm reference environment for at least 20 minutes (outdoors or inside at the open window), the sensor can be calibrated directly. The following steps are required for this:

1. Press KEY0 and keep it pressed until step 4
2. Press and release Reset on the ESP32DevKit
3. The *ESP32SmartBoard* starts a countdown of 9 seconds, the LED Bar blinkes every second and shows the remaining seconds
4. Keep KEY0 pressed the entire time until the countdown has elapsed and the LED Bar acknowledges the completed calibration with 3x quick flashes
5. Release KEY0

If KEY0 is released during the countdown, the *ESP32SmartBoard* aborts the procedure without calibrating the sensor. 

![\[Sequence Schema for direct manually Calibration\]](Documentation/SequenceSchema_ManuallyCalibration_Direct.png)
* (2b) Unattended, delayed Calibration:

The calibration can also be carried out unsupervised and with a time delay, when the *ESP32SmartBoard* has been placed in the 400 ppm reference environment (outdoors or inside at the open window). The following steps are required for this:

1. Press KEY1 and hold it down until step 2
2. Press and release Reset on the ESP32DevKit
3. Release KEY1

The *ESP32SmartBoard* starts a countdown of 25 minutes. During this countdown the LED Bar blinkes every second and shows the remaining time (25 minutes / 9 LEDs = 2:46 minutes / LED). After the countdown has elapsed, the calibration process is triggered and acknowledged with 3x quick flashing of the LED Bar. The *ESP32SmartBoard* is then restarted with a software reset.

![\[Sequence Schema for unattended, delayed Calibration\]](Documentation/SequenceSchema_ManuallyCalibration_Unattended.png)


## Run-time Output in the Serial Terminal Window

At runtime, all relevant information is output in the serial terminal window (115200Bd). In particular during the system start (sketch function `setup()`), error messages are also displayed here, which may be due to a faulty software configuration. These messages should be observed in any case, especially during commissioning.

In the main loop of the program (sketch function `main()`) the values of the DHT22 (temperature and humidity) and the MH-Z19 (CO2 level and sensor temperature) are displayed cyclically. In addition, messages here again provide information about any problems with accessing the sensors.

By activating the line `#define DEBUG` at the beginning of the sketch, further, very detailed runtime messages are displayed. These are particularly helpful during program development or for troubleshooting. By commenting out the line `#define DEBUG`, the output is suppressed again.


## Used Third Party Components 

1. **MQTT Library**
For the MQTT communication the library *"Arduino Client for MQTT"* is used: https://github.com/knolleary/pubsubclient. The installation is done with the Library Manager of the Arduino IDE.

2. **MH-Z19 CO2 Sensor**
The following driver libraries are used for the MH-Z19 CO2 sensor:
https://www.arduino.cc/reference/en/libraries/mh-z19/ and https://www.arduino.cc/en/Reference/SoftwareSerial. The installation is done with the Library Manager of the Arduino IDE.

3. **DHT Sensor**
The driver library from Adafruit is used for the DHT sensor (temperature, humidity). The installation is done with the Library Manager of the Arduino IDE.

4. **ArduinoJson**
The *"ArduinoJson"* library is used to create the JSON records:
https://github.com/bblanchon/ArduinoJson. The installation is done with the Library Manager of the Arduino IDE.

No third-party components are used for the Bluetooth configuration framework from the *"ESP32BleConfig"* project. Both the support for BLE and for the EEPROM are installed together with the Arduino ESP32 add-on.




