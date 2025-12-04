#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin Definitions
#define CE_PIN 9
#define CSN_PIN 10

// Radio Configuration
RF24 radio(CE_PIN, CSN_PIN);
const byte txAddress[6] = "1Node";  // Transmit address
const byte rxAddress[6] = "2Node";  // Receive address for telemetry

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Command Structure to Send
struct CommandPacket {
  char direction[5];     // "FWD", "REV", "STOP"
  int setpoint;          // 0-255 (PWM % or RPM setpoint)
  bool pidEnabled;       // true/false
  char checksum;         // Simple validation
};

// Telemetry Structure to Receive
struct TelemetryPacket {
  int motorSpeed;        // Current speed (RPM or PWM feedback)
  float temperature;     // TC74A0 temperature
  float humidity;        // DHT11 humidity
  int lightLevel;        // LDR reading (0-1023)
  char status[10];       // "RUNNING", "STOPPED", etc.
  char checksum;         // Simple validation
};

CommandPacket cmdPacket;
TelemetryPacket telemetry;

// Command buffer for Serial parsing
char cmdBuf[64];
int cmdIndex = 0;

// Display timing
unsigned long lastLCDUpdate = 0;
unsigned long lastTelemetryRequest = 0;
const unsigned long LCD_UPDATE_INTERVAL = 500;
const unsigned long TELEMETRY_INTERVAL = 200;

// LCD display mode
enum DisplayMode { SHOW_COMMAND, SHOW_TELEMETRY };
DisplayMode displayMode = SHOW_COMMAND;
unsigned long displayModeTimer = 0;

void setup() {
  // Initialize Serial communication with LabVIEW
  Serial.begin(9600);
  delay(100);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Final Lab TX");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Initialize Radio
  if (!radio.begin()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Radio FAIL!");
    Serial.println("ERROR:RADIO_INIT_FAILED");
    while (1);
  }
  
  // Configure radio for bidirectional communication
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(5, 15);
  radio.openWritingPipe(txAddress);
  radio.openReadingPipe(1, rxAddress);
  
  // Initialize command packet with defaults
  strcpy(cmdPacket.direction, "STOP");
  cmdPacket.setpoint = 0;
  cmdPacket.pidEnabled = false;
  cmdPacket.checksum = 0;
  
  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready");
  
  // Send ready signal to LabVIEW
  Serial.println("READY");
  Serial.flush();
}

void loop() {
  // Read commands from LabVIEW
  readSerialCommands();
  
  // Periodically request telemetry from receiver
  if (millis() - lastTelemetryRequest > TELEMETRY_INTERVAL) {
    requestTelemetry();
    lastTelemetryRequest = millis();
  }
  
  // Update LCD display
  if (millis() - lastLCDUpdate > LCD_UPDATE_INTERVAL) {
    updateLCD();
    lastLCDUpdate = millis();
  }
  
  // Toggle display mode every 3 seconds
  if (millis() - displayModeTimer > 3000) {
    displayMode = (displayMode == SHOW_COMMAND) ? SHOW_TELEMETRY : SHOW_COMMAND;
    displayModeTimer = millis();
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    // Build command until newline
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdIndex] = '\0';
      
      if (cmdIndex > 0) {
        parseAndSendCommand(cmdBuf);
        cmdIndex = 0;
      }
    }
    else if (cmdIndex < 63 && c >= 32 && c <= 126) {
      cmdBuf[cmdIndex++] = c;
    }
  }
}

