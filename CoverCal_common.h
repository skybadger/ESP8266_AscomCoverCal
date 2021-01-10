/*
ASCOM project common variables
*/
#ifndef _COVERCAL_COMMON_H_
#define _COVERCAL_COMMON_H_

const int MAX_NAME_LENGTH = 25;
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
const String Description = "Skybadger ESP2866-based wireless ASCOM switch device";
const String InterfaceVersion = "1";
const String DriverType = "ASCOM.CoverCalibrator";
char GUID[] = "0012-0000-0000-0001";
const int defaultInstanceNumber = 1;
int instanceNumber  = 1;
float instanceVersion = 1.0;
//Location setting for Management reporting
String Location = "RC12";

const int rcMinLimitDefault = 000;
const int rcMaxLimitDefault = 180;
#define RCPOSITIONINCREMENT 2

#define TZ              0       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

//Pin assignments
const int NULLPIN = 0; 
const int POWERPIN = 3; //Rx - pin 0
const int RCPIN = 0;    //SDA, pin 1
const int ELPIN = 2;    // If you have an EL Panel - this to enable it. 
//#define _TEST_RC_

#if defined ESP8266-01
//GPIO 1 is Serial tx, GPIO 3 is Rx, GPIO 2 I2C SDA, GPIO 0 SCL, so it depends on whether you use i2c or not.
//Guess that just leaves nothing
const int pinMap[] = {NULLPIN}; 
#elif defined ESP8266-12
//Most pins are used for flash, so we assume those for SSI are available.
//Typically use 4 and 5 for I2C, leaves 
const int pinMap[] = { 2, 14, 12, 13, 15};
#else
const int pinMap[] = {NULLPIN};
#endif 

const int MINPIN = 1; //device specific
const int MAXPIN = 16; //device specific
const int MAXDIGITALVALUE = 1023; //PWM limit

#endif
