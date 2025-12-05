#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#define LCD_ADDRESS 0x27   // too scared to touch this macro tbh
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DHT11.h>

// ================== pin assignments ==================
// L293D motor driver pins
const int motorIn1Pin = 3;
const int motorIn2Pin = 9;
const int motorEnablePin = 5;

// pushbutton pins (active LOW)
const int buttonStartPin = 4;
const int buttonPidPin = 6;
const int buttonStopPin = 10;

// light sensor pin
const int ldrAnalogPin = A2;

// DHT11 on digital pin 2
DHT11 myDht11Sensor(2);

// ================== timing / debounce stuff ==================
const unsigned long pidSampleTimeMs = 50;      // PID measurement every 50 ms (lab said so)
const unsigned long buttonSampleTimeMs = 50;   // debounce button 50 ms (pls no ghost presses)
const unsigned long displayRefreshTimeMs = 15; // update display every 15 ms (>60Hz, gaming LCD)

unsigned long lastPidTimestamp = 0;       // store last PID update time
unsigned long lastButtonTimestamp = 0;    // store last button check time
unsigned long lastDisplayTimestamp = 0;   // store last LCD update time

// ================== sensor values ==================
int dhtTemperatureF = 0;
int dhtHumidityPercent = 0;
int ldrLightReading = 0;

// ================== PID related variables ==================
float filterAlpha = 0.9;     // exponential filter factor (aka "magic" number)
float pidKp = 15;
float pidKi = 15;
float pidKd = 0.3;

float filteredPvValue = 0.0; // filtered process variable
float pidIntegralAccum = 0.0;

int speedSetpoint = 0;       // desired motor "speed" from remote

int motorBackEmfPin1 = 0;
int motorBackEmfPin2 = 0;

float prevMotorBackEmfPin1 = 0.0;
float prevMotorBackEmfPin2 = 0.0;
float prevFilteredPvValue = 0.0;

float pidOutputLimited = 0.0; // final PID signal after constrain()
float motorPwmOutput = 0.0;   // what we actually send to EN pin

// ================== button / motor control flags ==================
bool motorDirectionClockwise = 0;  // 0 = one way, 1 = the other way, we forgot which lol
bool pidControlEnabled = 1;        // 1 = PID mode, 0 = manual mode
bool motorIsEnabled = 1;           // master motor enable
bool lastPidToggleState = 1;       // last known PID enable state (for RF toggle)

// ================== nRF24L01 radio config ==================
RF24 wirelessRadioModule(7, 8);           // CE, CSN (hardwired in lab kit)
const byte radioPipeAddress[6] = "P2U80"; // don't touch, this just works

char rxPayloadBuffer[10] = {0};           // incoming data from transmitter (string-ish)
int ackPayloadArray[4] = {0, 0, 0, 0};    // data we send back as ACK (basic telemetry)

// ================== LCD config ==================
LiquidCrystal_I2C statusLcd(LCD_ADDRESS, 16, 2);  // tiny 16x2 but we try our best

void setup() {
    Serial.begin(9600);

    pinMode(buttonStartPin, INPUT_PULLUP);
    pinMode(buttonPidPin, INPUT_PULLUP);
    pinMode(buttonStopPin, INPUT_PULLUP);

    pinMode(motorIn1Pin, OUTPUT);
    pinMode(motorIn2Pin, OUTPUT);
    pinMode(motorEnablePin, OUTPUT);

    // ===== nRF24 setup =====
    wirelessRadioModule.begin();
    wirelessRadioModule.setDataRate(RF24_250KBPS); // slow but "reliable" (in theory)
    wirelessRadioModule.openReadingPipe(1, radioPipeAddress);
    wirelessRadioModule.enableAckPayload();
    wirelessRadioModule.startListening();
    wirelessRadioModule.writeAckPayload(1, &ackPayloadArray, sizeof(ackPayloadArray));

    // ===== LCD setup =====
    statusLcd.init();
    statusLcd.backlight(); // because we like seeing things
}

