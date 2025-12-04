// Includes
#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LCD_ADDRESS 0x27

// Pin defs
const int CE = 9, CSN = 8;
const int btn_mode = 3;
const int btn_fwd  = 4;
const int btn_bwd  = 5;
const int btn_stop = 6;
const int btn_idle = 2;

// Globals
RF24 radio(CE, CSN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// 5-byte pipe address (common RF24 style)
const byte address[6] = "P2U80";  // length 6 to include '\0', RF24 uses first 5

enum State : uint8_t { IDLE = 0, FWD, BWD, STOP };  // fixed enum
int mode = 0; // 0=Button mode, 1=LabVIEW mode

uint8_t incomingByte = 0;

// NEW: track which action is currently toggled on
State active = IDLE;

// Separate debounce timers per button
unsigned long lastModePress = 0;
unsigned long lastFwdPress  = 0;
unsigned long lastBwdPress  = 0;
unsigned long lastStopPress = 0;
unsigned long lastIdlePress = 0;
const unsigned long debounceDelay = 200;  // ms

// [OPTIONAL] add a timestamp for throttling serial output
unsigned long lastSerialMsg = 0;
const unsigned long serialInterval = 1000; // 1s


void applyState(State s) { //Function takes in enum State defined as s
  active = s; //s is the current active state
  uint8_t cmd = static_cast<uint8_t>(s); //assign the current state to the cmd
  radio.write(&cmd, sizeof(cmd)); //transmit the command
  Serial.println(cmd); //print the command

  switch (s) { //depending on the input value for s, compare it to the enum types and print to lcd and serial monitor
    case IDLE: Serial.println("Motor: IDLE"); lcd.setCursor(0, 1); lcd.print("Motor: IDLE     "); break;
    case FWD:  Serial.println("Motor: FWD");  lcd.setCursor(0, 1); lcd.print("Motor: FWD      "); break;
    case BWD:  Serial.println("Motor: BWD");  lcd.setCursor(0, 1); lcd.print("Motor: BWD      "); break;
    case STOP: Serial.println("Motor: STOP"); lcd.setCursor(0, 1); lcd.print("Motor: STOP     "); break;
  }
}

void setup() {
  //intialize buttons
  pinMode(btn_mode, INPUT_PULLUP);
  pinMode(btn_fwd,  INPUT_PULLUP);
  pinMode(btn_bwd,  INPUT_PULLUP);
  pinMode(btn_stop, INPUT_PULLUP);
  pinMode(btn_idle, INPUT_PULLUP);

  Serial.begin(115200);

  // Initialize the LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Initialize the Radio, wait if not responding
  if (!radio.begin()) {
    Serial.println("Radio NC");
    lcd.setCursor(0, 0); lcd.print("Radio NC");
    while (1) {} // hold here if radio not connected
  }

  // Radio setup
  radio.openWritingPipe(address);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);

  // improve reliability under RF noise
  radio.setRetries(5, 15);   // delay=5x250µs, up to 15 retries
  radio.setChannel(108);     // pick mid-band channel to avoid Wi-Fi overlap

  radio.stopListening();
  delay(1000); // wait for radio to be ready

  // Show initial selection
  lcd.setCursor(0, 0); lcd.print("Mode: Button     ");
  applyState(active);
}

void loop() {
  // Toggle mode with XOR on button press (debounced)
  if (digitalRead(btn_mode) == LOW && (millis() - lastModePress > debounceDelay)) {
    lastModePress = millis();
    mode ^= 1;  // flip 0 <-> 1
    lcd.setCursor(0, 0); lcd.print(mode == 0 ? "Mode: Button     " : "Mode: LabVIEW    ");
    Serial.print("Mode toggled to: "); Serial.println(mode);
    delay(200);
  }

  switch (mode) {
    case 0: { // Manual Button Mode
      lcd.setCursor(0, 0); lcd.print("Mode: Button     ");

      // Mutually exclusive toggles:
      // On a debounced press, if different from current 'active', switch to it.
      if (digitalRead(btn_idle) == LOW && (millis() - lastIdlePress > debounceDelay)) {
        lastIdlePress = millis();
        if (active != IDLE) applyState(IDLE);
      }

      if (digitalRead(btn_fwd) == LOW && (millis() - lastFwdPress > debounceDelay)) {
        lastFwdPress = millis();
        if (active != FWD) applyState(FWD);
      }

      if (digitalRead(btn_bwd) == LOW && (millis() - lastBwdPress > debounceDelay)) {
        lastBwdPress = millis();
        if (active != BWD) applyState(BWD);
      }

      if (digitalRead(btn_stop) == LOW && (millis() - lastStopPress > debounceDelay)) {
        lastStopPress = millis();
        if (active != STOP) applyState(STOP);
      }

      break; // prevent fall-through
    }

    case 1: { // LabVIEW / Serial Mode
      lcd.setCursor(0, 0); lcd.print("Mode: LabVIEW    ");
      if (Serial.available() > 0) { //If we are getting a serial input
        char c = (char)Serial.read(); //char c is assigned to the Serial.read value as a char data type
        if (c == '\r' || c == '\n') {
          // ignore line endings
        } else {
          uint8_t cmd;
          if (c >= '0' && c <= '3') {
            cmd = (uint8_t)(c - '0');   // 0 corresponds to 48 in ascii, so 48 - 48 = 0, 1 in ascii is 49 so 49 - 48 = 1, etc
            // reflect to UI and track 'active' similarly
            applyState(static_cast<State>(cmd)); //call the applyState function input the cmd and convert it to an enum type input
          } else {
            // ignore unknown chars
          }
        }
      } else {
        // [OPTIONAL] throttle debug prints to once per second
        if (millis() - lastSerialMsg > serialInterval) {
          lastSerialMsg = millis();
          Serial.println("No Serial Input, waiting");
          lcd.setCursor(0,1); lcd.print("Unknown CMD      ");
        }
      }
      break;
    }

    default:
      // Safety: shouldn’t hit this
      mode = 0;
      break;
  }
}
