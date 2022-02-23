#ifndef _ESP8266_COVERCAL_H_
#define _ESP8266_COVERCAL_H_

#include "CoverCal_eeprom.h"
#include <Wire.h>
#include "AlpacaErrorConsts.h"
//#include "ASCOMAPICCal_rest.h"

extern void setRCPower ( boolean );

//Function definitions
bool getUriField( char* inString, int searchIndex, String& outRef );
String& setupFormBuilderHeader( String& htmlForm );
String& setupFormBuilderDeviceHeader( String& htmlForm, String& errMsg );
String& setupFormBuilderDeviceStrings( String& htmlForm );
String& setupFormBuilderDriver0Header( String& htmlForm, String& errMsg );
String& setupFormBuilderDriver0Limits( String& htmlForm );
String& setupFormBuilderDriver0Brightness( String& htmlForm );
String& setupFormBuilderDriver0Positions( String& htmlForm );
String& setupFormBuilderFooter( String& htmlForm );

//Device properties & methods
void handlerBrightnessGet(void);
void handlerMaxBrightnessGet(void);
void handlerCoverStateGet(void);
void handlerCalibratorStateGet(void);
void handlerCalibratorOnPut(void );
void handlerCalibratorOffPut(void);
void handlerOpenCoverPut(void);
void handlerCloseCoverPut(void);
void handlerHaltCoverPut(void);

//Methods

//Function to chop up the uri into '/'delimited fields to analyse for wild cards and paths
bool getUriField( char* inString, int searchIndex, String& outRef )
{
  char *p = inString;
  char *str;
  char delims1[] = {"//"};
  char delims2[] = {"/:"};
  int chunkCtr = 0;
  bool  status = false;    
  int localIndex = 0;
  
  localIndex = String( inString ).indexOf( delims1 );
  if( localIndex >= 0 )
  {
    while ((str = strtok_r(p, delims2, &p)) != NULL) // delimiter is the semicolon
    {
       if ( chunkCtr == searchIndex && !status )
       {
          outRef = String( str );
          status = true;
       }
       chunkCtr++;
    }
  }
  else 
    status = false;
  
  return status;
}

//GET ​/cover​/{device_number}​/maxbrightness
void handlerBrightnessGet(void)
{
    String message;
    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
    
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    debugD( "Getting ( client %d trans %d ) calibrator brightness resulted in value: %d", clientID, transID, brightness );
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "BrightnessGet", Success , "" );    
    root["Value"] = brightness;
    
    root.printTo(message);
    server.send(200, F("application/json"), message);
    return ;
}

//GET ​/cover​/{device_number}​/maxbrightness
//Return the limit of the brightness setting available
void handlerMaxBrightnessGet(void)
{
    String message;
    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
    
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    debugD( "Getting ( client %d trans %d ) calibrator max brightness resulted in value: %d", clientID, transID, MAXDIGITALVALUE );
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "MaxBrightnessGet", Success , "" );    
    root["Value"] = MAXDIGITALVALUE;       
    root.printTo(message);
    server.send(200, F("application/json"), message);
    return ;
}

//Get the state of cover as an enum CoverStatus 
void handlerCoverStateGet(void)
{
    String message;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    DynamicJsonBuffer jsonBuffer(256);
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
    
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    debugD( "Getting (client %d trans %d) cover state resulted in value: %s", clientID, transID, coverStatusCh[coverState]  );
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("CoverStateGet"), Success , "" );    
 
    //translate private 'halted' command state into ASCOM 'unknown' state. 
    if( coverState == CoverStatus::Halted ) 
      root["Value"] = (int) CoverStatus::Moving; //we do this because it means the timer handler hasn't got round to setting the state from 'commanded to Halt' to 'unknown'
    else 
      root["Value"] = (int) coverState;

    root.printTo(message);
    server.send(returnCode, F("application/json"), message);
    return;
}

//Gets the state of the calibrator device
void handlerCalibratorStateGet(void)
{
    String message;
    int returnCode = 200;

    DynamicJsonBuffer jsonBuffer(256);
    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { F("clientID"), F("ClientTransactionID"), };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
    
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();
   
    debugD( "Getting (client %d trans %d) calibrator state resulted in state: %s, ", clientID, transID, calibratorStatusCh[calibratorState]  );
    
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("CalibratorStateGet"), Success , "" );    
 
    root["Value"] = (int) calibratorState;
    root.printTo(message);
    server.send( returnCode = 200, F("application/json"), message);
    return;
}

//PUT
//Set the calibrator according to brightness
void handlerCalibratorOnPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", "brightness" };
    int input = 0;
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    if ( hasArgIC( argToSearchFor[2], server, false) )
      input = server.arg(argToSearchFor[2]).toInt();

    if( connected == clientID) 
    {
      if( input >= 0 && input <= MAXDIGITALVALUE )
      {
        debugD( "New brightness requested: %i", input );          
        brightnessChanged = false;
        switch ( calibratorState )
        {
          case CalibratorStatus::CalNotPresent:
            errCode = notImplemented;
            errMsg = "Calibrator not present";            
            break; 
          case CalibratorStatus::Ready:
          case CalibratorStatus::NotReady: //already in the proces of turning on
            brightnessChanged = true;
            brightness = input;
          case CalibratorStatus::Off:
          case CalibratorStatus::CalUnknown:
          case CalibratorStatus::CalError:
            targetCalibratorState = CalibratorStatus::Ready;                      
            errCode = Success;
            errMsg = "";
            break; 
          default:
            break;
        }
      }
      else
      {
         errCode = invalidValue;
         errMsg = "brightness not in acceptable range";        
      }
    }
    else
    {
       errCode = notConnected;
       errMsg = "Not the connected client";   
    }
    debugD( "Setting (client %d trans %d) calibrator state to %s from %s at brightness %d resulted in code: %d, error: '%s'", clientID, transID, calibratorStatusCh[targetCalibratorState], calibratorStatusCh[calibratorState], brightness, errCode, errMsg.c_str() );    

    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("handlerCalibratorOnPut"), errCode, errMsg );    
    root.printTo(message);
   
    server.send(returnCode = 200, F("application/json"), message);
    return;
}

