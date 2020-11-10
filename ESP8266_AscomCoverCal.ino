/*
 Program to implement ASCOM ALPACA Cover Calibrator interface for remote telescope flip-flat devices. 
 Typically implemented using REST calls over wireless to talk to the device and the device controls a RC servo via power switching throughexternal hardware.
 Power control in this case is through a p-channel mosfet where the source is wired to 12v and drain is the output. Taking gate to low will turn on the drain output. 
 This means we can move the RC serv to the desired position and thhen turn it off to hold it in that position. 
 Supports web interface on port 80 returning json string

Notes: 
 
 To do:
 Complete Setup page - in progress
  
 Done: 
 Complete EEPROM calls - done.
 
 Pin Layout:
 ESP8266-01e
 GPIO 3 (Rx) to PWM output - use for calibrator illumination control or secondary dew heater.
 GPIO 2 (SDA) to PWM RC signal 
 GPIO 0 (SCL) free
 GPIO 1 used as Serial Tx. 
 
 ESP8266-12
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 All 3.3v logic. 
 
Test:
curl -X PUT http://espACC01/api/v1/covercalibrator/0/Connected -d "ClientID=0&ClientTransactionID=0&Connected=true" (note cases)
http://espACC01/api/v1/switch/0/status
telnet espACC01 32272 (UDP)
 */
#define ESP8266_01

#include "DebugSerial.h"
#include "SkybadgerStrings.h"
#include "CoverCal_common.h"
#include "AlpacaErrorConsts.h"
#include <esp8266_peri.h> //register map and access
#include <ESP8266WiFi.h>
#include <PubSubClient.h> //https://pubsubclient.knolleary.net/api.html
#include <EEPROM.h>
#include "EEPROMAnything.h"
#include <Wire.h>         //https://playground.arduino.cc/Main/WireLibraryDetailedReference
#include <Time.h>         //Look at https://github.com/PaulStoffregen/Time for a more useful internal timebase library
#include <WiFiUdp.h>      //WiFi UDP discovery responder
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>  //https://arduinojson.org/v5/api/
#define MAX_TIME_INACTIVE 0 //to turn off the de-activation of a telnet session
#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug
//Create a remote debug object
RemoteDebug Debug;

#include <Servo.h> //Arduino example sketch
Servo myServo;  // create servo object to control a servo
// twelve servo objects can be created on most boards

extern "C" { 
//Ntp dependencies - available from v2.4
#include <time.h>
#include <sys/time.h>
//#include <coredecls.h>

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <String.h> 
#include <user_interface.h>
            }
time_t now; //use as 'gmtime(&now);'

//Strings
const char* defaultHostname = "espACC01";
char* myHostname = nullptr;

//MQTT settings
char* thisID = nullptr;

//Private variables for the driver
bool elPresent = false;
bool coverPresent = false; 
int illuminatorPWM = 0;
int rcMinLimit;
int rcMaxLimit;
int rcPosition;//Varies from rcMinLimit to rcMaxLimit
bool rcPowerOn = false;//Turn on power output transistor - provides power to the servo 

//ASCOM variables for the driver
enum CoverStatus { NotPresent, Closed, Moving, Open, Unknown, Error, Halted };
/*
NotPresent  0 This device does not have a cover that can be closed independently
Closed  1 The cover is closed
Moving  2 The cover is moving to a new position
Open  3 The cover is open
Unknown 4 The state of the cover is unknown
Error 5 The device encountered an error when changing state
*/
enum CalibratorStatus { CalNotPresent, Off, NotReady, Ready, CalUnknown, CalError };

/*
NotPresent  0 This device does not have a calibration capability
Off 1 The calibrator is off
NotReady  2 The calibrator is stabilising or is not yet in the commanded state
Ready 3 The calibrator is ready for use
Unknown 4 The calibrator state is unknown
Error 5 The calibrator encountered an error when changing state
*/

