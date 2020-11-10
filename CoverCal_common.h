/*
ASCOM project common variables
*/
#ifndef _COVERCAL_COMMON_H_
#define _COVERCAL_COMMON_H_

#if !defined DEBUG
#define DEBUG
#endif 

const int MAX_NAME_LENGTH = 25;
#define ASCOM_DEVICE_TYPE "covercalibrator" //used in server handler uris
const int defaultInstanceNumber = 0;
int instanceNumber = 0;

//ASCOM driver common variables 
unsigned int transactionId;
extern const unsigned int NOT_CONNECTED;//Sourced from ASCOM_COMMON
unsigned int connected = NOT_CONNECTED;
int connectionCtr = 0;
const String DriverName = "Skybadger.CoverCalibrator";
const String DriverVersion = "0.0.1";
const String DriverInfo = "Skybadger.ESPCoverCal RESTful native device. ";
const String Description = "Skybadger ESP2866-based wireless ASCOM switch device";
const String InterfaceVersion = "1";
const String DriverType = "ASCOM.CoverCalibrator";
const int INSTANCE_NUMBER = 01;
const int GUID = 00001;

const int rcMinLimitDefault = 0;
const int rcMaxLimitDefault = 180;

#define TZ              0       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

//Pin limits
const int NULLPIN = 0; 
const int RCPIN = 0;
const int ELPIN = 2; // If you have an EL Panel - this to enable it. 
const int POWERPIN = 3;

#if defined ESP8266-01
//GPIO 0 is Serial tx, GPIO 2 I2C SDA, GPIO 1 SCL, so it depends on whether you use i2c or not.
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
const int MAXDIGITALVALUE = 1024; //PWM limit

//UDP discovery service responder struct.
#define ALPACA_DISCOVERY_PORT 32227
struct DiscoveryPacket
 {
  const char* protocol = "alpadiscovery1" ;
  byte version; //1-9, A-Z
  byte reserved[48];
 }; 
 
 //Req: Need to provide a method to change the discovery port using setup
 //Discovery response: 
 //JSON "AlpacaPort":<int>
 //JSON: unique identifier
 //JSON: hostname of device ?
 //JSON: type of device ?
 
#endif