//PUT
//Set the calibrator device to off
void handlerCalibratorOffPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;
    
    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "ClientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();

    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    if ( connected == clientID )
    {
      debugD( "New state requested: %s", "off" );
      targetCalibratorState = CalibratorStatus::Off;
      brightnessChanged = false;
      //don't change brightness until actual 'offing' happens
      errMsg = "";
      errCode = 0;
    }
    else
    {
      errMsg = "This is not the connected client";
      errCode = notConnected;
    }
    
    debugD( "Setting client %d trans %d calibrator state to %s from %s resulted in code: %d, error: '%s'", clientID, transID, calibratorStatusCh[targetCalibratorState], calibratorStatusCh[calibratorState], errCode, errMsg.c_str() );
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("handlerCalibratorOffPut"), errCode, errMsg );      
    root.printTo(message);
  
    server.send(returnCode, F("application/json"), message);
    return;
}

//PUT
//Set the cover to close
void handlerCloseCoverPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "ClientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();
     
    //Set targetCoverState to desired state. 
    if ( connected != clientID )
    {
      errCode = notConnected;  
      errMsg = "This is not the connected client";         
    }
    else
    {
      targetCoverState = CoverStatus::Closed;
      errCode = Success;
      errMsg = "";   
    }

    debugD( "Setting ( client %d trans %d ) cover state to %s from %s resulted in code: %d, error: '%s'", clientID, transID, coverStatusCh[targetCoverState], coverStatusCh[coverState], errCode, errMsg.c_str()  );
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("handlerCloseCoverPut"), errCode, errMsg );    
    root.printTo(message);

    server.send(returnCode = 200, F("application/json"), message);
    return;
}

//PUT
//Set the cover to halt
void handlerHaltCoverPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "ClientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();
 
    //Set targetCoverState to desired state. 
    if ( connected != clientID )
    {
      errCode = notConnected;  
      errMsg = "Not the connected client";         
    }
    else
    {
      //According to the COM API, the only acceptable values to return are OPEN, CLOSED or UNKNOWN
      //We use HALTED to flag the requested change of state clearly and end up with UNKNOWN once handled. 
      targetCoverState = CoverStatus::Halted;
      errCode = Success;
      errMsg = "";   
    }

    debugD( "Setting ( client %d trans %d ) cover state to %s from %s resulted in code: %d, error: '%s'", clientID, transID, coverStatusCh[targetCoverState], coverStatusCh[coverState], errCode, errMsg.c_str() );
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("handlerHaltCoverPut"), errCode, errMsg );    
    root.printTo(message);
    server.send(returnCode = 200, F("application/json"), message);
    return;
}

//PUT
//Set the cover to open
void handlerOpenCoverPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    //Set targetCoverState to desired state. 
    if ( connected != clientID )
    {
      errCode = notConnected;  
      errMsg = "Not connected";         
    }
    else
    {
      targetCoverState = CoverStatus::Open;
      errCode = Success;
      errMsg = "";
    }
    
    debugD( "Setting ( client %d trans %d ) cover state to %s from %s resulted in code: %d, error: '%s'", clientID, transID, coverStatusCh[targetCoverState], coverStatusCh[coverState], errCode, errMsg.c_str() );

    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("handlerCoverOpenPut"), errCode, errMsg );    
    root.printTo(message);

    server.send(returnCode = 200, F("application/json"), message);
    return;
}

////////////////////////////////////////////////////////////////////////////////////
//Additional non-ASCOM custom setup calls

void handlerNotFound()
{
  String message;
  int responseCode = 400;
  uint32_t clientID= -1;
  uint32_t transID = -1;
  String argToSearchFor[] = { "clientID", "ClientTransactionID", };
  
  if( hasArgIC( argToSearchFor[0], server, false ) )
    clientID = server.arg(argToSearchFor[0]).toInt();
  
  if ( hasArgIC( argToSearchFor[1], server, false) )
    transID = server.arg(argToSearchFor[1]).toInt();

  DynamicJsonBuffer jsonBuffer(250);
  JsonObject& root = jsonBuffer.createObject();
  String missingURL = F("HandlerNotFound");
  missingURL.concat(":");
  missingURL.concat( server.uri() );
  
  jsonResponseBuilder( root, clientID, transID, ++serverTransID, missingURL, invalidOperation , F("No REST handler found for argument - check ASCOM Switch v2 specification") );    
  root["Value"] = 0;
  root.printTo(message);
  server.send(responseCode, "application/json", message);
}

void handlerRestart( void)
{
  //Trying to do a redirect to the rebooted host so we pick up from where we left off. 
  server.sendHeader( WiFi.hostname().c_str(), String("/status"), true);
  server.send ( 302, F("text/html"), F("<!Doctype html><html>Redirecting for restart</html>"));
  debugI("Reboot requested");
  setRCPower( RCPOWERPIN_OFF );
  device.restart();
}

void handlerNotImplemented()
{
  String message;
  int responseCode = 400;
  uint32_t clientID= -1;
  uint32_t transID = -1;
  String argToSearchFor[] = { "clientID", "ClientTransactionID", };
  
  if( hasArgIC( argToSearchFor[0], server, false ) )
    clientID = server.arg(argToSearchFor[0]).toInt();
  
  if ( hasArgIC( argToSearchFor[1], server, false) )
    transID = server.arg(argToSearchFor[1]).toInt();

  DynamicJsonBuffer jsonBuffer(250);
  JsonObject& root = jsonBuffer.createObject();
  jsonResponseBuilder( root, clientID, transID, ++serverTransID, F("HandlerNotFound"), notImplemented  , F("No REST handler implemented for argument - check ASCOM Dome v2 specification") );    
  root["Value"] = 0;
  root.printTo(message);
  
  server.send(responseCode, F("application/json"), message);
}