//Returns the state of the device cover, if present, otherwise returns "NotPresent"
enum CoverStatus coverState = Closed;
enum CoverStatus targetCoverState = Closed;     

//Returns the current calibrator brightness in the range 0 (completely off) to MaxBrightness (fully on)
int brightness;         
//The Brightness value that makes the calibrator deliver its maximum illumination.

//Returns the state of the calibration device, if present, otherwise returns "NotPresent"
enum CalibratorStatus calibratorState = Off;
enum CalibratorStatus targetCalibratorState = Off ;   

WiFiClient espClient;
PubSubClient client(espClient);
volatile bool callbackFlag = false;

// Create an instance of the server
// specify the port to listen on as an argument
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

//UDP Port can be edited in setup page
int udpPort = ALPACA_DISCOVERY_PORT;
WiFiUDP Udp;

//Hardware device system functions - reset/restart, timers etc
EspClass device;
ETSTimer timer;
volatile bool newDataFlag = false;
void onTimer(void * pArg);

ETSTimer timeoutTimer;
void onTimeoutTimer(void * pArg);
volatile bool timeoutFlag = false;
volatile bool timerSet = false;

//Superceded by use of myServo library
//ETSTimer biTimer, monoTimer;
//void onBiTimer( void * pArg);
//void onMonoTimer( void * pArg);

const int loopFunctiontime = 250;//ms

//Order sensitive
#include "Skybadger_common_funcs.h"
#include "JSONHelperFunctions.h"
#include "CoverCal_common.h"
#include "CoverCal_eeprom.h"
#include "ASCOMAPICommon_rest.h" //ASCOM common driver web handlers. 
#include "ESP8266_coverhandler.h"

//Timer handler for 'soft' interrupt handler
void onTimer( void * pArg )
{
  newDataFlag = true;
}

//Used to complete MQTT timeout actions. 
void onTimeoutTimer( void* pArg )
{
  //Read command list and apply. 
  timeoutFlag = true;
}

void setup_wifi()
{
  int zz = 0;
  WiFi.mode(WIFI_STA); 
  WiFi.hostname( myHostname );
  WiFi.begin( ssid1, password1 );
  Serial.print("Searching for WiFi..");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
      Serial.print(".");
   if ( zz++ > 200 ) 
    device.restart();
  }

  Serial.println("WiFi connected");
  Serial.printf("SSID: %s, Signal strength %i dBm \n\r", WiFi.SSID().c_str(), WiFi.RSSI() );
  Serial.printf("Hostname: %s\n\r",      WiFi.hostname().c_str() );
  Serial.printf("IP address: %s\n\r",    WiFi.localIP().toString().c_str() );
  Serial.printf("DNS address 0: %s\n\r", WiFi.dnsIP(0).toString().c_str() );
  Serial.printf("DNS address 1: %s\n\r", WiFi.dnsIP(1).toString().c_str() );

  //Setup sleep parameters
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  //wifi_set_sleep_type(NONE_SLEEP_T);

  delay(5000);
  Serial.println( "WiFi connected" );
}

