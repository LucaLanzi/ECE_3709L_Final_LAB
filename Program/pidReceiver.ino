#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LCD_ADDRESS 0x27
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

enum State : uint8_t { IDLE = 0, FWD, BWD, STOP };

// Radio Setup
const int CE = 7, CSN = 8;
RF24 radio(CE, CSN);
const byte address[6] = "P2U80";
uint8_t payload = 0;

// --- Added: store the current latched state ---
State currentState = STOP;

unsigned long lastRx = 0;
const unsigned long rxTimeoutMs = 500;

// Stepper motor pins
#define IN1 9
#define IN2 5
#define IN3 6
#define IN4 3

int Steps = 0;

// === Function: run the stepper ===
// direction = true  → forward
// direction = false → backward
// steps     = number of microsteps (default 4096 = full revolution for 28BYJ-48)
// delayUs   = delay between steps (speed control)
void runStepper(bool direction, int steps = 4096, int delayUs = 200) {
  for (int i = 0; i < steps; i++) {
    // Output sequence for 8-step half stepping
    switch (Steps) {
      case 0: digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); break;
      case 1: digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);  digitalWrite(IN3, HIGH); digitalWrite(IN4, HIGH); break;
      case 2: digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  break;
      case 3: digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  break;
      case 4: digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  break;
      case 5: digitalWrite(IN1, HIGH); digitalWrite(IN2, HIGH); digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  break;
      case 6: digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  break;
      case 7: digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); break;
      default:digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  break;
    }

    // Move the step index
    if (direction) Steps++;
    else Steps--;
    if (Steps > 7) Steps = 0;
    if (Steps < 0) Steps = 7;

    delayMicroseconds(delayUs);
  }
}


void runStepperStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}


void setup(){
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0,0); lcd.print("Motor Init");
  Serial.println("Motor Init");

  // Initialize the Radio ONCE
  if (!radio.begin()) {
    Serial.println("Radio NC");
    lcd.setCursor(0, 0); lcd.print("Radio NC");
    while (1) {}
  }

  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);
  radio.setChannel(108);                    // <-- MATCH TX channel
  radio.openReadingPipe(1, address);
  radio.startListening();

  delay(350);
  lcd.setCursor(0,1); lcd.print("Listening..." );
}

void loop(){
  bool got = false;

  // Drain FIFO; keep last byte
  while (radio.available()) {
    radio.read(&payload, sizeof(payload));
    got = true;
    lastRx = millis();
  }

  // --- Only update when a new command arrives ---
  if (got) {
    s
  }

  // --- Always act on the current latched state ---
  switch(currentState){
    case IDLE:
      Serial.println("IDLE");
      lcd.setCursor(0, 0); lcd.print("TX Received!   ");
      lcd.setCursor(0, 1); lcd.print("Motor: IDLING  ");
      break;

    case FWD:
      Serial.println("FWD");
      lcd.setCursor(0, 0); lcd.print("TX Received!   ");
      lcd.setCursor(0, 1); lcd.print("Motor: FWD 100%");
      runStepper(true);
      break;

    case BWD:
      Serial.println("BWD");
      lcd.setCursor(0, 0); lcd.print("TX Received!   ");
      lcd.setCursor(0, 1); lcd.print("Motor: BWD 100%");
      runStepper(false);
      break;

    case STOP:
      Serial.println("STOP");
      lcd.setCursor(0, 0); lcd.print("TX Received!   ");
      lcd.setCursor(0, 1); lcd.print("Motor: STOPPED ");
      runStepperStop();
      break;

    default:
      Serial.println(payload);
      lcd.setCursor(0, 0); lcd.print("TX Received!   ");
      lcd.setCursor(0, 1); lcd.print("Bad CMD!STOP!  ");
      runStepperStop();
      break;
  }

  // --- Optional: comment out link-loss to hold state forever ---
  /*
  if (millis() - lastRx > rxTimeoutMs) {
    lcd.setCursor(0, 0); lcd.print("NO TX Received ");
    lcd.setCursor(0, 1); lcd.print("MOTOR: STOPPED ");
  }
  */
}
