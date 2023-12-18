/*************************************************************
  Filename - kitchenAutomationProjectV1 - NodeMCU version
  Description - Designed for my wife special requirment for kitchen switch/bulb automation.
  Requirement -
    Active Time frame between - 6AM to 7AM and 6PM to 9PM 
    1. Whenever switch is turned ON btw above time frame, the bulb turns ON for 30 minutes & then it turns OFF. So again if you want to turn it ON then you
       need to press the switch OFF and then ON.
    2. Work normally when switch is turned OFF. Irrespective of time left, the bulb has to be turned OFF.

  Updates/ Fixes [status]
  > Include Fridge based automation also
  > Include double click switch behaviour to override the automation.

  Debug info -
  1> Always check the active hours for lighting. Standard time - 6pm to 7am
  2> Check for bathroom light duration in minute. means how long the bulb will glow once turned ON
 *************************************************************/
// For RTC module (DS1307) - Connected to I2C GPIOs (D1 and D2)
#include "RTClib.h"

// GPIO Pin configuration details
// ------------------------------
// D1 & D2 - Reserved for I2C enabled devices (DS1307 RTC)
#define buzzer D3
#define kitchBulbRelay D5
#define kitchenBulbSwitch D6
#define kitchenMotionSensor D7

// D1(SCL) and D2(SDA) allotted to RTC module DS1307
RTC_DS1307 rtc;
DateTime currentTime;

// Basic configuration variables
char datestring[20];

// Tell whether LED bulb is On/ Off
boolean isKitchenLedOn = false;
long motionSensorStatus;

// Change this value Accordlingly (All are in mintues)
int nonActiveHourDuration = 5;
int activeHourDuration = 30;
int shortAlarmIncrementDuration = 3;
int longAlarmIncrementDuration = 15;
int longLookUpPeriod = 5;
int shortLookUpPeriod = 2;
int kitchenLightOnDuration = nonActiveHourDuration;

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
struct Time morningActiveStartTime = {5, 0};    // 5.00AM to 7.00AM
struct Time morningActiveEndTime = {7, 0};
struct Time eveningActiveStartTime = {18, 0};   // 6.00PM to 10.30PM 
struct Time eveningActiveEndTime = {22, 30};

void BuzzerOn(String duration) {
  if (duration == "short") {
      digitalWrite(buzzer, HIGH);
      delay(500);
      digitalWrite(buzzer, LOW);
      delay(500);

      digitalWrite(buzzer, HIGH);
      delay(500);
      digitalWrite(buzzer, LOW);
      delay(500);
  } else {
      digitalWrite(buzzer, HIGH);
      delay(500);
      digitalWrite(buzzer, LOW);
      delay(500);

      digitalWrite(buzzer, HIGH);
      delay(500);
      digitalWrite(buzzer, LOW);
      delay(500);

      digitalWrite(buzzer, HIGH);
      delay(500);
      digitalWrite(buzzer, LOW);
      delay(500);
  }
}

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
    boolean morningActiveHour = diffBtwTimePeriod({currentTime.hour(), currentTime.minute()}, morningActiveStartTime) && diffBtwTimePeriod(morningActiveEndTime, {currentTime.hour(), currentTime.minute()});
    boolean eveningActiveHour = diffBtwTimePeriod({currentTime.hour(), currentTime.minute()}, eveningActiveStartTime) && diffBtwTimePeriod(eveningActiveEndTime, {currentTime.hour(), currentTime.minute()});

    if (morningActiveHour || eveningActiveHour) {
        // Keep the bulb On when its a active hour
        kitchenLightOnDuration = activeHourDuration;
        Serial.println("active hour found");
    } else {
        // Default light duration
        kitchenLightOnDuration = nonActiveHourDuration;
        Serial.println("No active hour found");
    }
}

void printDateTime(const DateTime& dt) {
    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u:%02u.%02u.%02u:"),
            dt.month(),
            dt.day(),
            dt.year(),
            dt.hour(),
            dt.minute(),
            dt.second() );
    Serial.print(datestring);
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
  Description - Update the alarm
  Only update the 
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

        activeAlarm[alarmId].endTimeSecond = currentTime.second();
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
    tempHour = currentTime.hour() + duration;
    activeAlarm[alarmId].endTimeHour = alarmType == 1 ? setHour(tempHour) : currentTime.hour();

    // alarmType for Minute
    tempMinute = currentTime.minute() + duration;
    if (alarmType == 2) {
        if (tempMinute > 59) {
            activeAlarm[alarmId].endTimeMinute = tempMinute - 60;
            tempHour = currentTime.hour() + 1;
            activeAlarm[alarmId].endTimeHour = setHour(tempHour);
        } else {
            activeAlarm[alarmId].endTimeMinute = tempMinute;
        }
    } else {
        activeAlarm[alarmId].endTimeMinute = currentTime.minute();
    }

    activeAlarm[alarmId].endTimeSecond = currentTime.second();
}

