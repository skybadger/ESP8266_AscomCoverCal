<h1>ESP8266_ASCOMCoverCal</h1>
This application provides a remote ASCOM-enabled Telescope cover and calibrator device for use in Astronomical observatories and instrument setups. 

This instance runs on the ESP8266-01 wifi-enabled SoC device to provide remote REST access and control of the opening device and optionally an illuminator device to enable flat-fields, or alterntaively to drive a heater for the associated mirror or lens. 
This driver is closely related to the ASCOM Switch driver. 
The device reports system health to the central MQTT service and supports OTA updates using the http://<hostname>/update interface.

The device implements simple client (STATION) WiFi, including setting its hostname in the local network DNS and requires use of local DHCP services to provide a device IPv4 address, naming and network resolution services, and NTP time services. 

The unit provides a RC servo PWM signal to control a high power RC servo as the flap opener. Power to the servo is separately controlled on a separate signal to allow the servo to be turned off when fully open or closed. 
The unit also uses a PWM signal in 'real' PWM model to drive the brightness of any illuminator or heater present, controlling a high side current source at the voltage of the servo. 

<h2>Dependencies:</h2>
<ul><li>Arduino 1.86 IDE, </li>
<li>ESP8266 V2.6+ in lwip1.4 low bandwidth mode</li>
<li>Arduino MQTT client (https://pubsubclient.knolleary.net/api.html)</li>
<li>Arduino JSON library (pre v6) </li>
<li>RemoteDebug library </li>
<li>Common Alpaca and Skybadger helper files </li>
</ul>

<h3>Testing</h3>
In PCS9685 mode - there are no spare pins since even Tx is used to control the OE of the servo controller. 
In single servo mode, the Tx pin is free for use as serial debug output. 
Read-only monitoring by serial port - Tx only is available from device at 115,600 baud (8n1) at 3.3v. This provides debug monitoring output via Putty or another com terminal.
Wifi is used for MQTT reporting only and servicing REST API web requests
Use http://<hostname>/status to receive json-formatted output of current pins. 
Use the batch file to test direct URL response via CURL. 
Setup the ASCOM remote client and use the ASCOM script file ( Python or VBS) file to test response of the switch as an ASCOM device using the ASCOM remote interface. 

<h3>Use</h3>
Install latest <a href="https://www.ascom-standards.org/index.htm">ASCOM drivers </a> onto your platform. Add the <a href="https://www.ascom-standards.org/Developer/Alpaca.htm"> ASCOM ALPACA </a> remote interface.
Start the remote interface, configure it for the DNS name above on port 80 and select the option to explicitly connect.
ASCOM platfrom 6.5 adds some intelligence for auto-discovery which i am still working through but should be a good thing. 

Setup your device with a name and location and update the hostname as required. 
Specify what your local illuminator/heater can provide and the brightness steps it supports. 
Specify the open and close limits of the RC servo ( degrees as start, degrees as finish, ie 20 means its closed when issuing a .6ms RC servo pulse or therabouts. (assuming a range from 0.5 to 1.5 with a centre dead spot at 1.0 ) 

Use the custom setup Urls: 
<ul>
 <li>http://"hostname"/api/v1/switch/0/setup - web page to manually configure settings. </li>
 <li>http://"hostname"/api/v1/switch/0/status - json listing of current device status</li>
 <li>http://"hostname"/update                 - Update the firmware from your browser </li>
 <li>http://"hostname"/restart                - Reboot - sometimes required after a network glitch </li>
 </ul>
Once configured, the device keeps your settings through reboot and power outage by use of the onboard EEProm memory.

This driver supports Alpaca auto-discovery via the Ascom Alpaca mechanism on the standard port. 
<h3>ToDo:</h3>

 
<h3>Caveats:</h3> 
Currently there is no user access control on the connection to the web server interface. Anyone can connect. So use this behind a well-managed reverse proxy.
Also, note that the 'connected' settings is checked to ensure that the REST command is coming from a client who has previously called 'connected' and should effectively be in charge of the device from that point.

<h3>Structure:</h3>
This code pulls the source code into the file using header files inclusion. Hence there is an implied order, typically importing ASCOM headers last. At some point I will re-factor to look more like standard C compilation practice.

