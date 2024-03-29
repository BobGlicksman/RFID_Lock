/*********************************************************************
 * solenoidBox
 * 
 * This program is used to control a solenoid that actuates a lock.
 * The program listens for a Particle IO event named "checkin". It then
 * examines the data of the event to see if this event should be acted upon.
 * In this first implementation it expect the event data to be a JSON package.
 * This program provides a Particle Cloud function to configure the particular
 * device number it will respond to.
 * 
 * Checkin event Base format
 *      {“deviceType”:105, "secret":12345}
 * Extended format
 *      { “deviceType”:105, 
 *        “Modifiers”: 
 *           { 
 * 	         “Mod1”:”zyx”,
 *   	     “Mod2”: “abcs, wxy, 87r”,
 *	         “Mod3”: 5678
 * 	        }
 *       }
 *      The “Modifiers” section is to be determined as specific protocol between an RFID 
 *      station type and the lockboxes that are intended to respond. Mod1, etc are just placeholders.
 * 
 * The device will check that the "secret" is equal to checkinEventSecret. This assures the lock 
 * device that the checkin event was published by an authentic RFID station.
 * 
 * 
 * The configuration function is:
 *      setLockListenDevType(param1)
 *      where param1 is a string representation of the device type to listen for.
 * 
 * For logging purposes these devices also have an MN Device Type set through
 *      cloudSetDeviceType(String data)
 *      
 * Hints on reading this code ...
 * The action starts when we receive a "checkin" event from the Particle Cloud. That is stored
 * in a global and the main loop will see it and act on it.
 * 
 * PRE RELEASE DEVELOPMENT LOG
 * 
 * (c) 2020; Team Practical Projects
 * 
 * Authors: Bob Glicksman, Jim Schrempp
 * 
 *    0.1 copied over lockBox.ino, renamed the ...Action source files and changed 
 *        solenoidAction to be appropriate for the door lock solenoid we have.
 *    0.2 now tests checkin event for secret value that proves it came from a real RFID box
 *    0.3 uses new production compile directive in mnutils.h 
 *    0.4 added define in mnutils.h to prevent debugX events from publishing 
************************************************************************/
#define MN_FIRMWARE_VERSION 0.4
STARTUP(WiFi.selectAntenna(ANT_AUTO)); // if an external antenna is available, use it

// Our UTILITIES
#include "mnutils.h"
#include "solenoidAction.h"
#include "rfidkeys.h"

//----------- Global Variables

// holds event data for a received checkin event
// The loopLockbox() routine waits for this to be non null and then 
// handles the event. After handling event it sets this to the null
// string and its ready to go again.
String mg_checkinEventData = "";

String g_recentErrors = "";
String debug2 = "";
int debug3 = 0;
int debug4 = 0;
int debug5 = 0;

// ------------------   Forward declarations, when needed
//


// -------------------- UTILITIES -----------------------

template <size_t charCount>
void strcpy_safe(char (&output)[charCount], const char* pSrc)
{
    // Copy the string — don’t copy too many bytes.
    strncpy(output, pSrc, charCount);
    // Ensure null-termination.
    output[charCount - 1] = 0;
}

char * strcat_safe( const char *str1, const char *str2 ) 
{
    char *finalString = NULL;
    size_t n = 0;

    if ( str1 ) n += strlen( str1 );
    if ( str2 ) n += strlen( str2 );

    finalString = (char*) malloc( n + 1 );
    
    if ( ( str1 || str2 ) && ( finalString != NULL ) )
    {
        *finalString = '\0';

        if ( str1 ) strcpy( finalString, str1 );
        if ( str2 ) strcat( finalString, str2 );
    }

    return finalString;
}  


// ------------- firmwareupdatehandler
// Called by Particle OS when a firmware update is about to begin
//
void firmwareupdatehandler(system_event_t event, int data) {
    switch (data) {
    case firmware_update_begin:

        break;
    case firmware_update_complete:
        
        break;
    case firmware_update_failed:
        
        break;
    }
}

/***************
 * heartbeatLEDs
 * 
 * Called from Loop() 
 * 
 * Will heartbeat the D7 LED so we can tell that our code is alive
 * 
 */
