/****************************************************************************

  Copyright (c) 2021 - 2025 Ronald Sieber

  Project:      ESP32SmartBoard / MQTT Sensors
  Description:  MQTT Client Firmware to interact with ESP32SmartBoard

  -------------------------------------------------------------------------

    Arduino IDE Settings:

    Board:              "ESP32 Dev Module"
    Upload Speed:       "115200"
    CPU Frequency:      "240MHz (WiFi/BT)"
    Flash Frequency:    "80Mhz"
    Flash Mode:         "QIO"
    Flash Size:         "4MB (32Mb)"
    Partition Scheme:   "No OTA (2MB APP/2MB SPIFFS)"
    PSRAM:              "Disabled"

  -------------------------------------------------------------------------

  Revision History:

  2020/05/14 -rs:   V1.00 Initial version
  2021/01/02 -rs:   V1.10 Add Support for MH-Z19 CO2 Sensor
  2021/07/08 -rs:   V2.00 Integration of BLE Configuration
  2024/11/08 -rs:   V3.00 Moving MQTT data into JSON objects, add Watchdog

****************************************************************************/

#define DEBUG                                                           // Enable/Disable TRACE
// #define DEBUG_DUMP_BUFFER


#include <WiFi.h>
#include <PubSubClient.h>                                               // Requires Library "PubSubClient" by Nick O'Leary
#include <DHT.h>                                                        // Requires Library "DHT sensor library" by Adafruit
#include <MHZ19.h>                                                      // Requires Library "MH-Z19" by Jonathan Dempsey
#include <SoftwareSerial.h>                                             // Requires Library "EspSoftwareSerial" by Dirk Kaar, Peter Lerup
#include <ArduinoJson.h>                                                // Requires Library "ArduinoJson" by Benoit Blanchon
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include "ESP32BleCfgProfile.h"
#include "ESP32BleAppCfgData.h"
#include "SimpleMovingAverage.hpp"
#include "Trace.h"





/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
//  Application Configuration
//---------------------------------------------------------------------------

const int       APP_VERSION                         = 3;                // 3.xx
const int       APP_REVISION                        = 0;                // x.00
const char      APP_BUILD_TIMESTAMP[]               = __DATE__ " " __TIME__;

// Default Application Configuration (will be mapped at runtime via structure <AppCfgData_g>)
const int       DEFAULT_CFG_ENABLE_NETWORK_SCAN     = 1;
const int       DEFAULT_CFG_ENABLE_DI_DO            = 1;
const int       DEFAULT_CFG_ENABLE_DHT_SENSOR       = 1;
const int       DEFAULT_CFG_ENABLE_MHZ_SENSOR       = 1;
const int       DEFAULT_CFG_ENABLE_STATUS_LED       = 1;
const int       DEFAULT_CFG_ENABLE_HW_WDT           = 1;                // ESP32 internal Task Watchdog Timer (TWDT)
const int       DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV    = 1;                // Serve/Trigger of Watchdog is dependent on the reception of MQTT Data Packets

// EEPROM Size
#define         APP_EEPROM_SIZE                     512

// Application specific Device Type
#define         APP_DEVICE_TYPE                     1000000             // DeviceType associated with the BLE Profile

// Default/initial values for Application Configuration <tAppCfgData>
#define         APP_CFGDATA_MAGIC_ID                0x45735243          // ASCII 'EsRC' = [Es]p32[R]emote[C]onfig
#define         APP_DEFAULT_DEVICE_NAME             "{SmBrd_HIC}"       // will be replaced at runtime by <strChipID_g>
#define         APP_DEFAULT_WIFI_SSID               "{WIFI SSID Name}"
#define         APP_DEFAULT_WIFI_PASSWD             "{WIFI Password}"
#define         APP_DEFAULT_WIFI_OWNADDR            "0.0.0.0:0"         // DHCP: IP(0,0,0,0), otherwise static IP Address if running in Station Mode
#define         APP_DEFAULT_WIFI_OWNMODE            WIFI_OPMODE_STA     // WIFI_OPMODE_STA / WIFI_OPMODE_AP
#define         APP_DEFAULT_APP_RT_OPT1             DEFAULT_CFG_ENABLE_NETWORK_SCAN
#define         APP_DEFAULT_APP_RT_OPT2             DEFAULT_CFG_ENABLE_DI_DO
#define         APP_DEFAULT_APP_RT_OPT3             DEFAULT_CFG_ENABLE_DHT_SENSOR
#define         APP_DEFAULT_APP_RT_OPT4             DEFAULT_CFG_ENABLE_MHZ_SENSOR
#define         APP_DEFAULT_APP_RT_OPT5             DEFAULT_CFG_ENABLE_STATUS_LED
#define         APP_DEFAULT_APP_RT_OPT6             DEFAULT_CFG_ENABLE_HW_WDT
#define         APP_DEFAULT_APP_RT_OPT7             DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV
#define         APP_DEFAULT_APP_RT_OPT8             false
#define         APP_DEFAULT_APP_RT_PEERADDR         "0.0.0.0:0"

// Application specific Descriptor Text ('Labels') resp. FeatureLists for BLE Characteristics <tAppDescriptData>
#define         APP_DESCRPT_WIFI_OWNMODE_FEATLIST   (WIFI_OPMODE_STA)   // (WIFI_OPMODE_STA | WIFI_OPMODE_AP)

#define         APP_LABEL_APP_RT_OPT1               "Network Scan"
#define         APP_LABEL_APP_RT_OPT2               "Use DI/DO"
#define         APP_LABEL_APP_RT_OPT3               "Use DHT22 Sensor"
#define         APP_LABEL_APP_RT_OPT4               "Use MH-Z19 Sensor"
#define         APP_LABEL_APP_RT_OPT5               "Use Status LED"
#define         APP_LABEL_APP_RT_OPT6               "Use HW WDT"
#define         APP_LABEL_APP_RT_OPT7               "Srv WDT on MQTT Recv"
#define         APP_LABEL_APP_RT_OPT8               "# (not used)"      // Start with '#' -> disable in GUI Config Tool
#define         APP_LABEL_APP_RT_PEERADDR           "MQTT Broker"

// Simple Moving Average Filter Size
const int       APP_SMA_DHT_SAMPLE_WINDOW_SIZE      = 10;               // Window Size of Simple Moving Average Filter for DHT-Sensor (Temperature/Humidity)

// Watchdog / Program Execution Monitoring
const ulong     APP_WDT_TIMEOUT                     = 5;                // WatchDog Timeout in [sec]
const ulong     APP_WINSIZE_NON_ACK_MQTT_PACKETS    = 5;                // Window size for non-acknowledged MQTT Data Packets



//---------------------------------------------------------------------------
//  Definitions
//---------------------------------------------------------------------------

typedef  void (*tMqttSubCallback)(const char* pszMsgTopic_p, const uint8_t* pMsgPayload_p, unsigned int uiMsgPayloadLen_p);

#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])



//---------------------------------------------------------------------------
//  WLAN/WIFI Configuration
//---------------------------------------------------------------------------

// WIFI Station/Client Configuration
// const char*  WIFI_STA_SSID                       // -> <AppCfgData_g.m_szWifiSSID>
// const char*  WIFI_STA_PASSWORD                   // -> <AppCfgData_g.m_szWifiPasswd>
// IPAddress    WIFI_STA_LOCAL_IP(0,0,0,0);         // -> <AppCfgData_g.m_szWifiOwnAddr>

// PowerSavingMode for WLAN Interface
//
// Modem-sleep mode includes minimum and maximum power save modes.
// In minimum power save mode, station wakes up every DTIM to receive beacon.
// Broadcast data will not be lost because it is transmitted after DTIM.
// However, it can not save much more power if DTIM is short for DTIM is determined by AP.
//
// WIFI_PS_NONE:        No PowerSaving,     Power Consumption: 110mA, immediate processing of receipt mqtt messages
// WIFI_PS_MIN_MODEM:   PowerSaving active, Power Consumption:  45mA, up to 5 sec delayed processing of receipt mqtt messages
// WIFI_PS_MAX_MODEM:   interval to receive beacons is determined by the listen_interval parameter in <wifi_sta_config_t>
wifi_ps_type_t  WIFI_POWER_SAVING_MODE              = WIFI_PS_NONE;     // Disable ESP32 WiFi Power-Saving Mode, otherwise received packets will only be processed with a very long delay (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html)

const uint32_t  WIFI_TRY_CONNECT_TIMEOUT            = (30 * 1000);      // Timeout for trying to connect to WLAN [ms]