void loop() {

    // ================== read TX payload from remote ==================
    if (wirelessRadioModule.available())
    {
        while (wirelessRadioModule.available()) {
            // keep reading until we get the latest payload
            wirelessRadioModule.read(&rxPayloadBuffer, sizeof(rxPayloadBuffer));
        }
        // Serial.println(rxPayloadBuffer); // uncomment if you wanna spam serial monitor

        // ----- PID ON/OFF toggle -----
        if (rxPayloadBuffer[0] == 'M' && lastPidToggleState == 1)
        {
            // go to manual mode
            pidControlEnabled = 0;
            lastPidToggleState = 0;
        }
        else if (rxPayloadBuffer[0] == 'S' && lastPidToggleState == 0)
        {
            // go back to PID mode
            pidControlEnabled = 1;
            lastPidToggleState = 1;
        }

        // ----- motor direction + enable from remote -----
        if (rxPayloadBuffer[2] == 'W')
        {
            motorDirectionClockwise = 0;
            motorIsEnabled = 1;
        }
        else if (rxPayloadBuffer[2] == 'Z')
        {
            motorDirectionClockwise = 1;
            motorIsEnabled = 1;
        }
        else if (rxPayloadBuffer[2] == 'E')
        {
            motorIsEnabled = 0;
        }

        // ----- setpoint from payload (starting at index 4 as a string) -----
        speedSetpoint = atoi(&rxPayloadBuffer[4]); // yeah, we're trusting this string
    }

    // ================== build ACK payload back to TX ==================
    ackPayloadArray[0] = (int)dhtTemperatureF;      // temp
    ackPayloadArray[1] = ldrLightReading;          // light
    ackPayloadArray[2] = dhtHumidityPercent;       // humidity
    ackPayloadArray[3] = (int)filteredPvValue;     // motor "speed" feedback (ish)
    wirelessRadioModule.writeAckPayload(1, &ackPayloadArray, sizeof(ackPayloadArray));

    // ================== motor direction + enable control ==================
    if (motorIsEnabled)
    {
        if (motorDirectionClockwise) { // flip this if the motor goes backwards IRL
            digitalWrite(motorIn2Pin, HIGH);
            digitalWrite(motorIn1Pin, LOW);
        }
        else
        {
            digitalWrite(motorIn2Pin, LOW);
            digitalWrite(motorIn1Pin, HIGH);
        }
    }
    else
    {
        // fully stop motor
        digitalWrite(motorIn2Pin, LOW);
        digitalWrite(motorIn1Pin, LOW);
    }

    unsigned long currentMillisTime = millis();

    // ================== button debounce + logic ==================
    if (currentMillisTime - lastButtonTimestamp >= buttonSampleTimeMs) {
        // we only check buttons every 50 ms to avoid them being drama queens
        if (!digitalRead(buttonStartPin))
        {
            lastButtonTimestamp += buttonSampleTimeMs;
            motorIsEnabled = 1; // go brrr
        }
        else if (!digitalRead(buttonStopPin))
        {
            lastButtonTimestamp += buttonSampleTimeMs;
            motorIsEnabled = 0; // no brrr
        }
        else if (!digitalRead(buttonPidPin))
        {
            lastButtonTimestamp += buttonSampleTimeMs;
            lastPidToggleState = pidControlEnabled;
            pidControlEnabled = !pidControlEnabled; // manual <-> PID
        }
    }

    // ================== PID control loop (motor speed control) ==================
    if (currentMillisTime - lastPidTimestamp >= pidSampleTimeMs) {
        lastPidTimestamp += pidSampleTimeMs;

        // briefly turn off motor so we can read Back-EMF (this feels illegal but it's fine)
        analogWrite(motorEnablePin, LOW);
        delay(2);  // tiny delay so driver settles

        int rawMotorBackEmfPin1 = map(analogRead(A0), 0, 1023, 0, 255);
        int rawMotorBackEmfPin2 = map(analogRead(A1), 0, 1023, 0, 255);

        // turn motor back on with last PWM
        analogWrite(motorEnablePin, motorPwmOutput);

        // filter measurements because they are super noisy
        motorBackEmfPin1 = filterAlpha * prevMotorBackEmfPin1 + (1 - filterAlpha) * rawMotorBackEmfPin1;
        motorBackEmfPin2 = filterAlpha * prevMotorBackEmfPin2 + (1 - filterAlpha) * rawMotorBackEmfPin2;

        filteredPvValue = (motorDirectionClockwise) ? motorBackEmfPin1 : motorBackEmfPin2;
        prevFilteredPvValue = (motorDirectionClockwise) ? prevMotorBackEmfPin1 : prevMotorBackEmfPin2;

        // ===== PID math from Lab 6 (rewritten with more panicking) =====
        float pidError = (float)speedSetpoint - filteredPvValue;
        float pidProportional = pidKp * pidError;

        // integrate error over time
        pidIntegralAccum = pidIntegralAccum + pidKi * (pidSampleTimeMs / 1000.0) * pidError;

        // derivative on measurement (less noisy they said…)
        float pidDerivative = -pidKd * (filteredPvValue - prevFilteredPvValue) / (pidSampleTimeMs / 1000.0);

        prevMotorBackEmfPin1 = motorBackEmfPin1;
        prevMotorBackEmfPin2 = motorBackEmfPin2;

        float pidUnsaturatedOutput = pidProportional + pidIntegralAccum + pidDerivative;

        // clamp output to valid PWM range
        pidOutputLimited = constrain(pidUnsaturatedOutput, 0, 255);

        // anti-windup: undo integral if we're stuck at a limit
        if ((pidOutputLimited == 255 && pidError > 0) || (pidOutputLimited == 0 && pidError < 0)) {
            pidIntegralAccum -= pidKi * (pidSampleTimeMs / 1000.0) * pidError;
        }

        // if PID is enabled, use PID output; else go full "manual override"
        if (pidControlEnabled)
        {
            motorPwmOutput = pidOutputLimited;
        }
        else
        {
            motorPwmOutput = speedSetpoint; // direct control from remote (pls be careful)
        }

        analogWrite(motorEnablePin, motorPwmOutput);
    }

    // ================== LCD + sensor display stuff ==================
    if (currentMillisTime - lastDisplayTimestamp >= displayRefreshTimeMs)
    {
        lastDisplayTimestamp += displayRefreshTimeMs;

        // --- read DHT11 (temp + humidity) ---
        // yes this blocks a tiny bit but it's lab code so it's fine
        myDht11Sensor.readTemperatureHumidity(dhtTemperatureF, dhtHumidityPercent);

        // --- read LDR (aka "how bright is it") ---
        ldrLightReading = analogRead(ldrAnalogPin);

        // --- update LCD content ---
        statusLcd.setCursor(0, 0);
        statusLcd.print("M:");

        // print mode
        statusLcd.print((pidControlEnabled) ? "PID    " : "Manual ");

        statusLcd.setCursor(11, 0);
        statusLcd.print("T:");
        statusLcd.print(dhtTemperatureF);

        statusLcd.setCursor(0, 1);
        statusLcd.print("S:");
        statusLcd.print(speedSetpoint);
        statusLcd.print(" ");

        statusLcd.setCursor(6, 1);
        statusLcd.print("P:");
        statusLcd.print((int)motorPwmOutput);
        statusLcd.print(" ");

        statusLcd.print("H:");
        statusLcd.print(dhtHumidityPercent);

        // statusLcd.print(rxPayloadBuffer);   // debug, if we ever need it again
        // Serial.println(rxPayloadBuffer);    // same deal

        // spam some useful stuff to serial monitor for debugging
        Serial.print("Temp (F): ");
        Serial.println(dhtTemperatureF);
        Serial.print("Humidity (%): ");
        Serial.println(dhtHumidityPercent);
    }
}
