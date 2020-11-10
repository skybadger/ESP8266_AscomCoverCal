/*
ESP8266_coverhandler.h
This is a firmware application to implement the ASCOM ALPACA cover calibrator API.
Each device can manage one RC based flap and a power switch to turn on or off a calbrator light source
Each device can be 1 of 2 types - binary switch or digital pwm intensity control.
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

#ifndef _ESP8266_COVERCAL_H_
#define _ESP8266_COVERCAL_H_

#include "CoverCal_eeprom.h"
#include <Wire.h>
#include "AlpacaErrorConsts.h"
#include "ASCOMAPISwitch_rest.h"

//Function definitions
bool getUriField( char* inString, int searchIndex, String& outRef );
String& setupFormBuilder( String& htmlForm, String& errMsg );

//Properties
void handlerMaxBrightness(void);
void handlerBrightness(void);
void handlerCoverState(void);
void handlerCalibratorState(void);

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
//The number of switch devices managed by this driver
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

    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "Brightness", Success , "" );    
    root["Value"] = brightness;
    
    root.printTo(message);
    server.send(200, F("application/json"), message);
    return ;
}

//GET ​/cover​/{device_number}​/maxbrightness
//The number of switch devices managed by this driver
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

    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "MaxBrightness", Success , "" );    
    root["Value"] = MAXDIGITALVALUE;
    targetCoverState = Open;
        
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

    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "CoverState", Success , "" );    
 
    root["Value"] = coverState;
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
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
    
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();
   
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "CalibratorState", Success , "" );    
 
    root["Value"] = calibratorState;
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
    int input;
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    if ( hasArgIC( argToSearchFor[2], server, false) )
      input = server.arg(argToSearchFor[2]).toInt();

    if( input >= 0 && input <= MAXDIGITALVALUE )
    {
      switch ( calibratorState )
      {
        case NotPresent:
          errCode = notImplemented;
          errMsg = "Calibrator not present";
          break; 
        case Off:
          //turn on -- do something
          //set lamp power or something...
          analogWrite( ELPIN, brightness );
          //Turn on timer for illuminator to stabilise
          //ets_arm_new
          calibratorState = NotReady;
          errCode = Success;
          errMsg = "";
          break; 
        case NotReady:
          errCode = Success;
          errMsg = "";
          //Let the timer turn it on when it will
          break;
        case Ready:
          //Timer should have turned this on. Unless brightness has changed. 
          errCode = Success;
          errMsg = "";    
        case Unknown:
        case Error:
        default:
          break;
      }
    }
    else
    {
       errCode = invalidValue;
       errMsg = "Value out of range";   
    }
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "handlerCalibratorOn", Success, "" );    
    
    root["ErrorMessage"]= errMsg;
    root["ErrorNumber"] = errCode;

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
    String argToSearchFor[] = { "clientID", "ClientTransactionID", "state" };
    bool input;
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();

    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    
    debugD( "targetState requested %i ", calibratorState );
    switch ( calibratorState )
    {
      case CalNotPresent:
        errCode = valueNotSet;
        errMsg = "not present";
        break; 
      case Off:
        //turn off -- do something
        analogWrite( ELPIN, 0);
        calibratorState = Off;
        break; 
      case NotReady:
        errCode = valueNotSet;
        errMsg = "not ready";
        break;
      case Ready:      
      case Unknown:
      case CalError:
      default:
        break;
    }
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "handleCalibratorOff", Success, "" );    
    
    root["ErrorMessage"]= errMsg;
    root["ErrorNumber"] = errCode;

    root.printTo(message);
    server.send(returnCode, F("application/json"), message);
    return;
}

//PUT
//Set the calibrator according to brightness
void handlerCloseCoverPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    int input;
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    errCode = Success;
    errMsg = "";   
  
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "handlerCalibratorOn", Success, "" );    
    
    //Set targetCoverState to desired state. 
    if ( connected != NOT_CONNECTED || connected != clientID )
    {
      errCode = notConnected;  
      errMsg = "Not connected";         
    }
    else if ( coverState == Closed )
    {
       errCode = invalidValue;
       errMsg = "Already closed";
    }
    else
    {
      targetCoverState = Closed;
    }

    root["ErrorMessage"]= errMsg;
    root["ErrorNumber"] = errCode;

    root.printTo(message);
    server.send(returnCode = 200, F("application/json"), message);
    return;
}

//PUT
//Set the calibrator according to brightness
void handlerHaltCoverPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    int input;
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    errCode = Success;
    errMsg = "";   
  
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "handlerCalibratorOn", Success, "" );    
    
    //Set targetCoverState to desired state. 
    if ( connected != NOT_CONNECTED || connected != clientID )
    {
      errCode = notConnected;  
      errMsg = "Not connected";         
    }
    else if ( coverState != Moving )
    {
       errCode = invalidValue;
       errMsg = "Not moving";
    }
    else
    {
      targetCoverState = Halted;
    }

    root["ErrorMessage"]= errMsg;
    root["ErrorNumber"] = errCode;

    root.printTo(message);
    server.send(returnCode = 200, F("application/json"), message);
    return;
}

//PUT
//Set the calibrator according to brightness
void handlerOpenCoverPut(void)
{
    String message;
    String errMsg;
    int errCode = Success;
    int returnCode = 200;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    int input;
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
      
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();

    errCode = invalidValue;
    errMsg = "Value out of range";   
  
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "handlerCalibratorOn", Success, "" );    
    
    //Set targetCoverState to desired state. 
    //Set targetCoverState to desired state. 
    if ( connected != NOT_CONNECTED || connected != clientID )
    {
      errCode = notConnected;  
      errMsg = "Not connected";         
    }
    else if ( coverState == Open )
    {
       errCode = invalidValue;
       errMsg = "Alredy open";
    }
    else
    {
      targetCoverState = Open;
    }
    
    root["ErrorMessage"]= errMsg;
    root["ErrorNumber"] = errCode;

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
  jsonResponseBuilder( root, clientID, transID, ++serverTransID, "HandlerNotFound", invalidOperation , "No REST handler found for argument - check ASCOM Switch v2 specification" );    
  root["Value"] = 0;
  root.printTo(message);
  server.send(responseCode, "application/json", message);
}

void handlerRestart()
{
  String message;
  int responseCode = 200;
  message.concat( "restarting on user request" );
  server.send(responseCode, "text/plain", message);
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
  jsonResponseBuilder( root, clientID, transID, ++serverTransID, "HandlerNotFound", notImplemented  , "No REST handler implemented for argument - check ASCOM Dome v2 specification" );    
  root["Value"] = 0;
  root.printTo(message);
  server.send(responseCode, "application/json", message);
}

//GET ​/switch​/{device_number}​/status
//Get a descriptor of all the switches managed by this driver for discovery purposes
void handlerStatus(void)
{
    String message, timeString;
    int i=0;
    int returnCode = 400;

    uint32_t clientID= -1;
    uint32_t transID = -1;
    String argToSearchFor[] = { "clientID", "ClientTransactionID", };
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      clientID = server.arg(argToSearchFor[0]).toInt();
    
    if ( hasArgIC( argToSearchFor[1], server, false) )
      transID = server.arg(argToSearchFor[1]).toInt();
    
    DynamicJsonBuffer jsonBuffer(512);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, ++serverTransID, "Status", 0, "" );    
    
    root["time"] = getTimeAsString( timeString );
    root["host"] = myHostname;
    root["connected"] = (connected == NOT_CONNECTED )?"true":"false";
    root["clientID"] = connected;
    root["coverState"] = coverState;
    if( coverState != NotPresent )
    {
      root["position"] = 0;
    }
    root["calibratorState"]  = calibratorState;
    if( calibratorState != NotPresent ) 
    {
      root["brightness"] = brightness;
    }
    
    Serial.println( message);
    //Don't use prettyprint for a bunch of json parsers - they don't like it. 
    root.printTo(message);
    server.send(returnCode, "application/json", message);
    return;
}

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 */
 void handlerSetup(void)
 {
   String message, timeString, err= "";
    uint32_t switchID = -1;   
    int returnCode = 400;
    int i=0;

    if ( server.method() == HTTP_GET )
    {
        message = setupFormBuilder( message, err );      
        server.send( returnCode, "text/html", message ); 
    }
 }
 
 /*
  * Handler to update the hostname from the form.
  */
 void handlerSetupHostname(void) 
 {
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("clientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
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
          }
          else
             err = "New name too long";

          message = setupFormBuilder( message, err );      
          returnCode = 200;    
          saveToEeprom();
          server.send(returnCode, "text/html", message);
          device.reset();
        }
    }
    else
    {
      returnCode=400;
      err = "Bad HTTP request verb";
      message = setupFormBuilder( message, err );      
    }
    server.send(returnCode, "text/html", message);
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
String& setupFormBuilder( String& htmlForm, String& errMsg )
{
  String hostname = WiFi.hostname();
  
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
 
  htmlForm = "<!DocType html><html lang=en ><head><meta charset=\"utf-8\">";
  htmlForm += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  htmlForm += "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\">";
  htmlForm += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script>";
  htmlForm += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\"></script>";
  htmlForm += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\"></script>";
  
//CSS Style settings
  htmlForm += "<style>\
legend { font: 10pt;}\
h1 { margin-top: 0; }\
form { margin: 0 auto; width: 500px;padding: 1em;border: 1px solid #CCC;border-radius: 1em;}\
div+div { margin-top: 1em; }\
label span{display: inline-block;width: 150px;text-align: right;}\
input, textarea {font: 1em sans-serif;width: 150px;box-sizing: border-box;border: 1px solid #999;}\
input[type=checkbox], input[type=radio], input[type=submit]{width: auto;border: none;}\
input:focus,textarea:focus{border-color:#000;}\
textarea {vertical-align: top;height: 5em;resize: vertical;}\
fieldset {width: 410px;box-sizing: border-box;border: 1px solid #999;}\
button {margin: 20px 0 0 124px;}\
label {position:relative;}\
label em { position: absolute;right: 5px;top: 20px;}\
</style>";

  htmlForm += "</head>";
  htmlForm += "<body><div class=\"container\">";
  htmlForm += "<div class=\"row\" id=\"topbar\" bgcolor='A02222'>";
  htmlForm += "<p> This is the setup page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> Switch device '";
  htmlForm += myHostname;
  htmlForm += "' which uses the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b>";
  htmlForm += "</div>";

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += "<div class=\"row\" id=\"errorbar\" bgcolor='lightred'>";
    htmlForm += "<b>Error Message</b>";
    htmlForm += "</div>";
    htmlForm += "<hr>";
  }
 
