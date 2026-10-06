/* Testing native hardware SPI to push out 64 LED color info. 
    Board: Teensy 4.0
    PWM Hardware: Adafruit 24-Channel 12-bit PWM LED Driver
    Author: John Wiltse (With help from Google AI)
    Date: 10/2026
    Revisions:
*/
#include <Arduino.h>
#include <SPI.h>

#define NUM_BOARDS    8
#define CHANNELS_PER  24
#define TOTAL_CHANNELS (NUM_BOARDS * CHANNELS_PER) // 192 channels total
#define TOTAL_LEDS (NUM_BOARDS * 8) //each module can support 8 RGB leds

// Each board needs 288 bits (36 bytes). 8 boards = 288 bytes total.
#define DATA_BYTES    (NUM_BOARDS * 36)

//pins for the TLC5947
#define PWM_LATCH_PIN     10
// this pin is tied with the latch pin on the TLC5947, fixes a pulsing issue when the latch pin is toggled too quickly
//#define PWM_OE            23


//Pins for the decoder
#define DECODER_A0  0
#define DECODER_A1  1
#define DECODER_A2  2
#define DECODER_LE  4

const int CHANNELS_PER_LEVEL = 192; //in pairs of 3 (RGB) = 64 RGB leds per level
const int CHANNELS_PER_LED = 3; // R, G, B

const int milisBetweenAnimations = 250; //every 250 ms, we will update the animation sequence
elapsedMillis animationTimer; // Timer to track when to update the animation sequence

// Array to hold our raw byte data to be blasted over hardware SPI
uint8_t ledBuffer[DATA_BYTES];
//Full cube animation sequence: 8 levels, 192 channels each. 1 LED takes 3 channels (R,G,B) Each channel is a 12-bit value (0-4095)
// 8 levels * 192 LEDs (in pairs of 3) = 1536 total individule cathode elements
std::vector<uint16_t> animationSequence(1536, 0);

//250KHz * 31 then shift row?? each row holds for 124 microseconds = MUX 8KHz
volatile int currentLevel = 0;
volatile bool levelReadyToSwitch = false;

IntervalTimer levelTimer;

//function templates
void setChannel(uint16_t channel, uint16_t value);
void clearAnimationSequence();
//void writeLEDs();
//void setRGB(uint16_t ledIndex, uint16_t r, uint16_t g, uint16_t b);
void packBufferForLevel(int level);
void runBackgroundAnimationsAndMath();
uint16_t getAnimationValue(const std::vector<uint16_t>& vec, int level, int channel);
void setLedRGB(std::vector<uint16_t>& vec, int level, int led, uint16_t r, uint16_t g, uint16_t b); 
void getLedRGB(const std::vector<uint16_t>& vec, int level, int led, uint16_t& r, uint16_t& g, uint16_t& b);

/*  ISR to switch rows being shown.
    We set the address on the CD74HCT137E decoder pins A0:A2 then latch
*/
void levelChangerISR() {
  currentLevel = (currentLevel + 1) & 0x07;
  
  //Set the next level on the decoder pins A0:A2 and latch it
  digitalWriteFast(DECODER_LE, LOW);

  digitalWriteFast(DECODER_A0, (currentLevel >> 0) & 0x01);
  digitalWriteFast(DECODER_A1, (currentLevel >> 1) & 0x01);
  digitalWriteFast(DECODER_A2, (currentLevel >> 2) & 0x01);
  
  digitalWriteFast(DECODER_LE, HIGH);

  //now latch the data buffered into the TLC5947
  digitalWriteFast(PWM_LATCH_PIN, HIGH); // Latch the address
  delayNanoseconds(20); // Teensy 4.0 is too fast; needs a brief pause
  digitalWriteFast(PWM_LATCH_PIN, LOW);
  
  levelReadyToSwitch = true; // Flag to indicate the level has been switched and ready to buffer the next level's data for the next interrupt
}

/* Setup()
   called once on startup */