//Get a descriptor of all the components managed by this driver for discovery purposes
void handlerStatus(void)
{
    String message, timeString;
    int returnCode = 400;
    int i;
   
    DynamicJsonBuffer jsonBuffer(250);
    JsonObject& root = jsonBuffer.createObject();
   
    root["time"] = getTimeAsString( timeString );
    root["host"] = myHostname;
    root["connected"] = (connected == NOT_CONNECTED )?"false":"true";
    root["clientID"] = (int) connected;
    root["coverState"] = coverStatusCh[coverState];
    
    //Needs a json array for this. 
    JsonArray& entries = root.createNestedArray( "flaps" );
    if( coverState != CoverStatus::NotPresent )
    {
      JsonObject& entry = jsonBuffer.createObject();      
      for( i=0; i<flapCount; i++ )
      {
        entry["position"] = (int) flapPosition[i];
        entry["minimum"]  = (int) flapMinLimit[i];
        entry["maximum"]  = (int) flapMaxLimit[i];
        entries.add( entry ); 
      }
    }
    
    root["calibratorState"]  = calibratorStatusCh[calibratorState];
    if( calibratorState != CalibratorStatus::CalNotPresent ) 
    {
      root["brightness"] = brightness;
    }
    
    Serial.println( message);
    root.printTo(message);
    
    server.send(returnCode, F("application/json"), message);
    return;
}

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 */
 void handlerDeviceSetup(void)
 {
    String message, timeString, err= "";
    int returnCode = 400;

    if ( server.method() == HTTP_GET )
    {
        returnCode = 200;
        err = "";
    }
      
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader( message ); 
    server.send( returnCode, "text/html", message );

    setupFormBuilderDeviceHeader( message, err );      
    server.sendContent( message );
    
    setupFormBuilderDeviceStrings( message );      
    server.sendContent( message );

    setupFormBuilderFooter( message );
    server.sendContent( message );
 }

void handlerDriver0Setup(void)
 {
    String message, timeString, err= "";
    int returnCode = 400;

    if ( server.method() == HTTP_GET )
    {
        returnCode = 200;
        err = "";
    }
    
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader( message );      
    server.send( returnCode, "text/html", message );
   
    setupFormBuilderDriver0Header( message, err );
    server.sendContent( message );
    
    setupFormBuilderDriver0Limits( message );            
    server.sendContent( message );

    setupFormBuilderDriver0Positions( message );
    server.sendContent( message );
    
    setupFormBuilderDriver0Brightness( message );            
    server.sendContent( message );
    
    setupFormBuilderFooter( message );
    server.sendContent( message );
 }
  
 /* 
  *  Handler to update the hostname from the form.
  */
 void handlerDeviceHostname(void) 
 {
    String message, timeString, err = "";
   
    int returnCode = 400;
    String argToSearchFor[] = { "hostname", "numSwitches"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          String newHostname = server.arg(argToSearchFor[0]) ;
          //process form variables.
          if( newHostname.length() > 0 && newHostname.length() < MAX_NAME_LENGTH-1 )
          {
            //process new hostname
            strncpy( myHostname, newHostname.c_str(), MAX_NAME_LENGTH );
            saveToEeprom();
            returnCode = 200;
          }
          else
          {
            err = "New name too long";
            returnCode = 401;
          }
        }
    }
    else
    {
      returnCode=400;
      err = "Bad HTTP request verb";
    }
    
    if ( returnCode == 200 )
    {
      server.sendHeader( WiFi.hostname().c_str(), String("/status"), true);
      server.send ( 302, F("text/html"), F("<!Doctype html><html>Redirecting for restart</html>"));
      DEBUGSL1("Reboot requested");
      device.restart();
    }
    else
    {

      //Send large pages in chunks
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      setupFormBuilderHeader( message );      
      server.send( returnCode, "text/html", message );
  
      setupFormBuilderDeviceHeader( message, err );      
      server.sendContent( message );

      setupFormBuilderDeviceStrings( message );      
      server.sendContent( message );

      setupFormBuilderFooter( message );
      server.sendContent( message );
    }
 }

 /* 
  *  Handler to update the hostname from the form.
  */
 void handlerDeviceLocation(void) 
 {
    String message, timeString, err = "";
   
    int returnCode = 400;
    String argToSearchFor[] = { "location" };
    
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          String newLocation = server.arg(argToSearchFor[0]) ;
          debugD( "SetLocation:  %s", newLocation.c_str() );

          //process form variables.
          if( newLocation.length() > 0 && newLocation.length() < MAX_NAME_LENGTH-1 )
          {
            //process new hostname
            strncpy( Location, newLocation.c_str(), min( (int) MAX_NAME_LENGTH, (int) newLocation.length()) );
            saveToEeprom();
            returnCode = 200;
          }
          else
          {
            err = "New name too long";
            returnCode = 401;
          }
        }
    }
    else
    {
      returnCode=400;
      err = "Bad HTTP request verb";
    }
    
      //Send large pages in chunks
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      setupFormBuilderHeader( message );      
      server.send( returnCode, "text/html", message );
  
      setupFormBuilderDeviceHeader( message, err );      
      server.sendContent( message );

      setupFormBuilderDeviceStrings( message );      
      server.sendContent( message );

      setupFormBuilderFooter( message );
      server.sendContent( message );
 }