const String    astrWiFiStatus_g[]                  = { "WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "WL_SCAN_COMPLETED", "WL_CONNECTED", "WL_CONNECT_FAILED", "WL_CONNECTION_LOST", "WL_DISCONNECTED" };



//---------------------------------------------------------------------------
//  Runtime Configuration
//---------------------------------------------------------------------------

// Toggle Time Periods for Status LED
const uint32_t  STATUS_LED_PERIOD_NET_SCAN          = 50;               // 10Hz   = 50ms On + 50ms Off
const uint32_t  STATUS_LED_PERIOD_NET_CONNECT       = 100;              // 5Hz    = 100ms On + 100ms Off
const uint32_t  STATUS_LED_PERIOD_MQTT_CONNECT      = 200;              // 2.5Hz  = 200ms On + 200ms Off
const uint32_t  STATUS_LED_PERIOD_MAIN_LOOP         = 2000;             // 0.25Hz = 2000ms On + 2000ms Off

// Data Sources for LED Bar Indicator
typedef enum
{
    kLedBarNone             = 0,
    kLedBarDht22Temperature = 1,
    kLedBarDht22Humidity    = 2,
    kLedBarMhz19Co2Level    = 3

} tLedBar;

// Default Board Configuration Settings
const int       DEFAULT_STATUS_LED_HEARTBEAT        = true;
const tLedBar   DEFAULT_LED_BAR_INDICATOR           = kLedBarMhz19Co2Level;
const bool      DEFAULT_PRINT_SENSOR_VALUES         = true;
const bool      DEFAULT_PRINT_MQTT_DATA_PROC        = true;

// MH-Z19 Settings
const bool      DEFAULT_MHZ19_AUTO_CALIBRATION      = true;
const bool      DEFAULT_DBG_MHZ19_PRINT_COMM        = false;

const uint32_t  DEFAULT_DATA_PACKET_PUB_CYCLE_TIME  = (     60 * 1000); // Time between MQTT DataPackets [ms]
const uint32_t  MAX_SET_DATA_PACKET_PUB_CYCLE_TIME  = (30 * 60 * 1000); // max. permissible configuration value for Time between MQTT DataPackets [ms]



//---------------------------------------------------------------------------
//  Hardware/Pin Configuration
//---------------------------------------------------------------------------

const int       PIN_KEY_BLE_CFG                     = 36;               // PIN_KEY_BLE_CFG      (GPIO36 -> Pin02)
const int       PIN_KEY0                            = 35;               // KEY0                 (GPIO35 -> Pin05)
const int       PIN_KEY1                            = 34;               // KEY1                 (GPIO34 -> Pin04)

const int       PIN_LED0                            = 13;               // LED0 (green)         (GPIO13 -> Pin13)
const int       PIN_LED1                            = 12;               // LED1 (green)         (GPIO12 -> Pin12)
const int       PIN_LED2                            = 14;               // LED2 (green)         (GPIO14 -> Pin11)
const int       PIN_LED3                            =  4;               // LED3 (yellow)        (GPIO04 -> Pin20)
const int       PIN_LED4                            =  5;               // LED4 (yellow)        (GPIO05 -> Pin23)
const int       PIN_LED5                            = 18;               // LED5 (yellow)        (GPIO18 -> Pin24)
const int       PIN_LED6                            = 19;               // LED6 (red)           (GPIO19 -> Pin25)
const int       PIN_LED7                            = 21;               // LED7 (red)           (GPIO21 -> Pin26)
const int       PIN_LED8                            = 22;               // LED8 (red)           (GPIO22 -> Pin29)
const int       PIN_STATUS_LED                      =  2;               // On-board LED (blue)  (GPIO02 -> Pin19)

const int       DHT_TYPE                            = DHT22;            // DHT11, DHT21 (AM2301), DHT22 (AM2302,AM2321)
const int       DHT_PIN_SENSOR                      = 23;               // PIN used for DHT22 (AM2302/AM2321)
const uint32_t  DHT_SENSOR_SAMPLE_PERIOD            = (5 * 1000);       // Sample Period for DHT-Sensor in [ms]

const int       MHZ19_PIN_SERIAL_RX                 = 32;               // ESP32 Rx pin which the MH-Z19 Tx pin is attached to
const int       MHZ19_PIN_SERIAL_TX                 = 33;               // ESP32 Tx pin which the MH-Z19 Rx pin is attached to
const int       MHZ19_BAUDRATE_SERIAL               = 9600;             // Serial baudrate for communication with MH-Z19 Device
const uint32_t  MHZ19_SENSOR_SAMPLE_PERIOD          = (10 * 1000);      // Sample Period for MH-Z19-Sensor in [ms]



//---------------------------------------------------------------------------
//  MQTT Configuration
//---------------------------------------------------------------------------

// const char*  MQTT_SERVER                         // -> <AppCfgData_g.m_szAppRtPeerAddr>
// const int    MQTT_PORT                           // -> <AppCfgData_g.m_szAppRtPeerAddr>
const char*     MQTT_USER                           = "SmBrd_HIC";
const char*     MQTT_PASSWORD                       = "xxx";
const char*     MQTT_CLIENT_PREFIX                  = "SmBrd_";         // will be extended by ChipID
const char*     MQTT_DEVICE_ID                      = NULL;             // will be used to generate Device specific Topics
const char*     MQTT_SUPERVISOR_TOPIC               = "SmBrd_Supervisor";
const uint16_t  MQTT_BUFFER_SIZE                    = 2048;             // BuffSize must be at least sufficient for: (sizeof(TOPIC) + sizeof(PAYLOAD) + sizeof(MQTT_HEADER))

// IMPORTANT: The Entries in this Array are accessed via fixed indices
//            within the Function 'AppProcessMqttDataMessage()'.
const char*     MQTT_SUBSCRIBE_TOPIC_LIST_TEMPLATE[] =
{
    "SmBrd/<%>/Settings/Heartbeat",                                     // "<%>" will be replaced by DevID
    "SmBrd/<%>/Settings/LedBarIndicator",                               // "<%>" will be replaced by DevID
    "SmBrd/<%>/Settings/DataPackPubCycleTime",                          // "<%>" will be replaced by DevID
    "SmBrd/<%>/Settings/PrintSensorVal",                                // "<%>" will be replaced by DevID
    "SmBrd/<%>/Settings/PrintMqttDataProc",                             // "<%>" will be replaced by DevID
    "SmBrd/<%>/OutData/LedBar",                                         // "<%>" will be replaced by DevID
    "SmBrd/<%>/OutData/LedBarInv",                                      // "<%>" will be replaced by DevID
    "SmBrd/<%>/OutData/Led",                                            // "<%>" will be replaced by DevID
    "SmBrd/<%>/Ack/PacketNum"                                           // "<%>" will be replaced by DevID
};

const char*     MQTT_PUBLISH_TOPIC_LIST_TEMPLATE[] =
{
    "SmBrd/<%>/Data/Bootup",                                            // "<%>" will be replaced by DevID
    "SmBrd/<%>/Data/StData",                                            // "<%>" will be replaced by DevID
};



//---------------------------------------------------------------------------
//  Local types
//---------------------------------------------------------------------------

// Device Configuration -> Bootup Message
typedef struct
{
    uint32_t    m_ui32MqttPacketNum;
    int         m_iFirmwareVersion;
    int         m_iFirmwareRevision;
    int         m_eBootReason;
    String      m_strChipID;
    IPAddress   m_LocalIP;
    uint32_t    m_ui32DataPackCycleTm;
    int         m_fCfgNetworkScan;
    int         m_fCfgDiDo;
    int         m_fCfgTmpHumSensor;
    int         m_fCfgCO2Sensor;
    int         m_fCfgStatusLed;
    int         m_fCfgHwWdt;
    int         m_fCfgSrvWdtOnMqttRecv;

} tDeviceConfig;

// SensorData Record -> Cyclic Data Message
typedef struct
{
    uint32_t    m_ui32MqttPacketNum;
    ulong       m_ulMainLoopCycle;
    uint32_t    m_ui32Uptime;
    uint16_t    m_ui16NetErrorLevel;
    int         m_iKey0;
    int         m_iKey0Changed;
    int         m_iKey1;
    int         m_iKey1Changed;
    float       m_flTemperature;
    float       m_flHumidity;
    int         m_iCo2Value;
    int         m_iCo2SensTemp;

} tSensorData;



//---------------------------------------------------------------------------
//  Local Variables
//---------------------------------------------------------------------------

static  ESP32BleCfgProfile  ESP32BleCfgProfile_g;
static  ESP32BleAppCfgData  ESP32BleAppCfgData_g(APP_EEPROM_SIZE);


static tAppCfgData  AppCfgData_g =
{

    APP_CFGDATA_MAGIC_ID,                           // .m_ui32MagicID

    APP_DEFAULT_DEVICE_NAME,                        // .m_szDevMntDevName

    APP_DEFAULT_WIFI_SSID,                          // .m_szWifiSSID
    APP_DEFAULT_WIFI_PASSWD,                        // .m_szWifiPasswd
    APP_DEFAULT_WIFI_OWNADDR,                       // .m_szWifiOwnAddr
    APP_DEFAULT_WIFI_OWNMODE,                       // .m_ui8WifiOwnMode

    APP_DEFAULT_APP_RT_OPT1,                        // .m_fAppRtOpt1 : 1    -> DEFAULT_CFG_ENABLE_NETWORK_SCAN
    APP_DEFAULT_APP_RT_OPT2,                        // .m_fAppRtOpt2 : 1    -> DEFAULT_CFG_ENABLE_DI_DO
    APP_DEFAULT_APP_RT_OPT3,                        // .m_fAppRtOpt3 : 1    -> DEFAULT_CFG_ENABLE_DHT_SENSOR
    APP_DEFAULT_APP_RT_OPT4,                        // .m_fAppRtOpt4 : 1    -> DEFAULT_CFG_ENABLE_MHZ_SENSOR
    APP_DEFAULT_APP_RT_OPT5,                        // .m_fAppRtOpt5 : 1    -> DEFAULT_CFG_ENABLE_STATUS_LED
    APP_DEFAULT_APP_RT_OPT6,                        // .m_fAppRtOpt6 : 1    -> DEFAULT_CFG_ENABLE_HW_WDT
    APP_DEFAULT_APP_RT_OPT7,                        // .m_fAppRtOpt7 : 1    -> DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV
    APP_DEFAULT_APP_RT_OPT8,                        // .m_fAppRtOpt8 : 1
    APP_DEFAULT_APP_RT_PEERADDR                     // .m_szAppRtPeerAddr

};

static tAppDescriptData  AppDescriptData_g =
{

    APP_DESCRPT_WIFI_OWNMODE_FEATLIST,              // .m_ui16OwnModeFeatList

    APP_LABEL_APP_RT_OPT1,                          // .m_pszLabelOpt1
    APP_LABEL_APP_RT_OPT2,                          // .m_pszLabelOpt2
    APP_LABEL_APP_RT_OPT3,                          // .m_pszLabelOpt3
    APP_LABEL_APP_RT_OPT4,                          // .m_pszLabelOpt4
    APP_LABEL_APP_RT_OPT5,                          // .m_pszLabelOpt5
    APP_LABEL_APP_RT_OPT6,                          // .m_pszLabelOpt6
    APP_LABEL_APP_RT_OPT7,                          // .m_pszLabelOpt7
    APP_LABEL_APP_RT_OPT8,                          // .m_pszLabelOpt8

    APP_LABEL_APP_RT_PEERADDR                       // .m_pszLabelPeerAddr

};


static  IPAddress       WifiOwnIpAddress_g          = IPAddress(0,0,0,0);
static  char            szWifiOwnIpAddress_g[16]    = "";
static  uint16_t        ui16WifiOwnPortNum_g        = 0;

static  IPAddress       AppRtPeerIpAddress_g        = IPAddress(0,0,0,0);
static  char            szAppRtPeerIpAddress_g[16]  = "";
static  uint16_t        ui16AppRtPeerPortNum_g      = 1883;

static  bool            fStateBleCfg_g;
static  bool            fBleClientConnected_g       = false;

static  WiFiClient      WiFiClient_g;
static  PubSubClient    PubSubClient_g(WiFiClient_g);

static  String          strChipID_g;
static  String          strDeviceID_g;
static  String          strClientName_g;

static  ulong           ulMainLoopCycle_g           = 0;
static  unsigned int    uiMainLoopProcStep_g        = 0;
static  uint32_t        ui32LastTickWdtService_g    = 0;
static  uint16_t        ui16NetErrorLevel_g         = 0;
static  int             iLastStateKey0_g            = -1;
static  int             iLastStateKey1_g            = -1;

static  DHT             DhtSensor_g(DHT_PIN_SENSOR, DHT_TYPE);
static  uint32_t        ui32LastTickDhtRead_g       = -(DHT_SENSOR_SAMPLE_PERIOD);              // force reading already in the very first process cycle
static  float           flDhtTemperature_g          = 0;
static  float           flDhtHumidity_g             = 0;

static  MHZ19           Mhz19Sensor_g;
static  SoftwareSerial  Mhz19SoftSerial_g(MHZ19_PIN_SERIAL_RX, MHZ19_PIN_SERIAL_TX);
static  uint32_t        ui32LastTickMhz19Read_g     = -(MHZ19_SENSOR_SAMPLE_PERIOD);            // force reading already in the very first process cycle
static  int             iMhz19Co2Value_g            = 0;
static  int             iMhz19Co2SensTemp_g         = 0;
static  bool            fMhz19AutoCalibration_g     = DEFAULT_MHZ19_AUTO_CALIBRATION;
static  bool            fMhz19DbgPrintComm_g        = DEFAULT_DBG_MHZ19_PRINT_COMM;

static  bool            fStatusLedHeartbeat_g       = DEFAULT_STATUS_LED_HEARTBEAT;
static  hw_timer_t*     pfnOnTimerISR_g             = NULL;
static  volatile byte   bStatusLedState_g           = LOW;
static  uint32_t        ui32LastTickStatLedToggle_g = 0;

static  tLedBar         LedBarIndicator_g           = DEFAULT_LED_BAR_INDICATOR;
static  bool            fPrintSensorValues_g        = DEFAULT_PRINT_SENSOR_VALUES;
static  bool            fPrintMqttDataProc_g        = DEFAULT_PRINT_MQTT_DATA_PROC;

static  tDeviceConfig   DeviceConfig_g              = { 0 };
static  tSensorData     SensorDataRec_g             = { 0 };

static  uint32_t        ui32MqttPacketNum_g         = 0;
static  uint32_t        ui32LastAckMqttPacketNum_g  = 0;
static  uint32_t        ui32DataPackCycleTm_g       = DEFAULT_DATA_PACKET_PUB_CYCLE_TIME;
static  uint32_t        ui32LastTickDataPackPub_g   = -(DEFAULT_DATA_PACKET_PUB_CYCLE_TIME);    // force reading already in the very first process cycle

static  String          astrMqttSubscribeTopicList_g[ARRAY_SIZE(MQTT_SUBSCRIBE_TOPIC_LIST_TEMPLATE)];
static  String          astrMqttPublishTopicList_g[ARRAY_SIZE(MQTT_PUBLISH_TOPIC_LIST_TEMPLATE)];

static  esp_reset_reason_t                          eBootReason_g;

static  SimpleMovingAverage<float>                  AverageTemperature_g(APP_SMA_DHT_SAMPLE_WINDOW_SIZE);
static  SimpleMovingAverage<float>                  AverageHumidity_g(APP_SMA_DHT_SAMPLE_WINDOW_SIZE);





//=========================================================================//
//                                                                         //
//          S K E T C H   P U B L I C   F U N C T I O N S                  //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Application Setup
//---------------------------------------------------------------------------

void setup()
{

char    szTextBuff[64];
String  strBootReason;
char    acMhz19Version[4];
String  strJsonBootupPacket;
int     iResult;
int     iMhz19Param;
bool    fMhz19AutoCalibration;
bool    fStateKey0;
bool    fStateKey1;
bool    fSensorCalibrated;
int     iIdx;


    // Serial console
    Serial.begin(115200);
    Serial.println();
    Serial.println();
    Serial.println("======== APPLICATION START ========");
    Serial.println();
    Serial.flush();


    // Application Version Information
    snprintf(szTextBuff, sizeof(szTextBuff), "App Version:      %u.%02u", APP_VERSION, APP_REVISION);
    Serial.println(szTextBuff);
    snprintf(szTextBuff, sizeof(szTextBuff), "Build Timestamp:  %s", APP_BUILD_TIMESTAMP);
    Serial.println(szTextBuff);
    Serial.println();
    Serial.flush();


    // Device Identification
    strChipID_g = GetChipID();
    Serial.print("Unique ChipID:    ");
    Serial.println(strChipID_g);
    Serial.println();
    Serial.flush();


    // Initialize Workspace
    ulMainLoopCycle_g          = 0;
    uiMainLoopProcStep_g       = 0;
    ui16NetErrorLevel_g        = 0;
    fBleClientConnected_g      = false;
    flDhtTemperature_g         = 0;
    flDhtHumidity_g            = 0;
    iMhz19Co2Value_g           = 0;
    iMhz19Co2SensTemp_g        = 0;
    ui32MqttPacketNum_g        = 0;
    ui32LastAckMqttPacketNum_g = 0;


    // Evaluate Boot/Restart Reason
    eBootReason_g = esp_reset_reason();
    Serial.print("Boot/Restart Reason: ");
    Serial.print((int)eBootReason_g);
    Serial.print(" -> ");
    strBootReason = DecodeBootReason(eBootReason_g);
    Serial.println(strBootReason);


    // Get Configuration Data from EEPROM
    // (try to get Data from EEPROM, otherwise keep default values untouched)
    Serial.println("Configuration Data Block Size: " + String(sizeof(tAppCfgData)) + " Bytes");
    Serial.println("Get Configuration Data...");
    strncpy(AppCfgData_g.m_szDevMntDevName, strChipID_g.c_str(), sizeof(AppCfgData_g.m_szDevMntDevName));
    iResult = ESP32BleAppCfgData_g.LoadAppCfgDataFromEeprom(&AppCfgData_g);
    if (iResult == 1)
    {
        Serial.print("-> Use saved Data read from EEPROM");
    }
    else if (iResult == 0)
    {
        Serial.println("-> Keep default data untouched");
    }
    else
    {
        Serial.print("-> ERROR: Access to EEPROM failed! (ErrorCode=");
        Serial.print(iResult);
        Serial.println(")");
    }
    if (strlen(AppCfgData_g.m_szDevMntDevName) == 0)
    {
        Serial.println("-> Replace empty DevName with ChipID");
        strncpy(AppCfgData_g.m_szDevMntDevName, strChipID_g.c_str(), sizeof(AppCfgData_g.m_szDevMntDevName));
    }
    Serial.println("Configuration Data Setup:");
    AppPrintConfigData(&AppCfgData_g);

    iResult = AppSplitNetAddress (AppCfgData_g.m_szWifiOwnAddr, &WifiOwnIpAddress_g, &ui16WifiOwnPortNum_g);
    if (iResult >= 0)
    {
        strncpy(szWifiOwnIpAddress_g, WifiOwnIpAddress_g.toString().c_str(), WifiOwnIpAddress_g.toString().length());
        Serial.print("WifiOwnIpAddr:    ");     Serial.println(szWifiOwnIpAddress_g);
        Serial.print("WifiOwnPortNum:   ");     Serial.println(ui16WifiOwnPortNum_g);
    }
    else
    {
        Serial.print("-> ERROR: SplitNetAddress for 'WifiOwnIpAddress' failed! (ErrorCode=");
        Serial.print(iResult);
        Serial.println(")");
    }

    iResult = AppSplitNetAddress (AppCfgData_g.m_szAppRtPeerAddr, &AppRtPeerIpAddress_g, &ui16AppRtPeerPortNum_g);
    if (iResult >= 0)
    {
        strncpy(szAppRtPeerIpAddress_g, AppRtPeerIpAddress_g.toString().c_str(), AppRtPeerIpAddress_g.toString().length());
        Serial.print("AppRtPeerIpAddr:  ");     Serial.println(szAppRtPeerIpAddress_g);
        Serial.print("AppRtPeerPortNum: ");     Serial.println(ui16AppRtPeerPortNum_g);
    }
    else
    {
        Serial.print("-> ERROR: SplitNetAddress for 'AppRtPeerIpAddress' failed! (ErrorCode=");
        Serial.print(iResult);
        Serial.println(")");
    }


    // Determine Working Mode (BLE Config or Normal Operation)
    pinMode(PIN_KEY_BLE_CFG, INPUT);
    delay(10);
    fStateBleCfg_g = !digitalRead(PIN_KEY_BLE_CFG);                         // Keys are inverted (1=off, 0=on)
    if ( fStateBleCfg_g )
    {
        //-----------------------------------------------------------
        // BLE Config Mode -> Setup BLE Profile
        //-----------------------------------------------------------
        pinMode(PIN_STATUS_LED, OUTPUT);

        Serial.println("Setup BLE Profile...");
        iResult = ESP32BleCfgProfile_g.ProfileSetup(APP_DEVICE_TYPE, &AppCfgData_g, &AppDescriptData_g, AppCbHdlrSaveConfig, AppCbHdlrRestartDev, AppCbHdlrConStatChg);
        if (iResult >= 0)
        {
            Serial.println("-> BLE Server started successfully");
        }
        else
        {
            Serial.print("-> ERROR: BLE Server start failed! (ErrorCode=");
            Serial.print(iResult);
            Serial.println(")");
        }
    }
    else
    {
        //-----------------------------------------------------------
        // Normal Operation Mode -> User/Application specific Setup
        //-----------------------------------------------------------
        // Setup DI/DO (Key/LED)
        if ( AppCfgData_g.m_fAppRtOpt2 )            // -> DEFAULT_CFG_ENABLE_DI_DO
        {
            Serial.println("Setup DI/DO...");
            pinMode(PIN_KEY0, INPUT);
            pinMode(PIN_KEY1, INPUT);
            pinMode(PIN_LED0, OUTPUT);
            pinMode(PIN_LED1, OUTPUT);
            pinMode(PIN_LED2, OUTPUT);
            pinMode(PIN_LED3, OUTPUT);
            pinMode(PIN_LED4, OUTPUT);
            pinMode(PIN_LED5, OUTPUT);
            pinMode(PIN_LED6, OUTPUT);
            pinMode(PIN_LED7, OUTPUT);
            pinMode(PIN_LED8, OUTPUT);
        }

        iLastStateKey0_g = -1;
        iLastStateKey1_g = -1;


        // Setup Watchdog
        if ( AppCfgData_g.m_fAppRtOpt6 )            // -> DEFAULT_CFG_ENABLE_HW_WDT
        {
            Serial.println("Setup Watchdog (Task Watchdog Timer)...");
            snprintf(szTextBuff, sizeof(szTextBuff), "  WDT Timeout:    %d [sec]", APP_WDT_TIMEOUT);
            Serial.println(szTextBuff);
            esp_task_wdt_init(APP_WDT_TIMEOUT, true);                       // configure Task Watchdog Timer (TWDT), enable Panic Handler to restart ESP32 when TWDT times out
            esp_task_wdt_add(NULL);                                         // subscribe current Task to the Task Watchdog Timer (TWDT)
            ui32LastTickWdtService_g = millis();
        }


        // Setup DHT22 Sensor (Temerature/Humidity)
        if ( AppCfgData_g.m_fAppRtOpt3 )            // -> DEFAULT_CFG_ENABLE_DHT_SENSOR
        {
            Serial.println("Setup DHT22 Sensor...");
            DhtSensor_g.begin();
            snprintf(szTextBuff, sizeof(szTextBuff), "  Sample Period:    %d [sec]", (DHT_SENSOR_SAMPLE_PERIOD / 1000));
            Serial.println(szTextBuff);
        }


        // Setup MH-Z19 Sensor (CO2)
        if ( AppCfgData_g.m_fAppRtOpt4 )            // -> DEFAULT_CFG_ENABLE_MHZ_SENSOR
        {
            Serial.println("Setup MH-Z19 Sensor...");
            Mhz19SoftSerial_g.begin(MHZ19_BAUDRATE_SERIAL);                 // Initialize Software Serial Device to communicate with MH-Z19 sensor
            Mhz19Sensor_g.begin(Mhz19SoftSerial_g);                         // Initialize MH-Z19 Sensor (using Software Serial Device)
            if ( fMhz19DbgPrintComm_g )
            {
                Mhz19Sensor_g.printCommunication(false, true);              // isDec=false, isPrintComm=true
            }
            fStateKey0 = !digitalRead(PIN_KEY0);                            // Keys are inverted (1=off, 0=on)
            if ( fStateKey0 )                                               // KEY0 pressed?
            {
                fSensorCalibrated = AppMhz19CalibrateManually();
                if ( fSensorCalibrated )
                {
                    fStateKey0 = false;                                     // if calibration was done, don't invert AutoCalibration mode
                }
            }
            fStateKey1 = !digitalRead(PIN_KEY1);                            // Keys are inverted (1=off, 0=on)
            if ( fStateKey1 )                                               // KEY1 pressed?
            {
                AppMhz19CalibrateUnattended();
                ESP.restart();
            }
            fMhz19AutoCalibration = fMhz19AutoCalibration_g ^ fStateKey0;
            Mhz19Sensor_g.autoCalibration(fMhz19AutoCalibration);           // Set AutoCalibration Mode (true=ON / false=OFF)
            Mhz19Sensor_g.getVersion(acMhz19Version);
            snprintf(szTextBuff, sizeof(szTextBuff), "  Firmware Version: %c%c.%c%c", acMhz19Version[0], acMhz19Version[1], acMhz19Version[2], acMhz19Version[3]);
            Serial.println(szTextBuff);
            iMhz19Param = (int)Mhz19Sensor_g.getABC();
            snprintf(szTextBuff, sizeof(szTextBuff), "  AutoCalibration:  %s", (iMhz19Param) ? "ON" : "OFF");
            Serial.println(szTextBuff);
            iMhz19Param = Mhz19Sensor_g.getRange();
            snprintf(szTextBuff, sizeof(szTextBuff), "  Range:            %d", iMhz19Param);
            Serial.println(szTextBuff);
            iMhz19Param = Mhz19Sensor_g.getBackgroundCO2();
            snprintf(szTextBuff, sizeof(szTextBuff), "  Background CO2:   %d", iMhz19Param);
            Serial.println(szTextBuff);
            snprintf(szTextBuff, sizeof(szTextBuff), "  Sample Period:    %d [sec]", (MHZ19_SENSOR_SAMPLE_PERIOD / 1000));
            Serial.println(szTextBuff);
        }


        // Status LED Setup
        if ( AppCfgData_g.m_fAppRtOpt5 )            // -> DEFAULT_CFG_ENABLE_STATUS_LED
        {
            pinMode(PIN_STATUS_LED, OUTPUT);
            ui32LastTickStatLedToggle_g = 0;
            bStatusLedState_g = LOW;
        }


        // Default Board Configuration Settings
        fStatusLedHeartbeat_g = DEFAULT_STATUS_LED_HEARTBEAT;
        LedBarIndicator_g     = DEFAULT_LED_BAR_INDICATOR;
        fPrintSensorValues_g  = DEFAULT_PRINT_SENSOR_VALUES;
        fPrintMqttDataProc_g  = DEFAULT_PRINT_MQTT_DATA_PROC;


        // Network Scan
        if ( AppCfgData_g.m_fAppRtOpt1 )            // -> DEFAULT_CFG_ENABLE_NETWORK_SCAN
        {
            Serial.println();
            WifiScanNetworks();
            Serial.flush();
        }


        // Network Setup
        WifiConnectStationMode(AppCfgData_g.m_szWifiSSID, AppCfgData_g.m_szWifiPasswd, WifiOwnIpAddress_g, WIFI_POWER_SAVING_MODE);
        Serial.flush();


        // MQTT Setup
        MqttCheckNetworkConfig(WiFi.localIP(), szAppRtPeerIpAddress_g);
        strDeviceID_g = GetDeviceID(AppCfgData_g.m_szDevMntDevName);
        strClientName_g = GetUniqueClientName(MQTT_CLIENT_PREFIX);
        for (iIdx=0; iIdx<ARRAY_SIZE(MQTT_SUBSCRIBE_TOPIC_LIST_TEMPLATE); iIdx++)
        {
            astrMqttSubscribeTopicList_g[iIdx] = MqttBuildTopicFromTemplate(MQTT_SUBSCRIBE_TOPIC_LIST_TEMPLATE[iIdx], strDeviceID_g.c_str());
        }
        for (iIdx=0; iIdx<ARRAY_SIZE(MQTT_PUBLISH_TOPIC_LIST_TEMPLATE); iIdx++)
        {
            astrMqttPublishTopicList_g[iIdx] = MqttBuildTopicFromTemplate(MQTT_PUBLISH_TOPIC_LIST_TEMPLATE[iIdx], strDeviceID_g.c_str());
        }
        MqttPrintTopicLists(String(MQTT_SUPERVISOR_TOPIC), astrMqttSubscribeTopicList_g, ARRAY_SIZE(astrMqttSubscribeTopicList_g), astrMqttPublishTopicList_g, ARRAY_SIZE(astrMqttPublishTopicList_g));
        MqttSetup(szAppRtPeerIpAddress_g, ui16AppRtPeerPortNum_g, AppMqttSubCallback);
        MqttConnect(MQTT_USER, MQTT_PASSWORD, strClientName_g.c_str(), MQTT_SUPERVISOR_TOPIC, fPrintMqttDataProc_g);
        MqttSubscribeTopicList(astrMqttSubscribeTopicList_g, ARRAY_SIZE(astrMqttSubscribeTopicList_g), fPrintMqttDataProc_g);
        Serial.flush();


        // Encode and Publish Bootup Packet
        Serial.println("Encode Bootup Packet...");
        DeviceConfig_g.m_ui32MqttPacketNum    = ui32MqttPacketNum_g++;
        DeviceConfig_g.m_iFirmwareVersion     = APP_VERSION;
        DeviceConfig_g.m_iFirmwareRevision    = APP_REVISION;
        DeviceConfig_g.m_eBootReason          = (int)eBootReason_g;
        DeviceConfig_g.m_strChipID            = strChipID_g;
        DeviceConfig_g.m_LocalIP              = WiFi.localIP();
        DeviceConfig_g.m_ui32DataPackCycleTm  = ui32DataPackCycleTm_g;          // -> DEFAULT_DATA_PACKET_PUB_CYCLE_TIME
        DeviceConfig_g.m_fCfgNetworkScan      = AppCfgData_g.m_fAppRtOpt1;      // -> DEFAULT_CFG_ENABLE_NETWORK_SCAN
        DeviceConfig_g.m_fCfgDiDo             = AppCfgData_g.m_fAppRtOpt2;      // -> DEFAULT_CFG_ENABLE_DI_DO
        DeviceConfig_g.m_fCfgTmpHumSensor     = AppCfgData_g.m_fAppRtOpt3;      // -> DEFAULT_CFG_ENABLE_DHT_SENSOR
        DeviceConfig_g.m_fCfgCO2Sensor        = AppCfgData_g.m_fAppRtOpt4;      // -> DEFAULT_CFG_ENABLE_MHZ_SENSOR
        DeviceConfig_g.m_fCfgStatusLed        = AppCfgData_g.m_fAppRtOpt5;      // -> DEFAULT_CFG_ENABLE_STATUS_LED
        DeviceConfig_g.m_fCfgHwWdt            = AppCfgData_g.m_fAppRtOpt6;      // -> DEFAULT_CFG_ENABLE_HW_WDT
        DeviceConfig_g.m_fCfgSrvWdtOnMqttRecv = AppCfgData_g.m_fAppRtOpt7;      // -> DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV
        strJsonBootupPacket = AppEncodeBootupPacket(&DeviceConfig_g);
        Serial.println("Bootup Packet: ");
        Serial.println(strJsonBootupPacket);
        MqttPublishData(astrMqttPublishTopicList_g[0].c_str(), strJsonBootupPacket.c_str(), true, fPrintMqttDataProc_g);
    }

    return;

}



//---------------------------------------------------------------------------
//  Application Main Loop
//---------------------------------------------------------------------------

void loop()
{

char          szTextBuff[128];
uint32_t      ui32CurrTick;
unsigned int  uiProcStep;
uint32_t      ui32Uptime;
String        strUptime;
String        strJsonDataPacket;
uint32_t      ui32LastPubMqttPacketNum;
bool          fRes;


    // Determine Working Mode (BLE Config or Normal Operation)
    if ( fStateBleCfg_g )
    {
        //-----------------------------------------------------------
        // BLE Config Mode -> Run BLE Profile specific Loop Code
        //-----------------------------------------------------------
        ESP32BleCfgProfile_g.ProfileLoop();

        // signal BLE Config Mode on Status LED
        uiProcStep = (fBleClientConnected_g) ? (uiMainLoopProcStep_g++ % 15) : (uiMainLoopProcStep_g++ % 45);
        switch (uiProcStep)
        {
            case 0:
            case 1:
            {
                digitalWrite(PIN_STATUS_LED, HIGH);
                break;
            }

            default:
            {
                digitalWrite(PIN_STATUS_LED, LOW);
                break;
            }
        }
    }
    else
    {
        //-----------------------------------------------------------
        // Normal Operation Mode -> User/Application specific Loop
        //-----------------------------------------------------------
        // ensure that WLAN/WIFI and MQTT are available
        AppEnsureNetworkAvailability();

        // process WLAN/WIFI and MQTT
        fRes = PubSubClient_g.loop();
        if ( !fRes )
        {
            Serial.println("ERROR: MQTT Client is no longer connected -> Client.Loop() Failed!");
        }

        // process local periphery (i/o, sensors)
        uiProcStep = uiMainLoopProcStep_g++ % 10;
        switch (uiProcStep)
        {
            // provide main information
            case 0:
            {
                ulMainLoopCycle_g++;
                Serial.println();
                strUptime = GetSysUptime(&ui32Uptime);
                snprintf(szTextBuff, sizeof(szTextBuff), "Main Loop Cycle: %lu (Uptime: %s)", ulMainLoopCycle_g, strUptime.c_str());
                Serial.println(szTextBuff);
                Serial.flush();
                SensorDataRec_g.m_ulMainLoopCycle = ulMainLoopCycle_g;
                SensorDataRec_g.m_ui32Uptime = ui32Uptime;
                break;
            }

            // process local digital inputs
            case 1:
            {
                if ( AppCfgData_g.m_fAppRtOpt2 )            // -> DEFAULT_CFG_ENABLE_DI_DO
                {
                    AppProcessInputs(&SensorDataRec_g, fPrintSensorValues_g);
                }
                break;
            }

            // process DHT Sensor (Temperature/Humidity)
            case 2:
            {
                if ( AppCfgData_g.m_fAppRtOpt3 )            // -> DEFAULT_CFG_ENABLE_DHT_SENSOR
                {
                    AppProcessDhtSensor(DHT_SENSOR_SAMPLE_PERIOD, &SensorDataRec_g, fPrintSensorValues_g);
                }
                break;
            }

            // process MH-Z19 Sensor (CO2Value/SensorTemperature)
            case 3:
            {
                if ( AppCfgData_g.m_fAppRtOpt4 )            // -> DEFAULT_CFG_ENABLE_MHZ_SENSOR
                {
                    AppProcessMhz19Sensor(MHZ19_SENSOR_SAMPLE_PERIOD, &SensorDataRec_g, fPrintSensorValues_g);
                }
                break;
            }

            case 4:
            case 5:
            case 6:
            {
                break;
            }

            // encode and publish Data Packet
            case 7:
            {
                ui32CurrTick = millis();
                if ( ((ui32CurrTick - ui32LastTickDataPackPub_g) >= ui32DataPackCycleTm_g) ||
                     (SensorDataRec_g.m_iKey0Changed || SensorDataRec_g.m_iKey1Changed) )
                {
                    Serial.println("Encode Data Packet...");
                    SensorDataRec_g.m_ui32MqttPacketNum = ui32MqttPacketNum_g++;
                    SensorDataRec_g.m_ui16NetErrorLevel = ui16NetErrorLevel_g;
                    strJsonDataPacket = AppEncodeDataPacket(&SensorDataRec_g);
                    Serial.println("Data Packet: ");
                    Serial.println(strJsonDataPacket);
                    Serial.println("JsonDataPacket Length: " + String(strJsonDataPacket.length()));
                    fRes = MqttPublishData(astrMqttPublishTopicList_g[1].c_str(), strJsonDataPacket.c_str(), true, fPrintMqttDataProc_g);
                    if ( !fRes )
                    {
                        ui16NetErrorLevel_g += 2;
                    }
                    else
                    {
                        if (ui16NetErrorLevel_g > 0)
                        {
                            ui16NetErrorLevel_g -= 1;
                        }
                    }
                    Serial.println("NetErrorLevel: " + String(ui16NetErrorLevel_g));
                    Serial.flush();

                    ui32LastTickDataPackPub_g = ui32CurrTick;
                }
                else
                {
                    snprintf(szTextBuff, sizeof(szTextBuff), "Remaining TimeSpan until publishing next DataPacket: %i [sec]", ((ui32DataPackCycleTm_g - (ui32CurrTick - ui32LastTickDataPackPub_g)) / 1000));
                    Serial.println(szTextBuff);
                    Serial.flush();
                }
                break;
            }

            // process LED Bar Indicator depending on its data source
            case 8:
            {
                AppProcessLedBarIndicator();
                break;
            }

            // service Task Watchdog Timer (TWDT)
            case 9:
            {
                if ( AppCfgData_g.m_fAppRtOpt6 )            // -> DEFAULT_CFG_ENABLE_HW_WDT
                {
                    if ( AppCfgData_g.m_fAppRtOpt7 )        // -> DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV
                    {
                        ui32LastPubMqttPacketNum = ui32MqttPacketNum_g - 1;
                        snprintf(szTextBuff, sizeof(szTextBuff), "Last MQTT Packet published: %lu / Last MQTT Packet acknowledged: %lu", ui32LastPubMqttPacketNum, ui32LastAckMqttPacketNum_g);
                        Serial.println(szTextBuff);

                        if ((ui32LastPubMqttPacketNum - ui32LastAckMqttPacketNum_g) > APP_WINSIZE_NON_ACK_MQTT_PACKETS)
                        {
                            snprintf(szTextBuff, sizeof(szTextBuff), "WATCHDOG ALERT: Number of Non-Acknowledged MQTT Packets exceeds permitted Maximum of %lu", APP_WINSIZE_NON_ACK_MQTT_PACKETS);
                            Serial.println(szTextBuff);
                            snprintf(szTextBuff, sizeof(szTextBuff), "  -> DON'T TRIGGER WATCHDOG TO FORCE DEVICE REBOOT\n\n");
                            Serial.println(szTextBuff);
                            Serial.flush();

                            // WATCHDOG ALERT: exit without servicing Watchog
                            break;
                        }
                    }

                    esp_task_wdt_reset();
                    ui32CurrTick = millis();
                    snprintf(szTextBuff, sizeof(szTextBuff), "Service Watchdog (elapsed service interval: %i [ms])", (ui32CurrTick - ui32LastTickWdtService_g));
                    Serial.println(szTextBuff);
                    Serial.flush();
                    ui32LastTickWdtService_g = ui32CurrTick;
                }
                break;
            }

            // catch unexpected ProcSteps
            default:
            {
                break;
            }
        }   // switch (uiProcStep)

        // toggle Status LED
        if ( AppCfgData_g.m_fAppRtOpt5 )                    // -> DEFAULT_CFG_ENABLE_STATUS_LED
        {
            if ( fStatusLedHeartbeat_g )
            {
                ui32CurrTick = millis();
                if ((ui32CurrTick - ui32LastTickStatLedToggle_g) >= STATUS_LED_PERIOD_MAIN_LOOP)
                {
                    bStatusLedState_g = !bStatusLedState_g;
                    digitalWrite(PIN_STATUS_LED, bStatusLedState_g);

                    ui32LastTickStatLedToggle_g = ui32CurrTick;
                }
            }
        }
    }

    delay(50);

    return;

}





//=========================================================================//
//                                                                         //
//          S K E T C H   P R I V A T E   F U N C T I O N S                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Ensure that Network (WLAN/WiFi+MQTT) is available
//---------------------------------------------------------------------------

void  AppEnsureNetworkAvailability()
{

int  iWiFiStatus;


    // verify that WLAN/WiFi connection is still active
    iWiFiStatus = WiFi.status();
    if (iWiFiStatus != WL_CONNECTED)
    {
        Serial.println();
        Serial.println("ERROR: WLAN Connection lost, try to Reconnet...");
        Serial.print("       (-> '");
        Serial.print(astrWiFiStatus_g[iWiFiStatus]);
        Serial.println("'");

        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        WiFi.mode(WIFI_STA);

        WifiConnectStationMode(AppCfgData_g.m_szWifiSSID, AppCfgData_g.m_szWifiPasswd, WifiOwnIpAddress_g, WIFI_POWER_SAVING_MODE);

        // force MQTT reconnect in next step
        PubSubClient_g.disconnect();
    }

    // verify that MQTT connection is still active
    if ( !PubSubClient_g.connected() )
    {
        Serial.println();
        Serial.println("ERROR: MQTT Connection lost, try to Reconnet...");

        MqttConnect(MQTT_USER, MQTT_PASSWORD, strClientName_g.c_str(), MQTT_SUPERVISOR_TOPIC, fPrintMqttDataProc_g);
        MqttSubscribeTopicList(astrMqttSubscribeTopicList_g, ARRAY_SIZE(astrMqttSubscribeTopicList_g), fPrintMqttDataProc_g);
    }

    return;

}



//---------------------------------------------------------------------------
//  Application Callback Handler: Save Configuration Data Block
//---------------------------------------------------------------------------

void  AppCbHdlrSaveConfig(const tAppCfgData* pAppCfgData_p)
{

int  iResult;

    Serial.println();
    Serial.println("Save Configuration Data:");

    if (pAppCfgData_p != NULL)
    {
        memcpy(&AppCfgData_g, pAppCfgData_p, sizeof(AppCfgData_g));
        AppPrintConfigData(&AppCfgData_g);

        iResult = ESP32BleAppCfgData_g.SaveAppCfgDataToEeprom(&AppCfgData_g);
        if (iResult >= 0)
        {
            Serial.println("-> Configuration Data saved successfully");
        }
        else
        {
            Serial.print("ERROR: Saving Configuration Data failed! (ErrorCode=");
            Serial.print(iResult);
            Serial.println(")");
        }
    }
    else
    {
        Serial.println("ERROR: Configuration Failed!");
    }

    return;

}



//---------------------------------------------------------------------------
//  Application Callback Handler: Restart Device
//---------------------------------------------------------------------------

void  AppCbHdlrRestartDev()
{

    Serial.println();
    Serial.println("Restart Device:");
    Serial.println("-> REBOOT System now...");
    Serial.println();

    ESP.restart();

    return;

}



//---------------------------------------------------------------------------
//  Application Callback Handler: Inform about BLE Client Connect Status
//---------------------------------------------------------------------------

void  AppCbHdlrConStatChg (bool fBleClientConnected_p)
{

    fBleClientConnected_g = fBleClientConnected_p;

    if ( fBleClientConnected_g )
    {
        Serial.println();
        Serial.println("Client connected");
        Serial.println();
    }
    else
    {
        Serial.println();
        Serial.println("Client disconnected");
        Serial.println();
    }

    return;

}



//---------------------------------------------------------------------------
//  Print Configuration Data Block
//---------------------------------------------------------------------------

void  AppPrintConfigData (const tAppCfgData* pAppCfgData_p)
{

    if (pAppCfgData_p != NULL)
    {
        Serial.print("  DevMntDevName:  ");     Serial.println(pAppCfgData_p->m_szDevMntDevName);   Serial.flush();
        Serial.print("  WifiSSID:       ");     Serial.println(pAppCfgData_p->m_szWifiSSID);        Serial.flush();
        Serial.print("  WifiPasswd:     ");     Serial.println(pAppCfgData_p->m_szWifiPasswd);      Serial.flush();
        Serial.print("  WifiOwnAddr:    ");     Serial.println(pAppCfgData_p->m_szWifiOwnAddr);     Serial.flush();
        Serial.print("  WifiOwnMode:    ");     Serial.println(pAppCfgData_p->m_ui8WifiOwnMode);    Serial.flush();
        Serial.print("  AppRtOpt1:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt1);        Serial.flush();
        Serial.print("  AppRtOpt2:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt2);        Serial.flush();
        Serial.print("  AppRtOpt3:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt3);        Serial.flush();
        Serial.print("  AppRtOpt4:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt4);        Serial.flush();
        Serial.print("  AppRtOpt5:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt5);        Serial.flush();
        Serial.print("  AppRtOpt6:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt6);        Serial.flush();
        Serial.print("  AppRtOpt7:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt7);        Serial.flush();
        Serial.print("  AppRtOpt8:      ");     Serial.println(pAppCfgData_p->m_fAppRtOpt8);        Serial.flush();
        Serial.print("  AppRtPeerAddr:  ");     Serial.println(pAppCfgData_p->m_szAppRtPeerAddr);   Serial.flush();
    }

    return;

}



//---------------------------------------------------------------------------
//  Split Network Address String into IpAddress and PortNumber
//---------------------------------------------------------------------------

int  AppSplitNetAddress (const char* pszNetAddr_p, IPAddress* pIpAddress_p, uint16_t* pui16PortNum_p)
{

String     strNetAddr;
String     strIpAddr;
String     strPortNum;
IPAddress  IpAddr;
long       lPortNum;
int        iIdx;
bool       fSuccess;

    // process IPAddress
    strNetAddr = pszNetAddr_p;
    iIdx = strNetAddr.indexOf(':');
    if (iIdx > 0)
    {
        strIpAddr = strNetAddr.substring(0, iIdx);
    }
    else
    {
        strIpAddr = strNetAddr;
    }

    fSuccess = IpAddr.fromString(strIpAddr);
    if ( fSuccess )
    {
        if (pIpAddress_p != NULL)
        {
            *pIpAddress_p = IpAddr;
        }
    }
    else
    {
        return (-1);
    }

    // process PortNumber
    if (iIdx > 0)
    {
        strPortNum = strNetAddr.substring(iIdx+1);
        lPortNum = strPortNum.toInt();
        if ((lPortNum >= 0) && (lPortNum <= 0xFFFF))
        {
            if (pui16PortNum_p != NULL)
            {
                *pui16PortNum_p = (uint16_t)lPortNum;
            }
        }
        else
        {
            return (-2);
        }
    }

    return (0);

}





//=========================================================================//
//                                                                         //
//          W I F I   N E T W O R K   F U N C T I O N S                    //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Scan WLAN/WiFi Networks
//---------------------------------------------------------------------------

void  WifiScanNetworks()
{

int  iNumOfWlanNets;
int  iIdx;


    if ( AppCfgData_g.m_fAppRtOpt5 )            // -> DEFAULT_CFG_ENABLE_STATUS_LED
    {
        Esp32TimerStart(STATUS_LED_PERIOD_NET_SCAN);                // 10Hz = 50ms On + 50ms Off
    }

    if ( AppCfgData_g.m_fAppRtOpt6 )            // -> DEFAULT_CFG_ENABLE_HW_WDT
    {
        // service Task Watchdog Timer (TWDT)
        esp_task_wdt_reset();
        ui32LastTickWdtService_g = millis();
    }


    Serial.println("Scanning for WLAN Networks...");
    iNumOfWlanNets = WiFi.scanNetworks();
    if (iNumOfWlanNets > 0)
    {
        Serial.print("Networks found within Range: ");
        Serial.println(iNumOfWlanNets);
        Serial.println("---------------------------------+------+----------------");
        Serial.println("               SSID              | RSSI |    AUTH MODE   ");
        Serial.println("---------------------------------+------+----------------");
        for (iIdx=0; iIdx<iNumOfWlanNets; iIdx++)
        {
            Serial.printf("%32.32s | ", WiFi.SSID(iIdx).c_str());
            Serial.printf("%4d | ", WiFi.RSSI(iIdx));
            Serial.printf("%15s\n", WifiAuthModeToText(WiFi.encryptionType(iIdx)).c_str());
        }
    }
    else
    {
        Serial.println("No Networks found.");
    }

    Serial.println("");
    Serial.flush();

    if ( AppCfgData_g.m_fAppRtOpt6 )            // -> DEFAULT_CFG_ENABLE_HW_WDT
    {
        // service Task Watchdog Timer (TWDT)
        esp_task_wdt_reset();
        ui32LastTickWdtService_g = millis();
    }

    if ( AppCfgData_g.m_fAppRtOpt5 )            // -> DEFAULT_CFG_ENABLE_STATUS_LED
    {
        Esp32TimerStop();
    }

    return;

}



//---------------------------------------------------------------------------
//  Setup WLAN/WiFi SubSystem in Station Mode
//---------------------------------------------------------------------------

void  WifiConnectStationMode (const char* pszWifiSSID_p, const char* pszWifiPassword_p, IPAddress LocalIP_p, wifi_ps_type_t WifiPowerSavingMode_p)
{

IPAddress  WIFI_STA_GATEWAY(0,0,0,0);                               // no Gateway functionality supported
IPAddress  WIFI_STA_SUBNET(255,255,255,0);
uint32_t   ui32ConnectStartTicks;
bool       fResult;


    if ( AppCfgData_g.m_fAppRtOpt5 )            // -> DEFAULT_CFG_ENABLE_STATUS_LED
    {
        Esp32TimerStart(STATUS_LED_PERIOD_NET_CONNECT);             // 5Hz = 100ms On + 100ms Off
    }

    Serial.println("Setup WiFi Interface as STATION (Client)");

    // Set PowerSavingMode for WLAN Interface
    Serial.print("Set PowerSavingMode for WLAN Interface: ");
    switch (WifiPowerSavingMode_p)
    {
        case WIFI_PS_NONE:          Serial.println("OFF (WIFI_PS_NONE)");                   break;
        case WIFI_PS_MIN_MODEM:     Serial.println("ON / using DTIM (WIFI_PS_MIN_MODEM)");  break;
        case WIFI_PS_MAX_MODEM:     Serial.println("ON (WIFI_PS_MAX_MODEM)");               break;
        default:                    Serial.println("???unknown???");                        break;
    }
    esp_wifi_set_ps(WifiPowerSavingMode_p);

    // Set WiFi Mode to Station
    Serial.println("Set WiFi Mode to Station:");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    Serial.println("  -> done.");

    // Set IPAddress configuration
    // 0.0.0.0 = DHCP, otherwise set static IPAddress
    Serial.print("IP Address configuration: ");
    if (LocalIP_p != IPAddress(0,0,0,0))
    {
        Serial.print("static ");
        Serial.println(LocalIP_p.toString());
        WiFi.config(LocalIP_p, WIFI_STA_GATEWAY, WIFI_STA_SUBNET);
        Serial.println("  -> done.");
    }
    else
    {
        Serial.println("DHCP");
    }

    // Connecting to WLAN/WiFi
    Serial.print("Connecting to WLAN '");
    Serial.print(pszWifiSSID_p);
    Serial.print("' ");
    WiFi.begin(pszWifiSSID_p, pszWifiPassword_p);

    ui32ConnectStartTicks = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if ((millis() - ui32ConnectStartTicks) > WIFI_TRY_CONNECT_TIMEOUT)
        {
            Serial.println("");
            Serial.print("ERROR: Unable to connect to WLAN since more than ");
            Serial.print(WIFI_TRY_CONNECT_TIMEOUT/1000);
            Serial.println(" seconds.");
            Serial.println("-> REBOOT System now...");
            Serial.println("");

            ESP.restart();
        }

        if ( AppCfgData_g.m_fAppRtOpt6 )        // -> DEFAULT_CFG_ENABLE_HW_WDT
        {
            // service Task Watchdog Timer (TWDT)
            esp_task_wdt_reset();
            ui32LastTickWdtService_g = millis();
        }

        delay(500);
        Serial.print(".");
    }
    Serial.println(" -> connected.");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("   (MAC: ");
    Serial.print(GetChipMAC());
    Serial.println(")");
    Serial.flush();

    if ( AppCfgData_g.m_fAppRtOpt5 )            // -> DEFAULT_CFG_ENABLE_STATUS_LED
    {
        Esp32TimerStop();
    }

    return;

}



//---------------------------------------------------------------------------
//  Get WLAN/WiFi Authentication Mode as Text
//---------------------------------------------------------------------------

String  WifiAuthModeToText (wifi_auth_mode_t WifiAuthMode_p)
{

String  strWifiAuthMode;


    switch (WifiAuthMode_p)
    {
        case WIFI_AUTH_OPEN:
        {
            strWifiAuthMode = "Open";
            break;
        }

        case WIFI_AUTH_WEP:
        {
            strWifiAuthMode = "WEP";
            break;
        }

        case WIFI_AUTH_WPA_PSK:
        {
            strWifiAuthMode = "WPA PSK";
            break;
        }

        case WIFI_AUTH_WPA2_PSK:
        {
            strWifiAuthMode = "WPA2 PSK";
            break;
        }

        case WIFI_AUTH_WPA_WPA2_PSK:
        {
            strWifiAuthMode = "WPA/WPA2 PSK";
            break;
        }

        case WIFI_AUTH_WPA2_ENTERPRISE:
        {
            strWifiAuthMode = "WPA2 ENTERPRISE";
            break;
        }

        case WIFI_AUTH_MAX:
        {
            strWifiAuthMode = "MAX";
            break;
        }

        default:
        {
            strWifiAuthMode = "Unknown";
            break;
        }
    }

    return (strWifiAuthMode);

}





//=========================================================================//
//                                                                         //
//          M Q T T   M E S S A G E   F U N C T I O N S                    //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  MQTT Check Network Configuration
//---------------------------------------------------------------------------

bool  MqttCheckNetworkConfig (IPAddress LocalIP_p, const char* pszMqttServer_p)
{

IPAddress  MqttServer;
bool       fMismatch;
int        iIdx;


    MqttServer.fromString(pszMqttServer_p);

    // check LocalIP_p[0][1][2][*] with MqttServer[0][1][2][*]
    fMismatch = false;
    for (iIdx=0; iIdx<3; iIdx++)
    {
        if (LocalIP_p[iIdx] != MqttServer[iIdx])
        {
            fMismatch = true;
            break;
        }
    }

    if ( fMismatch )
    {
        Serial.println();
        Serial.println("******** WARNING ********");
        Serial.println("MQTT Server is located in a external Subnet:");
        Serial.print("MQTT Server:      ");  Serial.println(MqttServer);
        Serial.print("Local IP Address: ");  Serial.println(LocalIP_p);
        Serial.println("This Configuration can lead to a hang during connect.");
        Serial.println("*************************");
        Serial.println();
    }

    return (fMismatch);

}



//---------------------------------------------------------------------------
//  MQTT Setup
//---------------------------------------------------------------------------

void  MqttSetup (const char* pszMqttServer_p, int iMqttPort_p, tMqttSubCallback pfnMqttSubCallback_p)
{

    Serial.print("MQTT Configuration: Server='");
    Serial.print(pszMqttServer_p);
    Serial.print("', Port=");
    Serial.println(iMqttPort_p);

    PubSubClient_g.setServer(pszMqttServer_p, iMqttPort_p);

    if (pfnMqttSubCallback_p != NULL)
    {
        Serial.println("Register MQTT Callback Function");
        PubSubClient_g.setCallback(pfnMqttSubCallback_p);
    }

    return;

}



//---------------------------------------------------------------------------
//  MQTT Connect
//---------------------------------------------------------------------------

bool  MqttConnect (const char* pszMqttUser_p, const char* pszMqttPassword_p, const char* pszClientName_p, const char* pszSupervisorTopic_p, bool fPrintMqttDataProc_p)
{

String     strChipID;
String     strClientId;
IPAddress  LocalIP;
String     strRuntimeConfig;
String     strPayloadMsgBody;
String     strPayloadMsgConnect;
String     strPayloadMsgGotLost;
int        iIdx;
bool       fConnected;
bool       fRes;


    if ( AppCfgData_g.m_fAppRtOpt5 )                // -> DEFAULT_CFG_ENABLE_STATUS_LED
    {
        Esp32TimerStart(STATUS_LED_PERIOD_MQTT_CONNECT);            // 2.5Hz = 200ms On + 200ms Off
    }

    // Get unique ClientID
    if (pszClientName_p != NULL)
    {
        // reuse given ClientID
        strClientId = pszClientName_p;
    }
    else
    {
        // Create a unique ClientID, based on ChipID (the ChipID is essentially its 6byte MAC address)
        strChipID    = GetChipID();
        strClientId  = "SmBrdClient-";
        strClientId += strChipID;
    }

    // If SupervisorTopic is set, then create 'Connect' and 'GotLost' message payload
    if (pszSupervisorTopic_p != NULL)
    {
        LocalIP = WiFi.localIP();
        strRuntimeConfig = AppGetRuntimeConfig();
        strPayloadMsgBody = "CI=" + strClientId + ", IP=" + LocalIP.toString() + ", RC=" + strRuntimeConfig;
        strPayloadMsgConnect = strPayloadMsgBody + ", ST=Connected";
        strPayloadMsgGotLost = strPayloadMsgBody + ", ST=GotLost";
    }

    // adjust MQTT Buffer to necessary size (default is only 256 bytes, which is usually too small)
    // (the buffer must be large enough to contain the full MQTT packet. The packet will contain the
    // full topic string, the payload data and a small number of header bytes)
    Serial.print("Resize MQTT Buffer Size: ");
    fRes = PubSubClient_g.setBufferSize(MQTT_BUFFER_SIZE);
    if ( fRes )
    {
        Serial.println(" -> ok.");
    }
    else
    {
        Serial.println(" -> failed.");
    }

    // Connect to MQTT Server
    Serial.print("Connecting to MQTT Server: User='");
    Serial.print(pszMqttUser_p);
    Serial.print("', ClientId='");
    Serial.print(strClientId);
    Serial.print("' ...");

    fConnected = false;
    do
    {
        if ( AppCfgData_g.m_fAppRtOpt6 )            // -> DEFAULT_CFG_ENABLE_HW_WDT
        {
            // service Task Watchdog Timer (TWDT)
            esp_task_wdt_reset();
            ui32LastTickWdtService_g = millis();
        }

        // check if WLAN/WiFi Connection is established
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println();
            Serial.println("ERROR: No WLAN Connection -> unable to establish MQTT!");
            Serial.println();
            goto Exit;
        }

        // connect to MQTT Server
        if (pszSupervisorTopic_p != NULL)
        {
            fConnected = PubSubClient_g.connect(strClientId.c_str(),            // ClientID
                                                pszMqttUser_p,                  // Username
                                                pszMqttPassword_p,              // Password
                                                pszSupervisorTopic_p,           // LastWillTopic
                                                0,                              // LastWillQoS
                                                1,                              // LastWillRetain
                                                strPayloadMsgGotLost.c_str(),   // LastWillMessage
                                                true);                          // CleanSession
        }
        else
        {
            fConnected = PubSubClient_g.connect(strClientId.c_str(),            // ClientID
                                                pszMqttUser_p,                  // Username
                                                pszMqttPassword_p);             // Password
        }

        if ( fConnected )
        {
            Serial.println(" -> connected.");
        }
        else
        {
            Serial.println();
            Serial.print(" -> Failed, rc=");
            Serial.print(PubSubClient_g.state());
            Serial.println(", try again in 5 seconds");
            for (iIdx=0; iIdx<5; iIdx++)
            {
                if ( AppCfgData_g.m_fAppRtOpt6 )    // -> DEFAULT_CFG_ENABLE_HW_WDT
                {
                    // service Task Watchdog Timer (TWDT)
                    esp_task_wdt_reset();
                    ui32LastTickWdtService_g = millis();
                }
                delay(1000);
            }
        }
    }
    while ( !fConnected );


    // If SupervisorTopic is set, then publish 'Connect' message
    if (pszSupervisorTopic_p != NULL)
    {
        if ( fPrintMqttDataProc_p )
        {
            Serial.print("Publish Supervisor Message: Topic='");
            Serial.print(pszSupervisorTopic_p);
            Serial.print("', Payload='");
            Serial.print(strPayloadMsgConnect.c_str());
            Serial.print("'");
        }

        fRes = PubSubClient_g.publish(pszSupervisorTopic_p,                     // Topic
                                      strPayloadMsgConnect.c_str(),             // Payload
                                      1);                                       // Retain
        if ( fPrintMqttDataProc_p )
        {
            if ( fRes )
            {
                Serial.println(" -> ok.");
            }
            else
            {
                Serial.println(" -> failed.");
            }
        }
    }


    ui16NetErrorLevel_g = 0;


Exit:

    if ( AppCfgData_g.m_fAppRtOpt5 )                // -> DEFAULT_CFG_ENABLE_STATUS_LED
    {
        Esp32TimerStop();
    }

    return (fConnected);

}



//---------------------------------------------------------------------------
//  MQTT Subscribe Topic / Topic List
//---------------------------------------------------------------------------

bool  MqttSubscribeTopicList (String astrMqttSubscribeTopicList_p[], int iSizeOfTopicList_p, bool fPrintMqttDataProc_p)
{

int   iIdx;
bool  fRes;


    for (iIdx=0; iIdx<iSizeOfTopicList_p; iIdx++)
    {
        fRes = MqttSubscribeTopic(astrMqttSubscribeTopicList_p[iIdx].c_str(), fPrintMqttDataProc_p);
        if ( !fRes )
        {
            break;
        }
    }

    return (fRes);

}

//---------------------------------------------------------------------------

bool  MqttSubscribeTopic (const char* pszMsgTopic_p, bool fPrintMqttDataProc_p)
{

bool  fRes;


    if ( fPrintMqttDataProc_p )
    {
        Serial.print("Subscribe: Topic='");
        Serial.print(pszMsgTopic_p);
        Serial.print("'");
        Serial.flush();
    }

    fRes = PubSubClient_g.subscribe(pszMsgTopic_p);

    if ( fPrintMqttDataProc_p )
    {
        if ( fRes )
        {
            Serial.println(" -> ok.");
        }
        else
        {
            Serial.println(" -> failed.");
        }
    }

    return (fRes);

}



//---------------------------------------------------------------------------
//  MQTT Publish Data Message
//---------------------------------------------------------------------------

bool  MqttPublishData (const char* pszMsgTopic_p, const char* pszMsgPayload_p, bool fRetain_p, bool fPrintMqttDataProc_p)
{

bool  fRes;


    if ( fPrintMqttDataProc_p )
    {
        Serial.print("Publish Data Message: Topic='");
        Serial.print(pszMsgTopic_p);
        Serial.print("', Payload='");
        Serial.print(pszMsgPayload_p);
        Serial.print("'");
        Serial.flush();
    }

    fRes = PubSubClient_g.publish(pszMsgTopic_p,                                // Topic
                                  pszMsgPayload_p,                              // Payload
                                  fRetain_p);                                   // Retain
    if ( fPrintMqttDataProc_p )
    {
        if ( fRes )
        {
            Serial.println(" -> ok.");
        }
        else
        {
            Serial.println(" -> failed.");
        }
    }

    return (fRes);

}



//---------------------------------------------------------------------------
//  MQTT Receive Data Nessage (for subscribed MQTT Topics)
//---------------------------------------------------------------------------

void  AppMqttSubCallback (const char* pszMsgTopic_p, const uint8_t* pMsgPayload_p, unsigned int uiMsgPayloadLen_p)
{

String  strMsgTopic;
String  strMsgPayload;


    // convert Topic and Payload Buffers into String
    strMsgTopic   = MqttConvBuffToString((const uint8_t*)pszMsgTopic_p, -1);
    strMsgPayload = MqttConvBuffToString(pMsgPayload_p, uiMsgPayloadLen_p);

    if ( fPrintMqttDataProc_g )
    {
        Serial.println("");
        Serial.print("Received Data Message: Topic='");
        Serial.print(strMsgTopic);
        Serial.print("', Payload='");
        Serial.print(strMsgPayload);
        Serial.print("', PayloadLen=");
        Serial.print(uiMsgPayloadLen_p);
        Serial.println("");
    }

    // process received MQTT Data Message
    AppProcessMqttDataMessage(strMsgTopic, strMsgPayload);

    if ( fPrintMqttDataProc_g )
    {
        Serial.println("");
    }

    return;

}



//---------------------------------------------------------------------------
//  MQTT Build Topic from Template
//---------------------------------------------------------------------------

String  MqttBuildTopicFromTemplate (const char* pszTopicTemplate_p, const char* pszSignatur_p)
{

String  strTemplate;
String  strTopic;
int     iIdx;


    strTemplate = String(pszTopicTemplate_p);

    // split-up Template into left and right part, insert signature in between
    iIdx = strTemplate.indexOf("<%>");
    if ( (iIdx >= 0) && ((iIdx+sizeof("<%>")-1) < strTemplate.length()) )
    {
        strTopic  = strTemplate.substring(0, iIdx);
        strTopic += String(pszSignatur_p);
        strTopic += strTemplate.substring((iIdx+sizeof("<%>")-1), strTemplate.length());
    }
    else
    {
        strTopic = strTemplate;
    }

    return (strTopic);

}



//---------------------------------------------------------------------------
//  MQTT Print Subscribe and Publish Topic Lists
//---------------------------------------------------------------------------

void  MqttPrintTopicLists (String strMqttSupervisorTopic_p, String astrMqttSubscribeTopicList_p[], int iSizeOfSubscribeTopicList_p, String astrMqttPublishTopicList_p[], int iSizeOfPublishTopicList_p)
{

char  szLineBuff[128];
int   iIdx;


    Serial.println("MQTT Supervisor Topic:");
    snprintf(szLineBuff, sizeof(szLineBuff), "  SUP: '%s'", strMqttSupervisorTopic_p.c_str());
    Serial.println(szLineBuff);

    Serial.println("MQTT Subscribe Topic List:");
    for (iIdx=0; iIdx<iSizeOfSubscribeTopicList_p; iIdx++)
    {
        snprintf(szLineBuff, sizeof(szLineBuff), "  S%02d: '%s'", iIdx, astrMqttSubscribeTopicList_p[iIdx].c_str());
        Serial.println(szLineBuff);
    }
    Serial.flush();

    Serial.println("MQTT Publish Topic List:");
    for (iIdx=0; iIdx<iSizeOfPublishTopicList_p; iIdx++)
    {
        snprintf(szLineBuff, sizeof(szLineBuff), "  P%02d: '%s'", iIdx, astrMqttPublishTopicList_p[iIdx].c_str());
        Serial.println(szLineBuff);
    }
    Serial.flush();

    return;

}



//---------------------------------------------------------------------------
//  MQTT Convert Buffer to String
//---------------------------------------------------------------------------

String  MqttConvBuffToString (const uint8_t* pBuffer_p, unsigned int uiBufferLen_p)
{

unsigned int  uiBufferLen;
char          acBuffer[MQTT_MAX_PACKET_SIZE+1];
String        strBuffer;


    // Is ptr to buffer vald?
    if (pBuffer_p != NULL)
    {
        // Is buffer length declared explicitly?
        if (uiBufferLen_p < 0)
        {
            // no buffer length declared -> calculate string length
            uiBufferLen = strlen((const char*)pBuffer_p);
        }
        else
        {
            // use declared buffer length
            uiBufferLen = uiBufferLen_p;
        }

        // limit length to local buffer size
        if (uiBufferLen > MQTT_MAX_PACKET_SIZE)
        {
            uiBufferLen = MQTT_MAX_PACKET_SIZE;
        }

        memcpy(acBuffer, pBuffer_p, uiBufferLen);
        acBuffer[uiBufferLen] = '\0';
        strBuffer = String(acBuffer);
    }
    else
    {
        strBuffer = "<invalid>";
    }

    return (strBuffer);

}





//=========================================================================//
//                                                                         //
//          S M A R T B O A R D   A P P   F U N C T I O N S                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Encode Bootup Packet to JSON Object
//---------------------------------------------------------------------------

String  AppEncodeBootupPacket (const tDeviceConfig* pDeviceConfig_p)
{

JsonDocument  JsonBootupPacket;
char          szVerNum[16];
String        strJsonBootupPacket;


    if (pDeviceConfig_p == NULL)
    {
        return (String("{}"));
    }

    JsonBootupPacket["PacketNum"]       = pDeviceConfig_p->m_ui32MqttPacketNum;
    snprintf(szVerNum, sizeof(szVerNum), "%u.%02u", (unsigned)pDeviceConfig_p->m_iFirmwareVersion, (unsigned)pDeviceConfig_p->m_iFirmwareRevision);
    JsonBootupPacket["FirmwareVer"]     = szVerNum;
    JsonBootupPacket["BootReason"]      = pDeviceConfig_p->m_eBootReason;
    JsonBootupPacket["IP"]              = pDeviceConfig_p->m_LocalIP.toString();
    JsonBootupPacket["ChipID"]          = pDeviceConfig_p->m_strChipID;
    JsonBootupPacket["DataPackCycleTm"] = pDeviceConfig_p->m_ui32DataPackCycleTm / 1000;    // [ms] -> [sec]
    JsonBootupPacket["CfgNetworkScan"]  = pDeviceConfig_p->m_fCfgNetworkScan;
    JsonBootupPacket["CfgDiDo"]         = pDeviceConfig_p->m_fCfgDiDo;
    JsonBootupPacket["CfgTmpHumSensor"] = pDeviceConfig_p->m_fCfgTmpHumSensor;
    JsonBootupPacket["CfgCO2Sensor"]    = pDeviceConfig_p->m_fCfgCO2Sensor;
    JsonBootupPacket["CfgStatusLed"]    = pDeviceConfig_p->m_fCfgStatusLed;
    JsonBootupPacket["CfgHwWdt"]        = pDeviceConfig_p->m_fCfgHwWdt;
    JsonBootupPacket["CfgSrvWdtOnMqtt"] = pDeviceConfig_p->m_fCfgSrvWdtOnMqttRecv;

    serializeJsonPretty(JsonBootupPacket, strJsonBootupPacket);

    return (strJsonBootupPacket);

}



//---------------------------------------------------------------------------
//  Encode Data Packet to JSON Object
//---------------------------------------------------------------------------

String  AppEncodeDataPacket (const tSensorData* pSensorDataRec_p)
{

JsonDocument  JsonDataPacket;
String        strJsonDataPacket;


    if (pSensorDataRec_p == NULL)
    {
        return (String("{}"));
    }

    JsonDataPacket["PacketNum"]     = pSensorDataRec_p->m_ui32MqttPacketNum;
    JsonDataPacket["MainLoopCycle"] = pSensorDataRec_p->m_ulMainLoopCycle;
    JsonDataPacket["Uptime"]        = pSensorDataRec_p->m_ui32Uptime / 1000;                // [ms] -> [sec]
    JsonDataPacket["NetErrorLevel"] = pSensorDataRec_p->m_ui16NetErrorLevel;
    JsonDataPacket["Key0"]          = pSensorDataRec_p->m_iKey0;
    JsonDataPacket["Key0Chng"]      = pSensorDataRec_p->m_iKey0Changed;
    JsonDataPacket["Key1"]          = pSensorDataRec_p->m_iKey1;
    JsonDataPacket["Key1Chng"]      = pSensorDataRec_p->m_iKey1Changed;
    JsonDataPacket["Temperature"]   = ((float)((int)((pSensorDataRec_p->m_flTemperature + 0.05) * 10)) / 10.0);     // limit to only one decimal place
    JsonDataPacket["Humidity"]      = ((float)((int)((pSensorDataRec_p->m_flHumidity + 0.05) * 10)) / 10.0);        // limit to only one decimal place
    JsonDataPacket["Co2Value"]      = pSensorDataRec_p->m_iCo2Value;
    JsonDataPacket["Co2SensTemp"]   = pSensorDataRec_p->m_iCo2SensTemp;

    serializeJsonPretty(JsonDataPacket, strJsonDataPacket);

    return (strJsonDataPacket);

}



//---------------------------------------------------------------------------
//  Process received MQTT Data Messages (for subscribed MQTT Topics)
//---------------------------------------------------------------------------

void  AppProcessMqttDataMessage (String strMsgTopic_p, String strMsgPayload_p)
{

String    strMsgTopic;
String    strMsgPayload;
int       iValue;
int64_t   i64Value;
uint32_t  ui32CurrTick;
int       iLedNum;
bool      fLedState;


    TRACE2("+ 'AppProcessMqttDataMessage()': strMsgTopic_p=%s, strMsgPayload_p=%s\n", strMsgTopic_p.c_str(), strMsgPayload_p.c_str());

    strMsgTopic = strMsgTopic_p;
    strMsgTopic.toLowerCase();

    strMsgPayload = strMsgPayload_p;
    strMsgPayload.toLowerCase();

    if (strMsgTopic.indexOf("settings") > 0)
    {
        TRACE0("   Recognized Messages Typ: 'Settings'\n");

        // Message scheme: 'SmBrd/<%>/Settings/Heartbeat'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[0])
        {
            iValue = strMsgPayload.substring(0,1).toInt();
            fStatusLedHeartbeat_g = (iValue != 0) ? true : false;
            TRACE1("   Set 'Heartbeat' to: %s\n", String(fStatusLedHeartbeat_g));
            if ( !fStatusLedHeartbeat_g )
            {
                digitalWrite(PIN_STATUS_LED, LOW);
            }
        }

        // Message scheme: 'SmBrd/<DevID>/Settings/LedBarIndicator'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[1])
        {
            iValue = strMsgPayload.substring(0,1).toInt();
            switch (iValue)
            {
                case 0:     LedBarIndicator_g = kLedBarNone;                break;
                case 1:     LedBarIndicator_g = kLedBarDht22Temperature;    break;
                case 2:     LedBarIndicator_g = kLedBarDht22Humidity;       break;
                case 3:     LedBarIndicator_g = kLedBarMhz19Co2Level;       break;
                default:    LedBarIndicator_g = kLedBarNone;                break;
            }
            TRACE1("   Set 'LedBarIndicator' to: %d\n", LedBarIndicator_g);
            AppPresentLedBar(0);                                    // clear all LEDs in LED Bar
        }

        // Message scheme: "SmBrd/<DevID>/Settings/DataPackPubCycleTime"
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[2])
        {
            i64Value = strtoll(strMsgPayload.c_str(), NULL, 10);
            i64Value *= 1000;                                      // [sec] -> [ms]
            if (i64Value == 0)
            {
                // CycleTime = 0 -> Force one-time publishing of Data Packet without changing <ui32DataPackCycleTm_g>
                ui32CurrTick = millis();
                ui32LastTickDataPackPub_g = ui32CurrTick - ui32DataPackCycleTm_g;   // mark time interval as expired
                TRACE0("   Force one-time publishing of Data Packet without changing 'DataPackCycleTm'\n");
            }
            else
            {
                // CycleTime < 0 -> Reset CycleTime to default value
                if (i64Value < 0)
                {
                    i64Value = DEFAULT_DATA_PACKET_PUB_CYCLE_TIME;
                }
                if (i64Value <= MAX_SET_DATA_PACKET_PUB_CYCLE_TIME)
                {
                    ui32DataPackCycleTm_g = (uint32_t)i64Value;
                    TRACE1("   Set 'DataPackCycleTm': %lu [sec]\n", (ui32DataPackCycleTm_g / 1000));
                }
            }
        }

        // Message scheme: 'SmBrd/<DevID>/Settings/PrintSensorVal'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[3])
        {
            iValue = strMsgPayload.substring(0,1).toInt();
            fPrintSensorValues_g = (iValue != 0) ? true : false;
            TRACE1("   Set 'PrintSensorVal' to: %s\n", String(fPrintSensorValues_g));
        }

        // Message scheme: 'SmBrd/<DevID>/Settings/PrintMqttDataProc'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[4])
        {
            iValue = strMsgPayload.substring(0,1).toInt();
            fPrintMqttDataProc_g = (iValue != 0) ? true : false;
            TRACE1("   Set 'PrintMqttDataProc' to: %s\n", String(fPrintMqttDataProc_g));
        }
    }
    else if (strMsgTopic.indexOf("outdata") > 0)
    {
        TRACE0("   Recognized Messages Typ: 'OutData'\n");

        // Message scheme: 'SmBrd/<DevID>/OutData/LedBar'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[5])
        {
            LedBarIndicator_g = kLedBarNone;                        // prevent overwriting of LED Bar by other sources
            iValue = strMsgPayload.substring(0,1).toInt();
            TRACE1("   Set 'LedBar (normal)' to: %d\n", iValue);
            AppPresentLedBar(iValue, false);
        }

        // Message scheme: 'SmBrd/<DevID>/OutData/LedBarInv'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[5])
        {
            LedBarIndicator_g = kLedBarNone;                        // prevent overwriting of LED Bar by other sources
            iValue = strMsgPayload.substring(0,1).toInt();
            TRACE1("   Set 'LedBar (inverted)' to: %d\n", iValue);
            AppPresentLedBar(iValue, true);
        }

        // Message scheme: 'SmBrd/<DevID>/OutData/Led'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[7])
        {
            LedBarIndicator_g = kLedBarNone;                        // prevent overwriting of LED Bar by other sources
            iLedNum = strMsgPayload.substring(0,1).toInt();
            iValue = strMsgPayload.substring(2,3).toInt();
            fLedState = (iValue != 0) ? HIGH : LOW;
            TRACE2("   Set 'Led': %d to: %d\n", iLedNum, fLedState);
            AppSetLed(iLedNum, fLedState);
        }
    }
    else if (strMsgTopic.indexOf("ack") > 0)
    {
        TRACE0("   Recognized Messages Typ: 'Ack'\n");

        // Message scheme: 'SmBrd/<DevID>/Ack/PacketNum'
        if (strMsgTopic_p == astrMqttSubscribeTopicList_g[8])
        {
            i64Value = strtoll(strMsgPayload.c_str(), NULL, 10);
            ui32LastAckMqttPacketNum_g = (uint32_t)i64Value;
            TRACE1("   Received ACK for 'PacketNum': %lu\n", ui32LastAckMqttPacketNum_g);
        }
    }
    else
    {
        TRACE0("   WARNING: Unknown Messages Typ\n");
    }


    TRACE0("- 'AppProcessMqttDataMessage()'\n");

    return;

}




//---------------------------------------------------------------------------
//  Process LED Bar Indicator depending on its Data Source
//---------------------------------------------------------------------------

void  AppProcessLedBarIndicator()
{

    switch (LedBarIndicator_g)
    {
        case kLedBarNone:
        {
            break;
        }

        case kLedBarDht22Temperature:
        {
            AppSetDht22TemperatureLedBarIndicator();
            break;
        }

        case kLedBarDht22Humidity:
        {
            AppSetDht22HumidityLedBarIndicator();
            break;
        }

        case kLedBarMhz19Co2Level:
        {
            AppSetMhz19Co2LedBarIndicator();
            break;
        }

        default:
        {
            AppPresentLedBar(0);
            break;
        }
    }

    return;

}



//---------------------------------------------------------------------------
//  Set LED Bar Indicator for DHT22 Temperature
//---------------------------------------------------------------------------

void  AppSetDht22TemperatureLedBarIndicator()
{

int iBarValue;


    iBarValue = 0;

    //------------------------------------------
    if (flDhtTemperature_g < 0)
    {
        iBarValue = 0;
    }
    //------------------------------------------
    else if (flDhtTemperature_g <= 15)              // -+-
    {                                               //  |
        iBarValue = 1;                              //  |
    }                                               //  |
    else if (flDhtTemperature_g <= 18)              //  |
    {                                               //  |
        iBarValue = 2;                              //  | Green
    }                                               //  |
    else if (flDhtTemperature_g <= 20)              //  |
    {                                               //  |
        iBarValue = 3;                              //  |
    }                                               // -+-
    //------------------------------------------
    else if (flDhtTemperature_g <= 22)              // -+-
    {                                               //  |
        iBarValue = 4;                              //  |
    }                                               //  |
    else if (flDhtTemperature_g <= 24)              //  |
    {                                               //  |
        iBarValue = 5;                              //  | Yellow
    }                                               //  |
    else if (flDhtTemperature_g <= 26)              //  |
    {                                               //  |
        iBarValue = 6;                              //  |
    }                                               // -+-
    //------------------------------------------
    else if (flDhtTemperature_g <= 30)              // -+-
    {                                               //  |
        iBarValue = 7;                              //  |
    }                                               //  |
    else if (flDhtTemperature_g <= 35)              //  |
    {                                               //  |
        iBarValue = 8;                              //  | Red
    }                                               //  |
    else                                            //  |
    {                                               //  |
        iBarValue = 9;                              //  |
    }                                               // -+-


    AppPresentLedBar(iBarValue);


    return;

}



//---------------------------------------------------------------------------
//  Set LED Bar Indicator for DHT22 Humidity
//---------------------------------------------------------------------------

void  AppSetDht22HumidityLedBarIndicator()
{

int iBarValue;


    iBarValue = 0;

    //------------------------------------------
    if (flDhtHumidity_g < 5)
    {
        iBarValue = 0;
    }
    //------------------------------------------
    else if (flDhtHumidity_g <= 20)                 // -+-
    {                                               //  |
        iBarValue = 1;                              //  |
    }                                               //  |
    else if (flDhtHumidity_g <= 30)                 //  |
    {                                               //  |
        iBarValue = 2;                              //  | Green
    }                                               //  |
    else if (flDhtHumidity_g <= 40)                 //  |
    {                                               //  |
        iBarValue = 3;                              //  |
    }                                               // -+-
    //------------------------------------------
    else if (flDhtHumidity_g <= 50)                 // -+-
    {                                               //  |
        iBarValue = 4;                              //  |
    }                                               //  |
    else if (flDhtHumidity_g <= 60)                 //  |
    {                                               //  |
        iBarValue = 5;                              //  | Yellow
    }                                               //  |
    else if (flDhtHumidity_g <= 70)                 //  |
    {                                               //  |
        iBarValue = 6;                              //  |
    }                                               // -+-
    //------------------------------------------
    else if (flDhtHumidity_g <= 80)                 // -+-
    {                                               //  |
        iBarValue = 7;                              //  |
    }                                               //  |
    else if (flDhtHumidity_g <= 90)                 //  |
    {                                               //  |
        iBarValue = 8;                              //  | Red
    }                                               //  |
    else                                            //  |
    {                                               //  |
        iBarValue = 9;                              //  |
    }                                               // -+-


    AppPresentLedBar(iBarValue);


    return;

}



//---------------------------------------------------------------------------
//  Set LED Bar Indicator for MH-Z19 CO2 Level
//---------------------------------------------------------------------------

void  AppSetMhz19Co2LedBarIndicator()
{

int iBarValue;


    iBarValue = 0;

    //------------------------------------------
    if (iMhz19Co2Value_g < 10)
    {
        iBarValue = 0;
    }
    //------------------------------------------
    else if (iMhz19Co2Value_g <= 500)               // -+-
    {                                               //  |
        iBarValue = 1;                              //  |
    }                                               //  |
    else if (iMhz19Co2Value_g <= 750)               //  |
    {                                               //  |
        iBarValue = 2;                              //  | Green
    }                                               //  |
    else if (iMhz19Co2Value_g <= 1000)              //  |
    {                                               //  |
        iBarValue = 3;                              //  |
    }                                               // -+-
    //------------------------------------------
    else if (iMhz19Co2Value_g <= 1333)              // -+-
    {                                               //  |
        iBarValue = 4;                              //  |
    }                                               //  |
    else if (iMhz19Co2Value_g <= 1666)              //  |
    {                                               //  |
        iBarValue = 5;                              //  | Yellow
    }                                               //  |
    else if (iMhz19Co2Value_g <= 2000)              //  |
    {                                               //  |
        iBarValue = 6;                              //  |
    }                                               // -+-
    //------------------------------------------
    else if (iMhz19Co2Value_g <= 3000)              // -+-
    {                                               //  |
        iBarValue = 7;                              //  |
    }                                               //  |
    else if (iMhz19Co2Value_g <= 4000)              //  |
    {                                               //  |
        iBarValue = 8;                              //  | Red
    }                                               //  |
    else                                            //  |
    {                                               //  |
        iBarValue = 9;                              //  |
    }                                               // -+-


    AppPresentLedBar(iBarValue);


    return;

}



//---------------------------------------------------------------------------
//  Get App Runtime Configuration
//---------------------------------------------------------------------------

String  AppGetRuntimeConfig()
{

String  strRuntimeConfig;


    strRuntimeConfig  = "";
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt1 ? "N" : "";     // (N)etworkScan              (-> DEFAULT_CFG_ENABLE_NETWORK_SCAN)
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt2 ? "I" : "";     // (I)/O                      (-> DEFAULT_CFG_ENABLE_DI_DO)
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt3 ? "T" : "";     // (T)emperature+Humidity     (-> DEFAULT_CFG_ENABLE_DHT_SENSOR)
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt4 ? "C" : "";     // (C)O2                      (-> DEFAULT_CFG_ENABLE_MHZ_SENSOR)
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt5 ? "L" : "";     // Status(L)ed                (-> DEFAULT_CFG_ENABLE_STATUS_LED)
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt6 ? "W" : "";     // (W)atchdog                 (-> DEFAULT_CFG_ENABLE_HW_WDT)
    strRuntimeConfig += AppCfgData_g.m_fAppRtOpt7 ? "A" : "";     // Serve Watchdog on (A)CK    (-> DEFAULT_CFG_SRV_WDT_ON_MQTT_RECV)

    return (strRuntimeConfig);

}





//=========================================================================//
//                                                                         //
//          S M A R T B O A R D   H A R D W A R E   F U N C T I O N S      //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Process Inputs
//---------------------------------------------------------------------------

void  AppProcessInputs (tSensorData* pSensorDataRec_p, bool fPrintSensorValues_p)
{

int  iStateKey0;
int  iStateKey1;


    pSensorDataRec_p->m_iKey0Changed = 0;
    pSensorDataRec_p->m_iKey1Changed = 0;


    // process KEY0
    iStateKey0 = (digitalRead(PIN_KEY0) == LOW) ? 1 : 0;                // Keys are inverted (1=off, 0=on)
    if (iLastStateKey0_g != iStateKey0)
    {
        if (iStateKey0 == 1)
        {
            if ( fPrintSensorValues_p )
            {
                Serial.println("State changed: KEY0=1");
            }
            pSensorDataRec_p->m_iKey0 = 1;
            pSensorDataRec_p->m_iKey0Changed = 1;
        }
        else
        {
            if ( fPrintSensorValues_p )
            {
                Serial.println("State changed: KEY0=0");
            }
            pSensorDataRec_p->m_iKey0 = 0;
            pSensorDataRec_p->m_iKey0Changed = 1;
        }

        iLastStateKey0_g = iStateKey0;
    }

    // process KEY1
    iStateKey1 = (digitalRead(PIN_KEY1) == LOW) ? 1 : 0;                // Keys are inverted (1=off, 0=on)
    if (iLastStateKey1_g != iStateKey1)
    {
        if (iStateKey1 == 1)
        {
            if ( fPrintSensorValues_p )
            {
                Serial.println("State changed: KEY1=1");
            }
            pSensorDataRec_p->m_iKey1 = 1;
            pSensorDataRec_p->m_iKey1Changed = 1;
        }
        else
        {
            if ( fPrintSensorValues_p )
            {
                Serial.println("State changed: KEY1=0");
            }
            pSensorDataRec_p->m_iKey1 = 0;
            pSensorDataRec_p->m_iKey1Changed = 1;
        }

        iLastStateKey1_g = iStateKey1;
    }

    return;

}



//---------------------------------------------------------------------------
//  Process DHT Sensor (Temperature/Humidity)
//---------------------------------------------------------------------------

void  AppProcessDhtSensor (uint32_t ui32DhtReadInterval_p, tSensorData* pSensorDataRec_p, bool fPrintSensorValues_p)
{

char      szSensorValues[64];
uint32_t  ui32CurrTick;
float     flTemperature;
float     flHumidity;
float     flAverageTemperature;
float     flAverageHumidity;


    ui32CurrTick = millis();
    if ((ui32CurrTick - ui32LastTickDhtRead_g) >= ui32DhtReadInterval_p)
    {
        flTemperature = DhtSensor_g.readTemperature(false);                 // false = Temp in Celsius degrees, true = Temp in Fahrenheit degrees
        flHumidity    = DhtSensor_g.readHumidity();

        // check if values read from DHT22 sensor are valid
        if ( !isnan(flTemperature) && !isnan(flHumidity) )
        {
            // use average values to eliminate noise
            flAverageTemperature = AverageTemperature_g.CalcMovingAverage(flTemperature);
            pSensorDataRec_p->m_flTemperature = flAverageTemperature;
            flAverageHumidity = AverageHumidity_g.CalcMovingAverage(flHumidity);
            pSensorDataRec_p->m_flHumidity = flAverageHumidity;

            // chache DHT22 sensor values for displaying in the LED Bar indicator
            flDhtTemperature_g = flAverageTemperature;
            flDhtHumidity_g    = flAverageHumidity;

            // print DHT22 sensor values in Serial Console (Serial Monitor)
            if ( fPrintSensorValues_p )
            {
                snprintf(szSensorValues, sizeof(szSensorValues), "DHT22: Temperature = %.1f °C (Ø %.1f °C), Humidity = %.1f %% (Ø %.1f %%)", flTemperature, flAverageTemperature, flHumidity, flAverageHumidity);
                Serial.println(szSensorValues);
                Serial.flush();
            }
        }
        else
        {
            // reading sensor failed
            Serial.println("ERROR: Failed to read from DHT sensor!");
        }

        ui32LastTickDhtRead_g = ui32CurrTick;
    }

    return;

}



//---------------------------------------------------------------------------
//  Process MH-Z19 Sensor (CO2Value/SensorTemperature)
//---------------------------------------------------------------------------

void  AppProcessMhz19Sensor (uint32_t ui32Mhz19ReadInterval_p, tSensorData* pSensorDataRec_p, bool fPrintSensorValues_p)
{

char      szSensorValues[64];
uint32_t  ui32CurrTick;
int       iMhz19Co2Value;
int       iMhz19Co2SensTemp;


    ui32CurrTick = millis();
    if ((ui32CurrTick - ui32LastTickMhz19Read_g) >= ui32Mhz19ReadInterval_p)
    {
        iMhz19Co2Value    = Mhz19Sensor_g.getCO2();                     // MH-Z19: Request CO2 (as ppm)
        pSensorDataRec_p->m_iCo2Value = iMhz19Co2Value;
        iMhz19Co2SensTemp = Mhz19Sensor_g.getTemperature();             // MH-Z19: Request Sensor Temperature (as Celsius)
        pSensorDataRec_p->m_iCo2SensTemp = iMhz19Co2SensTemp;

        // chache MH-Z19 sensor value for displaying in the LED Bar indicator
        iMhz19Co2Value_g    = iMhz19Co2Value;
        iMhz19Co2SensTemp_g = iMhz19Co2SensTemp;

        // print MH-Z19 sensor values in Serial Console (Serial Monitor)
        if ( fPrintSensorValues_p )
        {
            snprintf(szSensorValues, sizeof(szSensorValues), "MH-Z19: CO2(ppm)=%i, SensTemp=%i", iMhz19Co2Value, iMhz19Co2SensTemp);
            Serial.println(szSensorValues);
        }

        ui32LastTickMhz19Read_g = ui32CurrTick;
    }

    return;

}



//---------------------------------------------------------------------------
//  Calibrate MH-Z19 Sensor Manually
//---------------------------------------------------------------------------

bool  AppMhz19CalibrateManually()
{

int   iCountDownCycles;
bool  fCalibrateSensor;


    // turn off auto calibration
    Mhz19Sensor_g.autoCalibration(false);

    Serial.println();
    Serial.println();
    Serial.println("************************************************");
    Serial.println("---- Manually MH-Z19 Sensor Calibration ----");
    Serial.print("Countdown: ");

    // run calibration start countdown
    // (KEY0=pressed -> continue countdown, KEY0=released -> abort countdown)
    iCountDownCycles = 9;                                               // LED Bar Length = 9 LEDs
    fCalibrateSensor = true;
    do
    {
        Serial.print(iCountDownCycles); Serial.print(" ");
        if ( digitalRead(PIN_KEY0) )                                    // Keys are inverted (1=off, 0=on)
        {
            fCalibrateSensor = false;
            break;
        }
        AppPresentLedBar(iCountDownCycles);
        delay(500);

        if ( digitalRead(PIN_KEY0) )                                    // Keys are inverted (1=off, 0=on)
        {
            fCalibrateSensor = false;
            break;
        }
        AppPresentLedBar(0);
        delay(500);
    }
    while (iCountDownCycles-- > 0);

    // run calibration or abort?
    if ( fCalibrateSensor )
    {
        // calibrate sensor
        Serial.println();
        Serial.println("MH-Z19 Sensor Calibration...");
        Mhz19Sensor_g.calibrate();
        Serial.println("  -> done.");

        // signal calibration finished
        iCountDownCycles = 3;
        do
        {
            AppPresentLedBar(9);
            delay(125);
            AppPresentLedBar(0);
            delay(125);
        }
        while (iCountDownCycles-- > 1);
    }
    else
    {
        Serial.println("ABORT");
    }

    Serial.println("************************************************");
    Serial.println();
    Serial.println();

    return (fCalibrateSensor);

}



//---------------------------------------------------------------------------
//  Calibrate MH-Z19 Sensor Unattended
//---------------------------------------------------------------------------

bool  AppMhz19CalibrateUnattended()
{

// According to DataSheet the MH-Z19 Sensor must be in stable 400ppm ambient
// environment for more than 20 minutes.
uint32_t  ui32LeadTime = (25 * 60 * 1000);                       // 25 Minutes in [ms]

char      szTextBuff[64];
uint32_t  ui32StartTime;
int32_t   i32RemainTime;
int       iCountDownCycles;
int       iLedBarValue;


    // turn off auto calibration
    Mhz19Sensor_g.autoCalibration(false);

    Serial.println();
    Serial.println();
    Serial.println("************************************************");
    Serial.println("---- Unattended MH-Z19 Sensor Calibration ----");
    Serial.println("Countdown:");

    // run countdown to stabilize sensor in a 400ppm ambient environment for more than 20 minutes
    ui32StartTime = millis();
    do
    {
        i32RemainTime = (int) (ui32LeadTime - (millis() - ui32StartTime));

        // divide the remaining time over the 9 LEDs of LED Bar
        iLedBarValue = (int)((i32RemainTime / (ui32LeadTime / 9)) + 1);
        if (iLedBarValue > 9)
        {
            iLedBarValue = 9;
        }

        snprintf(szTextBuff, sizeof(szTextBuff), "  Remaining Time: %d [sec] -> LedBarValue=%d", i32RemainTime/1000, iLedBarValue);
        Serial.println(szTextBuff);

        AppPresentLedBar(iLedBarValue);
        delay(500);
        AppPresentLedBar(0);
        delay(500);
    }
    while (i32RemainTime > 0);

    // calibrate sensor
    Serial.println();
    Serial.println("MH-Z19 Sensor Calibration...");
    Mhz19Sensor_g.calibrate();
    Serial.println("  -> done.");

    // signal calibration finished
    iCountDownCycles = 3;
    do
    {
        AppPresentLedBar(9);
        delay(125);
        AppPresentLedBar(0);
        delay(125);
    }
    while (iCountDownCycles-- > 1);

    Serial.println("************************************************");
    Serial.println();
    Serial.println();

    return (true);

}



//---------------------------------------------------------------------------
//  Present LED Bar (0 <= iBarValue_p <= 9)
//---------------------------------------------------------------------------

void  AppPresentLedBar (int iBarValue_p)
{
    AppPresentLedBar(iBarValue_p, false);
}
//---------------------------------------------------------------------------
void  AppPresentLedBar (int iBarValue_p, bool fInvertBar_p)
{

uint32_t  ui32BarBitMap;


    if (iBarValue_p < 0)
    {
        ui32BarBitMap = 0x0000;             // set LED0..LED8 = OFF
    }
    else if (iBarValue_p > 9)
    {
        ui32BarBitMap = 0x01FF;             // set LED0..LED8 = ON
    }
    else
    {
        ui32BarBitMap = 0x0000;
        while (iBarValue_p > 0)
        {
            ui32BarBitMap <<= 1;
            ui32BarBitMap  |= 1;
            iBarValue_p--;
        }
    }

    if ( fInvertBar_p )
    {
        ui32BarBitMap ^= 0x01FF;
    }

    digitalWrite(PIN_LED0, (ui32BarBitMap & 0b000000001) ? HIGH : LOW);
    digitalWrite(PIN_LED1, (ui32BarBitMap & 0b000000010) ? HIGH : LOW);
    digitalWrite(PIN_LED2, (ui32BarBitMap & 0b000000100) ? HIGH : LOW);
    digitalWrite(PIN_LED3, (ui32BarBitMap & 0b000001000) ? HIGH : LOW);
    digitalWrite(PIN_LED4, (ui32BarBitMap & 0b000010000) ? HIGH : LOW);
    digitalWrite(PIN_LED5, (ui32BarBitMap & 0b000100000) ? HIGH : LOW);
    digitalWrite(PIN_LED6, (ui32BarBitMap & 0b001000000) ? HIGH : LOW);
    digitalWrite(PIN_LED7, (ui32BarBitMap & 0b010000000) ? HIGH : LOW);
    digitalWrite(PIN_LED8, (ui32BarBitMap & 0b100000000) ? HIGH : LOW);

    return;

}



//---------------------------------------------------------------------------
//  Set Single LED
//---------------------------------------------------------------------------

void  AppSetLed (int iLedNum_p, bool fLedState_p)
{

    switch (iLedNum_p)
    {
        case 0:     digitalWrite(PIN_LED0, fLedState_p);    break;
        case 1:     digitalWrite(PIN_LED1, fLedState_p);    break;
        case 2:     digitalWrite(PIN_LED2, fLedState_p);    break;
        case 3:     digitalWrite(PIN_LED3, fLedState_p);    break;
        case 4:     digitalWrite(PIN_LED4, fLedState_p);    break;
        case 5:     digitalWrite(PIN_LED5, fLedState_p);    break;
        case 6:     digitalWrite(PIN_LED6, fLedState_p);    break;
        case 7:     digitalWrite(PIN_LED7, fLedState_p);    break;
        case 8:     digitalWrite(PIN_LED8, fLedState_p);    break;
        default:                                            break;
    }

    return;

}





//=========================================================================//
//                                                                         //
//          E S P 3 2   H A R D W A R E   T I M E R   F U N C T I O N S    //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  ESP32 Hardware Timer ISR
//---------------------------------------------------------------------------

void  IRAM_ATTR  OnTimerISR()
{

    bStatusLedState_g = !bStatusLedState_g;
    digitalWrite(PIN_STATUS_LED, bStatusLedState_g);

    return;

}



//---------------------------------------------------------------------------
//  ESP32 Hardware Timer Start
//---------------------------------------------------------------------------

void  Esp32TimerStart (uint32_t ui32TimerPeriod_p)
{

uint32_t  ui32TimerPeriod;


    // stop a timer that may still be running
    Esp32TimerStop();

    // use 1st timer of 4
    // 1 tick take 1/(80MHZ/80) = 1us -> set divider 80 and count up
    pfnOnTimerISR_g = timerBegin(0, 80, true);

    // attach OnTimerISR function to timer
    timerAttachInterrupt(pfnOnTimerISR_g, &OnTimerISR, true);

    // set alarm to call OnTimerISR function, repeat alarm automatically (third parameter)
    ui32TimerPeriod = (unsigned long)ui32TimerPeriod_p * 1000L;         // ms -> us
    timerAlarmWrite(pfnOnTimerISR_g, ui32TimerPeriod, true);

    // start periodically alarm
    timerAlarmEnable(pfnOnTimerISR_g);

    return;

}



//---------------------------------------------------------------------------
//  ESP32 Hardware Timer Stop
//---------------------------------------------------------------------------

void  Esp32TimerStop()
{

    if (pfnOnTimerISR_g != NULL)
    {
        // stop periodically alarm
        timerAlarmDisable(pfnOnTimerISR_g);

        // dettach OnTimerISR function from timer
        timerDetachInterrupt(pfnOnTimerISR_g);

        // stop timer
        timerEnd(pfnOnTimerISR_g);
    }

    bStatusLedState_g = LOW;
    digitalWrite(PIN_STATUS_LED, bStatusLedState_g);

    return;

}





//=========================================================================//
//                                                                         //
//          P R I V A T E   G E N E R I C   F U N C T I O N S              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Get DeviceID
//---------------------------------------------------------------------------

String  GetDeviceID (const char* pszDeviceID_p)
{

String  strDeviceID;


    TRACE1("+ GetDeviceID(pszDeviceID_p='%s')\n", (pszDeviceID_p ? pszDeviceID_p : "[NULL]"));
    if ( (pszDeviceID_p != NULL) && (strlen(pszDeviceID_p) > 0) )
    {
        TRACE0("  -> use calling paameter 'pszDeviceID_p'\n");
        strDeviceID = pszDeviceID_p;
    }
    else
    {
        // use ChipID (the ChipID is essentially its 6byte MAC address) as DeviceID
        TRACE0("  -> use ChipID\n");
        strDeviceID = GetChipID();
    }

    TRACE1("- GetDeviceID(strDeviceID='%s')\n", strDeviceID.c_str());
    return (strDeviceID);

}



//---------------------------------------------------------------------------
//  Get Unique Client Name
//---------------------------------------------------------------------------

String  GetUniqueClientName (const char* pszClientPrefix_p)
{

String  strChipID;
String  strClientName;


    // Create a unique client name, based on ChipID (the ChipID is essentially its 6byte MAC address)
    strChipID = GetChipID();
    strClientName  = pszClientPrefix_p;
    strClientName += strChipID;

    return (strClientName);

}



//---------------------------------------------------------------------------
//  Get ChipID as String
//---------------------------------------------------------------------------

String  GetChipID()
{

String  strChipID;


    strChipID = GetEsp32MacId(false);

    return (strChipID);

}



//---------------------------------------------------------------------------
//  Get ChipMAC as String
//---------------------------------------------------------------------------

String  GetChipMAC()
{

String  strChipMAC;


    strChipMAC = GetEsp32MacId(true);

    return (strChipMAC);

}



//---------------------------------------------------------------------------
//  Get GetEsp32MacId as String
//---------------------------------------------------------------------------

String  GetEsp32MacId (bool fUseMacFormat_p)
{

uint64_t  ui64MacID;
String    strMacID;
byte      bDigit;
char      acDigit[2];
int       iIdx;


    ui64MacID = ESP.getEfuseMac();
    strMacID = "";
    for (iIdx=0; iIdx<6; iIdx++)
    {
        bDigit = (byte) (ui64MacID >> (iIdx * 8));
        sprintf(acDigit, "%02X", bDigit);
        strMacID += String(acDigit);

        if (fUseMacFormat_p && (iIdx<5))
        {
            strMacID += ":";
        }
    }

    strMacID.toUpperCase();

    return (strMacID);

}



//---------------------------------------------------------------------------
//  Get System Uptime
//---------------------------------------------------------------------------

String  GetSysUptime (uint32_t* pui32Uptime_p)
{

uint32_t  ui32Uptime;
String    strUptime;


    ui32Uptime = millis();
    strUptime = FormatDateTime(ui32Uptime, false, true);

    *pui32Uptime_p = ui32Uptime;
    return (strUptime);

}



//---------------------------------------------------------------------------
//  Format Date/Time
//---------------------------------------------------------------------------

String  FormatDateTime (uint32_t ui32TimeTicks_p, bool fForceDay_p, bool fForceTwoDigitsHours_p)
{

const uint32_t  MILLISECONDS_PER_DAY    = 86400000;
const uint32_t  MILLISECONDS_PER_HOURS  = 3600000;
const uint32_t  MILLISECONDS_PER_MINUTE = 60000;
const uint32_t  MILLISECONDS_PER_SECOND = 1000;


char      szTextBuff[64];
uint32_t  ui32TimeTicks;
uint32_t  ui32Days;
uint32_t  ui32Hours;
uint32_t  ui32Minutes;
uint32_t  ui32Seconds;
String    strDateTime;


    ui32TimeTicks = ui32TimeTicks_p;

    ui32Days = ui32TimeTicks / MILLISECONDS_PER_DAY;
    ui32TimeTicks = ui32TimeTicks - (ui32Days * MILLISECONDS_PER_DAY);

    ui32Hours = ui32TimeTicks / MILLISECONDS_PER_HOURS;
    ui32TimeTicks = ui32TimeTicks - (ui32Hours * MILLISECONDS_PER_HOURS);

    ui32Minutes = ui32TimeTicks / MILLISECONDS_PER_MINUTE;
    ui32TimeTicks = ui32TimeTicks - (ui32Minutes * MILLISECONDS_PER_MINUTE);

    ui32Seconds = ui32TimeTicks / MILLISECONDS_PER_SECOND;

    if ( (ui32Days > 0) || fForceDay_p )
    {
        snprintf(szTextBuff, sizeof(szTextBuff), "%ud/%02u:%02u:%02u", (uint)ui32Days, (uint)ui32Hours, (uint)ui32Minutes, (uint)ui32Seconds);
    }
    else
    {
        if ( fForceTwoDigitsHours_p )
        {
            snprintf(szTextBuff, sizeof(szTextBuff), "%02u:%02u:%02u", (uint)ui32Hours, (uint)ui32Minutes, (uint)ui32Seconds);
        }
        else
        {
            snprintf(szTextBuff, sizeof(szTextBuff), "%u:%02u:%02u", (uint)ui32Hours, (uint)ui32Minutes, (uint)ui32Seconds);
        }
    }
    strDateTime = String(szTextBuff);

    return (strDateTime);

}



//---------------------------------------------------------------------------
//  Decode Boot/Restart Reason
//---------------------------------------------------------------------------

String  DecodeBootReason (esp_reset_reason_t eBootReason_p)
{

String  strBootReason;


    switch (eBootReason_p)
    {
        case ESP_RST_UNKNOWN:       // 0
        {
            strBootReason = "Unknown reset reason (ESP_RST_UNKNOWN)";
            break;
        }

        case ESP_RST_POWERON:       // 1
        {
            strBootReason = "Reset due to power-on event (ESP_RST_POWERON)";
            break;
        }

        case ESP_RST_EXT:           // 2
        {
            strBootReason = "Reset by external pin - not applicable for ESP32 (ESP_RST_EXT)";
            break;
        }

        case ESP_RST_SW:            // 3
        {
            strBootReason = "Software reset via esp_restart (ESP_RST_SW)";
            break;
        }

        case ESP_RST_PANIC:         // 4
        {
            strBootReason = "Software reset due to exception/panic (ESP_RST_PANIC)";
            break;
        }

        case ESP_RST_INT_WDT:       // 5
        {
            strBootReason = "Reset (software or hardware) due to interrupt watchdog (ESP_RST_INT_WDT)";
            break;
        }

        case ESP_RST_TASK_WDT:      // 6
        {
            strBootReason = "Reset due to task watchdog (ESP_RST_TASK_WDT)";
            break;
        }

        case ESP_RST_WDT:           // 7
        {
            strBootReason = "Reset due to other watchdogs (ESP_RST_WDT)";
            break;
        }

        case ESP_RST_DEEPSLEEP:     // 8
        {
            strBootReason = "Reset after exiting deep sleep mode (ESP_RST_DEEPSLEEP)";
            break;
        }

        case ESP_RST_BROWNOUT:      // 9
        {
            strBootReason = "Brownout reset by software or hardware (ESP_RST_BROWNOUT)";
            break;
        }

        case ESP_RST_SDIO:          // 10
        {
            strBootReason = "Reset over SDIO (ESP_RST_SDIO)";
            break;
        }

        default:                    // ???
        {
            strBootReason = "??? unknown ???";
            break;
        }
    }

    return (strBootReason);

}





//=========================================================================//
//                                                                         //
//          D E B U G   F U N C T I O N S                                  //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  DEBUG: Dump Buffer
//---------------------------------------------------------------------------

#ifdef DEBUG_DUMP_BUFFER

void  DebugDumpBuffer (String strBuffer_p)
{

int            iBufferLen = strBuffer_p.length();
unsigned char  abDataBuff[iBufferLen];

    strBuffer_p.getBytes(abDataBuff, iBufferLen);
    DebugDumpBuffer(abDataBuff, strBuffer_p.length());

    return;

}

//---------------------------------------------------------------------------

void  DebugDumpBuffer (const void* pabDataBuff_p, unsigned int uiDataBuffLen_p)
{

#define COLUMNS_PER_LINE    16

const unsigned char*  pabBuffData;
unsigned int          uiBuffSize;
char                  szLineBuff[128];
unsigned char         bData;
int                   nRow;
int                   nCol;

    // get pointer to buffer and length of buffer
    pabBuffData = (const unsigned char*)pabDataBuff_p;
    uiBuffSize  = (unsigned int)uiDataBuffLen_p;


    // dump buffer contents
    for (nRow=0; ; nRow++)
    {
        sprintf(szLineBuff, "\n%04lX:   ", (unsigned long)(nRow*COLUMNS_PER_LINE));
        Serial.print(szLineBuff);

        for (nCol=0; nCol<COLUMNS_PER_LINE; nCol++)
        {
            if ((unsigned int)nCol < uiBuffSize)
            {
                sprintf(szLineBuff, "%02X ", (unsigned int)*(pabBuffData+nCol));
                Serial.print(szLineBuff);
            }
            else
            {
                Serial.print("   ");
            }
        }

        Serial.print(" ");

        for (nCol=0; nCol<COLUMNS_PER_LINE; nCol++)
        {
            bData = *pabBuffData++;
            if ((unsigned int)nCol < uiBuffSize)
            {
                if ((bData >= 0x20) && (bData < 0x7F))
                {
                    sprintf(szLineBuff, "%c", bData);
                    Serial.print(szLineBuff);
                }
                else
                {
                    Serial.print(".");
                }
            }
            else
            {
                Serial.print(" ");
            }
        }

        if (uiBuffSize > COLUMNS_PER_LINE)
        {
            uiBuffSize -= COLUMNS_PER_LINE;
        }
        else
        {
            break;
        }

        Serial.flush();     // give serial interface time to flush data
    }

    Serial.print("\n");

    return;

}

#endif




// EOF
