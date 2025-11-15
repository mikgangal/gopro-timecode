#include <Arduino.h>

constexpr uint8_t kLedPin = 2;

void reportLedState(bool is_on) {
  Serial.print("LED is ");
  Serial.println(is_on ? "ON" : "OFF");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(kLedPin, OUTPUT);
  reportLedState(false);
}

void loop() {
  digitalWrite(kLedPin, HIGH);
  reportLedState(true);
  delay(2000);

  digitalWrite(kLedPin, LOW);
  reportLedState(false);
  delay(1000);
}