void handlerDeviceUdpPort(void) 
 {
    String message, timeString, err = "";
   
    int returnCode = 400;
    String argToSearchFor[] = { "discoveryport"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          int newPort = server.arg(argToSearchFor[0]).toInt() ;
          //process form variables.
          if( newPort > 1024 && newPort < 65536  )
          {
            //process new hostname
            udpPort = newPort;
            saveToEeprom();
            returnCode = 200;
          }
          else
          {
            err = "New Discovery port value out of range ";
            returnCode = 401;
          }
        }
    }
    else
    {
      returnCode=400;
      err = "Bad HTTP request verb";
    }
    
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader( message );      
    server.send( returnCode, "text/html", message );

    setupFormBuilderDeviceHeader( message, err );      
    server.sendContent( message );
    
    setupFormBuilderDeviceStrings( message );      
    server.sendContent( message );

    setupFormBuilderFooter( message );
    server.sendContent( message );
 }

 /* 
  *  Handler to update the hostname from the form.
  */
 void handlerDriver0Limits(void) 
 {
    String message, timeString, err = "";
    int i= 0;
    int newMinLimit = -1;
    int newMaxLimit = -1;
    int newLimitIndex = 0;
    int returnCode = 400;
    String argToSearchFor[] = { "minLimit", "maxLimit", "limitIndex" };
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
      for ( i = 0; i < flapCount; i++ )
      {
        String minIndexedArg = argToSearchFor[0]+i;
        String maxIndexedArg = argToSearchFor[1]+i;
        if( hasArgIC( minIndexedArg, server, false )  &&  hasArgIC( maxIndexedArg, server, false )  )
        {
          newMinLimit = server.arg( minIndexedArg ).toInt();
          newMaxLimit = server.arg( maxIndexedArg ).toInt();
          debugD( "New flap limit values: %s: %d %s: %d ", minIndexedArg.c_str(), newMinLimit, maxIndexedArg.c_str(), newMaxLimit );
           
          if( newMinLimit >= rcMinLimit && newMinLimit <= rcMaxLimit &&
              newMaxLimit >= rcMinLimit && newMaxLimit <= rcMaxLimit ) 
          {
            debugI( "new flap limit values: %d %d ", newMinLimit, newMaxLimit );
            flapMinLimit[i] = newMinLimit;
            flapMaxLimit[i] = newMaxLimit;
          }
          else
          {
            returnCode = 400;
            err = "Invalid values - one or more outside allowed range";
            break;
          }          
        
        }
        else
        {
            returnCode = 402;
            err = "Invalid arguments - one or more not found ";          
        }
      //Save the ouputs 
      returnCode = 200;    
      saveToEeprom();
      }
    }
    else
    {
      returnCode = 400;
      err = "Bad HTTP request verb";
    }

      //Send large pages in chunks
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      setupFormBuilderHeader( message );      
      server.send( returnCode, "text/html", message );
  
      setupFormBuilderDriver0Header( message, err );      
      server.sendContent( message );

      setupFormBuilderDriver0Limits( message );            
      server.sendContent( message );

      setupFormBuilderDriver0Positions( message );            
      server.sendContent( message );
      
      setupFormBuilderDriver0Brightness( message );            
      server.sendContent( message );
      
      setupFormBuilderFooter( message );
      server.sendContent( message );
    return;
 }

void handlerDriver0FlapCount(void) 
 {
    String message, timeString, err = "";
   
    int returnCode = 400;
    String argToSearchFor[] = { "flapcount"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          int* flaps[3] = { nullptr, nullptr, nullptr };
          int newCount = server.arg(argToSearchFor[0]).toInt() ;
          
          //process form variables.
          if( newCount > 0 && newCount <= 16  )//1-based count
          {
            flaps[0] = (int*) calloc ( sizeof(int*), newCount);
            flaps[1] = (int*) calloc ( sizeof(int*), newCount);
            flaps[2] = (int*) calloc ( sizeof(int*), newCount);
            //process new value - common values
            for ( int i=0; i < newCount && i < flapCount; i++ )
            {
              flaps[0][i] = flapMinLimit[i];
              flaps[1][i] = flapMaxLimit[i];
              flaps[2][i] = flapPosition[i];
            }
            if ( newCount > flapCount ) 
            {
              for ( int j=flapCount; j < newCount ; j++ )
              {
              //Extends the last known value into the larger array 
              flaps[0][j] = flapMinLimit[flapCount -1];
              flaps[1][j] = flapMaxLimit[flapCount -1];
              flaps[2][j] = flapPosition[flapCount -1];
              } 
            }
            //Update the memory allocations
            flapCount = newCount;
            free( flapMinLimit );
            free( flapMaxLimit );
            free( flapPosition );
            flapMinLimit = flaps[0];
            flapMaxLimit = flaps[1];
            flapPosition = flaps[2];            
            
            saveToEeprom();
            returnCode = 200;
          }
          else
          {
            err = "New flapCount value out of range ";
            returnCode = 401;
          }
        }
        else
        {
            err = "New flapCount argument not found";
            returnCode = 402;          
        }
    }
    else
    {
      returnCode=400;
      err = "Bad HTTP request verb";
    }
    
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);

    setupFormBuilderHeader( message );      
    server.send( returnCode, "text/html", message );

    setupFormBuilderDriver0Header( message, err );      
    server.sendContent( message );
    
    setupFormBuilderDriver0Limits( message );            
    server.sendContent( message );

    setupFormBuilderDriver0Positions( message );            
    server.sendContent( message );
    
    setupFormBuilderDriver0Brightness( message );            
    server.sendContent( message );

    setupFormBuilderFooter( message );
    server.sendContent( message );
 }