void heartbeatLEDs() {
    
    static unsigned long lastBlinkTimeD7 = 0;
    static int ledState = HIGH;
    const int blinkInterval = 500;  // in milliseconds
    
    if (millis() - lastBlinkTimeD7 > blinkInterval) {
        
        if (ledState == HIGH) {
            ledState = LOW;
        } else {
            ledState = HIGH;
        }
        
        digitalWrite(ONBOARD_LED_PIN, ledState);
        lastBlinkTimeD7 = millis();
    
    }
    
}

// ---------- Reboot Manager ----------
// rebootSet()
//   Called in the main loop with a 0 parameter
//   Checks to see if a reboot is pending and if so, is it time to do the reboot.
// 
//   Pass in a number of milliseconds to wait for a reboot and it will not 
//   reboot but instead it will set the reboot timer.
//   
void rebootSet(long millisecondsToWait) {

    static unsigned long rebootTime = 4294967290;  // makes it so we don't reboot as soon as the system starts

    if (millisecondsToWait > 0) {

        //set the reboot time
        rebootTime = millis() + millisecondsToWait;
    
    } else if (millis() > rebootTime) {

        System.reset();

    }

}

// ------------- Set Device Type ---------
// Called with a number to set device type to determine behavior
// Valid values are in eDeviceConfigType
//
int cloudSetDeviceType(String data) {

    int deviceType = data.toInt();

    debugEvent("devtypeinput: " + String(deviceType));

    if (deviceType == -1) {
        buzzerGoodBeepOnce();
        return EEPROMdata.deviceType;
    } else if ((deviceType) or (data == "0")) {
        int oldType = EEPROMdata.deviceType;
        logToDB("DeviceTypeChange", String(deviceType),0,"","");
        EEPROMdata.deviceType = deviceType;
        EEPROMWrite(); // push the new devtype into EEPROM 
        digitalWrite(REJECT_LED, HIGH);
        // reset variable so mainloop does not change LCD display 
        // (I admit this is a bit of a hack, but we are going to reboot right away anyway and
        //  until we do, we are technically the oldType. The type only changes on reboot.)
        EEPROMdata.deviceType = oldType;  
        rebootSet(1000);
        return 0;
    }

    return 1;

}

// ------------- Set Lock Listen Type ---------
// Called with a number to set the device type that a lock box will act upon
// Valid values are in eDeviceConfigType
//
int cloudSetLockListenDevType(String data) {

    int lockListenType = data.toInt();

    debugEvent("lockListenType: " + String(lockListenType));

    if (lockListenType == -1) {
        // Return current lockListenType
        return EEPROMdata.lockListenType;
    } else if ((lockListenType) or (data == "0")) {
        int oldType = EEPROMdata.lockListenType;
        logToDB("ListenTypeChange", String(lockListenType),0,"","");
        EEPROMdata.lockListenType = lockListenType;
        EEPROMWrite(); // push the new devtype into EEPROM 
        // reset variable so mainloop does not change LCD display 
        // (I admit this is a bit of a hack, but we are going to reboot right away anyway and
        //  until we do, we are technically the old lockListenType. The lockListenType only changes on reboot.)
        EEPROMdata.lockListenType = oldType;  
        rebootSet(1000);
        return 0;
    }

    return 1;

}

// ------------- Cloud Trip Lock  ---------
// Called to open the lock as if a checkin event was received
// The parameter is ignored.
//
int cloudTripLock(String data) {
    tripSolenoid();
    return 0;
}



//--------------- particleEventcheckin --------------------
// 
// This routine  is registered with the particle cloud to receive any
// events that come from a device in this Particle account and 
// named "checkin"
//
void particleCallbackEventCheckin (const char *event, const char *data) {
    // hold off on debugEvent publishing while callback is active
    allowDebugToPublish = millis() + 20000;

    String eventName = String(event);
    
    // NOTE: NEVER call particle publish (incuding debugEvent) from any
    // routine called from here. Your Particle  processor will  panic

    if (eventName.indexOf("checkin") >= 0) {

        if (mg_checkinEventData.length() > 0) {
            //we have not processed the previous checkin event
            debugEvent("checkin buffer overrun");
        } else {
            mg_checkinEventData = String(data);
        }

    } else {
        // only here for debugging. In fact, there could be events that are not "checkin" and we should not care
        // debugEvent("unknown particle event 1: " + String(event));

    }

    allowDebugToPublish = millis() + 500;
    
        
}

