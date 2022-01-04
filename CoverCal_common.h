/*
ASCOM project common variables

ESP8266_coverhandler.h
This is a firmware application to implement the ASCOM ALPACA cover calibrator API.
Each device can manage one RC based flap and a power switch to turn on or off a calbrator light source
Each power switch can be 1 of 2 types - binary switch or digital pwm intensity control.
The setup allows specifying:
  the host/device name.
  The UDP management interface listening port. 

This device assumes the use of ESP8266 devices to implement. 
Using an ESP8266-01 this leaves one pin free to be a digital device. 
Using an ESP8266-12 this leaves a number of pins free to be a digital device.

 To do:
 Debug, trial
 
 Layout: 
 (ESP8266-12)
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 (ESP8266-01)
 GPIO 0 - SDA
 GPIO 1 - Rx - re-use as PWM output for testing purposes
 GPIO 2 - SCL
 GPIO 3 - Tx
 All 3.3v logic. 
 
*/

#ifndef _COVERCAL_COMMON_H_
#define _COVERCAL_COMMON_H_

//Manage the remote debug interface, it takes 6K of memory with all the strings even when not in use but loaded
//
//#define DEBUG_DISABLED //Check the impact on the serial interface. 

//#define DEBUG_DISABLE_AUTO_FUNC    //Turn on or off the auto function labelling feature .
#define WEBSOCKET_DISABLED true      //No impact to memory requirement
#define MAX_TIME_INACTIVE 0          //to turn off the de-activation of the debug telnet session

#include "DebugSerial.h"
//Remote debugging setup 
#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug
#include "SkybadgerStrings.h"

//Define this to enable use of the PWM servo breakout multiple-servo interface for multi-leaf covers. 
//Otherwise it dedicates a single pin to manage a single servo
#define USE_SERVO_PCA9685 

//Define this to put the device in continual looping servo test mode
//#define _TEST_RC_  

//Time between Servo motion change request to start after chnage of request state
#define SERVO_INC_TIME 5

//Use for client testing
//#define DEBUG_MQTT            //enable low-level MQTT connection debugging statements.    

const char* defaultHostname = "espACC00";

const int MAX_NAME_LENGTH = 40;
#define MAX_SERVOS 16
#define EEPROMSIZE ( 4 + (2*MAX_NAME_LENGTH) + ( MAX_SERVOS * 2 * sizeof(int) ) + 4 * sizeof(int) ) //bytes: UDP port, brightness, flapcount

#define ASCOM_DEVICE_TYPE "covercalibrator" //used in server handler uris

//ASCOM driver common variables 
unsigned int transactionId;
unsigned int clientId;
int connectionCtr = 0; //variable to count number of times something has connected compared to disconnected. 
extern const unsigned int NOT_CONNECTED;//Sourced from ASCOM_COMMON
unsigned int connected = NOT_CONNECTED;
const String DriverName = "Skybadger.CoverCalibrator";
const String DriverVersion = "0.0.1";
const String DriverInfo = "Skybadger.ESPCoverCal RESTful native device. ";
const String Description = "Skybadger ESP2866-based wireless ASCOM Cover-calibrator device";
const String InterfaceVersion = "1.1";
const String DriverType = "CoverCalibrator"; //Must be a ASCOM type to be recognised by UDP discovery. 
//char GUID[] = "0012-0000-0000-0000";//prototype
//char GUID[] = "0012-0000-0000-0001";//Hi-power single servo instance
char GUID[] = "0012-0000-0000-0002";//PCA_9685 standard multi-servo instance 

const int defaultInstanceNumber = 0;

//UDP Port can be edited in setup page - eventually.
#define ALPACA_DISCOVERY_PORT 32227
int udpPort = ALPACA_DISCOVERY_PORT;

//Location setting for Management reporting
char* Location = nullptr;
int instanceNumber  = defaultInstanceNumber; //Does this need to be zero to reflect the api or something else ? 
float instanceVersion = 1.0;

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

const int rcMinLimit = 0;
const int rcMaxLimit = 180;
const int rcMinLimitDefault = 30;
const int rcMaxLimitDefault = 150;
#define RCPOSITIONINCREMENT ((int) 2)
#define RCFLAPTIME 50
#define MULTIFLAPSINCREMENTCOUNT ((int) 10)
#define MULTIFLAPTIME 30
//Define time between handling stateupdate and kicking off servo movements. 
#define SERVO_START_TIME 5