/* 
  *  Handler to update the hostname from the form.
  */
 void handlerDriver0Brightness(void) 
 {
    String message, timeString, err = "";
    int newBrightness = -1;
    String newState = "";
     
    int returnCode = 400;
    String argToSearchFor[] = { "brightness", "calibratorstate" };
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false ) && hasArgIC( argToSearchFor[1], server, false ) )
        {
          newBrightness  = ( int ) server.arg( argToSearchFor[0] ).toInt();         
          newState = server.arg( argToSearchFor[1] );
      
          if ( newState.equalsIgnoreCase("On") )//turn on or already on
          {
            if ( newBrightness >= 0 && newBrightness <= MAXDIGITALVALUE) 
            { 
              brightness = newBrightness;
              //If its already on - flag a brightness change rather than state change
              if(  calibratorState == Ready || calibratorState == NotReady )
              {
                brightnessChanged = true;
              }
              targetCalibratorState = CalibratorStatus::Ready;             
              returnCode = 200;    
            }
            else
            {
              returnCode = 400;
              err = "Invalid value - brightness argument not found range";
            }
          }
          else if ( newState.equalsIgnoreCase("Off") )
          {
              //set later //brightness = 0 ;
              //its a state change 
              brightnessChanged = false;
              targetCalibratorState = CalibratorStatus::Off;
              returnCode = 200;               
          }
          else
          {
              returnCode = 200;               
              err = "Not a valid calibrator target state for method";
          }
        }
        else
        {
          returnCode = 402;
          err = "Bad number of arguments for method ";
        }        
    }
    else
    {
      returnCode = 400;
      err = "Bad HTTP request verb";
    }

      //Send large pages in chunks
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);

      setupFormBuilderHeader( message );      
      server.send( returnCode, "text/html", message ); 
      
      setupFormBuilderDriver0Header( message, err );      
      server.sendContent( message );
      
      setupFormBuilderDriver0Limits( message );            
      server.sendContent( message );
      
      setupFormBuilderDriver0Positions( message );            
      server.sendContent( message );
      
      setupFormBuilderDriver0Brightness( message );            
      server.sendContent( message );
      
      setupFormBuilderFooter( message );
      server.sendContent( message );
    return;
 }

/* 
  *  Handler to update the hostname from the form.HAndles fine control of flap positions 
  */
 void handlerDriver0Positions(void) 
 {
    String message, timeString, err = "";
    int i = 0;
    enum CoverStatus newState = coverState;
    int newPosition = rcMinLimitDefault;
     
    int returnCode = 400;
    String argToSearchFor[] = { "coverposition", };
     

   if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
#if defined HANDLER_MANUAL_FLAP_CONTROL
      for ( i = 0; i < flapCount; i++ )
      {
        String newPositionArg = argToSearchFor[0]+i;
        if( hasArgIC( newPositionArg, server, false ) )
        {
          newPosition = server.arg( newPositionArg ).toInt();
          debugD( "New flap limit values: %s: %d %s: %d ", newPositionArg, newPosition );
           
          if( newPosition >= rcMinLimit && newMinLimit <= rcMaxLimit ) 
          {
            flapPosition[i] = newPosition;
          }
          else
          {
            returnCode = 400;
            err = "Invalid values - one or more outside allowed range";
            break;
          }                  
        }
        else
        {
            returnCode = 402;
            err = "Invalid arguments - one or more not found ";          
        }
      //Save the ouputs 
      returnCode = 200;    
      saveToEeprom();
      }
    
#else
      if( hasArgIC( argToSearchFor[0], server, false )  )
      {
        debugD( "Target cover state is %s", server.arg( argToSearchFor[0]).c_str() );
        if( server.arg( argToSearchFor[0]).equalsIgnoreCase( "Open" )  )
          targetCoverState = CoverStatus::Open;
        else if ( server.arg( argToSearchFor[0]).equalsIgnoreCase("Close" )  )
          targetCoverState = CoverStatus::Closed;
        else if ( server.arg( argToSearchFor[0]).equalsIgnoreCase( "Halt" )  )
          targetCoverState  = CoverStatus::Halted;
        else 
        {
          returnCode = 402;
          err = "Bad request - position argument not found.";
        }               
        if( newState == CoverStatus::Open || newState == CoverStatus::Closed || newState == CoverStatus::Halted  )
        {
          returnCode = 200;    
        }
      }
#endif 
    }
    else
    {
      returnCode = 400;
      err = "Bad HTTP request verb";
    }

    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader( message );      
    server.send( returnCode, "text/html", message ); 
    message = "";
    
    setupFormBuilderDriver0Header( message, err );      
    server.sendContent( message );
    message = "";
    
    setupFormBuilderDriver0Limits( message );            
    server.sendContent( message );
    message = "";
    
    setupFormBuilderDriver0Positions( message );            
    server.sendContent( message );
    message = "";

    setupFormBuilderDriver0Brightness( message );            
    server.sendContent( message );
    message = "";
    
    setupFormBuilderFooter( message );
    server.sendContent( message );
    return;
 }

/*
 Handler for setup dialog - issue call to <hostname>/setup and receive a webpage
 Fill in the form and submit and handler for each form button will store the variables and return the same page.
 optimise to something like this:
 https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/
 Bear in mind HTML standard doesn't support use of PUT in forms and changes it to GET so arguments get sent in plain sight as 
 part of the URL.
 */