/*
<body>
<div class="container">
<div class="row" id="topbar" bgcolor='A02222'><p> This is the setup page for the Skybadger <a href="https://www.ascom-standards.org">ASCOM</a> Switch device 'espASW01' which uses the <a href="https://www.ascom-standards.org/api">ALPACA</a> v1.0 API</b></div>

<!--<div class="row" id="udpDiscoveryPort" bgcolor='lightblue'>-->
<p> This device supports the <a href="placeholder">ALPACA UDP discovery API</a> on port: 32227 </p>
<p> It is not yet configurable.</p>
<!-- </div> -->
*/

  //UDP Discovery port 
  htmlForm += "<p> This device supports the <a href=\"placeholder\">ALPACA UDP discovery API</a> on port: ";
  htmlForm += udpPort;
  htmlForm += "</p> <p> It is not yet configurable.</p>";

  //Device instance number
  
//<div class="row">
//<h2> Enter new hostname for device</h2><br>
//<p>Changing the hostname will cause the device to reboot and may change the IP address!</p>
//</div>
//<div class="row float-left" id="deviceAttrib" bgcolor='blue'>
//<form method="POST" id="hostname" action="http://espASW01/sethostname">
//<label for="hostname" > Hostname </label>
//<input type="text" name="hostname" maxlength="25" value="espASW01"/>
//<input type="submit" value="Update" />
//</form>
//</div>

  //Device settings hostname and number of switches on this device
  htmlForm += "<div class=\"row\">";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new hostname for device</h2><br/></div>";
  htmlForm += "<p>Changing the hostname will cause the device to reboot and may change the IP address!</p></div>";
  htmlForm += "<div class=\"row float-left\" id=\"deviceAttrib\" bgcolor='blue'>\n";
  htmlForm += "<form method=\"POST\" id=\"hostname\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/hostname\">";
  htmlForm += "<input type=\"text\" name=\"hostname\" maxlength=\"25\" value=\"";
  htmlForm.concat( myHostname );
  htmlForm += "\"/>";
  htmlForm += "<label for=\"hostname\" > Hostname </label>";
  htmlForm += "<input type=\"submit\" value=\"Update\" />";
  htmlForm += "</form></div>";
  
/*
</form>
</div>
</body></html>
*/
//Add a friendly device name
//add a maximum brightness value ?

  htmlForm += "</div></body></html>";
  return htmlForm;
}
#endif
