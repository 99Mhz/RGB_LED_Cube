### Overview

Home to code out and build the 8x8 RGB LED Cube project.
Designer, builder and developer: John Wiltse

### Timeline

    **<2016**
    Built out 8x LED pannels 8x8 (64) LEDS to use for the cube. 
    
    **2026**
    Picked up the project once more to try and make headway!

### Hardware

    Controller: Teensy 3.1 maybe to 4.0
        https://www.pjrc.com/teensy/teensy31.html
        https://www.pjrc.com/store/teensy40.html

    Boards: TBD
    
    LEDs: 512 difused common anode RGB LEDs
    
    PWM Driver: TLC5947 via https://www.adafruit.com/product/1429
    
    Level Shifter: Teensy -> CD74HCT137E(decoder) -> SUP70101EL-GE3(P-Channel MOSFET) (simplified see images/schematic.png for details) 
    
    Other: 
        Tinned wire to tie all cathods together and cube structure. See images/

### Software

    Just getting simple project into source control and switching from Arduino editor to VSCode/PlatformIO. Will be using a Teensy 3.1 until the 4.0 arrives. 

### Theory

    TBD