String& setupFormBuilderHeader( String& htmlForm )
{
  String hostname = WiFi.hostname();
  int i;
  
/*
<!DocType html>
<html lang=en >
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css">
<script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js"></script>
<script src="https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js"></script>
<script>function setTypes( a ) {   var searchFor = "types"+a;  var x = document.getElementById(searchFor).value;  if( x.indexOf( "PWM" ) > 0 || x.indexOf( "DAC" ) > 0 )  {      document.getElementById("pin"+a).disabled = false;      document.getElementById("min"+a).disabled = false;      document.getElementById("max"+a).disabled = false;      document.getElementById("step"+a).disabled = false;  }  else  {      document.getElementById("pin"+a).disabled = true;      document.getElementById("min"+a).disabled = true;      document.getElementById("max"+a).disabled = true;      document.getElementById("step"+a).disabled = true;  }}</script>
</meta>
<style>
legend { font: 10pt;}
h1 { margin-top: 0; }
form {
    margin: 0 auto;
    width: 500px;
    padding: 1em;
    border: 1px solid #CCC;
    border-radius: 1em;
}
div+div { margin-top: 1em; }
label span {
    display: inline-block;
    width: 150px;
    text-align: right;
}
input, textarea {
    font: 1em sans-serif;
    width: 150px;
    box-sizing: border-box;
    border: 1px solid #999;
}
input[type=checkbox], input[type=radio], input[type=submit] {
    width: auto;
    border: none;
}
input:focus, textarea:focus { border-color: #000; }
textarea {
    vertical-align: top;
    height: 5em;
    resize: vertical;
}
fieldset {
    width: 410px;
    box-sizing: border-box;
    border: 1px solid #999;
}
button { margin: 20px 0 0 124px; }
label {  position: relative; }
label em {
  position: absolute;
  right: 5px;
  top: 20px;
}
</style>
</head>
*/
 
  htmlForm =  F( "<!DocType html><html lang=en ><head><meta charset=\"utf-8\">");
  htmlForm += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=\"1\">");
  htmlForm += F("<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/css/bootstrap.min.css\">");
  htmlForm += F("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js\"></script>");
  htmlForm += F("<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\"></script>");
  htmlForm += F("<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/js/bootstrap.min.js\"></script>");
  
//CSS Style settings
  htmlForm += "<style>\
legend { font: 10pt;}\
body { bgcolor:'dark gray'; } \
h1 { margin-top: 0; }\
form { margin: 0 auto; width: 500px;padding: 1em;border: 1px solid #CCC;border-radius: 1em;}\
div+div { margin-top: 1em; }\
label span{display: inline-block;width: 150px;text-align: right;}\
input, textarea {font: 1em sans-serif;width: 150px;box-sizing: border-box;border: 1px solid #999;}\
input[type=checkbox], input[type=radio], input[type=submit]{width: auto;border: 1px;}\
input:focus,textarea:focus{border-color:#000;}\
input[type='number']{width: 60px;} \
textarea {vertical-align: top;height: 5em;resize: vertical;}\
fieldset {width: 410px;box-sizing: border-box;border: 1px solid #999;}\
button {margin: 20px 0 0 124px;}\
label {position:relative;}\
label em { position: absolute;right: 5px;top: 20px;}\
</style> \
</head>";

  return htmlForm;
}

String& setupFormBuilderDeviceHeader( String& htmlForm, String& errMsg )  
{
  htmlForm =  F("<body><div class=\"container\">" );
  htmlForm += F( "<div class=\"row\" id=\"topbar-device\" bgcolor='A02222'> ");
  htmlForm += F("<p> This is the <b><i>device setup </i></b>page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> device '");
  htmlForm += myHostname;
  htmlForm += F("' which implements the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b></p> </div>");

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += F("<div class=\"row\" id=\"device-errorbar\" bgcolor='lightred'> <b>Error Message</b><pre>");
    htmlForm += errMsg;
    htmlForm += F("</pre></div>");
  }
  return htmlForm;
}

String& setupFormBuilderDriver0Header( String& htmlForm, String& errMsg )  
{
  //
  htmlForm =  F("<body><div class=\"container\">");
  htmlForm += F("<div class=\"row\" id=\"topbar-driver\" bgcolor='A02222'>");
  htmlForm += F("<p> This is the <b><i>driver setup </i></b> page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> Cover Calibrator device hosted on '");
  htmlForm += myHostname;
  htmlForm += F("' which implements the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b></p> </div>");

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += F("<div class=\"row\" id=\"driver-errorbar\" bgcolor='lightred'> <b>Error Message</b><pre>");
    htmlForm += errMsg;
    htmlForm += F("</pre></div>");
    //htmlForm += "<hr>";
  }
  return htmlForm;
}
//todo - add location
String& setupFormBuilderDeviceStrings( String& htmlForm )
{
  String hostname = WiFi.hostname();
  int i;
    
  //UDP Discovery port 
  //Device instance number
  htmlForm = F("<div class=\"row\" id=\"topbar-strings\">");
  htmlForm += F("<div class=\"col-sm-12\">");
  htmlForm += F("<p>This device supports the <a href=\"placeholder\">ALPACA UDP discovery API</a> on port: ");
  htmlForm.concat( udpPort);
  
  //Links to setup pages for each implemented driver
  htmlForm += F("</p> <p> This device implements drivers with driver IDs : <ul><li>");
  //TODO - handle multiple GUIDs for multiple devices. 
  htmlForm.concat( GUID );
  htmlForm += F(" <a href=\"/setup/v1/covercalibrator/0/setup\"> setup cover 0 </a>");
  htmlForm += F("</li></ul></p></div></div>");
  
  htmlForm += "<div class=\"row\" id=\"discovery-port\" >";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new Discovery port number for device</h2>";
  htmlForm += "<form method=\"POST\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/udpport\">";
  htmlForm += "<label for=\"udpport\" id=\"udpport\"> Port number to use for Management API discovery </label>";
  htmlForm += "<input type=\"number\" name=\"udpport\" min=\"1024\" max=\"65535\" ";
  htmlForm += "value=\"";
  htmlForm.concat( udpPort );
  htmlForm += "\"/>";
  htmlForm += "<input type=\"submit\" value=\"Set port\" />";
  htmlForm += "</form></div></div>"; 
  
  //Device settings - hostname 
  htmlForm += "<div class=\"row float-left\" id=\"set-hostname\">";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new hostname for device</h2>";
  htmlForm += "<p>Changing the hostname will cause the device to reboot and may change the IP address!</p>";
  htmlForm += "<form method=\"POST\" id=\"hostname\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/hostname\">";
  htmlForm += "<label for=\"hostname\" > Hostname </label>";
  htmlForm += "<input type=\"text\" name=\"hostname\" maxlength=\"";
  htmlForm.concat( MAX_NAME_LENGTH );
  htmlForm += "\" value=\"";
  htmlForm.concat( myHostname );
  htmlForm += "\"/>";
  htmlForm += "<input type=\"submit\" value=\"Set hostname\" />";
  htmlForm += "</form></div></div>";

  //Device settings - Location 
  htmlForm += "<div class=\"row float-left\">";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new descriptive location for device</h2>";
  htmlForm += "<form method=\"POST\" id=\"location\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/location\">";
  htmlForm += "<label for=\"location\" >Location description </label>";
  htmlForm += "<input type=\"text\" class=\"form-control\" name=\"location\" maxlength=\"";
  htmlForm += MAX_NAME_LENGTH;
  htmlForm += "\" value=\"";
  htmlForm.concat( Location );
  htmlForm += "\"/>";
  htmlForm += "<input type=\"submit\" value=\"Set location\" />";
  htmlForm += "</form></div></div>";

  return htmlForm;
}

