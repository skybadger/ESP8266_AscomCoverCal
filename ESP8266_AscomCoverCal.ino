/*
 Program to implement ASCOM ALPACA Cover Calibrator interface for remote telescope flip-flat devices. 
 Typically implemented using REST calls over wireless to talk to the device. 
 In the single servo version, the device controls a RC servo via PNP power switching through external hardware and a heater through a similar circuit
 Power control in this case is through a p-channel mosfet where the source is wired to 12v and drain is the output. Taking gate to low will turn on the drain output. 
 This means we can move the RC serv to the desired position and then turn it off to hold it in that position. 

 In the multi-servo version, used for a telescope with multiple flaps that need to open to uncover the lens, this circuit requires a PCA9685 servo controller attached via I2C. 
 Un-comment the USE_SERVO_PCA9685 define to enable the multi-servo case. 
 Both versions use the heater 
 
 Supports web interface on port 80 returning json string
 
Notes: 
 
 To do:
 Complete Setup page chunking - in progress - ready for test
 Test multiple leaf servo control
  
 Done: 
 Complete EEPROM calls - done.
 
 Pin Layout:
 ESP8266_01e
 GPIO 3 (Rx) Used to control power to servo using PNP poweer mosfet.
 GPIO 2 (SDA) to PWM RC signal 
 GPIO 0 (SCL) Used to control illuminator using PWM via PNP power mosfet. 
 GPIO 1 used as Serial Tx. 
 
 ESP8266_12
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 All 3.3v logic. 
 
Test:
curl -X PUT http://espACC01/api/v1/covercalibrator/0/Connected -d "ClientID=0&ClientTransactionID=0&Connected=true" (note cases)
http://espACC01/api/v1/switch/0/status
telnet espACC01 32272 (UDP)

Dependencies
PCA 9685 16-channel i2c servo control board : https://github.com/NachtRaveVL/PCA9685-Arduino - see Arduino library manager in IDE 
JSON parsing for arduino : https://arduinojson.org/v5/api/
Remote debug over telnet : https://github.com/JoaoLopesF/RemoteDebug
MQTT pubsub library : https://pubsubclient.knolleary.net/api.html

Change Log
12/06/2021 Completed ALPACA Management API - tested using Conform for dynamic registration
12/06/2021 Updated setup form handlers to separate Device and driver variables. 
12/09/2021 Updated PCA9865 code from PCA9865 driver examples  - example code works fine on hardware. Now to expand.
           Updated code to move as much as possible into PSTR memory using F() function to free up the dynamic heap. 
21/12/2021 Updated code to support PCA9685. Tested working in software - validated using conform. 
           Updated AlpacaMgmt code to successfully dynamically find device. Requires DeviceType to be accurarately set to a known type. 
           Updated coverHandler to be able to set cover position and calibrator state from the web page. 
           Currently failing to drive servo board though. 
           Currently serial output also fails after i2c scan. 
04/01/22   Fixed serial reporting - due to bad defines in SERIAL_DEBUG. include order changed to handle. 
           Fixed ALPACA device discovery routine
04/01/2022 Updated HTML to set limits, positions and brightness
           Updated HTML to add reset and update cmmmands to device setup
           Updated GUID to recognise this device  
           Added ability to set which method of opening flaps is used - enum created. Set at compile time unless a REST call is added.     
 
30/01/2022 Added PNP output transistor to board for 6-flap cover using Rx as control. POtential conflict with 
           other boards already using Tx. Need to check. 
           Updated setBrightness function from html pages to properly check for brightness control 
           Probably neeed to spluit turn on/off from setBrightness handler & presentation
           replaced &amp; with &nbsp; as intended. 
            
 
 */

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

//Create a remote debug object
#if !defined DEBUG_DISABLED
RemoteDebug Debug;
#endif 

#if defined USE_SERVO_PCA9685
// Uncomment or -D this define to enable debug output.
#define PCA9685_ENABLE_DEBUG_OUTPUT
//These settings need to be edited in the driver header file not here. They are here so you can see if we have done anything with them.
// Uncomment or -D this define to enable use of the software i2c library (min 4MHz+ processor).
//#define PCA9685_ENABLE_SOFTWARE_I2C             // http://playground.arduino.cc/Main/SoftwareI2CLibrary
// Uncomment or -D this define to swap PWM low(begin)/high(end) phase values in register reads/writes (needed for some chip manufacturers).
//#define PCA9685_SWAP_PWM_BEG_END_REGS

#include "PCA9685.h"
//Multi-servo code for handling multi - flaps generalised to the 1-16 flap case 
//Library using default B000000 (A5-A0) i2c address (I2C 7-bit address is B 1 A5 A4 A3 A2 A1 A0, ie 0x40 ) , and default Wire @400kHz
PCA9685 pwmController; //( B1000000, Wire, 100000 );//Base address, i2c interface and i2c speed
// Linearly interpolates between standard 2.5%/12.5% phase length (102/512) for -90°/+90°
//Which means all servo values need to be adjusted from 0-180 range to -90/+90 range by subtracting 90
PCA9685_ServoEval pwmServo;