void setup() {

  Serial.begin(115200);
  while (!Serial && millis() < 4000); // Wait up to 4 seconds for serial monitor to open
  Serial.println("\n\nHello World from Teensy 4.0! Starting LED Cube Test...");

  // Setup TLC5947 Control Pins
  // this pin is tied with the latch pin on the TLC5947, fixes a pulsing issue when the latch pin is toggled too quickly
  //pinMode(PWM_OE, INPUT_PULLUP);
  pinMode(PWM_LATCH_PIN, OUTPUT);

  //digitalWriteFast(PWM_OE, HIGH); //turn whole cube off to begin with

  // Setup Decoder Address Pins
  pinMode(DECODER_A0, OUTPUT);
  pinMode(DECODER_A1, OUTPUT);
  pinMode(DECODER_A2, OUTPUT);
  pinMode(DECODER_LE, OUTPUT);

  digitalWriteFast(DECODER_A0, LOW);
  digitalWriteFast(DECODER_A1, LOW);
  digitalWriteFast(DECODER_A2, LOW);
  digitalWriteFast(DECODER_LE, LOW);

  //fire up the interrupt to change levels every 2000ms -> 500Hz refresh rate for the cube
  levelTimer.begin(levelChangerISR, 200000);
  
  // Initialize the Teensy hardware SPI bus
  SPI.begin();

  clearAnimationSequence();

  //Test the helpers: Set Level 2, LED 8 to bright Purple (Max Red, No Green, Max Blue)
  //setLedRGB(animationSequence, 2, 8, 4095, 0, 4095);


  //random sample animation sequence for testing. Each level has 192 channels (8 boards * 24 channels) (Full cube)
  // 8 levels * 192 LEDs * 3 colors = 4608 total elements
  //first TCC in series is at the end of the array, last TCC in series is at the beginning of the array
  animationSequence = {
    255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, //level 0
    1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, //level 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 8, //level 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 16, //level 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 32, //level 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 64, //level 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 128, //level 6 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 0, 0, 0, 256, //level 7 
  };
}

/* loop()
   repeatedly run */
void loop() {

 // Serial.println("Main loop running. Current Level: " + String(currentLevel));

  if(levelReadyToSwitch) {
    levelReadyToSwitch = false;

    int nextLevel = (currentLevel + 1) & 0x07;

    //Pack 12-bit animation data into the 8-bit SPI buffer
    packBufferForLevel(nextLevel);

    //Blast data via Hardware SPI
    //Teensy 4.0 easily supports up to 30MHz SPI clocks for the TLC5947
    SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(ledBuffer, NULL, 288); // Direct block transfer was 288?
    SPI.endTransaction();

    //Dont latch the data until the next level is ready to be displayed. The ISR will handle latching the data when the next level is switched.
    //digitalWrite(PWM_LATCH_PIN, HIGH); 
    //delayMicroseconds(1);           // A brief pause to ensure the chip sees it
    //digitalWrite(PWM_LATCH_PIN, LOW);  

    // Example: Read those values back later
    //uint16_t currentR, currentG, currentB;
    //getLedRGB(animationSequence, 0, 63, currentR, currentG, currentB);
    //Serial.printf("R=%d, G=%d, B=%d\n", currentR, currentG, currentB);
  }

  runBackgroundAnimationsAndMath(); 
}

// Sets a specific channel (0 to 191) to a brightness value (0 to 4095)
void setChannel(uint16_t channel, uint16_t value) {
  if (channel >= TOTAL_CHANNELS) return;
  if (value > 4095) value = 4095; // Cap at 12-bit max

  // The TLC5947 expects data shifted in MSB-first, starting with the highest channel.
  // We reverse the indexing so channel 0 in code maps to the correct physical pin.
  uint16_t reversedChannel = TOTAL_CHANNELS - 1 - channel;

  int bitPos = reversedChannel * 12;
  int bytePos = bitPos / 8;
  int offset = bitPos % 8;

  if (offset == 0) {
    ledBuffer[bytePos] = (value >> 4) & 0xFF;
    ledBuffer[bytePos + 1] = (ledBuffer[bytePos + 1] & 0x0F) | ((value << 4) & 0xF0);
  } else {
    ledBuffer[bytePos] = (ledBuffer[bytePos] & 0xF0) | ((value >> 8) & 0x0F);
    ledBuffer[bytePos + 1] = value & 0xFF;
  }
}

void clearAnimationSequence() {
  //Serial.println("Clearing animation sequence.");
  memset(animationSequence.data(), 0, animationSequence.size() * sizeof(uint16_t));
}