void eventcheckin(String data) {

    const int capacity = JSON_OBJECT_SIZE(8) + 2*JSON_OBJECT_SIZE(8);
    StaticJsonDocument<capacity> docJSON;
   
    char temp[1000]; //This has to be long enough for an entire JSON response
    strcpy_safe(temp, data);

    // will it parse?
    DeserializationError err = deserializeJson(docJSON, temp );
    JSONParseError =  err.c_str();
    if (!err) {
        //We have valid full JSON response 
        int secret = docJSON["secret"];
        if (secret != checkinEventSecret ) {
            // secret did not validate, the event publisher is a fraud 
            debugEvent("received checkin with bad secret:" + String(secret));
        } else {
            // secret value was correct
            if (docJSON["deviceType"] == EEPROMdata.lockListenType) {

                tripSolenoid();
                logToDB("Unlock", "Unlocking based on checkin of devType: " 
                        +  String((int) EEPROMdata.deviceType), 0, "", "");
                debugEvent("Unlocking based on checkin of devType: " 
                        +  String((int) EEPROMdata.deviceType) );

            } else {
                // Not the device type we are looking for
                debugEvent("received checkin event, not my devType");
            }
        }
   
    } else {
        debugEvent ("checkin event JSON parse error. " + String(temp));
    }

}





//--------------- particleCallbackMNLOGDB --------------------
// 
// This routine  is registered with the particle cloud to receive any
// events that begin with this device and "mnlogdb" or "fdb". This routine will accept
// the callback and then call the appropriate handler.
//
void particleCallbackMNLOGDB (const char *event, const char *data) {

   // hold off on debugEvent publishing while callback is active
    allowDebugToPublish = millis() + 20000;

    String eventName = String(event);
    String myDeviceID = System.deviceID();
    
    // NOTE: NEVER call particle publish (incuding debugEvent) from any
    // routine called from here. Your Particle  processor will  panic

    if (eventName.indexOf(myDeviceID + "fdbGetStationConfig") >= 0) {

        fdbReceiveStationConfig(event, data);

    } else {

        debugEvent("unknown particle event 2: " + String(event));

    }

    allowDebugToPublish = millis() + 500;
    
}




// ----------------- clearStationConfig
//
//
//
void clearStationConfig() {

    g_stationConfig.isValid = false;
    g_stationConfig.deviceType = 0;
    g_stationConfig.deviceName = "";
    g_stationConfig.LCDName = "";
    g_stationConfig.photoDisplay = "";
    g_stationConfig.logEvent = "";
    g_stationConfig.OKKeywords = "";

}

// --------------------- fdbGetStationConfig ------------
//
//
//
void fdbGetStationConfig() {

    clearStationConfig();
    String output = String(EEPROMdata.deviceType);
    Particle.publish("fdbGetStationConfig", output, PRIVATE);

}

// ----------------- fdbReceiveStationConfig ----------
//
//  fdbReceiveStationConfig
//
void fdbReceiveStationConfig(const char *event, const char *data) {

    const int capacity = JSON_OBJECT_SIZE(8) + 2*JSON_OBJECT_SIZE(8);
    StaticJsonDocument<capacity> docJSON;
   
    char temp[3000]; //This has to be long enough for an entire JSON response
    strcpy_safe(temp, data);

    // will it parse?
    DeserializationError err = deserializeJson(docJSON, temp );
    JSONParseError =  err.c_str();
    if (!err) {
        JsonObject root_0 = docJSON[0];
        //We have valid full JSON response 
        if (root_0["deviceType"].as<int>() == EEPROMdata.deviceType) {

            int devType = root_0["deviceType"].as<int>();
            String deviceName =  root_0["deviceName"].as<char*>();
            String LCDName = root_0["LCDName"].as<char*>();
            String logEvent = root_0["logEvent"].as<char*>();
            String photoDisplay = root_0["photoDisplay"].as<char*>();
            String OKKeywords = root_0["OKKeywords"].as<char*>();

            setStationConfig(
                devType,
                deviceName,
                LCDName,
                logEvent,
                photoDisplay,
                OKKeywords
            );

        } else {
            debugEvent("Station Config deviceType does not match. Expected: " 
                    + String((int) EEPROMdata.deviceType) 
                    + " received: " 
                    + String(root_0["deviceType"].as<int>()) );
            // the admin loop will be waiting for the isValid member to be true.
        }
   
    } else {
        debugEvent ("Staion Config JSON parse error. " + String(temp));
        // the admin loop will be waiting for the isValid member to be true.
    }

}