#else
#include <Servo.h> //Arduino native servo library
Servo myServo;  // create servo object to control a servo
// twelve servo objects can be created on most boards
#endif 

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
char* myHostname = nullptr;

//MQTT settings
char* thisID = nullptr;

//Private variables for the driver
bool elPresent = false;
bool coverPresent = false; 

WiFiClient espClient;
PubSubClient client(espClient);
volatile bool callbackFlag = false;

// Create an instance of the server
// specify the port to listen on as an argument
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

//Setup the management discovery listener
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

#include "CoverCal_eeprom.h"
#include "ASCOMAPICommon_rest.h" //ASCOM common driver web handlers. 
#include "ESP8266_coverhandler.h"
#include <AlpacaManagement.h>

//State Management functions called in loop.
int manageCalibratorState( CalibratorStatus calibratorTargetState );
int manageCoverState( CoverStatus coverTargetState );

//wrapper function used to control output power transistor for servo/PCA9865
void setRCPower( bool powerState );
void setSoloRCPosition(int value );

//wrapper function used to control output power transistor for calibrator panel illumination
void setELPower( int pwmSetting  );

//Unit servo test function 
void testServo();

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

#if defined USE_SERVO_PCA9685
void onRCTimeoutTimer( void * pArg )
{
  //flap tracking variables
  static int flapId = 0;
  bool restartTimer = false;
  
  //keep flapId in range - check -ve wrap behaviour by pre-adding flapCount
  flapId = ( flapId + flapCount ) % flapCount;

  //Not sure we are allowed to use the Serial port here in this soft interrupt handler.
  //but it seems to work OK
  debugV( "Updating cover servo - flap %d, position %d, flap Min: %d, max: %d", flapId, flapPosition[ flapId ], flapMinLimit[flapId], flapMaxLimit[flapId] );

  if( targetCoverState == CoverStatus::Open )
  {
    //We can do this in two ways - open each flap fully then the next or open all flaps a small step
    //if they overlap, we need to do it sequentially. 
    //Either way, we need to open each flap a small amount each time. 
    //experimentally - opening each a small amount in sequence causines them to jam if they overlap.   
    
    //Have we finished - could be starting from halted state rather than fully open or closed 
    //Is the last flap in each (0 to flapCount-1) circuit fully open ?
    if( flapPosition[ flapCount -1] >= flapMaxLimit[flapCount -1] )
    {
      debugV( "Halting servo motion - flap %d, position %d", flapId, flapPosition[ flapId ] );
      coverState = CoverStatus::Open;
      setRCPower( RCPOWERPIN_OFF ); //turn power off
      flapId = flapCount - 1;
      restartTimer = false;
    }
    else
    {
      if ( rcPowerOn != true )
      {
        setRCPower( RCPOWERPIN_ON ); //turn power on
      }

      flapPosition[flapId] += MULTIFLAPSINCREMENTCOUNT;//((flapMaxLimit[flapId] - flapMinLimit[flapId] ) / MULTIFLAPSINCREMENTCOUNT);
      if( flapPosition[flapId] >= flapMaxLimit[flapId] ) 
      {  
        flapPosition[flapId] = flapMaxLimit[flapId];
        if ( movementMethod  == FlapMovementType::EachFlapInSequence )
          flapId++;//we do each flap fully, then the next by incremting the flap id on completion. 
      }            
      
      if( movementMethod == AllFlapsAtOnce )
      {
        for ( int i = 0 ; i< flapCount; i++ ) 
          pwmController.setChannelPWM( i, pwmServo.pwmForAngle( flapPosition[ 0 ] - 90 ) );
        flapId = 0; //reset here since we do all flaps together, using the position of the first. 
      }
      else 
        pwmController.setChannelPWM( flapId, pwmServo.pwmForAngle( flapPosition[ flapId ] - 90 ) );

      if( movementMethod == EachFlapIncremental )
      {
        flapId++;
      }
      restartTimer = true;
    }
  }
  else if ( targetCoverState == CoverStatus::Closed )
  {
    //Have we closed yet ?
    if ( flapPosition[ 0 ] <= flapMinLimit[ 0 ] )
    {
      debugV( "Halting servo motion - flap %d, position %d", flapId, flapPosition[ flapId ] );
      coverState = CoverStatus::Closed;
      setRCPower( RCPOWERPIN_OFF ); //turn power off   
      flapId = 0;
      restartTimer = false;
    }
    else    //we're still closing
    {
      flapPosition[flapId] -= MULTIFLAPSINCREMENTCOUNT; //((flapMaxLimit[flapId] - flapMinLimit[flapId] )/ MULTIFLAPSINCREMENTCOUNT );

      if( flapPosition[flapId] <= flapMinLimit[flapId] ) 
      {
        flapPosition[flapId] = flapMinLimit[flapId];
        if ( movementMethod  == FlapMovementType::EachFlapInSequence )
            flapId--;//we do each flap fully, then the next by decrementing the flap id on completion. 
      }

      if( movementMethod == AllFlapsAtOnce )
      {
        for ( int i = flapCount ; i>= 0; i-- ) 
          pwmController.setChannelPWM( i, pwmServo.pwmForAngle( flapPosition[ 0 ] - 90 ) );
        flapId = 0; //reset here since we do all flaps together, using the position of the first. 
      }
      else 
        pwmController.setChannelPWM( flapId, pwmServo.pwmForAngle( flapPosition[ flapId ] - 90 ) );

      if( movementMethod == EachFlapIncremental )
      {
         flapId --;        
      }
      restartTimer = true;
    }
  }
  else if ( targetCoverState == CoverStatus::Halted )
  {
    restartTimer = false;
    setRCPower( RCPOWERPIN_OFF ); //turn power off
    coverState = CoverStatus::Unknown;
    targetCoverState = CoverStatus::Unknown;
  }
    
  if ( restartTimer ) 
    ets_timer_arm_new( &rcTimer, MULTIFLAPTIME, 0/*one-shot*/, 1);
}

