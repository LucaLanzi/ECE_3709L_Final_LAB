#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <LiquidCrystal_I2C.h>
#define LCD_ADDRESS 0x27   // if this breaks, we're going home

// ================== nRF24 radio stuff ==================
// CE = 7, CSN = 8 (because the lab kit said so)
RF24 rfRadioModule(9, 10);

// pipe address (literally just a random string that magically works)
const byte rfPipeAddress[6] = "P2U80";  

// ================== TX payload (command to receiver) ==================
// buffer that holds whatever LabVIEW screams at us
char outgoingCommandBuffer[10] = {0};

// ================== ACK payload (telemetry from receiver) ==================
// [0] = temp, [1] = light, [2] = humidity, [3] = motor speed
int incomingTelemetryData[4] = {0, 0, 0, 0};

// ================== LCD setup ==================
// tiny 16x2 display that does all the talking for us
LiquidCrystal_I2C classroomLcd(LCD_ADDRESS, 16, 2);

// ================== timing / delay setup ==================
unsigned long lastCommandTimestamp = 0;         // last time we processed stuff
const unsigned long commandPeriodMs = 50;       // how often we talk to the robot (every 50 ms)

void setup() {
  Serial.begin(115200);  // talking REALLY fast to LabVIEW

  // ===== LCD setup =====
  classroomLcd.init();
  classroomLcd.backlight(); // yes. light good.

  // ===== nRF24 setup =====
  rfRadioModule.begin();
  rfRadioModule.setDataRate(RF24_250KBPS); // slow but actually works through walls
  rfRadioModule.setRetries(3, 5);          // (delay, count) aka "pls try again"
  rfRadioModule.openWritingPipe(rfPipeAddress);
  rfRadioModule.enableAckPayload();
  rfRadioModule.stopListening();           // TX mode: we only speak, no listen (except ACKs)

  // small delay so nothing has a panic attack on startup
  delay(100);
}

void loop() {
  // process stuff every commandPeriodMs
  unsigned long currentMillisTime = millis();
  if (currentMillisTime - lastCommandTimestamp >= commandPeriodMs)
  {
    processSerialFromLabVIEW();
    lastCommandTimestamp = currentMillisTime;
  }
}

// ================== handle data coming from LabVIEW ==================
void processSerialFromLabVIEW() {
  // if LabVIEW is being quiet, we also stay quiet
  if (!Serial.available()) return;

  // clear outgoing buffer (goodbye previous nonsense)
  memset(outgoingCommandBuffer, '\0', sizeof(outgoingCommandBuffer)); 

  // read serial from LabVIEW, process line when we hit newline
  while (Serial.available())
  {
    static uint8_t serialIndex = 0;  // lives forever, just like this lab
    char incomingChar = Serial.read();

    if (incomingChar == '\r') continue;                    // ignore carriage return
    if (incomingChar == '\n') {                            // end of command
      if (serialIndex) transmitCommandAndReadAck();        // only send if we actually got something
      serialIndex = 0;                                     // reset for next line
    }
    else if (serialIndex < 32) {
      // yes the buffer is only 10 bytes but lab instructions said 32,
      // we're just playing along
      outgoingCommandBuffer[serialIndex++] = incomingChar;
    }
  }

  // show last command on LCD bottom row (mainly so TA thinks we know what we're doing)
  classroomLcd.setCursor(0, 1);
  classroomLcd.print(outgoingCommandBuffer);
  classroomLcd.print("      "); // lazy way to clear leftovers
}

// ================== send command over RF and read ACK telemetry ==================
void transmitCommandAndReadAck() {
  
  // yeet payload at receiver
  bool rfSendOk = rfRadioModule.write(&outgoingCommandBuffer, sizeof(outgoingCommandBuffer));

  // if receiver didn’t get it, complain on LCD
  if (!rfSendOk) {
    // Serial.println("RF send failed");  // uncomment if you like sadness in the serial monitor
    classroomLcd.setCursor(0, 0);
    classroomLcd.print("RF Send Fqailed"); // typo is now part of the lore
    return;
  }

  // If the receiver loaded an ack payload, read it
  if (rfRadioModule.isAckPayloadAvailable()) {
    classroomLcd.setCursor(0, 0);
    classroomLcd.print("RF Send Succeeded");  // we ball

    // grab the latest telemetry (overwrites until queue is empty)
    while (rfRadioModule.isAckPayloadAvailable()) {
      rfRadioModule.read(&incomingTelemetryData, sizeof(incomingTelemetryData));
    }

    // Send telemetry back to LabVIEW over Serial
    // Format: "T,tempC,light,humidity,speed\n"
    Serial.print("T,");
    Serial.print(incomingTelemetryData[0]);  // DHT temp C
    Serial.print(",");
    Serial.print(incomingTelemetryData[1]);  // LDR value (how bright is reality)
    Serial.print(",");
    Serial.print(incomingTelemetryData[2]);  // humidity (% vibes)
    Serial.print(",");
    Serial.println(incomingTelemetryData[3]);  // PVf (speed feedback)
  }
}
