
Echo "Testing driver info "
curl espacc01.i-badger.co.uk

REM "Connecting "
curl -X PUT -d "ClientID=99&ClientTransactionID=123&connected=true" "http://espacc01/api/v1/covercalibrator/0/connected"
timeout /t 2

echo "Testing basic driver information"
REM curl "http://espacc01/api/v1/covercalibrator/0/connected?ClientID=99&ClientTransactionID=123"
REM timeout /t 2
REM curl "http://espacc01/api/v1/covercalibrator/0/description?ClientID=99&ClientTransactionID=123"
REM timeout /t 2
REM curl "http://espacc01/api/v1/covercalibrator/0/driverinfo?ClientID=99&ClientTransactionID=123" 
REM timeout /t 2
REM curl "http://espacc01/api/v1/covercalibrator/0/name?ClientID=99&ClientTransactionID=123"
REM timeout /t 2
REM curl "http://espacc01/api/v1/covercalibrator/0/driverversion?ClientID=99&ClientTransactionID=123"
REM timeout /t 2
REM curl "http://espacc01/api/v1/covercalibrator/0/interfaceversion?ClientID=99&ClientTransactionID=123"
REM timeout /t 2
REM curl "http://espacc01/api/v1/covercalibrator/0/supportedactions?ClientID=99&ClientTransactionID=123"
REM timeout /t 2

Echo "Testing Custom actions"
REM curl -X PUT -d "ClientID=99&ClientTransactionID=123&thing=123" "http://espacc01/api/v1/covercalibrator/0/action"
REM timeout /t 2
REM curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/commandblind"
REM timeout /t 2
REM curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/commandbool"
REM timeout /t 2
REM curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/commandstring"
REM timeout /t 2

ECHO "Testing covercalibrator GET statements"
REM curl "http://espacc01/api/v1/covercalibrator/0/brightness?ClientID=99&ClientTransactionID=123"
REM timeout /t 2

REM curl "http://espacc01/api/v1/covercalibrator/0/maxbrightness?ClientID=99&ClientTransactionID=123"
REM timeout /t 2

REM curl "http://espacc01/api/v1/covercalibrator/0/coverstate?ClientID=99&ClientTransactionID=123"
REM timeout /t 2

REM curl "http://espacc01/api/v1/covercalibrator/0/calibratorstate?ClientID=99&ClientTransactionID=123"
REM timeout /t 2


ECHO "Testing put statements"
REM curl -X PUT -d "ClientID=99&ClientTransactionID=123&brightness=123" "http://espacc01/api/v1/covercalibrator/0/calibratoron"
REM timeout /t 2

REM curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/calibratoroff"
REM timeout /t 2

curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/closecover"
timeout /t 5

curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/haltcover"
timeout /t 2

curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espacc01/api/v1/covercalibrator/0/opencover"
timeout /t 5

REM "disconnecting"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&connected=false" "http://espacc01/api/v1/covercalibrator/0/connected"
timeout /t 2

pause

REM still to be tested
REM Non-ascom - yet to be tested 
REM status - works 
REM setup page
REM setupswitches
REM UDP discovery 