#else
//With a single flap/single pin model, use single servo code for CCal. 
//Timeout handler for myServo instance
//Timer handler for 'soft' interrupt handler
void onRCTimeoutTimer( void * pArg )
{
  bool restartTimer = false;
  static int flapId = 0;
  
  flapId = (flapId + flapCount) % flapCount;
  
  debugV( "Updating cover servo %d - position %d", flapId, flapPosition[flapCount -1 ] );
  if( targetCoverState == CoverStatus::Open )
  {
    if( flapPosition[ flapCount -1] >= flapMaxLimit[flapCount -1] )
    {
      coverState = CoverStatus::Open;
      setRCPower( RCPOWERPIN_OFF ); //turn power off
      flapId = flapCount - 1;
      restartTimer = false;
    }
    else 
    {
      if ( rcPowerOn != true )
      {
        setRCPower( RCPOWERPIN_ON ); //turn power on
      }
      flapPosition[ flapId ] += RCPOSITIONINCREMENT;
      setSoloRCPosition( flapPosition[ flapId ] );
      flapId++;
      restartTimer = true;
    }
  }
  else if ( targetCoverState == CoverStatus::Closed )
  {
    if ( flapPosition[0] <= flapMinLimit[ 0] )
    {
      setRCPower( RCPOWERPIN_OFF ); //turn power off
      coverState = CoverStatus::Closed;
      restartTimer = false;
      flapId = 0;
    }
    else
    {
      if ( rcPowerOn != true )
      {
        setRCPower( RCPOWERPIN_ON ); //turn power on
      }
      flapPosition[ flapId ] -= RCPOSITIONINCREMENT;
      setSoloRCPosition( flapPosition[ flapId ] );
      flapId --;
      restartTimer = true;
    }
  }
  else if ( targetCoverState == CoverStatus::Halted )
  {
    setRCPower( RCPOWERPIN_OFF ); //turn power off
    coverState = CoverStatus::Unknown;
    targetCoverState = CoverStatus::Unknown;
    restartTimer = false;
  }
    
  if ( restartTimer ) 
    ets_timer_arm_new( &rcTimer, RCFLAPTIME, 0/*one-shot*/, 1);
}
#endif 

void onELTimeoutTimer(void * pArg)
{
  ELTimeoutFlag = false;
  
  //Update our state from NOTREADY to READY
  if ( brightness == 0 ) 
  {
    calibratorState       = CalibratorStatus::Off;
  }
  else 
    calibratorState       = CalibratorStatus::Ready;
  
  targetCalibratorState = CalibratorStatus::Ready;
}

void setSoloRCPosition( int input )
{
  myServo.write( input );
  //analogWrite( RCPIN, map( input, 0 , 256, 0, 1024 )  );
}

