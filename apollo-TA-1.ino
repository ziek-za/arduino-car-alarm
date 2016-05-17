/*
 * apollo-TA-1.ino
 * 
 * Author:  Darryn Papthanasiou
 * Date:    17-05-2015
 * Car alarm system which couples with the Apollo Tracker and GSM system.
 */
// #define __DEBUG__

#include <SoftwareSerial.h>
#include "alarm.h"


void setup() {
  /* Init sequence */
#ifdef __DEBUG__
    Serial.begin(9600);
    delay(500);
#endif

  /* Define PIN modes */
  pinMode(MOTION_IN, INPUT);
  pinMode(TOGGLE_IN, INPUT);  

  pinMode(CIRCUIT_OUT, OUTPUT);
  pinMode(GATE_OUT, OUTPUT);
  pinMode(STATE_OUT, OUTPUT);
  pinMode(CALL_OUT, OUTPUT);
} // END SETUP

void loop() {
  unsigned long now = millis();
  
  /* Toggle alarm status from receiver input */
  if (digitalRead(TOGGLE_IN) &&
      now - lastToggle >= toggleTimeout) {
    if (toggle_alarm())
      lastToggle = now;
  }

  /* Check alarm sensors input's to ensure that the alarm hasn't been triggered */
  if (alarmState == AS_ACTIVATED) {
    /* - Check doors, bonnet and back hatch loop
     * (will read LOW if grounded)
     * - Check for motion within the car
     * (Will read HIGH if motion is detected)
     */
    if (analogRead(CIRCUIT_IN) <= 500 || (digitalRead(MOTION_IN) && now - lastToggle >= motionTimeout)) {
      trigger_alarm();
    }
  } else if (alarmState == AS_TRIGGERED || alarmState == AS_SILENCED) {
    /* Check for alarm timeout period */
    if (now - lastTriggered >= alarmTimeout) {
      toggle_alarm_timeout(); // set the alarm to activated again
      lastTriggered = now;
    }
    /* Trigger sirens and alarm */
    if (now - lastTriggerIndication >= indicationTimeout) {
      lastTriggerIndication = now;
      // Ensure that the alarm is silenced by not switching state of the gate
      if (alarmState == AS_TRIGGERED) {
        digitalWrite(GATE_OUT, !digitalRead(GATE_OUT));
      }
    }
  }
    
} // END LOOP

