/* Holocron Toolbox v0.01 : ruthsarian@gmail.com
 * A project to interact with holocrons from Galaxy's Edge.
 * 
 * This code was developed with an Arduino Nano and a KY-032
 * "obstacle avoidance" module, which has an IR transmitter and receiver.
 * However it should be possible to get this code working with just about any
 * device that is supported by Arduino IDE.
 * 
 * TODO
 *  - if no holocron is "seen", button press puts its into jedi mode (for pairing with sith)
 *  - way to behave as s1 or s2 holocron
 * 
 */

// Hardware setup
#define IR_SEND_PIN           3
#define IR_RECV_PIN           2
#define IR_RECV_ACTIVE_STATE  LOW
#define IR_BUTTON_PIN         5

// Holcron signal timings, all in microseconds
#define PREAMBLE_ACTIVE       4400
#define S1_PERIOD             2000
#define S1_ACTIVE_ZERO        500 
#define S1_BIT_COUNT          8
#define S2_PERIOD             1125
#define S2_ACTIVE_ZERO        350
#define S2_BIT_COUNT          12
#define MAX_BIT_DURATION      3000    // limit how long an inactive duration can be before data is reset
#define CONNECT_TIMEOUT       10000

// Jedi Holcron commands
#define JEDI_S1_BRIGHT        0x96
#define JEDI_S1_DARK          0xA5
#define JEDI_S1_PING          0xB4
#define JEDI_S1_BREATHE       0xC3
#define JEDI_S1_FLASH         0xD2
#define JEDI_S1_LIGHTS_OFF    0xE1
#define JEDI_S1_BEACON        0xF0
#define JEDI_S2_BEACON        0x00
#define JEDI_S2_PING          0x044
#define JEDI_S2_DARK          0x055
#define JEDI_S2_LIGHT         0x066
#define JEDI_S2_BREATHE       0x077

// Sith Holocron commands
#define SITH_S1_HELLO         0x3C
#define SITH_S1_BUTTON        0x69
#define SITH_S1_ACK           0x78
#define SITH_S2_ACK           0x880
#define SITH_S2_BUTTON        0x891

// Misc
#define NO_DATA               -1
#define SERIES_ONE            1
#define SERIES_TWO            2
#define NO_BUTTON_PRESS       0
#define SHORT_BUTTON_PRESS    1
#define LONG_BUTTON_PRESS     2
#define LONG_BUTTON_TIME      1000    // holding the button for at least 1 second for a long button press

// state machine
enum holocron_states {
  IDLE,
  BEACON_FOUND,
  S1_JEDI_FOUND,
  S2_JEDI_FOUND,
  S1_JEDI_PAIRING,
  S2_JEDI_PAIRING,
  S1_PAIRED,
  S2_PAIRED,
  S2_BUTTON_SEND1,
  S2_BUTTON_SEND2,
  S2_BUTTON_SEND3,
  NUM_STATES
};

// IR receiver routine
// going old school and controlling the PIN directly rather than using an IR library
// also not using an interrupt as we need to know if a state is being held beyond some amount
// of time to determine the end of the receiving data stream. an ISR won't let us do that (directly)
short get_ir_data() {
  static bool last_state;
  static bool preambled = false;
  static unsigned short data = 0;
  static unsigned long last_change = 0;
  static unsigned long last_duration = 0;

  bool current_state;
  unsigned long current_time;
  unsigned long duration;
  unsigned short ir_data;

  // gather data about the current moment
  current_time = micros();
  current_state = digitalRead(IR_RECV_PIN);

  // has the pin state changed?
  if (current_state != last_state) {
    last_state = current_state;

    // a last_change value of 0 means we either just started or just finished receiving a packet
    // record the time of change and continue...
    if (last_change == 0) {
      last_change = current_time;
      return(NO_DATA);
    }

    // calculate some timing values
    duration = current_time - last_change;
    last_change = current_time;

    // if we haven't received a preamble and we're in a state to receive one, receive it.
    if (!preambled && duration > MAX_BIT_DURATION && current_state != IR_RECV_ACTIVE_STATE) {
      preambled = true;
      return(NO_DATA);
    }

    // if we've seen a preamble then we're collecting bits
    // a bit is transmitted via the time spent inactive then active.
    // if we're inactive then we were just active, that means a new bit 
    // has been received!
    if (preambled && current_state != IR_RECV_ACTIVE_STATE) {

      // to receive new bit, shift data left 1
      data = data << 1;

      // if new bit is 1, add it to data
      if (duration > last_duration) {
        data = data + 1;
      }
    } 

    // move duration into last_duration for use the next time through
    last_duration = duration;

  // if we're pre-ambled and last_change isn't set and it's been more MAX_BIT_DURATION microseconds 
  // since the last state change, assume we've reached the end of the packet. 
  // record the data, reset variables, and return the received data
  } else if (preambled && last_change != 0 && last_change + MAX_BIT_DURATION < micros()) {
    ir_data = data;
    data = 0;
    last_change = 0;
    preambled = false;
    return(ir_data);
  }

  return(NO_DATA);
}