void parseAndSendCommand(char* cmd) {
  String command = String(cmd);
  command.trim();
  
  // Echo received command
  Serial.print("RX:");
  Serial.println(command);
  
  // Parse command format: "COMMAND:VALUE"
  // Examples: "FWD", "STOP", "SETPOINT:150", "PID:ON", "PID:OFF"
  
  int colonIndex = command.indexOf(':');
  String cmdType = command;
  String cmdValue = "";
  
  if (colonIndex > 0) {
    cmdType = command.substring(0, colonIndex);
    cmdValue = command.substring(colonIndex + 1);
    cmdType.toUpperCase();
    cmdValue.toUpperCase();
  } else {
    cmdType.toUpperCase();
  }
  
  bool validCommand = true;
  
  // Process direction commands
  if (cmdType == "F" || cmdType == "FWD" || cmdType == "FORWARD") {
    strcpy(cmdPacket.direction, "FWD");
  }
  else if (cmdType == "R" || cmdType == "REV" || cmdType == "REVERSE") {
    strcpy(cmdPacket.direction, "REV");
  }
  else if (cmdType == "S" || cmdType == "STOP") {
    strcpy(cmdPacket.direction, "STOP");
  }
  // Process setpoint command
  else if (cmdType == "SETPOINT" || cmdType == "SET" || cmdType == "SP") {
    int value = cmdValue.toInt();
    if (value >= 0 && value <= 255) {
      cmdPacket.setpoint = value;
    } else {
      validCommand = false;
      Serial.println("ERROR:SETPOINT_OUT_OF_RANGE");
    }
  }
  // Process PID enable/disable
  else if (cmdType == "PID") {
    if (cmdValue == "ON" || cmdValue == "1" || cmdValue == "TRUE" || cmdValue == "ENABLE") {
      cmdPacket.pidEnabled = true;
    }
    else if (cmdValue == "OFF" || cmdValue == "0" || cmdValue == "FALSE" || cmdValue == "DISABLE") {
      cmdPacket.pidEnabled = false;
    }
    else {
      validCommand = false;
      Serial.println("ERROR:INVALID_PID_VALUE");
    }
  }
  else {
    validCommand = false;
    Serial.println("ERROR:UNKNOWN_COMMAND");
  }
  
  // If valid command, transmit it
  if (validCommand) {
    transmitCommand();
  }
}

void transmitCommand() {
  // Calculate simple checksum
  cmdPacket.checksum = (cmdPacket.direction[0] + cmdPacket.setpoint + cmdPacket.pidEnabled) & 0xFF;
  
  // Stop listening to transmit
  radio.stopListening();
  
  // Transmit command packet
  bool success = radio.write(&cmdPacket, sizeof(cmdPacket));
  
  // Send status back to LabVIEW
  if (success) {
    Serial.println("ACK");
    
    // Show on LCD briefly
    displayMode = SHOW_COMMAND;
    displayModeTimer = millis();
  } else {
    Serial.println("FAIL");
  }
  
  // Resume listening for telemetry
  radio.startListening();
}

void requestTelemetry() {
  // Listen for telemetry from receiver
  radio.startListening();
  
  if (radio.available()) {
    radio.read(&telemetry, sizeof(telemetry));
    
    // Validate checksum
    char expectedChecksum = (telemetry.motorSpeed + (int)telemetry.temperature + 
                            (int)telemetry.humidity + telemetry.lightLevel) & 0xFF;
    
    if (telemetry.checksum == expectedChecksum) {
      // Send telemetry to LabVIEW
      Serial.print("TELEM:");
      Serial.print(telemetry.motorSpeed);
      Serial.print(",");
      Serial.print(telemetry.temperature, 1);
      Serial.print(",");
      Serial.print(telemetry.humidity, 1);
      Serial.print(",");
      Serial.print(telemetry.lightLevel);
      Serial.print(",");
      Serial.println(telemetry.status);
    }
  }
}

void updateLCD() {
  lcd.clear();
  
  if (displayMode == SHOW_COMMAND) {
    // Display current command settings
    lcd.setCursor(0, 0);
    lcd.print(cmdPacket.direction);
    lcd.print(" SP:");
    lcd.print(cmdPacket.setpoint);
    
    lcd.setCursor(0, 1);
    if (cmdPacket.pidEnabled) {
      lcd.print("PID:ON");
    } else {
      lcd.print("PID:OFF");
    }
  }
  else {
    // Display telemetry data
    lcd.setCursor(0, 0);
    lcd.print("RPM:");
    lcd.print(telemetry.motorSpeed);
    lcd.print(" T:");
    lcd.print((int)telemetry.temperature);
    lcd.print("C");
    
    lcd.setCursor(0, 1);
    lcd.print("H:");
    lcd.print((int)telemetry.humidity);
    lcd.print("% L:");
    lcd.print(telemetry.lightLevel);
  }
}