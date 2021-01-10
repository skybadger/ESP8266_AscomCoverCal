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
 GPIO 3 (Rx) Used to control power to servo using PNP poweer mosfet.
 GPIO 2 (SDA) to PWM RC signal 
 GPIO 0 (SCL) Used to control illuminator using PWM via PNP power mosfet. 
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
//#define _TEST_RC_ //Enable simple RC servo testing. 
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
int rcMinLimit  = rcMinLimitDefault;
int rcMaxLimit  = rcMaxLimitDefault;
int initialPosition = 90;
volatile int rcPosition;//Varies from rcMinLimit to rcMaxLimit
bool rcPowerOn = false;//Turn on power output transistor - provides power to the servo 
#define RCPOWERPIN_ON 0
#define RCPOWERPIN_OFF 1

//Returns the current calibrator brightness in the range 0 (completely off) to MaxBrightness (fully on)
int brightness;         
bool brightnessChanged = false;
//The Brightness value that makes the calibrator deliver its maximum illumination.

//ASCOM variables for the driver
enum CoverStatus { NotPresent, Closed, Moving, Open, Unknown, Error, Halted };
const char* coverStatusCh[] = { "NotPresent", "Closed", "Moving", "Open", "Unknown", "Error", "Halted" };
/*
NotPresent  0 This device does not have a cover that can be closed independently
Closed  1 The cover is closed
Moving  2 The cover is moving to a new position
Open  3 The cover is open
Unknown 4 The state of the cover is unknown
Error 5 The device encountered an error when changing state
*/
enum CalibratorStatus { CalNotPresent, Off, NotReady, Ready, CalUnknown, CalError };
const char* calibratorStatusCh[] = { "NotPresent", "Off", "NotReady", "Ready", "Unknown", "Error" };

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

//ALPACA support additions
//UDP Port can be edited in setup page - eventually.
#define ALPACA_DISCOVERY_PORT 32227
int udpPort = ALPACA_DISCOVERY_PORT;
WiFiUDP Udp;

//Hardware device system functions - reset/restart, timers etc
EspClass device;
ETSTimer timer;
volatile bool newDataFlag = false;
void onTimer(void * pArg);

//Used by MQTT included code
ETSTimer timeoutTimer;
void onTimeoutTimer(void * pArg);
volatile bool timeoutFlag = false;
volatile bool timerSet = false;

ETSTimer rcTimer;
void onRcTimeoutTimer(void * pArg);

ETSTimer ELTimer;
void onELTimeoutTimer(void * pArg);
volatile bool ELTimeoutFlag = false;

const int loopFunctiontime = 250;//ms

//Order sensitive
#include "Skybadger_common_funcs.h"
#include "JSONHelperFunctions.h"
#include "CoverCal_common.h"
#include "CoverCal_eeprom.h"
#include "ASCOMAPICommon_rest.h" //ASCOM common driver web handlers. 
#include "ESP8266_coverhandler.h"
#include <AlpacaManagement.h>

//State Management functions called in loop.
int manageCalibratorState( CalibratorStatus calibratorTargetState );
int manageCoverState( CoverStatus coverTargetState );

//Timer handler for 'soft' interrupt handler
void onTimer( void* pArg )
{
  newDataFlag = true;
}

//Used to complete MQTT timeout actions. 
void onTimeoutTimer( void* pArg )
{
  //Read command list and apply. 
  timeoutFlag = true;
}