String& setupFormBuilderDriver0Limits( String& htmlForm )
{
  String hostname = WiFi.hostname();
  int i = 0;
/*  
  htmlForm = "<div class=\"row float-left\" id=\"placeholder-flapcount\" >";
  htmlForm += "<div class=\"col-sm-12\" ><h2>Placeholder for flapcount </h2></div>";
  htmlForm += "</div>";
DEBUGSL1( htmlForm .c_str() );
*/

#define HTMLCOUNT
#ifdef HTMLCOUNT
#pragma warning "HTMLCOUNT defined"
  //Device settings - Number of flaps to open - up to 16 handled by Servo board. 
  htmlForm = "<div class=\"row float-left\" id=\"flapcount\">";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new flap number for device</h2>";
  htmlForm += "<form class=\"form-inline\" method=\"POST\" id=\"flapCount\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/v1/covercalibrator/0/setup/flapcount\">";

  htmlForm += "<div class=\"form-group\">";
  htmlForm += "<label for=\"flapCount\" > Number of flaps in cover: </label>";
  htmlForm += "<input type=\"number\" class=\"form-control\" size=\"6\" name=\"flapCount\" min=\"1\" max=\"";
  htmlForm.concat( MAX_SERVOS );
  htmlForm += "\" value=\"";
  htmlForm.concat( flapCount );
  htmlForm += "\"/>";
  htmlForm += "<input type=\"submit\" class=\"btn btn-default\" value=\"Set flap count\" />";
  htmlForm += "</div></form>";
  htmlForm += "</div></div>";

  DEBUGSL1( htmlForm.c_str() );
#endif 

/*
  htmlForm += "<div class=\"row float-left\" id=\"placeholder-flaplimits\">";
  htmlForm += "<div class=\"col-sm-12\"> <h2>Placeholder for flaplimits </h2></div>";
  htmlForm += "</div>";
DEBUGSL1( htmlForm.c_str() );
*/

  //Device settings - opening minlimit and maxlimit 
  htmlForm += F("<div class=\"row float-left\" id=\"flaplimits\">");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Enter new opener arm limits for device</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"POST\" id=\"limits\" action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/setup/v1/covercalibrator/0/setup/limits\"> ");

#define HTMLLIMITS  
#ifdef  HTMLLIMITS
  String local = "";

  for (i = 0; i< flapCount; i++ )
  {    
    local += "<div class=\"form-group\">";
    //Update the min swing limit ( closed) 
    local += "<label for=\"minLimit";
    local.concat( i );
    local += "\">Switch [";
    local.concat( i );  
    local.concat( "]: Closed limit: &nbsp; </label>");
    local += "<input type=\"number\" size=\"6\" maxlength=\"6\"  name=\"minLimit";
    local.concat( i ); 
    local += "\" min=\"";
    local.concat( rcMinLimit );
    local += "\" max=\"";
    local.concat( rcMaxLimit );
    local += "\" value=\"";
    local.concat( flapMinLimit[i] );
    local += "\">";
    
    //Update the max swing limit ( open ) 
    local += "<label for=\"maxLimit";
    local.concat( i ); 
    local += "\" >&nbsp; Opening limit: </label>"; 

    local += "<input type=\"number\" size=\"6\" name=\"maxLimit";
    local.concat( i ); 
    local += "\" min=";
    local.concat ( rcMinLimit );
    local += "\" max=\"";
    local.concat( rcMaxLimit );
    local += "\" value=\"";
    local.concat( flapMaxLimit[i] );
    local += "\"><br>";
    local += "</div>"; //form-group
  }
#endif   
  local += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Update limits\" /> </form></div></div>");
  DEBUGSL1( local );
  htmlForm += local;
  
  return htmlForm;
}