void printAlarm(int alarmId) {
    Serial.println("New Alarm ");
    Serial.print(alarmId);
    Serial.print(" > ");
    Serial.print(activeAlarm[alarmId].endTimeHour);
    Serial.print(":");
    Serial.print(activeAlarm[alarmId].endTimeMinute);
    Serial.print(":");
    Serial.print(activeAlarm[alarmId].endTimeSecond);
}

void matchAlarm() {
    // Match condition
    for (int i=0; i < 2; i++) {
        // Look for motion 5 min before alarm triggered.
        if (currentTime.hour() == activeAlarm[i].endTimeHour && currentTime.minute() >= activeAlarm[i].endTimeMinute && currentTime.second() >= activeAlarm[i].endTimeSecond && !activeAlarm[i].isAlarmTriggered) {
            actionMessageLogger("matchAlarm :: Timer Matched - alarm triggered");

            // kitchenBulb handler - Check if current time reached the Off Timer and LED still ON
            if (activeAlarm[i].alarmId == 0) {
                activeAlarm[i].isAlarmSet = 0;
                activeAlarm[i].isAlarmTriggered = 1;
                // Turn OFF the kitchen bulb
                turnBulb("OFF", "kitchenBulb"); 
                // unset the 2nd alarm
                unsetAlarm(1);
            }

            if (activeAlarm[i].alarmId == 1) {
                Serial.println("Lookup period started");
                motionSensorStatus = digitalRead(kitchenMotionSensor);
                // Activity detected
                if (motionSensorStatus == HIGH) {
                  // AlarmId, AlarmType = mintue, Duration in mintue
                  // Increase the main alarm timeline by 10 or 3 minutes based on active Hour
                  updateAlarm(0, 2, kitchenLightOnDuration == activeHourDuration ? longAlarmIncrementDuration : shortAlarmIncrementDuration);
                  printAlarm(0);
                  // Increase the second alarm timeline by 10 or 3 minutes based on active Hour
                  updateAlarm(1, 2, kitchenLightOnDuration == activeHourDuration ? longAlarmIncrementDuration : shortAlarmIncrementDuration);
                  printAlarm(1);

                  Serial.println("UpdatAlarm called and sound the buzzer");
                  // Two short beep
                  BuzzerOn("short");
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
            // Setup the second alarm for lookup action - detect motion [Just before 2 minute before closing the light]
            // SetAlarm - alarmId, alarmType, duration
            // 
            setAlarm(1, 2, (kitchenLightOnDuration - (kitchenLightOnDuration == activeHourDuration ? longLookUpPeriod : shortLookUpPeriod)));
            // Turn On the kitchen bulb
            turnBulb("ON", "kitchenBulb");
            // Set the flag true
            isKitchenLedOn = true;
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
        delay(100);
    }

    matchAlarm();
}

// For RTC module setup
void rtcSetup() {
    Serial.println("rtcSetup :: Health status check");
    delay(1000);

    if (! rtc.begin()) {
        Serial.println("rtcSetup :: Couldn't find RTC");
        Serial.flush();
        while (1) delay(10);
    }

    if (! rtc.isrunning()) {
        Serial.println("rtcSetup :: RTC is NOT running, Please uncomment below lines to set the time!");
        // When time needs to be set on a new device, or after a power loss, the
        // following line sets the RTC to the date & time this sketch was compiled
        //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        
        // This line sets the RTC with an explicit date & time, for example to set
        // January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    Serial.println("rtcSetup :: RTC is running fine and Current time >");

    currentTime = rtc.now();
    Serial.println(currentTime.hour());
    Serial.print(":");
    Serial.print(currentTime.minute());
    Serial.print(":");
    Serial.print(currentTime.second());
}

void setup() {
    // Debug console
    Serial.begin(115200);

    // Initial setup
    pinMode(buzzer, OUTPUT);
    pinMode(kitchBulbRelay, OUTPUT);
    pinMode(kitchenMotionSensor, INPUT);    
    pinMode(kitchenBulbSwitch, INPUT_PULLUP);

    // Setup the RTC mmodule
    rtcSetup();

    // buzzer sound
    BuzzerOn("long");
    Serial.println("Setup :: Setup completed");
}

void loop() {
    currentTime = rtc.now();

    // call all manual control i.e. switches, relay
    kitchen_control();

    delay(300);
}
