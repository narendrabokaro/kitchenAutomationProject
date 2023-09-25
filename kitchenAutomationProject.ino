/*************************************************************
  Filename - kitchenAutomationProjectV1 - NodeMCU version
  Description - Designed for my wife special requirment for kitchen switch/bulb automation.
  Requirement -
    Active Time frame between - 6AM to 7AM and 6PM to 9PM 
    1. Whenever switch is turned ON btw above time frame, the bulb turns ON for 30 minutes & then it turns OFF. So again if you want to turn it ON then you
       need to press the switch OFF and then ON.
    2. Work normally when switch is turned OFF. Irrespective of time left, the bulb has to be turned OFF.

  version - 2.0.0
  Updates/ Fixes [status]
  > Including motion sensor to catch the activities before closing the light, if found then increase the time

  Debug instructions -
  1> Always check the active hours for bathroom lighting. Standard time - 6pm to 7am
  2> Check for bathroom light duration in minute. means how long the bulb will glow once turned ON
 *************************************************************/
#include <FS.h>
#include <string.h>
#include <ThreeWire.h>  
#include <RtcDS1302.h>

// GPIO Pin configuration details
// ------------------------------
#define kitchenMotionSensor D0
#define kitchBulbRelay D1
#define kitchenBulbSwitch D6

// ThreeWire myWire(D4,D5,D2); // IO, SCLK, CE
ThreeWire myWire(D4,D5,D2);
RtcDS1302<ThreeWire> Rtc(myWire);
RtcDateTime currentTime;

// Basic configuration variables
char datestring[20];
// Tell whether LED bulb is On/ Off
boolean isKitchenLedOn = false;
long motionSensorStatus;
// Data logger file name
String fileName = "/logdata.txt";
// Change this value Accordlingly
int nonActiveHourDuration = 5;    // in minute
int activeHourDuration = 45;    // in minute
int kitchenLightOnDuration = nonActiveHourDuration;    // In minutes

// Check whether motion detected before closing the light and then we extend the alarm by 3 minute
bool isAlarmUpdated = false;

// To maintain the active alarms
struct alarm {
    int alarmId;
    int alarmType;  // 1 - Daily, 2 - Weekly, 3 - Monthly
    int isAlarmSet;
    // Flag indicate whether alarm triggered or not
    int isAlarmTriggered;
    int duration;
    char whomToActivate[25];

    int endTimeHour;
    int endTimeMinute;
    int endTimeSecond;
};

// Time for various comparision
struct Time {
    int hours;
    int minutes;
};

// Created this struct to maintain more than one alarms i.e. one for kitchen bulb
// 0 - kitchen timer
struct alarm activeAlarm[2];
#define countof(a) (sizeof(a) / sizeof(a[0]))

/*
Active Time frame between - 6AM to 7AM and 6PM to 9PM 
    1. Whenever switch is turned ON btw above time frame, the bulb turns ON for 30 minutes & then it turns OFF. So again if you want to turn it ON then you
       need to press the switch OFF and then ON.
    2. Work normally when switch is turned OFF. Irrespective of time left, the bulb has to be turned OFF.
*/

// Active hours
struct Time morningActiveStartTime = {6, 0};    // 6.00AM to 7.00AM
struct Time morningActiveEndTime = {7, 0};
struct Time eveningActiveStartTime = {18, 0};   // 6.00PM to 10.00PM 
struct Time eveningActiveEndTime = {22, 0};

// Indicate (boolean) if time if greater/less than given time
bool diffBtwTimePeriod(struct Time start, struct Time stop) {
   while (stop.minutes > start.minutes) {
      --start.hours;
      start.minutes += 60;
   }

   return (start.hours - stop.hours) >= 0;
}

// Set light ON duration in case time fall btw active hours
void checkActiveHours() {
    boolean morningActiveHour = diffBtwTimePeriod({currentTime.Hour(), currentTime.Minute()}, morningActiveStartTime) && diffBtwTimePeriod(morningActiveEndTime, {currentTime.Hour(), currentTime.Minute()});
    boolean eveningActiveHour = diffBtwTimePeriod({currentTime.Hour(), currentTime.Minute()}, eveningActiveStartTime) && diffBtwTimePeriod(eveningActiveEndTime, {currentTime.Hour(), currentTime.Minute()});

    if (morningActiveHour || eveningActiveHour) {
        // Keep the bulb On when its a active hour
        kitchenLightOnDuration = activeHourDuration;
    } else {
        // Default light duration
        kitchenLightOnDuration = nonActiveHourDuration;
    }
}

void printDateTime(const RtcDateTime& dt) {
    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u:%02u.%02u.%02u:"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
}

// Log the data into the file
void writeFile(char msg[20]) {
    File file = SPIFFS.open(fileName, "a");

    if (!file) {
      Serial.println("Error opening file for writing");
      return;
    }

    // Format the string
    char printString[50];
    printDateTime(currentTime);
    strcat(printString, datestring);
    strcat(printString, msg);

    int bytesWritten = file.print(printString);

    if (bytesWritten == 0) {
      Serial.println("File write failed");
      return;
    }

    file.close(); 
}

