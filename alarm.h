/*
 * ALARM.h
 * 
 * Author:  Darryn Papthanasiou
 * Date:    17-05-2015
 * Car alarm system which couples with the Apollo Tracker and GSM system.
 * 
 */
              
enum _alarmState {
  AS_ACTIVATED,
  AS_TRIGGERED,
  AS_SILENCED,
  AS_DEACTIVATED
};

const uint8_t MOTION_IN   = 8,  // Motion sensor input IN
              TOGGLE_IN   = 9,  // Alarm toggle input IN
              CIRCUIT_OUT = 10, // Circuit out
              GATE_OUT    = 11, // Gate out
              STATE_OUT   = 12, // Alarm state out
              CALL_OUT    = 13, // Alarm call notify out
              CIRCUIT_IN  = A0; // Circuit in

byte alarmState                     = AS_DEACTIVATED; // Default alarm state
unsigned long lastToggle            = 0,
              lastTriggered         = 0,
              lastTriggerIndication = 0,
              motionTimeout         = 4000,
              alarmTimeout          = 20000,
              indicationTimeout     = 500,
              toggleTimeout         = 500;

/* Used to turn the alarm ON and OFF */
boolean toggle_alarm() {
#ifdef __DEBUG__
  Serial.println("Toggling alarm status...");
#endif
  if (alarmState == AS_ACTIVATED ||
      alarmState == AS_TRIGGERED ||
      alarmState == AS_SILENCED) {
      alarmState = AS_DEACTIVATED;  // Deactivate alarm
      digitalWrite(STATE_OUT, LOW); // Toggle armed display LED
      // Show change in state
      digitalWrite(GATE_OUT, HIGH);
      delay(60);
      digitalWrite(GATE_OUT, LOW);
      digitalWrite(CALL_OUT, LOW); // Stop call notification
  } else if (alarmState == AS_DEACTIVATED) {
      alarmState = AS_ACTIVATED;
      // Toggle armed display LED
      digitalWrite(STATE_OUT, HIGH);
      // Set circuit to LIVE
      digitalWrite(CIRCUIT_OUT, HIGH);
      // Show change in state
      digitalWrite(GATE_OUT, HIGH);
      delay(60);
      digitalWrite(GATE_OUT, LOW);
      delay(120);
      digitalWrite(GATE_OUT, HIGH);
      delay(60);
      digitalWrite(GATE_OUT, LOW);
  }
  // Ensuring alarm is OFF/LOW when toggling alarm
  digitalWrite(GATE_OUT, LOW);
  return true;
} // END TOGGLE ALARM

/* Used to set the alarm status to triggered */
boolean trigger_alarm() {
#ifdef __DEBUG__
  Serial.println("Triggering alarm...");
#endif
  alarmState = AS_TRIGGERED;
  digitalWrite(CIRCUIT_OUT, LOW); // Deactivate circuit
  digitalWrite(STATE_OUT, LOW);   // Turn the alarm display and state to off
  digitalWrite(CALL_OUT, HIGH);   // Signal the alarm is required to call owner's cell phone
  return true;
} // END TRIGGER ALARM

/* Used to silence alarm after time out period */
void toggle_alarm_timeout() {
  switch(alarmState) {
    case(AS_SILENCED):
      alarmState = AS_TRIGGERED;
      break;
    case(AS_TRIGGERED):
      {
        alarmState = AS_SILENCED;
        digitalWrite(GATE_OUT, LOW); // Ensure alarm is turned off
      }
    break;
  }
} // END TOGGLE ALARM TIMEOUT