/*************** FUNCTIONS FOR USE IN REAL CARD READ APPLICATIONS *******************************/




// ------------ Functions for the Admin app 
//
//  These functions are called by the Admin Android app
//




// ---------------------- LOOP FOR equipment station ---------------------
// ----------------------                  ----------------------
// This function is called from main loop when configured for any device except admin or checkin 
//
void loopLockbox() {    
    //WoodshopLoopStates
    enum llbxState {
        llbxINIT,
        llbxWAITFOREVENTDATA,
        llbxERROR
    };
    
    static llbxState llbxloopState = llbxINIT;
    //static unsigned long processStartMilliseconds = 0; 
    
    switch (llbxloopState) {
    case llbxINIT:
        llbxloopState = llbxWAITFOREVENTDATA;
        break;
    
    case llbxWAITFOREVENTDATA:{

        if (mg_checkinEventData.length() > 0) {

            eventcheckin(mg_checkinEventData);
            mg_checkinEventData = ""; // we handled the event, so clear it

        }

        // just stay in this state
        break;
    }
    case llbxERROR:
        break;

    default:
        break;
    } //switch (mainloopState)
    


}




// ---------------- loopUndefinedDevice ------
// This is called over and over from the main loop when the
// device is configured as type 0, undefined. This is most 
// likely when the device is first set up or is undergoing some 
// hardware debug.
void loopUndefinedDevice() {

    digitalWrite(READY_LED,HIGH);
    digitalWrite(ADMIT_LED,HIGH);
    digitalWrite(REJECT_LED,HIGH);

}



// --------------------- SETUP -------------------
// This routine runs only once upon reset
void setup() {

    Particle.variable ("recentErrors",g_recentErrors);
    Particle.variable ("JSONParseError", JSONParseError);
      
    int success = 0; 
    if (success) {}; // xxx to remove warning, but should we check this below?

    // The following are useful for debugging
    //Particle.variable ("RFIDCardKey", g_clientInfo.RFIDCardKey);
    //Particle.variable ("g_Packages",g_packages);
    //success = Particle.function("GetCheckInToken", ezfGetCheckInTokenCloud);
    // xxx
    //Particle.variable ("debug2", debug2);
    //Particle.variable ("debug3", debug3);
    //Particle.variable ("debug4", debug4);
    
    // Used by all device types
    success = Particle.function("SetDeviceType", cloudSetDeviceType);

    // used by lockbox
    success = Particle.function("SetLockListenType",cloudSetLockListenDevType);
    success = Particle.function("TripLock",cloudTripLock);
   
     // Needed for all devices
    Particle.subscribe(System.deviceID() + "mnlogdb",particleCallbackMNLOGDB, MY_DEVICES); // older
    Particle.subscribe(System.deviceID() + "fdb",particleCallbackMNLOGDB, MY_DEVICES); // newer
    Particle.subscribe("checkin",particleCallbackEventCheckin, MY_DEVICES); 

    System.on(firmware_update, firmwareupdatehandler);

    allowDebugToPublish = millis();  // Allows our particle publish routines to run now

    // initialize the lockAction library
    initSolenoidAction();

    // Gawd, dealing with dst!
    Time.zone(-8); //PST We are not dealing with DST in this device
    bool yesDST = false;
    if ( (Time.month() > 3) && (Time.month() < 11) ) {
        yesDST = true;
    }
    if ( (Time.month() == 3) && (Time.day() > 10 ) ) {
        yesDST = true;
    }
    if ( (Time.month() == 3) && (Time.day() == 10) && (Time.hour() > 2) ){
        yesDST = true;
    }
    if ( (Time.month() == 11) && (Time.day() < 3 ) ) {
        yesDST = true;
    }
    if ( (Time.month() == 11) && (Time.day() == 3) && (Time.hour() < 2) ){
        yesDST = true;
    }
    if (yesDST) {
        Time.beginDST();
        debugEvent("DST is set"); // xxx
    } 

    // read EEPROM data for device type 
    EEPROMRead();
  
    pinMode(ONBOARD_LED_PIN, OUTPUT);   // the D7 LED
    digitalWrite(ONBOARD_LED_PIN,LOW);
   
    logToDB("restart",String(MN_FIRMWARE_VERSION),0,"","" );
   
    // Signal ready to go
    //flash the D7 LED twice
    for (int i = 0; i < 2; i++) {
        digitalWrite(ONBOARD_LED_PIN, HIGH);
        delay(500);
        digitalWrite(ONBOARD_LED_PIN, LOW);
        delay(500);
    } 


}