// Create a empty file with headers
void createFile() {
    File file = SPIFFS.open(fileName, "w");

    if (!file) {
      Serial.println("Error opening file for writing");
      return;
    }

    int bytesWritten = file.print("Date:Time:Location:Status\n");

    if (bytesWritten == 0) {
      Serial.println("File write failed");
      return;
    }

    file.close();
}

// Read the specified file
void readFile() {
    File file = SPIFFS.open(fileName, "r");

    if (!file) {
      Serial.println("Failed to open file for reading");
      return;
    }

    while (file.available()) {
      Serial.write(file.read());
    }

    file.close();
}

void fileSystemMount() {
    bool success = SPIFFS.begin();

    if (!success) {
      Serial.println("Error mounting the file system");
      return;
    }

    // If file not exists then create it first
    if (!SPIFFS.exists(fileName)) {
        Serial.println("Preparing a fresh file to write.");
        createFile();
    } else {
        Serial.print("Good, file exist .. lets read the file data");
        readFile();
    }
}

/*
* This function write data into two places - NodeMCU file system (for offline support) and for google sheet API
*/
void actionMessageLogger(String message) {
    Serial.println(message);
    printDateTime(currentTime);
}

// turnBulb("ON", "OutBulb")
void turnBulb(String action, String bulbLocation) {
    if (bulbLocation == "kitchenBulb") {
        // Turn ON the bulb by making relay LOW
        // Turn OFF the bulb by making relay HIGH
        digitalWrite(kitchBulbRelay, action == "ON" ? HIGH : LOW);
        actionMessageLogger(action == "ON" ? "KitchenBulb :: Turn ON the kitchen bulb" : "KitchenBulb :: Turn OFF the kitchen bulb");
    }
}

void unsetAlarm(int alarmId) {
    Serial.println("Removed the alarm");
    activeAlarm[alarmId].isAlarmSet = 0;
    // Set this flag so that next time, it can be set again
    activeAlarm[alarmId].isAlarmTriggered = 1;
}

int setHour(int providedHour) {
    return providedHour > 23 ? (providedHour - 24) : providedHour;
}

/*
  Description - Set the alarm
  alarmId Unique ID for each alarm
  alarmType [1 for Hour | 2 for Minute]
  duration Duration of alarm (i.e. For how long you want to set alarm)
*/
void updateAlarm(int alarmId, int alarmType, int duration) {
    if (activeAlarm[alarmId].alarmId == alarmId) {
        int tempMinute = 0;
        int tempHour = 0;
        // Set this flag so that next time, it can be set again
        activeAlarm[alarmId].isAlarmTriggered = 0;

        // alarmType for Hour
        if (alarmType == 1) {
            tempHour = activeAlarm[alarmId].endTimeHour + duration;
            activeAlarm[alarmId].endTimeHour = setHour(tempHour);
        }

        // alarmType for Minute
        if (alarmType == 2) {
            tempMinute = activeAlarm[alarmId].endTimeMinute + duration;
            if (tempMinute > 59) {
                activeAlarm[alarmId].endTimeMinute = tempMinute - 60;
                tempHour = activeAlarm[alarmId].endTimeHour + 1;
                activeAlarm[alarmId].endTimeHour = setHour(tempHour);
            } else {
                activeAlarm[alarmId].endTimeMinute = tempMinute;
            }
        }
    }
}

/*
  Description - Set the alarm
  alarmId Unique ID for each alarm
  alarmType [1 for Hour | 2 for Minute]
  duration Duration of alarm (i.e. For how long you want to set alarm)
*/
void setAlarm(int alarmId, int alarmType, int duration) {
    int tempMinute = 0;
    int tempHour = 0;

    // Bathroom Timer duration
    activeAlarm[alarmId].alarmId = alarmId;
    activeAlarm[alarmId].alarmType = alarmType;
    // tell that alarm is set
    activeAlarm[alarmId].isAlarmSet = 1;
    // Set this flag so that next time, it can be set again
    activeAlarm[alarmId].isAlarmTriggered = 0;
    activeAlarm[alarmId].duration = duration;

    // alarmType for Hour
    tempHour = currentTime.Hour() + duration;
    activeAlarm[alarmId].endTimeHour = alarmType == 1 ? setHour(tempHour) : currentTime.Hour();

    // alarmType for Minute
    tempMinute = currentTime.Minute() + duration;
    if (alarmType == 2) {
        if (tempMinute > 59) {
            activeAlarm[alarmId].endTimeMinute = tempMinute - 60;
            tempHour = currentTime.Hour() + 1;
            activeAlarm[alarmId].endTimeHour = setHour(tempHour);
        } else {
            activeAlarm[alarmId].endTimeMinute = tempMinute;
        }
    } else {
        activeAlarm[alarmId].endTimeMinute = currentTime.Minute();
    }

    activeAlarm[alarmId].endTimeSecond = currentTime.Second();
}

