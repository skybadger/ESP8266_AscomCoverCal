/*
File to define the eeprom variable save/restor operations for the ASCOM switch web driver
*/
#ifndef _COVERCAL_EEPROM_H_
#define _COVERCAL_EEPROM_H_

#include "CoverCal_common.h"
#include "DebugSerial.h"
#include "eeprom.h"
#include "EEPROMAnything.h"

static const byte magic = '*';

//definitions
void setDefaults(void );
void saveToEeprom(void);
void setupFromEeprom(void);

/*
 * Write default values into variables - don't save yet. 
 */
void setDefaults( void )
{
  DEBUGSL1( "Eeprom setDefaults: entered");
  rcMinLimit = rcMinLimitDefault;
  rcMaxLimit = rcMaxLimitDefault;

  if ( myHostname != nullptr ) 
     free ( myHostname );
  myHostname = (char* )calloc( sizeof (char), MAX_NAME_LENGTH );
  strcpy( myHostname, defaultHostname);
  WiFi.hostname( myHostname );

  //MQTT thisID copied from hostname
  if ( thisID != nullptr ) 
     free ( thisID );
  thisID = (char*) calloc( MAX_NAME_LENGTH, sizeof( char)  );       
  strcpy ( thisID, myHostname );

  udpPort = ALPACA_DISCOVERY_PORT; 
  
  rcMinLimit = rcMinLimitDefault;
  rcMaxLimit  = rcMaxLimitDefault;
  rcPosition = initialPosition;
  brightness = 512;
  
   
  //#if defined DEBUG
  //Read them back for checking  - also available via status command.
  Serial.printf( "Hostname: %s \n" , myHostname );
  Serial.printf( "Discovery port: %i \n" , udpPort );
//#endif
  DEBUGSL1( "setDefaults: exiting" );
}

/*
 * Save current variables of interest into EEPROM
 */
void saveToEeprom( void )
{
  int eepromAddr = 4;
  DEBUGSL1( "savetoEeprom: Entered ");
   
  //UDP Port
  EEPROMWriteAnything( eepromAddr, udpPort );
  eepromAddr += sizeof(int);  
  DEBUGS1( "Written udpPort: ");DEBUGSL1( udpPort );

  //hostname
  EEPROMWriteString( eepromAddr, myHostname, MAX_NAME_LENGTH );
  eepromAddr += MAX_NAME_LENGTH;   
  DEBUGS1( "Written hostname: ");DEBUGSL1( myHostname );

  //Add min limit and max limit, turn on value. 
  EEPROMWriteAnything( eepromAddr, rcPosition );
  eepromAddr += sizeof(rcPosition );  
  EEPROMWriteAnything( eepromAddr, rcMinLimit );
  eepromAddr += sizeof(rcMinLimit);  
  EEPROMWriteAnything( eepromAddr, rcMaxLimit );
  eepromAddr += sizeof(rcMaxLimit);  
  EEPROMWriteAnything( eepromAddr, initialPosition );
  eepromAddr += sizeof(initialPosition );  
  EEPROMWriteAnything( eepromAddr, brightness );
  eepromAddr += sizeof(brightness );  
  
  DEBUGS1( "Written udpPort: ");DEBUGSL1( udpPort );
  
  //Magic number write for data write complete. 
  EEPROM.put( 0, magic );
  EEPROM.commit();
  DEBUGS1( "Wrote ");DEBUGS1(eepromAddr);DEBUGSL1( " bytes ");
   
  //Test readback of contents
  String input = "";
  char ch;
  for ( int i = 0; i < 600 ; i++ )
  {
    ch = (char) EEPROM.read( i );
    if ( ch == '\0' )
      ch = '~';
    if ( (i % 32 ) == 0 )
      input.concat( "\n\r" );
    input.concat( ch );
  }
  
  Serial.printf( "EEPROM contents after: \n %s \n", input.c_str() );
  DEBUGSL1( "saveToEeprom: exiting ");
}

void setupFromEeprom( void )
{
  int eepromAddr = 0;
    
  DEBUGSL1( "setUpFromEeprom: Entering ");
  byte myMagic = '\0';
  //Setup internal variables - read from EEPROM.
  myMagic = EEPROM.read( 0 );
  DEBUGS1( "Read magic: ");DEBUGSL1( (char) myMagic );
  
  if ( (byte) myMagic != magic ) //initialise eeprom for first time use. 
  {
    setDefaults();
    saveToEeprom();
    DEBUGSL1( "Failed to find init magic byte - wrote defaults & restarted.");
    device.restart();
    return;
  }    
    
  //UDP port 
  EEPROMReadAnything( eepromAddr = 4, udpPort );
  eepromAddr  += sizeof(int);  
  DEBUGS1( "Read UDPport: ");DEBUGSL1( udpPort );

  //hostname - directly into variable array 
  if( myHostname != nullptr )
    free( myHostname );
  myHostname = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
  EEPROMReadString( eepromAddr, myHostname, MAX_NAME_LENGTH );
  eepromAddr  += MAX_NAME_LENGTH * sizeof(char);  
  DEBUGS1( "Read hostname: ");DEBUGSL1( myHostname );

  EEPROMReadAnything( eepromAddr, rcPosition );
  eepromAddr += sizeof(rcPosition );  
  EEPROMReadAnything( eepromAddr, rcMinLimit );
  eepromAddr += sizeof(rcMinLimit);  
  EEPROMReadAnything( eepromAddr, rcMaxLimit );
  eepromAddr += sizeof(rcMaxLimit);  
  EEPROMReadAnything( eepromAddr, initialPosition );
  eepromAddr += sizeof(initialPosition ); 
  EEPROMReadAnything( eepromAddr, brightness );
  eepromAddr += sizeof( brightness ); 
  
  
  //Setup MQTT client id based on hostname
  if ( thisID != nullptr ) 
     free ( thisID );
  thisID = (char*) calloc( MAX_NAME_LENGTH, sizeof( char)  );       
  strcpy ( thisID, myHostname );
  DEBUGS1( "Read MQTT ID: ");DEBUGSL1( thisID );
  
  DEBUGSL1( "setupFromEeprom: exiting" );
}
#endif