enum FlapMovementType { EachFlapInSequence, EachFlapIncremental, AllFlapsAtOnce };
enum FlapMovementType movementMethod = FlapMovementType::EachFlapInSequence;

//Populate from eeprom
int flapCount = 1; 
int* flapMinLimit = nullptr;
int* flapMaxLimit = nullptr;
int* flapPosition = nullptr;

bool rcPowerOn = false;//Turn on power output transistor - provides power to the servo 

//Returns the current calibrator brightness in the range 0 (completely off) to MaxBrightness (fully on)
int brightness;         
bool brightnessChanged = false;
//The Brightness value that makes the calibrator deliver its maximum illumination.

//ASCOM variables for the driver
//Returns the state of the device cover, if present, otherwise returns "NotPresent"
enum CoverStatus coverState = CoverStatus::Closed;
enum CoverStatus targetCoverState = CoverStatus::Closed;     

//Returns the state of the calibration device, if present, otherwise returns "NotPresent"
enum CalibratorStatus calibratorState = Off;
enum CalibratorStatus targetCalibratorState = Off ;   

#define TZ              0       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

#define RCPOWERPIN_ON  ((bool)true )
#define RCPOWERPIN_OFF ((bool) false )

const int MINPIN = 1; //device specific
const int MAXPIN = 16; //device specific
const int MAXDIGITALVALUE = 1023; //PWM limit
const int NULLPIN = 0; 

#if defined ESP8266-01
//GPIO 0 is I2C SCL pin 3, so it depends on whether you use i2c or not.
//GPIO 1 is Serial tx pin 5
//GPIO 2 is I2C SDA pin 2
//GPIO 3 is Rx pin 1 
//Guess that just leaves nothing
const int pinMap[] = {NULLPIN}; 

//Multi-servo handling case for 1-16 flaps means we need the i2c bus and then have two pins left - one for Rx and one for Tx. 
//Still need a high current servo power feed control and still need an ELPanel PWM control
//So we lose Tx and Rx. Means the board starts to look different than before since the wiring has changed. 
#if defined USE_SERVO_PCA9685
//Pin assignments
const int POWERPIN = 3; //Rx is GPIO 3 on pin 0 - controls OE. 
const int RCPIN = 0;    //SDA IS GPIO 0 on pin 1
const int ELPIN = 3;    //ELPIN is GPIO 1 on pin5 (Tx) If you have an EL Panel - this to enable it but loses Tx serial. 
const int SCLPIN = 2;   //SCL is GPIO 2 on pin2  

#else //Single-servo direct pin control case. 
//Pin assignments
const int POWERPIN = 3; //Rx - pin 0
const int RCPIN = 0;    //SDA, pin 1
const int ELPIN = 2;    // If you have an EL Panel - use this to enable it or PWM control it. . 
const int SCLPIN = 2;   //SCL is GPIO 2 on pin2  
#endif //servo types

#elif defined ESP8266-12
//Most pins are used for flash, so we assume those for SSI are available.
//Typically use 4 and 5 for I2C, leaves 
const int pinMap[] = { 2, 14, 12, 13, 15};

//Multi-servo handling case for 1-16 flaps means we need the i2c bus but have more pins left than just Rx/Tx. 
//Need a high current servo power feed control and an ELPanel PWM control
//So we pick a pin from the spare pin list 
#if defined USE_SERVO_PCA9685
//Pin assignments
const int POWERPIN = 12; //
const int RCPIN = 4;    //SDA 
const int ELPIN = 13;    //
const int SCLPIN = 5;   //SCL 

#else //Single-servo direct pin control case. don;t need i2c but have more sprate pins to choose from  .
//Pin assignments
const int POWERPIN = 12; //Rx
const int RCPIN = 4;     //SDA
const int ELPIN = 13;    // If you have an EL Panel - use this to enable it or PWM control it. . 
const int SCLPIN = 5;    //SCL 
#endif //servo types

#else 
#pragma warning "ESP type not specified - pin map not set correctly" 
const int pinMap[] = {NULLPIN};
#endif 

#endif
