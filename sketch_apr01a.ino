#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
/* SIM800L */
#define SIM800_TX_PIN 4
#define SIM800_RX_PIN 3
#define SIM800_RST_PIN 5
/* RFID */
#define SS_PIN 10
#define RST_PIN 7
/* Door, bonnet and hatch loop */
#define CIRC_LOOP_PIN_OUT 9
#define CIRC_LOOP_PIN_IN A0
/* Alarm state display */
#define STATE_DISPLAY_PIN 8 
#define STATE_GATE_PIN 6
/* Vibration sensors */
#define VIBR_INTERRUPT_PIN 2

#define __DEBUG__ false

/* Create software serial object to communicate with SIM800 */
SoftwareSerial serialSIM800(SIM800_TX_PIN,SIM800_RX_PIN);
enum GSM_STATES {ready, unknown, ringing, call_in_progress};
int GSM_STATE, GSM_output_index;
char GSM_output[500];
enum NOTIFICATION_CALL_STATES {available, calling, stopped};
int NOTIFICATION_CALL_STATE; 
/* Valid NUID stored in uint */
const byte VALID_NUID[2][4] = {
  {115, 104, 102, 199},
  {64, 207, 31, 101}
};
/* Initiate RFID variables */
MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
/* Global alarm variable for the state of the alarm */
enum ALARM_STATES {activated, deactivated, triggered, silenced};
int ALARM_STATE;
/* Alarm values */
const long circuit_break_threshold = 100; // detection for when the circuit has been broken (higher = more likely)
const long alarm_pulse_rate = 500; // 500ms per. flash/sirean noise
/* Used for timing events */
const long alarm_timeout_period = 16000; // alarm will time out after 16 seconds
const long GSM_module_status_check_rate = 3000; // How often the GSM module is pinged and status returned
const long GSM_module_unknown_flash_rate = 100; // When a bad status is returned, this is the rate at which the display flashes
unsigned long previous_millis = 0;
unsigned long previous_millis_since_triggered = 0;
unsigned long previous_millis_since_GSM_status_check = 0;
unsigned long previous_millis_since_GSM_unknown = 0;
int GSM_count_before_reset = 0;
const int GSM_reset_threshold = 8;

void setup() { 
  Serial.begin(9600);
  
  //Begin serial communication witj Arduino and SIM800
  serialSIM800.begin(9600);
  
  // Initiate RFID scanner
  //SPI.begin(); // Init SPI bus
  //rfid.PCD_Init(); // Init MFRC522
  
  // Set PIN modes
  pinMode(STATE_DISPLAY_PIN, OUTPUT);
  pinMode(CIRC_LOOP_PIN_OUT, OUTPUT);
  pinMode(STATE_GATE_PIN, OUTPUT);
  pinMode(VIBR_INTERRUPT_PIN, INPUT); // Reading in the interrupt
  pinMode(SIM800_RST_PIN, OUTPUT);

  // Set default alarm state
  ALARM_STATE = ALARM_STATES::deactivated;

  // Set pin defaults
  digitalWrite(STATE_GATE_PIN, LOW);
  digitalWrite(SIM800_RST_PIN, HIGH);

  // Set GSM state
  GSM_STATE = GSM_STATES::unknown;
  GSM_output_index = 0;
  NOTIFICATION_CALL_STATE = NOTIFICATION_CALL_STATES::available;

  // Set interrupt pin for vibration detection
  attachInterrupt(digitalPinToInterrupt(VIBR_INTERRUPT_PIN), trigger_alarm, RISING);
}