void setup()
{
  String i2cMsg = "";

#if !defined USE_SERVO_PCA9685 //uses all the pins including the serial pin. 
  Serial.begin( 115200, SERIAL_8N1, SERIAL_TX_ONLY);
#else
  Serial.end();//https://www.arduino.cc/reference/en/language/functions/communication/serial/end/
#endif 
  DEBUG_ESP_MH("ESP starting.");
  
  delay(5000);
  int i=0;
  int j = 0 ;
  
  //Start NTP client
  configTime(TZ_SEC, DST_SEC, timeServer1, timeServer2, timeServer3 );
  delay( 5000);
  
  //Setup default data structures
  DEBUGSL1("Setup EEprom variables"); 
  EEPROM.begin( EEPROMSIZE );
  setupFromEeprom();
  DEBUGSL1("Setup eeprom variables complete."); 
  
  // Connect to wifi 
  scanNet();
  delay(2000);
  setup_wifi();                   
   
#if !defined DEBUG_DISABLED
  //Debugging over telnet setup
  // Initialize the server (telnet or web socket) of RemoteDebug
  Debug.begin( WiFi.hostname().c_str(), Debug.VERBOSE ); 
  Debug.setSerialEnabled(true);//until set false 
  // Options
  // Debug.setResetCmdEnabled(true); // Enable the reset command
  // Debug.showProfiler(true); // To show profiler - time between messages of Debug
  //In practice still need to use serial commands until debugger is up and running.. 
  debugE("Remote debugger enabled and operating");
#endif //remote debug

  //for use in debugging reset - may need to move 
  DEBUG_ESP_MH( "Device reset reason: %s\n", device.getResetReason().c_str() );
  DEBUG_ESP_MH( "device reset info: %s\n",   device.getResetInfo().c_str() );
    
  //Open a connection to MQTT
  DEBUG_ESP_MH( ("Setting up MQTT.")); 
  client.setServer( mqtt_server, 1883 );
  client.connect( thisID, pubsubUserID, pubsubUserPwd ); 
  String lastWillTopic = outHealthTopic; 
  lastWillTopic.concat( myHostname );
  client.connect( thisID, pubsubUserID, pubsubUserPwd, lastWillTopic.c_str(), 1, true, "Disconnected", false ); 
  //Create a heartbeat-based callback that causes this device to read the local i2C bus devices for data to publish.
  client.setCallback( callback );
  client.subscribe( inTopic );
  publishHealth();
  DEBUG_ESP_MH( ("MQTT Setup complete."));   

  debugI("Setting up RC controls\n");
   
  //Use ELPIN to control EL brightness via PWM (0-1023) if present. 
  //Assume the calibrator is present until we have a more sophisticated way of handlling this.
  //common to both i2c and single servo operations. 
  elPresent = true; 

  debugI("Setting up EL panel controls ");
  analogWriteFreq( 1000 );
  setELPower( 0 ); 
  DEBUG_ESP_MH("Setting up EL panel controls complete\n");
  
  //Pins mode and direction setup for i2c on ESP8266_01
#if defined USE_SERVO_PCA9685 // i2c driven multiple servo case
  debugI("Setting up multi-servo I2C controls");
  Wire.begin( 2, 0 );//was 0, 2 for normal arrangement, trying 2, 0 for ASW02 : ESP8266_01
  //Wire.begin( 5, 4 ); //ESP8266_012 testing GPIO 5 (SCL) is D1, GPIO 4 (SDA) is D2 on ESP8266_12 breakout normally. 
  debugI("Setting up Wire pins complete\n");
  
  Wire.setClock(400000 );//100KHz target rate
  DEBUG_ESP("Setting up Wire bus speed complete\n");
  
  DEBUG_ESP("Scanning I2C bus for devices\n");
  scanI2CBus();
  DEBUG_ESP("Setting up Wire complete\n");
     
  //Assume present until we can do some sensible detection of device - parse i2cbus response for found address for example. 
  debugI("Setting up PWM controller \n");
  pwmController.resetDevices();       // Resets all PCA9685 devices on i2c line
  pwmController.init(PCA9685_OutputDriverMode::PCA9685_OutputDriverMode_TotemPole,
                     PCA9685_OutputEnabledMode::PCA9685_OutputEnabledMode_Normal,
                     PCA9685_OutputDisabledMode::PCA9685_OutputDisabledMode_Low,
                     PCA9685_ChannelUpdateMode::PCA9685_ChannelUpdateMode_AfterStop,
                     PCA9685_PhaseBalancer::PCA9685_PhaseBalancer_None);

  //pwmController.init();             // Initializes module using default totem-pole driver mode, and default disabled phase balancer
  pwmController.setPWMFreqServo();    // Set PWM freq to 50Hz 
  
  //drive all flaps to stored positions for 'closed'
  for( i=0; i < flapCount; i++ )
  {
      DEBUG_ESP_MH("PWM [%d] Initial value read as %0d \n", i, pwmController.getChannelPWM(i) ); 
      DEBUG_ESP_MH("PWM [%d] Initial angle to set is %0d \n", i, flapMinLimit[ i ] ); 
      pwmController.setChannelPWM( i, pwmServo.pwmForAngle( flapMinLimit[ i ] - 90 ));
      DEBUG_ESP_MH("PWM [%d] value set and read back as %0d \n", i, pwmController.getChannelPWM(i) ); 
      delay(20); 
  }

  //Test to see whether we can use the OE pin or need a power transistor to do the job... 
  //Use RCRCPOWERPIN to control OE pin on PCA9685
  setRCPower( RCPOWERPIN_OFF );
  DEBUG_ESP_MH("Set up i2c Servo controller complete\n");
   
#else //single servo only supported using a dedicated ESP8266 pin
  DEBUG_ESP_MH("Setting up single pin servo controls\n");
  //This pin controls the state of the power supply to the RC servo.
  pinMode( RCPOWERPIN, OUTPUT );
  setRCPower( RCPOWERPIN_ON );

  //RC pulse pin
  myServo.attach( RCPIN );  // attaches the servo on RCPIN to the servo object 
  //Drives the RC pulse timing to keep the RC servo in position
  //Position is read from the Eeprom setup. 
  setSoloRCPosition( flapPosition[0] ); 
  delay(1000);
  setRCPower( RCPOWERPIN_OFF );

  DEBUG_ESP_MH("Setting up single pin servo controls complete\n");

#endif  
  
#if defined TEST_SERVO   
  DEBUG_ESP("testing servo(s) \n");
  testServo();
  debugI("Test servo complete\n");
#endif 

  //Setup webserver handler functions 
  //Common ASCOM handlers
  server.on(F("/api/v1/covercalibrator/0/action"),              HTTP_PUT, handleAction );
  server.on(F("/api/v1/covercalibrator/0/commandblind"),        HTTP_PUT, handleCommandBlind );
  server.on(F("/api/v1/covercalibrator/0/commandbool"),         HTTP_PUT, handleCommandBool );
  server.on(F("/api/v1/covercalibrator/0/commandstring"),       HTTP_PUT, handleCommandString );
  server.on(F("/api/v1/covercalibrator/0/connected"),           handleConnected );
  server.on(F("/api/v1/covercalibrator/0/description"),         HTTP_GET, handleDescriptionGet );
  server.on(F("/api/v1/covercalibrator/0/driverinfo"),          HTTP_GET, handleDriverInfoGet );
  server.on(F("/api/v1/covercalibrator/0/driverversion"),       HTTP_GET, handleDriverVersionGet );
  server.on(F("/api/v1/covercalibrator/0/interfaceversion"),    HTTP_GET, handleInterfaceVersionGet );
  server.on(F("/api/v1/covercalibrator/0/name"),                HTTP_GET, handleNameGet );
  server.on(F("/api/v1/covercalibrator/0/supportedactions"),    HTTP_GET, handleSupportedActionsGet );

//CoverCalibrator-specific functions
//Properties - all GET
  server.on(F("/api/v1/covercalibrator/0/brightness"),          HTTP_GET, handlerBrightnessGet );  
  server.on(F("/api/v1/covercalibrator/0/maxbrightness"),       HTTP_GET, handlerMaxBrightnessGet );
  server.on(F("/api/v1/covercalibrator/0/coverstate"),          HTTP_GET, handlerCoverStateGet );
  server.on(F("/api/v1/covercalibrator/0/calibratorstate"),     HTTP_GET, handlerCalibratorStateGet );

//Methods
  server.on(F("/api/v1/covercalibrator/0/calibratoron"),        HTTP_PUT, handlerCalibratorOnPut );
  server.on(F("/api/v1/covercalibrator/0/calibratoroff"),       HTTP_PUT, handlerCalibratorOffPut );
  server.on(F("/api/v1/covercalibrator/0/closecover"),          HTTP_PUT, handlerCloseCoverPut );  
  server.on(F("/api/v1/covercalibrator/0/haltcover"),           HTTP_PUT, handlerHaltCoverPut );
  server.on(F("/api/v1/covercalibrator/0/opencover"),           HTTP_PUT, handlerOpenCoverPut );

//Additional ASCOM ALPACA Management setup calls
  //Per device
  //TODO - split whole device setup from per-instance driver setup e.g. hostname, alpaca port to device compared to ccal setup on the driver, 
  server.on(F("/setup"),                               HTTP_GET, handlerDeviceSetup );
  server.on(F("/setup/hostname") ,                     HTTP_ANY, handlerDeviceHostname );
  server.on(F("/setup/udpport"),                       HTTP_ANY, handlerDeviceUdpPort );
  server.on(F("/setup/location"),                      HTTP_ANY, handlerDeviceLocation );
  
  //Per driver - there may be several on this device. 
  server.on(F("/setup/v1/covercalibrator/0/setup"),            HTTP_GET, handlerDriver0Setup );
  server.on(F("/setup/v1/covercalibrator/0/setup/brightness"), HTTP_ANY, handlerDriver0Brightness );
  server.on(F("/setup/v1/covercalibrator/0/setup/positions"),  HTTP_ANY, handlerDriver0Positions );
  server.on(F("/setup/v1/covercalibrator/0/setup/position"),   HTTP_ANY, handlerDriver0Positions );
  server.on(F("/setup/v1/covercalibrator/0/setup/flapcount"),  HTTP_ANY, handlerDriver0FlapCount );
  server.on(F("/setup/v1/covercalibrator/0/setup/limits"),     HTTP_ANY, handlerDriver0Limits );
  
//Custom
  server.on(F("/status"),                              HTTP_GET, handlerStatus);
  server.on(F("/restart"),                             HTTP_ANY, handlerRestart); 

//Management interface calls.
/* ALPACA Management and setup interfaces
 * The main browser setup URL would be http://<hostname>/api/v1/setup 192.168.1.89:7843/setup
 * The JSON list of supported interface versions would be available through a GET to http://192.168.1.89:7843/management/apiversions
 * The JSON list of configured ASCOM devices would be available through a GET to http://192.168.1.89:7843/management/v1/configureddevices
 */
  //Management API - https://www.ascom-standards.org/api/?urls.primaryName=ASCOM%20Alpaca%20Management%20API#/Management%20Interface%20(JSON)
  server.on(F("/management/apiversions"),             HTTP_GET, handleMgmtVersions );
  server.on(F("/management/v1/description"),          HTTP_GET, handleMgmtDescription );
  server.on(F("/management/v1/configureddevices"),     HTTP_GET, handleMgmtConfiguredDevices );
  
  //Basic settings
  server.on(F("/"), handlerStatus );
  server.onNotFound(handlerNotFound); 

  updater.setup( &server );
  server.begin();
  debugI("Web server handlers setup & started\n");
  
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
  
  //fire loop function handler timer every xx msec
  ets_timer_arm_new( &timer, 250, 1 /*repeat*/, 1);

  //Supports the MQTT async reconnect timer. Armed elsewhere.
  //ets_timer_arm_new( &timeoutTimer, 2500, 0/*one-shot*/, 1);
  
  //Supports the calibrator lamp warm up sequence. Armed elsewhere
  //ets_timer_arm_new( &ELTimer, 10000, 0/*one-shot*/, 1);

  //baseline driver variables
 
  debugI( "Setup complete\n" );

#if !defined DEBUG_ESP_MH && !defined DEBUG_DISABLE
//turn off serial debug if we are not actively debugging.
//use telnet access for remote debugging
   Debug.setSerialEnabled(false); 
#endif
}