void send_packet(unsigned short data, uint8_t holocron_series) {
  unsigned char bit_out, bit_count;
  unsigned long next;
  unsigned short inactive, period, active_zero;
  
  // set certain timing values based on which series holocron it is
  if (holocron_series == SERIES_ONE) {
    period = S1_PERIOD;
    bit_count = S1_BIT_COUNT;
    active_zero = S1_ACTIVE_ZERO;
  } else {
    period = S2_PERIOD;
    bit_count = S2_BIT_COUNT;
    active_zero = S2_ACTIVE_ZERO;
  }

  // send the preamble
  digitalWrite(IR_SEND_PIN, HIGH);
  next = micros() + PREAMBLE_ACTIVE;
  while (micros() < next) {;}
  digitalWrite(IR_SEND_PIN, LOW);

  // send the data
  while (bit_count--) {

    // get the outgoing bit
    bit_out = (data >> bit_count) & 1;
   
    // sending a 1
    if (bit_out) {
      inactive = active_zero;

    // sending a 0
    } else {
      inactive = period - active_zero;
    }

    // send the bit
    next = next + inactive;
    while (micros() < next) {;}
    digitalWrite(IR_SEND_PIN, HIGH);
    next = next + period - inactive;
    while (micros() < next) {;}
    digitalWrite(IR_SEND_PIN, LOW);
  }
}

// return true on button release
unsigned char button_press() {
  static unsigned long button_down_time = 0;
  unsigned char button_state;

  button_state = digitalRead(IR_BUTTON_PIN);
  if (button_state == LOW) {
    if (button_down_time == 0) {
      button_down_time = millis();
    }
  } else {
    if (button_down_time != 0) {
      if (millis() < button_down_time + LONG_BUTTON_TIME) {
        button_down_time = 0;
        return(SHORT_BUTTON_PRESS);
      } else {
        button_down_time = 0;
        return(LONG_BUTTON_PRESS);
      }
    }
  }

  return(NO_BUTTON_PRESS);
}

void setup() {
  // setup serial for debug messages
  Serial.begin(115200);

  // setup IR receiver PIN
  pinMode(IR_RECV_PIN, INPUT);

  // setup IR send PIN
  pinMode(IR_SEND_PIN, OUTPUT);
  digitalWrite(IR_SEND_PIN, LOW);

  // setup BUTTON
  pinMode(IR_BUTTON_PIN, INPUT_PULLUP);

  // ready to party
  Serial.println("Ready.");
}

void loop() {
  short data;
  static unsigned long last_packet_time = 0;
  static holocron_states current_state = IDLE;

  data = get_ir_data();
  if (data != NO_DATA) {

//    Serial.print(data, HEX);
//    Serial.print(" ");
//    } if (false) {

    switch (current_state) {

      // looking for a S2 JEDI holocron - step 1
      case IDLE:
        if (data == JEDI_S1_BEACON) {
          current_state = BEACON_FOUND;
        }
        break;

      // looking for a S2 JEDI holocron - step 2
      case BEACON_FOUND:

        // this is a series 2 jedi holocron
        if (data == JEDI_S2_BEACON) {
          Serial.println("Series 2 JEDI Holocron Found!");
          Serial.println("  Press button to pair.");
          current_state = S2_JEDI_FOUND;

        // this is a series 1 jedi holocron
        } else if (data == JEDI_S1_BEACON) {
          Serial.println("Series 1 JEDI Holocron Found!");
          Serial.println("  Press button to pair.");
          current_state = S1_JEDI_FOUND;
        }

        // don't know what this is, reset.
        else  {
          current_state = IDLE;
        }
        break;

      case S1_JEDI_FOUND:
        if (button_press()) {
          current_state = S1_JEDI_PAIRING;
        }
        break;

      case S2_JEDI_FOUND:
        if (button_press()) {
          current_state = S2_JEDI_PAIRING;
        }
        break;

      case S1_JEDI_PAIRING:
        send_packet(SITH_S1_HELLO, SERIES_ONE);
        current_state = S1_PAIRED;
        break;

      case S2_JEDI_PAIRING:
         if (data == JEDI_S2_BEACON) {
          send_packet(SITH_S2_ACK, SERIES_TWO);
          current_state = S2_PAIRED;
         }
         break;

      // paired
      case S1_PAIRED:
        if (button_press()) {
          send_packet(SITH_S1_BUTTON, SERIES_ONE);
        } else if (   
               data == JEDI_S1_BRIGHT
            || data == JEDI_S1_DARK
            || data == JEDI_S1_PING
            || data == JEDI_S1_BREATHE
            || data == JEDI_S1_FLASH
            || data == JEDI_S1_LIGHTS_OFF
            || data == JEDI_S1_BEACON
        ) {
          send_packet(SITH_S1_ACK, SERIES_ONE);
          last_packet_time = millis();
        }
        break;

      case S2_PAIRED:

        if (button_press()) {
          send_packet(SITH_S2_BUTTON, SERIES_TWO);
          current_state = S2_BUTTON_SEND1;
        } else if (   
               data == JEDI_S2_BEACON
            || data == JEDI_S2_PING
            || data == JEDI_S2_BREATHE
            || data == JEDI_S2_DARK
            || data == JEDI_S2_LIGHT
        ) {
          send_packet(SITH_S2_ACK, SERIES_TWO);
          last_packet_time = millis();
        }
        break;

      case S2_BUTTON_SEND1:
      case S2_BUTTON_SEND2:
        last_packet_time = millis();
        send_packet(SITH_S2_BUTTON, SERIES_TWO);
        current_state = (current_state == S2_BUTTON_SEND1 ? S2_BUTTON_SEND2 : S2_PAIRED);
        break;

      default:
        Serial.println("  Reset");
        current_state = IDLE;
        last_packet_time = 0;
        break;
    }
  }

  if (last_packet_time > 0 && last_packet_time + CONNECT_TIMEOUT < millis()) {
    Serial.println("  Connection lost...");
    current_state = IDLE;
    last_packet_time = 0;
  }
}
