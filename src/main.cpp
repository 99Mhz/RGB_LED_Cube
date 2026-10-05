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
#define PWM_OE            23


//Pins for the decoder
#define DECODER_A0  0
#define DECODER_A1  1
#define DECODER_A2  2
#define DECODER_LE  4

const int CHANNELS_PER_LEVEL = 192; //in pairs of 3 (RGB) = 64 RGB leds per level
const int CHANNELS_PER_LED = 3; // R, G, B

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
void clearAll();
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
  digitalWriteFast(PWM_OE, HIGH); // Blank display

  currentLevel = (currentLevel + 1) & 0x07;
  //int currentBits = currentLevel;

  digitalWriteFast(DECODER_LE, LOW);

  digitalWriteFast(DECODER_A0, (currentLevel >> 0) & 0x01);
  digitalWriteFast(DECODER_A1, (currentLevel >> 1) & 0x01);
  digitalWriteFast(DECODER_A2, (currentLevel >> 2) & 0x01);

  digitalWriteFast(PWM_LATCH_PIN, HIGH); // Latch the address
  delayNanoseconds(20); // Teensy 4.0 is too fast; needs a brief pause
  digitalWriteFast(PWM_LATCH_PIN, LOW);
  
  digitalWriteFast(PWM_OE, LOW); // Unblank display

  levelReadyToSwitch = true; // Flag to indicate the level has been switched
}

/* Setup()
   called once on startup */
void setup() {

  Serial.begin(115200);
  while (!Serial && millis() < 4000); // Wait up to 4 seconds for serial monitor to open
  Serial.println("\n\nHello World from Teensy 4.0! Starting LED Cube Test...");

  // Setup TLC5947 Control Pins
  pinMode(PWM_OE, INPUT_PULLUP);
  pinMode(PWM_LATCH_PIN, OUTPUT);

  digitalWriteFast(PWM_OE, HIGH); //turn whole cube off to begin with

  // Setup Decoder Address Pins
  pinMode(DECODER_A0, OUTPUT);
  pinMode(DECODER_A1, OUTPUT);
  pinMode(DECODER_A2, OUTPUT);
  pinMode(DECODER_LE, OUTPUT);

  //fire up the interrupt to change levels every 2000ms -> 500Hz refresh rate for the cube
  levelTimer.begin(levelChangerISR, 2000);
  
  // Initialize the Teensy hardware SPI bus
  SPI.begin();

  //Test the helpers: Set Level 2, LED 8 to bright Purple (Max Red, No Green, Max Blue)
  setLedRGB(animationSequence, 2, 8, 4095, 0, 4095);

  // Example: Read those values back later
  uint16_t currentR, currentG, currentB;
  getLedRGB(animationSequence, 2, 8, currentR, currentG, currentB);
  Serial.printf("LED at Level 2, Index 8: R=%d, G=%d, B=%d\n", currentR, currentG, currentB);
}

/* loop()
   repeatedly run */
void loop() {

  if(levelReadyToSwitch) {
    levelReadyToSwitch = false;

    int nextLevel = (currentLevel + 1) & 0x07;

    //Pack 12-bit animation data into the 8-bit SPI buffer
    packBufferForLevel(nextLevel);

    //Blast data via Hardware SPI
    //Teensy 4.0 easily supports up to 30MHz SPI clocks for the TLC5947
    SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(ledBuffer, NULL, 288); // Direct block transfer
    SPI.endTransaction();
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

// Clears the buffer to 0 (all LEDs off)
void clearAll() {
  memset(ledBuffer, 0, DATA_BYTES);
}

void clearAnimationSequence() {
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

  //random sample animation sequence for testing. Each level has 192 channels (8 boards * 24 channels) (Full cube)
  // 8 levels * 192 LEDs * 3 colors = 4608 total elements
  animationSequence = {
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 2, //level0
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 4, //level1
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 8, //level2
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 16, //level3
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 32, //level4
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 64, //level5
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 128, //level6
    4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 2048, 0, 0, 0, 4095, 0, 0, 0, 4095, 0, 0, 0, 4095, 4095, 4095, 0, 0, 4095, 4095, 4095, 0, 4095, 2048, 2048, 0, 4095, 4095, 4095, 0, 4095, 2048, 256, //level7
  };
}

// --- Helper to SET an RGB LED ---
void setLedRGB(std::vector<uint16_t>& vec, int level, int led, uint16_t r, uint16_t g, uint16_t b) {
    // Calculate where the Red channel starts for this specific LED
    int baseIndex = (level * CHANNELS_PER_LEVEL) + (led * CHANNELS_PER_LED);
    
    vec[baseIndex]     = r; // Red
    vec[baseIndex + 1] = g; // Green
    vec[baseIndex + 2] = b; // Blue
}

// --- Helper to READ an RGB LED ---
// We pass r, g, and b by reference so the function can modify them directly
void getLedRGB(const std::vector<uint16_t>& vec, int level, int led, uint16_t& r, uint16_t& g, uint16_t& b) {
    int baseIndex = (level * CHANNELS_PER_LEVEL) + (led * CHANNELS_PER_LED);
    
    r = vec[baseIndex];
    g = vec[baseIndex + 1];
    b = vec[baseIndex + 2];
}