// Helper to pack 12-bit values tightly into 8-bit bytes for SPI
void packBufferForLevel(int level) {
  int byteIdx = 0;
  // Step through all 192 channels (8 boards * 24) in pairs of two channels
  // because two 12-bit values (24 bits) fit perfectly into 3 bytes.
  for (int ch = 0; ch < 192; ch += 2) {
    uint16_t pwm1 = getAnimationValue(animationSequence, level, ch);
    uint16_t pwm2 = getAnimationValue(animationSequence, level, ch + 1);

    // Pack 24 bits into 3 consecutive buffer bytes
    ledBuffer[byteIdx++] = (pwm1 >> 4) & 0xFF;
    ledBuffer[byteIdx++] = ((pwm1 & 0x0F) << 4) | ((pwm2 >> 8) & 0x0F);
    ledBuffer[byteIdx++] = pwm2 & 0xFF;
  }
}

uint16_t getAnimationValue(const std::vector<uint16_t>& vec, int level, int channel) {
  //TODO: figure out where the animation data is coming from. For now, just return a constant value for testing.
  //      We are going to need an array of 8 levels, each with 192 channels, to hold the animation data.
  return vec[(level * CHANNELS_PER_LEVEL) + channel];
}

void runBackgroundAnimationsAndMath() {
  // Math, frames updates, serial communication, etc.

  //TODO: setup animation timer. This would update any animation way too fast

  if(animationTimer >= milisBetweenAnimations) {
    animationTimer = 0;
    // Update animation state here
    //Serial.println("Updating animation state...");

    clearAnimationSequence();

    //testing the last 8 (since I only have 1 board wired up right now) 
    //LEDs on the last level (level 0) to see if they are working

    //TLC-8 off the MC frount row
    setLedRGB(animationSequence, 0, 0, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 7, 0, 512, 0);   //bright white kind of

    //TLC-7 off the MC
    setLedRGB(animationSequence, 0, 8, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 15, 0, 512, 0);  //bright white kind of

    //TLC-6 off the MC
    setLedRGB(animationSequence, 0, 16, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 23, 0, 512, 0);   //bright white kind of

    //TLC-5 off the MC
    setLedRGB(animationSequence, 0, 24, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 31, 0, 512, 0);   //bright white kind of

    //TLC-4 off the MC
    setLedRGB(animationSequence, 0, 32, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 39, 0, 512, 0);   //bright white kind of

    //TLC-3 off the MC
    setLedRGB(animationSequence, 0, 40, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 47, 0, 512, 0);   //bright white kind of

    //TLC-2 off the MC
    setLedRGB(animationSequence, 0, 48, 0, 0, 2048);  //bright white kind of
    setLedRGB(animationSequence, 0, 55, 0, 512, 0);   //bright white kind of

    /*TLC-1 off the MC back row*/
    setLedRGB(animationSequence, 0, 56, 128, 0, 0);   //red
    setLedRGB(animationSequence, 1, 57, 0, 256, 0);   //green
    setLedRGB(animationSequence, 2, 58, 0, 0, 256);   //blue
    setLedRGB(animationSequence, 3, 59, 0, 0, 4095);  //bright blue
    setLedRGB(animationSequence, 4, 60, 0, 1024, 0);  //bright green
    setLedRGB(animationSequence, 5, 61, 1024, 0, 0);  //red
    setLedRGB(animationSequence, 6, 62, 512, 0, 512); //magenta
    setLedRGB(animationSequence, 7, 63, 4095, 4095, 4095);  //bright white kind of

  }

  //for(int i = 62; i > 0; i--) {
  //  setLedRGB(animationSequence, 0, i, 4095, 0, 0); // Set the first LED of each level to red
 // }
 
}

// --- Helper to SET an RGB LED ---
void setLedRGB(std::vector<uint16_t>& vec, int level, int led, uint16_t r, uint16_t g, uint16_t b) {
    // Calculate where the Red channel starts for this specific LED
    int baseIndex = (level * CHANNELS_PER_LEVEL) + (led * CHANNELS_PER_LED);
    
    vec[baseIndex + 2] = r; // Red
    vec[baseIndex + 1] = g; // Green
    vec[baseIndex] = b; // Blue
}

// --- Helper to READ an RGB LED ---
// We pass r, g, and b by reference so the function can modify them directly
void getLedRGB(const std::vector<uint16_t>& vec, int level, int led, uint16_t& r, uint16_t& g, uint16_t& b) {
    int baseIndex = (level * CHANNELS_PER_LEVEL) + (led * CHANNELS_PER_LED);

   // Serial.printf("Reading LED at Level %d, Index %d: Base Index = %d\n", level, led, baseIndex);
    
    r = vec[baseIndex + 2];
    g = vec[baseIndex + 1];
    b = vec[baseIndex];
}