void matchAlarm() {
    // Match condition
    for (int i=0; i < 2; i++) {
        // Look for motion 5 min before alarm triggered.
        if (currentTime.Hour() == activeAlarm[i].endTimeHour && currentTime.Minute() >= activeAlarm[i].endTimeMinute && currentTime.Second() >= activeAlarm[i].endTimeSecond && !activeAlarm[i].isAlarmTriggered) {
            actionMessageLogger("matchAlarm :: Timer Matched - alarm triggered");
            activeAlarm[i].isAlarmSet = 0;
            activeAlarm[i].isAlarmTriggered = 1;

            // kitchenBulb handler - Check if current time reached the Off Timer and LED still ON
            if (activeAlarm[i].alarmId == 0) {
                // Turn OFF the kitchen bulb
                turnBulb("OFF", "kitchenBulb");
                // unset the 2nd alarm
                unsetAlarm(1);
                // Writting the file
                char msgString[] = "kit-Auto:OFF\n";
                writeFile(msgString);
            }

            if (activeAlarm[i].alarmId == 1) {
                Serial.println("Lookup period started");
                motionSensorStatus = digitalRead(kitchenMotionSensor);
                // Activity detected
                if (motionSensorStatus == HIGH) {
                  // AlarmId, AlarmType = mintue, Duration in mintue
                  // Increase the main alarm timeline by 5 minutes
                  updateAlarm(0, 2, 5);
                  // Increase the second alarm also by 5 min
                  updateAlarm(1, 2, 5);

                  Serial.println("UpdatAlarm called");
                }               
            }
        }
    }
}

void kitchen_control() {
    // when kitchen switch pressed = ON
    if (digitalRead(kitchenBulbSwitch) == LOW && !isKitchenLedOn) {
        if (activeAlarm[0].isAlarmSet == 0) {
            Serial.println("button pressed ON, setting alarm");
            // Check the active hours
            checkActiveHours();
            // First Alarm configured - alarmId, alarmType, duration,
            setAlarm(0, 2, kitchenLightOnDuration);
            // Setup the second alarm for lookup action - detect motion [Just before 3 minute before closing the light]
            // SetAlarm - alarmId, alarmType, duration
            setAlarm(1, 2, (kitchenLightOnDuration - 3));
            // Turn On the kitchen bulb
            turnBulb("ON", "kitchenBulb");
            // Set the flag true
            isKitchenLedOn = true;
            // Prepare the string for file writting
            char msgString[] = "kitchen:ON\n";
            // File writting activities
            writeFile(msgString);
        }
    }

    // when kitchen switch pressed = OFF
    if (digitalRead(kitchenBulbSwitch) == HIGH && isKitchenLedOn) {
        Serial.println("button pressed OFF");
        // Unset both the alarm
        unsetAlarm(0);
        unsetAlarm(1);
        isKitchenLedOn = false;
        turnBulb("OFF", "kitchenBulb");
        char msgString[] = "kitchen:OFF\n";
        writeFile(msgString);
        delay(100);
    }

    matchAlarm();
}

void rtcSetup() {
    Serial.print("compiled: ");
    Serial.print(__DATE__);
    Serial.println(__TIME__);

    Rtc.Begin();

    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    Serial.println();

    if (!Rtc.IsDateTimeValid()) {
        // Common Causes:
        //    1) first time you ran and the device wasn't running yet
        //    2) the battery on the device is low or even missing

        Serial.println("RTC lost confidence in the DateTime!");
        Rtc.SetDateTime(compiled);
    }

    if (Rtc.GetIsWriteProtected()) {
        Serial.println("RTC was write protected, enabling writing now");
        Rtc.SetIsWriteProtected(false);
    }

    if (!Rtc.GetIsRunning()) {
        Serial.println("RTC was not actively running, starting now");
        Rtc.SetIsRunning(true);
    }

    RtcDateTime now = Rtc.GetDateTime();
    if (now < compiled) {
        Serial.println("RTC is older than compile time!  (Updating DateTime)");
        Rtc.SetDateTime(compiled);
    } else if (now > compiled) {
        Serial.println("RTC is newer than compile time. (this is expected)");
    } else if (now == compiled) {
        Serial.println("RTC is the same as compile time! (not expected but all is fine)");
    }
}

void setup() {
    // Debug console
    Serial.begin(57600);

    // Initial setup
    pinMode(kitchBulbRelay, OUTPUT);
    pinMode(kitchenBulbSwitch, INPUT_PULLUP);

    // Setup the RTC mmodule
    rtcSetup();
    // File system mount process
    fileSystemMount();
    Serial.println("Setup :: Setup completed");
}

void loop() {
    currentTime = Rtc.GetDateTime();

    if (!currentTime.IsValid()) {
        // Common Causes:
        // the battery on the device is low or even missing and the power line was disconnected
        Serial.println("RTC lost confidence in the DateTime!");
    }

    // call all manual control i.e. switches, relay
    kitchen_control();

    delay(1000);
}