void loop() {
  // Used for timing events
  unsigned long current_millis = millis();
  //Read SIM800 output (if available) and print it in Arduino IDE Serial Monitor
 /*if(serialSIM800.available()){
    Serial.write(serialSIM800.read());
  }
  //Read Arduino IDE Serial Monitor inputs (if available) and send them to SIM800
  if(Serial.available()){    
    serialSIM800.write(Serial.read());
  }*/

  gsm_serial_connection_update(current_millis);

  // Verify if the NUID has been read
  /*if (rfid.PICC_IsNewCardPresent() &&
    rfid.PICC_ReadCardSerial()) {      
    // Check is the PICC of Classic MIFARE type
    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
    if (piccType == MFRC522::PICC_TYPE_MIFARE_MINI ||  
      piccType == MFRC522::PICC_TYPE_MIFARE_1K ||
      piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
        bool matched = false;
        for (int i = 0; i < 2; i++) {
          if (rfid.uid.uidByte[0] == VALID_NUID[i][0] && 
          rfid.uid.uidByte[1] == VALID_NUID[i][1] && 
          rfid.uid.uidByte[2] == VALID_NUID[i][2] && 
          rfid.uid.uidByte[3] == VALID_NUID[i][3]) {
            matched = true;
            break;
          }
        }
        if (matched)
          toggle_alarm_status();
    }
  }*/
  // Halt PICC
  //rfid.PICC_HaltA();
  // Stop encryption on PCD
  //rfid.PCD_StopCrypto1();
  
  // Check over alarm states
  if (ALARM_STATE == ALARM_STATES::activated) {
    // Display if GSM module is able to call
    if (GSM_STATE == GSM_STATES::unknown &&
      current_millis - previous_millis_since_GSM_unknown >=  GSM_module_unknown_flash_rate) {
        digitalWrite(STATE_DISPLAY_PIN, !digitalRead(STATE_DISPLAY_PIN));
        previous_millis_since_GSM_unknown = current_millis;
    } else if (GSM_STATE == GSM_STATES::ready)
      digitalWrite(STATE_DISPLAY_PIN, HIGH);
    // Check for circuit break
    if (analogRead(CIRC_LOOP_PIN_IN) < circuit_break_threshold) {
      trigger_alarm();
      // Deactivate circuit
      digitalWrite(CIRC_LOOP_PIN_OUT, LOW);
    }
  } else if (ALARM_STATE == ALARM_STATES::triggered ||
              ALARM_STATE == ALARM_STATES::silenced) {
    // Check for timeout period
    if (current_millis - previous_millis_since_triggered >= alarm_timeout_period) {
      // set the alarm to activated again
      toggle_alarm_timeout();
      previous_millis_since_triggered = current_millis;
    }
    
    // Trigger siren + lights
    if (current_millis - previous_millis >= alarm_pulse_rate) {
      previous_millis = current_millis;
      // Make the gate in time with the state_display
      int state = digitalRead(STATE_DISPLAY_PIN);
      digitalWrite(STATE_DISPLAY_PIN, !state);
      // Ensure that the alarm is silenced by not switching state of the gate
      if (ALARM_STATE == ALARM_STATES::triggered) {
        digitalWrite(STATE_GATE_PIN, !state);
      }
    }
    
    // Call number
    if (GSM_STATE == GSM_STATES::ready &&
        NOTIFICATION_CALL_STATE == NOTIFICATION_CALL_STATES::available &&
        ALARM_STATE == ALARM_STATES::triggered) {
      Serial.println("Attempting call...\n> ATD 0724805970;");
      NOTIFICATION_CALL_STATE = NOTIFICATION_CALL_STATES::calling;
      // Call notification number
      serialSIM800.write("ATD 0724805970;\n\r");
    }
  }
}

/* Method declarations */
/* Used to communicate between the SIM800L module and arduino ,
 * - Updates the state of the GSM module (0, 2, 3, 4)
 * - Listens for ATD response (BUSY, NO DIAL, NO ANSWER etc)
 */