// This routine gets called repeatedly, like once every 5-15 milliseconds.
// Spark firmware interleaves background CPU activity associated with WiFi + Cloud activity with your code. 
// Make sure none of your code delays or blocks for too long (like more than 5 seconds), or weird things can happen.
void loop() {

    // Main Loop State
    enum mlsState {
        mlsASKFORSTATIONCONFIG,
        mlsWAITFORSTATIONCONFIG,
        mlsDEVICELOOP, 
        mlsERROR
    };
    
    static mlsState mainloopState = mlsASKFORSTATIONCONFIG;
    static unsigned long processStart = 0;
    
    switch (mainloopState) {
    case mlsASKFORSTATIONCONFIG:
        if ((EEPROMdata.deviceType == DEVICETYPE_UNDEFINED) || (EEPROMdata.deviceType == DEVICETYPE_CHECKIN)) {
           
            // if type is undefined or checkin, then don't need config.
            if (EEPROMdata.deviceType == DEVICETYPE_UNDEFINED) {
                setStationConfig( DEVICETYPE_UNDEFINED, "Undefined","Undefined","Undefined","","");
            } else if (EEPROMdata.deviceType == DEVICETYPE_CHECKIN) {
                setStationConfig( DEVICETYPE_CHECKIN, "CheckIn", "Check In","Check In","","");
            } else {
                setStationConfig( 99999,"code error 1","code error 1","code error 1","","" );
            }

            mainloopState = mlsDEVICELOOP;

        } else {
            
            // type is other than undefined or checkin, get config.
            // Get config info from the Facility database
            processStart = millis();
            fdbGetStationConfig();
            mainloopState = mlsWAITFORSTATIONCONFIG;

        }

        break;

    case mlsWAITFORSTATIONCONFIG:
        if (millis() - processStart > 20000 ) {

            // Failed to get station config
            // xxx how to alert to this error??
            mainloopState = mlsERROR;

        } else if (g_stationConfig.isValid) {

            mainloopState = mlsDEVICELOOP;
        }
        // otherwise stay in this state 

        break;

    case mlsDEVICELOOP: {

        // Once the intial steps have occurred, the main loop will stay
        // in this state forever. It calls out to the correct device code and
        // the "loop" continues there

        // This version of software only supports the Lockbox
        
        // all other stations
        loopLockbox();
        
        break;
    }
    case mlsERROR: 
        mainloopState = mlsERROR; // No way out of this except a reboot
        break;

    default:
        // how to alert to this error
        mainloopState = mlsERROR;
        break;
    }
    
    heartbeatLEDs(); // heartbead on D7 LED 

    debugEvent("");  // need this to pump the debug event process

    // Reboot once a day.
    if ( (Time.hour() == 1) && (Time.minute() == 5)) {   
        // Since we only check the Minute, set the reboot to be at least one minute from now
        long rebootDelay = (int) random(70000,30000); // 70 seconds - 5 minutes
        rebootSet(rebootDelay); 
    }
    rebootSet(0); // check to see if a reboot should happen
}