//Timer handler for 'soft' interrupt handler
void onRCTimeoutTimer( void * pArg )
{
  bool restartTimer = false;
  debugV( "Updating cover servo - position %d", rcPosition );
  if( targetCoverState == CoverStatus::Open )
  {
    if ( rcPosition >= rcMaxLimit )
    {
      coverState = CoverStatus::Open;
      digitalWrite( POWERPIN, RCPOWERPIN_OFF ); //turn power on
      rcPowerOn = false;   
      restartTimer = false;
    } 
    else 
    {
      if ( rcPowerOn != true )
      {
        digitalWrite( POWERPIN, RCPOWERPIN_ON ); //turn power on
        rcPowerOn = true;                         
      }
      rcPosition += RCPOSITIONINCREMENT;
      myServo.write( rcPosition );
      restartTimer = true;
    }
  }
  else if ( targetCoverState == CoverStatus::Closed )
  {
    if ( rcPosition <= rcMinLimit )
    {
      digitalWrite( POWERPIN, RCPOWERPIN_OFF ); //turn power off
      rcPowerOn = false;                 
      coverState = CoverStatus::Closed;
      restartTimer = false;
    }
    else
    {
      rcPosition -= RCPOSITIONINCREMENT;
      myServo.write( rcPosition );
      restartTimer = true;
    }
  }
  else if ( targetCoverState == CoverStatus::Halted )
  {
    restartTimer = false;
    digitalWrite( POWERPIN, RCPOWERPIN_OFF ); //turn power off
    rcPowerOn = false;                 
    coverState = CoverStatus::Halted;
  }
    
  if ( restartTimer ) 
    ets_timer_arm_new( &rcTimer, 100, 0/*one-shot*/, 1);
}

void onELTimeoutTimer(void * pArg)
{
  ELTimeoutFlag = false;
  
  //Update our state from NOTREADY to READY
  calibratorState       = CalibratorStatus::Ready;
  targetCalibratorState = CalibratorStatus::Ready;
}

void setup_wifi()
{
  int zz = 0;
  WiFi.mode(WIFI_STA); 
  WiFi.hostname( myHostname );
  WiFi.begin( ssid4, password4 );
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
  EEPROM.begin( 4 + (2*MAX_NAME_LENGTH) + ( 10 * sizeof(int) ) );
  //setDefaults();
  setupFromEeprom();
  DEBUGSL1("Setup eeprom variables complete."); 
  
  // Connect to wifi 
  setup_wifi();                   
  
  //Debugging over telnet setup
  // Initialize the server (telnet or web socket) of RemoteDebug
  //Debug.begin(HOST_NAME, startingDebugLevel );
  Debug.begin( WiFi.hostname().c_str(), Debug.ERROR ); 
  Debug.setSerialEnabled(false);//until set false 
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
    
  debugI("Setting up RC controls");
  
  //Pins mode and direction setup for i2c on ESP8266-01
  //RC pulse pin
  pinMode( RCPIN, OUTPUT);
  digitalWrite( RCPIN, 0 );
  myServo.attach(RCPIN);  // attaches the servo on RCPIN to the servo object
  
  //Drives the RC pulse timing to keep the RC servo in position
  myServo.write( rcPosition );//rcPosition is setup from the Eeprom setup. 
  
  //This pin controls the state of the power to the RC servo. Low is ON
  pinMode( POWERPIN, OUTPUT );
  digitalWrite( POWERPIN, RCPOWERPIN_OFF );
  rcPowerOn = false;
  
  //Use to control EL brightness via PWM (0-1023) if present. 
  pinMode( ELPIN, OUTPUT);
  analogWrite( ELPIN, 0); 
   
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
  server.on("/setup/brightness" ,                   HTTP_ANY, handlerSetBrightness );//todo
  server.on("/setup/hostname" ,                     HTTP_ANY, handlerStatus );
  server.on("/setup/udpport",                       HTTP_ANY, handlerStatus );//todo
  server.on("/setup/limits",                        HTTP_ANY, handlerSetLimits );

//Management interface calls.
/* ALPACA Management and setup interfaces
 * The main browser setup URL would be http://192.168.1.89:7843/setup
 * The JSON list of supported interface versions would be available through a GET to http://192.168.1.89:7843/management/apiversions
 * The JSON list of configured ASCOM devices would be available through a GET to http://192.168.1.89:7843/management/v1/configureddevices
 */
  //Management API
  server.on("/management/description",              HTTP_GET, handleMgmtDescription );
  server.on("/management/apiversions",              HTTP_GET, handleMgmtVersions );
  server.on("/management/v1/configuredevices",     HTTP_GET, handleMgmtConfiguredDevices );
  
  //Basic settings
  server.on("/", handlerStatus );
  server.onNotFound(handlerNotFound); 

  updater.setup( &server );
  server.begin();
  debugI("Web server handlers setup & started");
  
  //Starts the discovery responder server
  Udp.begin( udpPort );
  
  //Setup timers
  //setup interrupt-based 'soft' alarm handler for periodic update of handler loop
  ets_timer_setfn( &timer, onTimer, NULL ); 
  
  //Timer for MQTT reconnect handler
  ets_timer_setfn( &timeoutTimer, onTimeoutTimer, NULL ); 
  
  //Timer for ELTimer calibrator lamp warmup
  ets_timer_setfn( &ELTimer, onELTimeoutTimer, NULL ); 

  //Timer for RC Servo flap opener/closer motion
  ets_timer_setfn( &rcTimer, onRCTimeoutTimer, NULL ); 
  
  //fire loop function handler timer every 1000 msec
  ets_timer_arm_new( &timer, 1000, 1 /*repeat*/, 1);

  //Supports the MQTT async reconnect timer. Armed elsewhere.
  //ets_timer_arm_new( &timeoutTimer, 2500, 0/*one-shot*/, 1);
  
  //Supports the calibrator lamp warm up sequence. Armed elsewhere
  //ets_timer_arm_new( &timeoutTimer, 10000, 0/*one-shot*/, 1);

  //set the startup position  
  myServo.write( rcPosition );
  //Leave the servo power off though 
  digitalWrite ( POWERPIN, RCPOWERPIN_OFF );

  //baseline driver variables
 
  debugI( "Setup complete" );
  Debug.setSerialEnabled(true); 
}