void setup()
{
  Serial.begin( 115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println(F("ESP starting."));

  //Start NTP client
  configTime(TZ_SEC, DST_SEC, timeServer1, timeServer2, timeServer3 );
  delay( 5000);
  
  //Setup default data structures
  DEBUGSL1("Setup EEprom variables"); 
  EEPROM.begin( 4 + (2*MAX_NAME_LENGTH) + (2 * sizeof(int) ) );
  //setDefaults();
  setupFromEeprom();
  DEBUGSL1("Setup eeprom variables complete."); 
  
  // Connect to wifi 
  setup_wifi();                   
  
  //Debugging over telnet setup
  // Initialize the server (telnet or web socket) of RemoteDebug
  //Debug.begin(HOST_NAME, startingDebugLevel );
  Debug.begin( WiFi.hostname().c_str(), Debug.ERROR ); 
  Debug.setSerialEnabled(true);//until set false 
  // Options
  // Debug.setResetCmdEnabled(true); // Enable the reset command
  // Debug.showProfiler(true); // To show profiler - time between messages of Debug
  //In practice still need to use serial commands until debugger is up and running.. 
  debugE("Remote debugger enabled and operating");

  //Open a connection to MQTT
  DEBUGSL1("Setting up MQTT."); 
  client.setServer( mqtt_server, 1883 );
  client.connect( thisID, pubsubUserID, pubsubUserPwd ); 
  //Create a heartbeat-based callback that causes this device to read the local i2C bus devices for data to publish.
  client.setCallback( callback );
  client.subscribe( inTopic );
  publishHealth();
    
  DEBUGSL1("Setup RC controls");
  
  //Pins mode and direction setup for i2c on ESP8266-01
  //RC pulse pin
  pinMode( RCPIN, OUTPUT);
  digitalWrite( RCPIN, 0);
  myServo.attach(RCPIN);  // attaches the servo on GIO2 to the servo object
  //Drives the RC pulse timing to keep the RC servo in position
  rcPosition = rcMinLimitDefault; //0 - 180 degrees.  Always start closed at 0
  myServo.write( rcPosition );
  
  //Currently spare - use to control EL if present. 
  pinMode( ELPIN, OUTPUT);
  digitalWrite( ELPIN, 0);
  
  //GPIO 3 (normally RX on -01) swap the pin to a GPIO or PWM. 
  //This pin controls the state of the power to the RC servo. Low is ON
  pinMode( POWERPIN, OUTPUT );
  digitalWrite( POWERPIN, 1);
  rcPowerOn = false;
   
  //Setup webserver handler functions
  
  //Common ASCOM handlers
  server.on("/api/v1/covercalibrator/0/action",              HTTP_PUT, handleAction );
  server.on("/api/v1/covercalibrator/0/commandblind",        HTTP_PUT, handleCommandBlind );
  server.on("/api/v1/covercalibrator/0/commandbool",         HTTP_PUT, handleCommandBool );
  server.on("/api/v1/covercalibrator/0/commandstring",       HTTP_PUT, handleCommandString );
  server.on("/api/v1/covercalibrator/0/connected",           handleConnected );
  server.on("/api/v1/covercalibrator/0/description",         HTTP_GET, handleDescriptionGet );
  server.on("/api/v1/covercalibrator/0/driverinfo",          HTTP_GET, handleDriverInfoGet );
  server.on("/api/v1/covercalibrator/0/driverversion",       HTTP_GET, handleDriverVersionGet );
  server.on("/api/v1/covercalibrator/0/interfaceversion",    HTTP_GET, handleInterfaceVersionGet );
  server.on("/api/v1/covercalibrator/0/name",                HTTP_GET, handleNameGet );
  server.on("/api/v1/covercalibrator/0/supportedactions",    HTTP_GET, handleSupportedActionsGet );

//CoverCalibrator-specific functions
//Properties - all GET
  server.on("/api/v1/covercalibrator/0/brightness",          HTTP_GET, handlerBrightnessGet );  
  server.on("/api/v1/covercalibrator/0/maxbrightness",       HTTP_GET, handlerMaxBrightnessGet );
  server.on("/api/v1/covercalibrator/0/coverstate",          HTTP_GET, handlerCoverStateGet );
  server.on("/api/v1/covercalibrator/0/calibratorstate",     HTTP_GET, handlerCalibratorStateGet );

//Methods
  server.on("/api/v1/covercalibrator/0/calibratoron",        HTTP_PUT, handlerCalibratorOnPut );
  server.on("/api/v1/covercalibrator/0/calibratoroff",       HTTP_PUT, handlerCalibratorOffPut );
  server.on("/api/v1/covercalibrator/0/closecover",          HTTP_PUT, handlerCloseCoverPut );  
  server.on("/api/v1/covercalibrator/0/haltcover",           HTTP_PUT, handlerHaltCoverPut );
  server.on("/api/v1/covercalibrator/0/opencover",           HTTP_PUT, handlerOpenCoverPut );

//Additional non-ASCOM custom setup calls
  server.on("/api/v1/covercalibrator/0/setup",      HTTP_GET, handlerSetup );
  server.on("/setup",                               HTTP_GET, handlerSetup );
  server.on("/status",                              HTTP_GET, handlerStatus);
  server.on("/restart",                             HTTP_ANY, handlerRestart);
  server.on("/setup/maxbrightness" ,                HTTP_ANY, handlerStatus );//todo
  server.on("/setup/hostname" ,                     HTTP_ANY, handlerStatus );//todo
  server.on("/setup/udpport",                       HTTP_ANY, handlerStatus );//todo
  server.on("/setup/setrcPWMLimits",                HTTP_ANY, handlerStatus );//todo

//Management interface calls.
//  server.on("/management/udpport",                       HTTP_ANY, handlerStatus );//todo
//  server.on("/management/setrcPWMLimits",                HTTP_ANY, handlerStatus );//todo

  //Basic setttings
  server.on("/", handlerStatus );
  server.onNotFound(handlerNotFound); 

  updater.setup( &server );
  server.begin();
  DEBUGSL1("Web server handlers setup & started");
  
  //Starts the discovery responder server
  Udp.begin( udpPort);
  
  //Setup timers
  //setup interrupt-based 'soft' alarm handler for periodic acquisition of new bearing
  ets_timer_setfn( &timer, onTimer, NULL ); 
  //Timer for MQTT reconnect handler
  ets_timer_setfn( &timeoutTimer, onTimeoutTimer, NULL ); 
  
  //Timer for one-shot pulse width timer. 
  //ets_timer_setfn( &monoTimer, onMonoTimer, NULL ); 
  //Timer for 20ms timing windows for RC pulse control 
  //ets_timer_setfn( &biTimer, onBiTimer, NULL ); 
  
  //fire loop function handler timer every 1000 msec
  ets_timer_arm_new( &timer, 1000, 1 /*repeat*/, 1);

  //Supports the MQTT async reconnect timer. Set elsewhere.
  //ets_timer_arm_new( &timeoutTimer, 2500, 0/*one-shot*/, 1);
  
  //Turn on the power to the RC 
  digitalWrite ( POWERPIN, 0 );

  //baseline driver variables
  
  Serial.println( "Setup complete" );
  Debug.setSerialEnabled(false);//until set false   
}

//Main processing loop
void loop()
{
  String timestamp;
  String output;
  
  DynamicJsonBuffer jsonBuffer(256);
  JsonObject& root = jsonBuffer.createObject();

  int udpBytesIn = Udp.parsePacket();
  if( udpBytesIn > 0  ) 
    handleDiscovery( udpBytesIn );
    
  if( newDataFlag == true ) 
  {
    //do some work
    manageCoverState( targetCoverState );
    manageCalibratorState ( targetCalibratorState );
    
    myServo.write( rcPosition );
    newDataFlag = false;
  }
    
  if( client.connected() )
  {
    if (callbackFlag == true )
    {
      //publish results
      publishHealth();
      callbackFlag = false;
    }
  }
  else
  {
    reconnect();
    client.subscribe (inTopic);
  }
  client.loop();
  
  //Handle web requests
  server.handleClient();

    // Remote debug over WiFi
  Debug.handle();
  // Or
  //debugHandle(); // Equal to SerialDebug  
}

 //Function to manage cover states
 int manageCoverState( CoverStatus targetState )
 {
  String msg = "";
  ///enum CoverStatus = {NotPresent, Closed, Moving, Open, Unknown, Error, Halted };
  //need a targetState.
  debugI( "Entered %s", "manageCoverState " );
  switch( coverState )
  {          
      case NotPresent: 
          break;
      case Moving:
          if( targetCoverState == Closed && rcPosition <= rcMinLimit ) 
          {
            digitalWrite( POWERPIN, 1 ); //turn power off                
            coverState = Closed;
          }
          else if ( targetCoverState == Open && rcPosition >= rcMaxLimit )
          {
            digitalWrite( POWERPIN, 1 ); //turn power off                
            coverState = Open;
          }
          else //Move towards target position
          {
            if ( targetCoverState == Closed )
              rcPosition = ( rcPosition <= rcMinLimit ) ? rcMinLimit : rcPosition -5;
            else 
              rcPosition = ( rcPosition >= rcMaxLimit ) ? rcMaxLimit :  rcPosition +5;              
          }
          break;
      case Open: 
          if ( rcPosition <=rcMaxLimit ) 
          {
            coverState = Moving;
            digitalWrite( POWERPIN, 0 ); //turn power on
          }
            break;
      case Closed:
          if ( rcPosition >= rcMinLimit ) 
          {
            coverState = Moving;
            digitalWrite( POWERPIN, 0 ); //turn power on
          }
            break;
      case Unknown:
      case Error:
      default:
        break;
  }

  return 0;
}
 
 int manageCalibratorState( CalibratorStatus calibratorTargetState )
 {
  return 0;
 }
 
 /* MQTT callback for subscription and topic.
 * Only respond to valid states ""
 * Publish under ~/skybadger/sensors/<sensor type>/<host>
 * Note that messages have an maximum length limit of 18 bytes - set in the MQTT header file. 
 */
void callback(char* topic, byte* payload, unsigned int length) 
{  
  //set callback flag
  callbackFlag = true;  
}

/*
 * Had to do a lot of work to get this to work 
 * Mostly around - 
 * length of output buffer
 * reset of output buffer between writing json strings otherwise it concatenates. 
 * Writing to serial output was essential.
 */
 void publishHealth( void )
 {
  String outTopic;
  String output;
  String timestamp;
  
  //checkTime();
  getTimeAsString2( timestamp );

  //publish to our device topic(s)
  DynamicJsonBuffer jsonBuffer(256);
  JsonObject& root = jsonBuffer.createObject();
  root["Time"] = timestamp;
  root["hostname"] = myHostname;
  root["Message"] = "Listening";
  
  root.printTo( output);
  
  //Put a notice out regarding device health
  outTopic = outHealthTopic;
  outTopic.concat( myHostname );
  if ( client.publish( outTopic.c_str(), output.c_str(), true ) )
    debugI( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
  else
    debugW( "Failed to publish : %s to %s", output.c_str(), outTopic.c_str() );
 }
 
 void handleDiscovery( int udpBytesCount )
 {
    char inBytes[64];
    String message;
    DiscoveryPacket discoveryPacket;
 
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    
    Serial.printf("UDP: %i bytes received from %s:%i\n", udpBytesCount, Udp.remoteIP().toString().c_str(), Udp.remotePort() );

    // We've received a packet, read the data from it
    Udp.read( inBytes, udpBytesCount); // read the packet into the buffer

    // display the packet contents
    for (int i = 0; i < udpBytesCount; i++ )
    {
      Serial.print( inBytes[i]);
      if (i % 32 == 0)
      {
        Serial.println();
      }
      else Serial.print(' ');
    } // end for
    Serial.println();
   
    //Is it for us ?
    char protocol[16];
    strncpy( protocol, (char*) inBytes, 16);
    if ( strcasecmp( discoveryPacket.protocol, protocol ) == 0 )
    {
      Udp.beginPacket( Udp.remoteIP(), Udp.remotePort() );
      //Respond with discovery message
      root["IPAddress"] = WiFi.localIP().toString().c_str();
      root["Type"] = DriverType;
      root["AlpacaPort"] = 80;
      root["Name"] = WiFi.hostname();
      root["UniqueID"] = system_get_chip_id();
      root.printTo( message );
      Udp.write( message.c_str(), sizeof( message.c_str() ) * sizeof(char) );
      Udp.endPacket();   
    }
 }
 