//Main processing loop
void loop()
{
  String timestamp;
  String output;
  
  if( newDataFlag == true ) 
  {
    if( coverState == CoverStatus::Moving )
      debugV( "cover position : flap 0 at %d", flapPosition[0]); 
    
    //do some work
#if defined TEST_SERVO
    //Test servo
    testServo();
#else
    if ( coverPresent ) 
      manageCoverState( targetCoverState );
    
    if ( elPresent ) 
      manageCalibratorState ( targetCalibratorState );
#endif

    //Handle web requests
    server.handleClient();

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

  //Handle Alpaca requests
  handleManagement();
  
#if !defined DEBUG_DISABLED
  // Remote debug over WiFi
  Debug.handle();
  // Or
  //debugHandle(); // Equal to SerialDebug  
#endif
}

 //Function to manage cover states
 int manageCoverState( CoverStatus targetState )
 {
  String msg = "";
  ///enum CoverStatus = {NotPresent, Closed, Moving, Open, Unknown, Error, Halted };
  //need a targetState.
  if ( coverState == targetState ) 
    return 0;
   
  switch( coverState )
  {          
      //current status is Closed, new target status is asking for a different action. 
      case CoverStatus::Closed:
          switch (targetState)
          {
            //If we change these target states while moving, the timer will respond automatically
            case CoverStatus::Open:
                  debugD("targetState set to Open from Closed, actual position flap[0] is %d ", flapPosition[0] );
                  debugV("Arming timer to move cover(s)");
                  coverState = CoverStatus::Moving;
                  //turn on the timer to move the servo smoothly
                  ets_timer_arm_new( &rcTimer, RCFLAPTIME, 0/*one-shot*/, SERVO_START_TIME /*ms*/);      
                  break;          
            case CoverStatus::Halted:
                  debugD("targetState set to Halted from Closed, actual position flap[0] is %d ", flapPosition[0] );
                  if ( flapPosition[flapCount -1] <= flapMinLimit[ flapCount -1] )
                  {
                    coverState = CoverStatus::Closed;
                    debugI("targetState updated to Closed due to position while halted");
                  }
                  else 
                    coverState = CoverStatus::Unknown;                    
                  break;
            case CoverStatus::Closed:
                  debugD("targetState set to Closed from Closed, actual position flap[0] is %d ", flapPosition[0] );
                  break;//Quietly ignore. 
            case CoverStatus::Moving:
            case CoverStatus::Unknown:
            case CoverStatus::Error:
            default:
                  debugW("Requested target state of %s while Closed - does it make sense ?", coverStatusCh[targetState] ); 
                  break;
          }
          break;      

      case CoverStatus::Moving:
          //current status is moving, new target status is asking for a different action. 
          switch (targetState)
          {
            //If we change these target states while moving, the timer handler managing movement will respond automatically
            case CoverStatus::Closed:
            case CoverStatus::Open:
                  debugD("Still moving  - %s to %s", coverStatusCh[coverState], coverStatusCh[targetState] );
                  break;
            case CoverStatus::Halted:
                  //Let timer elapse and turn power off
                  debugD( "Change of state requested from Moving to Halted - letting timer elapse");
                  break;
            case CoverStatus::Unknown:
            case CoverStatus::Error:
                  if( flapPosition[flapCount -1 ] > flapMinLimit[ flapCount -1] )
                    coverState = CoverStatus::Open;
                  break;
            case CoverStatus::NotPresent: 
            default:
                  debugW("Requested target state of %s while moving - does it make sense ?", coverStatusCh[targetState] ); 
                  break;
          }
          break;
      //Used to be Halted but Halted is not an ASCOM value - unknown should be returned as the state when the flap is halted during movement or before initialised
      case CoverStatus::Unknown: 
          switch( targetState )
          {
            case CoverStatus::Closed:
                 debugD("targetState changed to Closed from Unknown. Starting motion timer");
                 coverState = CoverStatus::Moving;
                 //turn on the timer to move the servo smoothly
                 ets_timer_arm_new( &rcTimer, RCFLAPTIME, 0/*one-shot*/, SERVO_START_TIME /*ms*/);      
                 break;
            case CoverStatus::Open:
                 debugD("targetState changed to Open from Unknown. Starting motion timer");
                 coverState = CoverStatus::Moving;
                 //turn on the timer to move the servo smoothly
                 ets_timer_arm_new( &rcTimer, RCFLAPTIME, 0/*one-shot*/, SERVO_START_TIME /*ms*/);      
                 break;
            case CoverStatus::Moving:
            case CoverStatus::Halted:            
            case CoverStatus::Error:
            default:
                 debugW("Unexpected targetState %s from Unknown", coverStatusCh[ targetState ] );
              break;
          }
          break;
      case CoverStatus::Open: 
          switch( targetState )
          {
            case CoverStatus::Closed:
              debugD("targetState set to Closed from Open, actual position flap[0] is %d ", flapPosition[0] );
              coverState = CoverStatus::Moving;
              //turn on the timer to move the servo smoothly
              ets_timer_arm_new( &rcTimer, RCFLAPTIME, 0/*one-shot*/, SERVO_INC_TIME /*ms*/);      
              break;
            case CoverStatus::Halted:
              debugD("targetState set to Halted from Open, actual position flap[0] is %d ", flapPosition[0] );
              if ( flapPosition[ flapCount -1 ] >= flapMaxLimit[ flapCount -1 ] )
              {
                coverState = CoverStatus::Open;
                debugI("targetState updated to Open due to position while halted");
              }                    
              else 
              {
                debugI("targetState changed to Unknown from Open. ");
                coverState = CoverStatus::Unknown;
                targetCoverState = Unknown;
              }            
              break;
            case CoverStatus::Open:
              debugD("targetState set to Open from Open, actual position flap[0] is %d ", flapPosition[0] );
              break;//Quietly ignore. 
            case CoverStatus::Moving:
            case CoverStatus::Unknown:
            case CoverStatus::Error:
            default:
              debugW("unexpected targetState from Open %s", coverStatusCh[targetState]);
              break;
          }
          break;
      case CoverStatus::Error:
          switch ( targetCoverState )
          {
            case CoverStatus::Open:
            case CoverStatus::Closed:
              debugD("Requested targetState of %s from %s - setting Halted first", coverStatusCh[coverState], coverStatusCh[targetCoverState] );
              //turn on the timer to move the servo smoothly
              ets_timer_arm_new( &rcTimer, RCFLAPTIME, 0/*one-shot*/, SERVO_INC_TIME /*ms*/);      
              break;
            case CoverStatus::Halted:
              setRCPower( RCPOWERPIN_OFF ); //turn power off
              coverState = CoverStatus::Unknown;
              targetCoverState = CoverStatus::Unknown;
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
  
  //debugD( "Exiting - final state %s, targetState %s ", coverStatusCh[coverState], coverStatusCh[targetCoverState] );
  return 0;
}

 int manageCalibratorState( CalibratorStatus calibratorTargetState )
 {
    //if ( calibratorState == calibratorTargetState ) 
    //  return 0; 
    
    debugV( "Entered - current state %s, target State %s, brightness %i", calibratorStatusCh[(int) calibratorState], calibratorStatusCh[(int) calibratorTargetState], brightness );
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
              //Get here while we are waiting for the current state to become READY - brightness may be changed in the meantime.
              if( brightnessChanged )
              {
                //we have a new brightness. 
                //turn on to desired brightness - this is different from setting to off/0. 
                setELPower( brightness );
                ets_timer_arm_new( &ELTimer, 5000, 0/*one-shot*/, 1);
                brightnessChanged = false;
                debugI( "Brightness changed (%d) while warming up lamp - set new brightness", brightness);
                //leave current state of not ready alone. 
              }
              else
                debugV("Waiting for timer to move to Ready");
               break;
          case Off:
              //turn off              
              //adjust brightness to be zero or just use brightness to record current requested setting when on ? API says use 
              setELPower( brightness = 0 ); 
              brightnessChanged = false;              
              calibratorState = CalibratorStatus::Off;
              break; 
          case CalibratorStatus::CalError:
          case CalibratorStatus::CalUnknown:
          default:
            debugW("targetstate invalid : %s", calibratorStatusCh[calibratorTargetState] );
            break;          
        }    
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
             setELPower( brightness );
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
        break;

      case CalibratorStatus::Ready:      
        switch ( calibratorTargetState )
        {
          case CalibratorStatus::CalNotPresent:
              debugW("targetState invalid - trying to set NotPresent from Ready");
              break;
          case CalibratorStatus::NotReady:
              //Waiting for timer completion - not a valid target state
              debugW("targetState invalid - trying to set NotReady from Ready");
              break;
          case CalibratorStatus::Ready:  
              //Already on - leave on, manage changing brightness
              if( brightnessChanged )
              {
                debugD( "Arming (0) timer to turn on lamp" ); 
                setELPower( brightness );
                //Turn on timer for illuminator to stabilise
                ets_timer_arm_new( &ELTimer, 5000, 0/*one-shot*/, 1);
                //targetCalibratorState = CalibratorStatus::Ready; //already set
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
              setELPower( brightness = 0 );
              calibratorState = CalibratorStatus::Off;
              break;
          case CalibratorStatus::CalUnknown:
          default:
              debugD("targetState invalid - trying to move from Unknown to %s", calibratorStatusCh[calibratorTargetState]  );
            break;          
        }
        break;
      case CalibratorStatus::CalUnknown:
        switch( calibratorTargetState )  
        {
          case CalibratorStatus::Off:
            //Turn off 
            setELPower( brightness = 0 );
            calibratorState = Off;
            break;
          case CalibratorStatus::Ready:
            debugD( "Arming (1) timer to turn on lamp" ); 
            setELPower( brightness );
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
            setELPower( brightness );
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
        break;      

      default:
        debugE( "Unknown calibrator state on entry! %s, target State %s", calibratorStatusCh[calibratorState], calibratorStatusCh[calibratorTargetState] );
        break;
    }
  debugV( "Exited - final state %s, target State %s, brightnessL %i", calibratorStatusCh[(int) calibratorState], calibratorStatusCh[ (int) calibratorTargetState], brightness );        
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
  {
    debugI( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
  }
  else
  {
    debugW( "Failed to publish : %s to %s", output.c_str(), outTopic.c_str() );
  }
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
  root[F("Time")] = timestamp;
  root["hostname"] = myHostname;
  root["coverState"] = (int) coverState;
  root["calibratorState"] = (int) calibratorState;
  
  root.printTo( output);
  
  //Put a notice out regarding device health
  outTopic = outFnTopic;
  outTopic.concat( myHostname );
  outTopic.concat( "/" );
  outTopic.concat( DriverType );
  
  if ( client.publish( outTopic.c_str(), output.c_str(), true ) )
  {
    debugI( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
  }
  else
  {
    debugW( "Failed to publish : %s to %s", output.c_str(), outTopic.c_str() );
  }
 }

//Wrapped into a function to allow us to isolate this for different behaviours under PCA9865 and direct to dedicated servo
 void setRCPower( bool powerState )
 {
  
#if defined USE_PCA9865
  debugD( "Setting RCRCPOWERPIN pca9685 power state to %d", powerState );
#if defined REVERSE_POWER_LOGIC
//RCPOWERPIN maps to the io pin used to power the base or gate of the transistor providing power to the servo 
  
  digitalWrite( RCPOWERPIN, !powerState );
#else
  digitalWrite( RCPOWERPIN, powerState );
#endif

#else
  debugD( "Setting RCRCPOWERPIN SOLO RC power state to %d", powerState );
#if defined REVERSE_POWER_LOGIC
//RCPOWERPIN maps to the io pin used to power the base or gate of the transistor providing power to the servo 
  digitalWrite( RCPOWERPIN, !powerState );
#else
  digitalWrite( RCPOWERPIN, powerState );
#endif

#endif
  //update our record. 
  rcPowerOn = powerState;
 }
 
void setELPower( int pwmSetting  )
 {
  //RCPOWERPIN maps to the io pin used to power the base or gate of the transistor providing power to the servo 
  debugD( "Setting new ELPower setting %d ", pwmSetting);
#if defined REVERSE_POWER_LOGIC
  analogWrite( ELPIN, MAXDIGITALVALUE - pwmSetting );
#else 
  analogWrite( ELPIN, pwmSetting );
#endif

 }

//#define TEST_SERVO
 void testServo(void)
 {
  int i = 0, j = 0;
  int pwmIncrement = 15;

  setRCPower( RCPOWERPIN_ON );
  delay( 1000 );
  
  for ( i=0; i< flapCount ; i++ )
  {
      for ( j= 30; j< 160; j+= pwmIncrement ) 
      {
         Serial.printf( "channel[%d] set to %d\n", i, j );
#if defined TEST_SERVO_PCA9685
         pwmController.setChannelPWM(  i, pwmServo.pwmForAngle( j - 90 ) );
#else //test the single servo pin case. 
         setSoloRCPosition ( j  );
#endif
         delay(250);
         yield();
      }
      
  }
  //down
  for ( i= flapCount-1; i>= 0 ; i-- )
  {
      for ( j= 160; j>30; j-= pwmIncrement ) 
      {
         Serial.printf( "channel[%d] set to %d\n", i, j );
#if defined TEST_SERVO_PCA9685
         pwmController.setChannelPWM(  i, pwmServo.pwmForAngle( j - 90 ) );
#else //test the single servo pin case. 
         setSoloRCPosition ( j  );
#endif
         delay(1000);
         yield();
      }
      
  }
  //setRCPower( RCPOWERPIN_OFF );
  debugI("Servo testing complete "); 
 }

 void setup_wifi()
{
  int zz = 0;
  WiFi.mode(WIFI_STA); 
  WiFi.hostname( myHostname );
  //WiFi.begin( ssid2, password2 );
  WiFi.begin( ssid1, password1 );
  Serial.print(F("Searching for WiFi.."));
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
      Serial.print(F("."));
   if ( zz++ > 200 ) 
    device.restart();
  }

  Serial.println(F("WiFi connected") );
  Serial.printf("SSID: %s, Signal strength %i dBm \n\r", WiFi.SSID().c_str(), WiFi.RSSI() );
  Serial.printf("Hostname: %s\n\r",      WiFi.hostname().c_str() );
  Serial.printf("IP address: %s\n\r",    WiFi.localIP().toString().c_str() );
  Serial.printf("DNS address 0: %s\n\r", WiFi.dnsIP(0).toString().c_str() );
  Serial.printf("DNS address 1: %s\n\r", WiFi.dnsIP(1).toString().c_str() );

  //Setup sleep parameters
  //wifi_set_sleep_type(LIGHT_SLEEP_T);
  wifi_set_sleep_type(NONE_SLEEP_T);

  delay(5000);
  Serial.println( "WiFi connected" );
}