//Main processing loop
void loop()
{
  String timestamp;
  String output;
  static int pos = rcMinLimit;
  
  if( newDataFlag == true ) 
  {
    //do some work
#ifndef _TEST_RC_
    manageCoverState( targetCoverState );
    manageCalibratorState ( targetCalibratorState );
#else
    //Test servo
    pos = pos + 5;
    if ( pos >= rcMaxLimit )
    {
      pos = rcMinLimit;
      debugI( "reset pos to minLimit" ); 
    }
    debugI( "Writing position %i to pin %d", pos, RCPIN );
    myServo.write( pos );
    
    delay(5);  
#endif
    newDataFlag = false;
  }
    
  if( client.connected() )
  {
    if (callbackFlag == true )
    {
      //publish results
      publishHealth();
      publishFunction();
      callbackFlag = false;
    }
  }
  else
  {
    reconnectNB();
    client.subscribe (inTopic);
  }
  client.loop();
  
  //Handle web requests
  server.handleClient();

  //Handle Alpaca requests
  handleManagement();
  
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
  debugI( "current state is %s, targetState is %s", coverStatusCh[coverState], coverStatusCh[targetState] );
  switch( coverState )
  {          
      case CoverStatus::NotPresent: 
          debugW("Cover not present - why calling ?");
          break;
      case CoverStatus::Moving:
          //current status is moving, new target status is asking for a different action. 
          switch (targetState)
          {
            //If we change these target states while moving, the timer will respond automatically
            case CoverStatus::Closed:
            case CoverStatus::Open:
                  debugI("Still moving  - %s to %s", coverStatusCh[coverState], coverStatusCh[targetState] );
                  break;
            case CoverStatus::Halted:
                  //Let timer elapse and turn power off
                  debugI( "Change of state requested from Moving to Halted - letting timer elapse");
                  break;
            default:
                  debugW("Requested target state of %s while moving - does it make sense ?", coverStatusCh[targetState] ); 
                  break;
          }
          break;
      case CoverStatus::Halted:
          switch( targetState )
          {
            case CoverStatus::Halted:
                 debugI( "Requested targetState of 'Halted' when already Halted. Nothing to do.");
                 break;
            case CoverStatus::Closed:
                 debugI("targetState changed to Closed from Halted. Starting motion timer");
                 if ( rcPosition > rcMinLimit ) 
                 {
                    coverState = CoverStatus::Moving;
                    digitalWrite( POWERPIN, RCPOWERPIN_ON ); //turn power on
                    rcPowerOn = true;     
                    //turn on the timer to move the servo smoothly
                    ets_timer_arm_new( &rcTimer, 200, 0/*one-shot*/, 1 /*ms*/);      
                 }
                 break;
            case CoverStatus::Open:
                 debugI("targetState changed to Open from Halted. Starting motion timer");
                 if ( rcPosition < rcMaxLimit ) 
                 {
                    coverState = CoverStatus::Moving;
                    digitalWrite( POWERPIN, RCPOWERPIN_ON ); //turn power on
                    rcPowerOn = true;     
                    //turn on the timer to move the servo smoothly
                    ets_timer_arm_new( &rcTimer, 200, 0/*one-shot*/, 1 /*ms*/);      
                 }
                 break;
            case CoverStatus::Moving:
            default:
              debugW("unexpected targetState while Open");
              break;
          }
          break;
      case CoverStatus::Open: 
          switch( targetState )
          {
            case CoverStatus::Open:
                 debugI( "Requested targetState of 'Open' when already open. Nothing to do.");
                 break;
            case CoverStatus::Closed:
                 if ( rcPosition > rcMinLimit ) 
                 {
                    coverState = CoverStatus::Moving;
                    digitalWrite( POWERPIN, RCPOWERPIN_ON ); //turn power on
                    rcPowerOn = true;     
                    //turn on the timer to move the servo smoothly
                    ets_timer_arm_new( &rcTimer, 200, 0/*one-shot*/, 1 /*ms*/);      
                 }
                 else 
                 {
                    coverState = CoverStatus::Closed;
                    //ensure power is off
                    digitalWrite( POWERPIN, RCPOWERPIN_OFF ); //turn power off
                    rcPowerOn = false;     
                 }
                 break;
            case CoverStatus::Halted:
              //State will inform active timer - let it lapse.
              debugI("targetState changed to Halted from Moving. Waiting for motion timer");
              break;
            case CoverStatus::Moving:
            default:
              debugW("unexpected targetState while Open");
              break;
          }
          break;
      case CoverStatus::Closed:
          //current status is Closed, new target status is asking for a different action. 
          switch (targetState)
          {
            //If we change these target states while moving, the timer will respond automatically
            case CoverStatus::Closed:
                  //nothing to do 
                  debugI("targetState set to Closed while Closed - nothing to do");
                  break;
            case CoverStatus::Open:
                  debugI("targetState set to Open from Closed");
                   if ( rcPosition < rcMaxLimit ) 
                   {
                      debugD("Arming timer to open cover from Closed");
                      coverState = CoverStatus::Moving;
                      digitalWrite( POWERPIN, RCPOWERPIN_ON ); //turn power on
                      rcPowerOn = true;     
                      //turn on the timer to move the servo smoothly
                      ets_timer_arm_new( &rcTimer, 200, 0/*one-shot*/, 1 /*ms*/);      
                   }
                  break;          
            case CoverStatus::Halted:
                  debugI("targetState set to Halted from Closed");
                  if ( rcPosition <= rcMinLimit )
                  {
                    coverState = Closed;
                    debugI("targetState updated to Closed due to position while halted");
                  }                    
                  break;
            default:
                  debugW("Requested target state of %s while Closed - does it make sense ?", coverStatusCh[targetState] ); 
                  break;
          }
          break;
      case CoverStatus::Unknown:
      case CoverStatus::Error:
          switch ( targetCoverState )
          {
            case CoverStatus::Open:
            case CoverStatus::Closed:
              debugI("Requested targetState of %s from %s - setting Halted first", coverStatusCh[coverState], coverStatusCh[targetCoverState] );
              //Let the coverState be updated to halted and then next iteration will move.  
              break;
            case CoverStatus::Halted:
              digitalWrite( POWERPIN, RCPOWERPIN_OFF ); //turn power off
              rcPowerOn = false;                   
              coverState = CoverStatus::Halted;
              break;
            case CoverStatus::NotPresent:            
            case CoverStatus::Unknown:
            case CoverStatus::Error:
            default:
              debugW( "Changing state from %s to %s - nothing to do", coverStatusCh[coverState], coverStatusCh[targetCoverState] );
              break;
          }          
      default:
        break;
  }
  
  debugI( "Exiting - final state %s, targetState %s ", coverStatusCh[coverState], coverStatusCh[targetCoverState] );
  return 0;
}

 int manageCalibratorState( CalibratorStatus calibratorTargetState )
 {
     
    debugI( "Entered - current state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
    switch ( calibratorState )
    {
      case CalibratorStatus::CalNotPresent:
        switch (calibratorTargetState)
        {
          case CalibratorStatus::CalNotPresent:
          case CalibratorStatus::NotReady:
          case Off:
          case Ready:      
          case CalibratorStatus::CalUnknown:
          case CalibratorStatus::CalError:
          default:
            debugW("Asked to do something when calibrator not present");
            break;          
        }
        debugV( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
        break; 
      case CalibratorStatus::NotReady:
        switch (calibratorTargetState)
        {
          case CalibratorStatus::CalNotPresent:
              debugW("Invalid target state requested - from NotReady to %s", calibratorStatusCh[calibratorTargetState] );
              break;
          case CalibratorStatus::NotReady:
              //This is not a valid target state
              debugV("Ignoring intermediate target state requested - from NotReady to %s", calibratorStatusCh[calibratorTargetState] );
              break;
          case Ready:
              //Get here while we are waiting for the current state to become READY - brightness may be changedin the meantime.
              if( brightnessChanged )
              {
                //we have a new brightness. 
                //turn on to desired brightness - this is different from setting to off/0. 
                analogWrite( ELPIN, brightness );
                brightnessChanged = false;
                debugI( "Brightness changed (%d) while warming up lamp - set new brightness", brightness);
              }
              else
                debugV("Waiting for timer to move to Ready");
               break;
          case Off:
              //turn off              
              //adjust brightness to be zero or just use brightness to record current requested setting when on ?
              analogWrite( ELPIN, 0 ); 
              calibratorState = CalibratorStatus::Off;
              break; 
          case CalibratorStatus::CalError:
          case CalibratorStatus::CalUnknown:
          default:
            debugW("targetstate invalid : %s", calibratorStatusCh[calibratorTargetState] );
            break;          
        }    
        debugV( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
        break;
      case CalibratorStatus::Off:
        switch (calibratorTargetState)
        {
          case CalibratorStatus::CalNotPresent:
             debugW("targetstates invalid - attempt to jump to NotPresent from Off : %s", calibratorStatusCh[calibratorTargetState] );             
             break;
          case CalibratorStatus::NotReady:
             //waiting for timer to move to ready
              break;
          case CalibratorStatus::Ready:      
             //turn on to desired brightness
             analogWrite( ELPIN, brightness );
             //Turn on timer for illuminator to stabilise
             ets_timer_arm_new( &ELTimer, 5000, 0/*one-shot*/, 1);
             //update state to waiting
             calibratorState = NotReady;
             brightnessChanged = false;
             debugD("Lamp requested to turn on at brightness %d", brightness );
             break;
          case CalibratorStatus::Off://Already off 
             calibratorState = Off;
             break;              
          case CalibratorStatus::CalUnknown:
          case CalibratorStatus::CalError:
          default:
            debugW("targetstates invalid : %s", calibratorStatusCh[calibratorTargetState]);
            break;          
        }
        debugV( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
        break;

      case CalibratorStatus::Ready:      
        switch ( calibratorTargetState )
        {
          case CalibratorStatus::CalNotPresent:
              debugW("targetState invalid - trying to set NotPresent from Ready");
              break;
          case CalibratorStatus::NotReady:
              //Waiting for timer completion.
              break;
          case CalibratorStatus::Ready:  
              //Already on - leave alone.
              if( brightnessChanged )
              {
                debugD( "Arming (0) timer to turn on lamp" ); 
                analogWrite( ELPIN, brightness );
                //Turn on timer for illuminator to stabilise
                ets_timer_arm_new( &ELTimer, 5000, 0/*one-shot*/, 1);
                calibratorState = NotReady;
                brightnessChanged = false;
                debugI( "Lamp changed while already on - setting new brightness");
              }
              else
                debugD( "Lamp on  - leaving alone");
              break;
          case CalibratorStatus::Off:
          case CalibratorStatus::CalError:
              //Turn off 
              analogWrite( ELPIN, 0 );
              calibratorState = Off;
              break;
          case CalibratorStatus::CalUnknown:
          default:
              debugD("targetState invalid - trying to move from Unknown to %s", calibratorStatusCh[calibratorTargetState]  );
            break;          
        }
        debugV( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] ); 
        break;
      case CalibratorStatus::CalUnknown:
        switch( calibratorTargetState )  
        {
          case CalibratorStatus::Off:
            //Turn off 
            analogWrite( ELPIN, 0 );
            calibratorState = Off;
            break;
          case CalibratorStatus::Ready:
            debugD( "Arming (1) timer to turn on lamp" ); 
            analogWrite( ELPIN, brightness );
            //Turn on timer for illuminator to stabilise
            ets_timer_arm_new( &ELTimer, 5000, 0/*one-shot*/, 1);
            calibratorState = NotReady;
            brightnessChanged = false;
            debugD("Lamp requested to turn on at brightness %d", brightness );
            break;
          default:
            debugW( "Attempting to update calibratorState from Unknown to %s - valid ?", calibratorStatusCh[calibratorTargetState] ); 
          break;      
        }
        debugV( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
        break;
      case CalibratorStatus::CalError:
        //Assume we can recover from an error by moving to a valid state. 
        switch (calibratorTargetState)
        {      
          case CalibratorStatus::CalNotPresent:
          case CalibratorStatus::NotReady:             
             debugD("Target states invalid - attempt to jump to NotPresent from Error : %s", calibratorStatusCh[calibratorTargetState] );             
             break;
          case CalibratorStatus::Ready:      
            //turn on to desired brightness
            debugD("Arming (2) timer to turn on lamp");
            analogWrite( ELPIN, brightness );
            //Turn on timer for illuminator to stabilise
            ets_timer_arm_new( &ELTimer, 5000, 0/*one-shot*/, 1);
            calibratorState = NotReady;
            brightnessChanged = false;
            debugD("Lamp requested to turn on at brightness %d", brightness );           
            break;
          case CalibratorStatus::Off:
             analogWrite( ELPIN, 0 );
             calibratorState = Off;
             break;              
          case CalibratorStatus::CalError:
          default:
            debugW("targetstates invalid : %d", calibratorTargetState );
            break;          
        }
        debugV( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );        
        break;      

      default:
        debugE( "Unknown calibrator state on entry! %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
        break;
    }
  debugI( "Exited - final state %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );        
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
 
 //Publish state to the function subscription point. 
 void publishFunction(void )
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
  root["coverState"] = coverState;
  root["calibratorState"] = calibratorState;
  
  root.printTo( output);
  
  //Put a notice out regarding device health
  outTopic = outFnTopic;
  outTopic.concat( myHostname );
  outTopic.concat( "/" );
  outTopic.concat( DriverType );
  
  if ( client.publish( outTopic.c_str(), output.c_str(), true ) )
    debugI( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
  else
    debugW( "Failed to publish : %s to %s", output.c_str(), outTopic.c_str() );
 }
 
