#include <Arduino.h>

/* Setup()
   called once on startup */
void setup() {

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  while (!Serial && millis() < 4000); // Wait up to 4 seconds for serial monitor to open
  Serial.println("\n\nHello World from Teensy 3.1!");
}

/* loop()
   repeatedly run */
void loop() {

  //classic microcontroller hello world!
  digitalWrite(LED_BUILTIN, HIGH);   // Turn the LED on
  delay(1000);              // Wait for 1 second
  digitalWrite(LED_BUILTIN, LOW);    // Turn the LED off
  delay(1000);              // Wait for 1 second
}