void gsm_serial_connection_update(unsigned long current_millis) {
  if (GSM_count_before_reset >= GSM_reset_threshold) {
    if (__DEBUG__) {
      Serial.println("Resetting GSM module...");
    }
    digitalWrite(SIM800_RST_PIN, LOW);
    delay(200);
    digitalWrite(SIM800_RST_PIN, HIGH);
    GSM_count_before_reset = 0;
  }
  // Check and assign GSM status
  if (current_millis - previous_millis_since_GSM_status_check >= GSM_module_status_check_rate) {
      if (GSM_output_index == 0) {
          // pass command for status check
          serialSIM800.write("AT+CPAS\r\nAT+CLCC\r\n");
          if (__DEBUG__) {
            Serial.println("Checking GSM call and function status (AT+CPAS \ AT+CLCC)");
          }
      }
      previous_millis_since_GSM_status_check = current_millis;
  }

  // Read serial response from GSM module
  char last_read_char = serialSIM800.read();
  if (int(last_read_char) != -1) {
      GSM_output[GSM_output_index++] = last_read_char;
   // Evaluate response
   } else if (GSM_output_index != 0) {
      String GSM_output_string(GSM_output);
      // Get the state associated with the +CPAS command
      char CPAS_GSM_state = GSM_output_string[GSM_output_string.indexOf("+CPAS: ") + 7];
      // Update the state of the GSM module accordingly
      switch(CPAS_GSM_state) {
        // Ready
        case '0':
          GSM_STATE = GSM_STATES::ready;
          GSM_count_before_reset = 0;
          break;
        // Unknown
        case '2':
          GSM_STATE = GSM_STATES::unknown;
          GSM_count_before_reset++;
          break;
        // Ringing
        case '3':
          GSM_STATE = GSM_STATES::ringing;
          break;
        // Call in progress
        case '4':
          GSM_STATE = GSM_STATES::call_in_progress;
          break;      
      }
      if (CPAS_GSM_state && __DEBUG__) {
        // Display state
        Serial.println("CPA State: ");
        Serial.println(CPAS_GSM_state);
      }
      // Check to see if incoming serial connection responds to ATD
      int GSM_ATD_BUSY = GSM_output_string.indexOf("BUSY");
      int GSM_ATD_NO_ANSWER = GSM_output_string.indexOf("NO ANSWER");
      int GSM_ATD_NO_CARRIER = GSM_output_string.indexOf("NO CARRIER");
      int GSM_ATD_CLCC = GSM_output_string.indexOf("+CLCC:");
      // Check to see for incoming call
      if (GSM_STATE == GSM_STATES::ringing && GSM_ATD_CLCC != -1) {
        if (GSM_output_string.indexOf("+27724805970") != -1)
          toggle_alarm_status();
          // hang up the call
          serialSIM800.write("ATH\n\r");
          if (__DEBUG__) {
            Serial.println("User calling...");
          }
      }
      // the keyword 'BUSY' will be found if the user has declined the call,
      // thus stopping the alarm from calling the user
      if (GSM_ATD_BUSY != -1) {
        NOTIFICATION_CALL_STATE = NOTIFICATION_CALL_STATES::stopped;
        if (__DEBUG__) {
          Serial.println("Phone call declined, stopping dialling...");
        }
      }
      // If the user doesn't answer the phone, continuously call
      else if (GSM_ATD_NO_ANSWER != -1 || GSM_ATD_NO_CARRIER != -1 &&
        GSM_STATE != GSM_STATES::ringing) {
        NOTIFICATION_CALL_STATE = NOTIFICATION_CALL_STATES::available;
        if (__DEBUG__) {
          Serial.println("User phone NOT answered, redialling...");
        }
      }
      // Display output if display_serial_read == true
     // if (__DEBUG__) {
        Serial.println("-=-=-=- SIM800L input -=-=-=-=");
        Serial.print(GSM_output);
        Serial.println("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
     // }
      // Reset index and array
      GSM_output_index = 0;
      memset(GSM_output,0,sizeof(GSM_output));
   }
}
/* Used to set the alarm status to triggered */
void trigger_alarm() {
  if (ALARM_STATE == ALARM_STATES::activated) {
    ALARM_STATE = ALARM_STATES::triggered;
    // Set timing event since triggered (Add 1000ms for relative timing)
    previous_millis_since_triggered = millis() - 1000;
    if (__DEBUG__)
      Serial.println("Triggering alarm...");
  }
}

/* Used to toggle between alarm states */
void toggle_alarm_status() {
  if (__DEBUG__) {
    Serial.println("Toggling alarm status...");
  }
  if (ALARM_STATE == ALARM_STATES::activated ||
    ALARM_STATE == ALARM_STATES::triggered ||
    ALARM_STATE == ALARM_STATES::silenced) {
      ALARM_STATE = ALARM_STATES::deactivated;
      // Toggle armed display LED
      digitalWrite(STATE_DISPLAY_PIN, LOW);
      // Show change in state
      digitalWrite(STATE_GATE_PIN, HIGH);
      delay(50);
      digitalWrite(STATE_GATE_PIN, LOW);
      // Stop calling if currently calling
      serialSIM800.write("ATH\n\r");
  } else if (ALARM_STATE == ALARM_STATES::deactivated) {
      ALARM_STATE = ALARM_STATES::activated;
      // Toggle armed display LED
      digitalWrite(STATE_DISPLAY_PIN, HIGH);
      // Set circuit to LIVE
      digitalWrite(CIRC_LOOP_PIN_OUT, HIGH);
      // Show change in state
      digitalWrite(STATE_GATE_PIN, HIGH);
      delay(50);
      digitalWrite(STATE_GATE_PIN, LOW);
      delay(50);
      digitalWrite(STATE_GATE_PIN, HIGH);
      delay(50);
      digitalWrite(STATE_GATE_PIN, LOW);
  }
  // Ensuring alarm is OFF/LOW when toggling alarm
  digitalWrite(STATE_GATE_PIN, LOW);
  // Ensure dialing is ready
  NOTIFICATION_CALL_STATE = NOTIFICATION_CALL_STATES::available;
}

/* Used to silence alarm after time out period */
void toggle_alarm_timeout() {
    if (ALARM_STATE == ALARM_STATES::silenced) {
      ALARM_STATE = ALARM_STATES::triggered;
    } else if (ALARM_STATE == ALARM_STATES::triggered) {
      ALARM_STATE = ALARM_STATES::silenced;
      // Ensure alarm is turned off
      digitalWrite(STATE_GATE_PIN, LOW);
    }
}