String& setupFormBuilderDriver0Positions( String& htmlForm) 
{
  //add a maximum brightness value ?
  //Device settings - brightness 
  /*
   *<div class="row float-left" id="positions"> 
      <div class="col-sm-12"> <h2> Enter new flap positions</h2><br>
        <form class="form-inline" method="POST"  action="http://myHostname/setup/v1/covercalibrator/0/setup/positions" >
            <div class="form-group">
              <label for="position0"> Position[0]: </label>
              <input type="number" class="form-control" name="brightness" min="0" max="180" value="5" size="6">
              <label for="position1"> Position[1]: </label>
              <input type="number" class="form-control" name="brightness" min="0" max="180" value="5" size="6">
              <label for="position2"> Position[2]: </label>
              <input type="number" class="form-control" name="brightness" min="0" max="180" value="5" size="6">
              <label for="position3"> Position[3]: </label>
              <input type="number" class="form-control" name="position" min="0" max="180" value=".." size="6">
              etc
              <input type="submit" class="btn btn-default" value="Set positions" />
            </div>
        </form>
      </div>
    </div>
   */

  htmlForm = F("<div class=\"row float-left\" id=\"positions\">");
  htmlForm += F("<div class=\"col-sm-12\"><h2>Set Cover State</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"POST\"  action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/setup/v1/covercalibrator/0/setup/position\">");

  String local = "";
  int i = 0;

#if defined HANDLER_MANUAL_FLAP_CONTROL
  for (i = 0; i< flapCount; i++ )
  {    
    local += "<div class=\"form-group\">";
    //Update the current position 
    local += "<label for=\"position";
    local.concat( i );
    local += "\">Position[";
    local.concat( i );  
    local.concat( "]: </label>");
    local += "<input type=\"number\" maxlength=\"6\" size=\"6\" name=\"position";
    local.concat( i ); 
    local += "\" min=\"";
    local.concat( flapMinLimit[i] );
    local += "\" max=\"";
    local.concat( flapMaxLimit[i] );
    local += "\" value=\"";
    local.concat( flapPosition[i] );
    local += "\">";   
    local += "</div>"; //form-group
  }
#else
    local = "<div class=\"form-group\">";
    //set the default selected button to be the opposite of current state. 
    if( coverState == CoverStatus::Open ) 
      local += "<input type=\"radio\" id=\"position1\" name=\"coverposition\" value=\"Open\" >";
    else 
      local += "<input type=\"radio\" id=\"position1\" name=\"coverposition\" value=\"Open\" selected>";
    local += "<label for=\"position1\">Open cover    </label><br>";

    if ( coverState == CoverStatus::Closed ) 
      local += "<input type=\"radio\" id=\"position2\" name=\"coverposition\" value=\"Close\">";
    else
      local += "<input type=\"radio\" id=\"position2\" name=\"coverposition\" value=\"Close\" selected >";

    local += "<label for=\"position2\">Close cover   </label><br>";
    local += "<input type=\"radio\" id=\"position3\" name=\"coverposition\" value=\"Abort\">";
    local += "<label for=\"position3\">Abort moving  </label><br>";
    local += "</div>"; //form-group
#endif   

  htmlForm += local;   
  htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Set cover position\" />");
  htmlForm += F("</form></div></div>");
  return htmlForm;
}
 
String& setupFormBuilderDriver0Brightness( String& htmlForm )
{
  //add a maximum brightness value ?
  //Device settings - brightness 
  /*
   *<div class="row float-left" id="brightnessfield"> 
      <div class="col-sm-12"> <h2> Enter new brightness</h2><br>
        <form class="form-inline" method="POST"  action="http://myHostname/setup/v1/covercalibrator/0/setup/brightness" >
            <div class="form-group">
              <label for="brightness"> Brightness: </label>
              <input type="number" class="form-control" name="brightness" min="0" max="16" value="5" >
              <input type="submit" class="btn btn-default" value="Set brightness" />
            </div>
        </form>
      </div>
    </div>
   */

  htmlForm =  F("<div class=\"row float-left\" id=\"brightnessfield\" >");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Set Calibrator state</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"POST\"  action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/setup/v1/covercalibrator/0/setup/brightness\">");

  htmlForm += F("<div class=\"form-group\">");
  htmlForm += F("<label for=\"brightness\" > Brightness: </label>");
  htmlForm += F("<input type=\"number\" class=\"form-control\" name=\"brightness\" size=\"6\" min=\"");
  htmlForm.concat ( 0 );
  htmlForm += F("\" max=\"");
  htmlForm.concat( MAXDIGITALVALUE );
  htmlForm += F("\" value=\"");
  htmlForm.concat( String( brightness ) );
  htmlForm += F("\"/><br> ");

  //Turn on or off the calibrator lamp. 
  htmlForm += "<input type=\"radio\" id=\"calibrator1\" name=\"calibratorstate\" value=\"On\">";
  htmlForm += "<label for=\"calibrator1\">&nbsp; Turn on Calibrator</label><br>";
  
  htmlForm += "<input type=\"radio\" id=\"calibrator2\" name=\"calibratorstate\" value=\"Off\">";
  htmlForm += "<label for=\"calibrator2\">&nbsp; Turn off calibrator</label><br>";
  htmlForm += "</div>"; //form-group
  
  htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Set Calibrator state\" />");
  htmlForm += F("</form>");
  htmlForm += F("</div></div>");

  return htmlForm;
}
//footer
String& setupFormBuilderFooter( String& htmlForm )
{
  //restart button
  htmlForm =  F("<div class=\"row float-left\" id=\"restartField\" >");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Restart device</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"POST\"  action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/restart\">");
  htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Restart device\" />");
  htmlForm += F("</form>");
  htmlForm += F("</div></div>");
  
  //Update button
  htmlForm += F("<div class=\"row float-left\" id=\"updateField\" >");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Update firmware</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"GET\"  action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/update\">");
  htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Update firmware\" />");
  htmlForm += F("</form>");
  htmlForm += F("</div></div>");

  //Close page
  htmlForm += F("<div class=\"row float-left\" id=\"positions\">");
  htmlForm += F("<div class=\"col-sm-12\"><br> </div></div>");
  htmlForm += F("</div></body></html>");
  return htmlForm;
}
#endif
