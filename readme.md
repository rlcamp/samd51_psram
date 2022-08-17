# SAMD51 PSRAM

Non-blocking DMA read and write access to a discrete SPI-attached PSRAM chip from a SAMD51

## Prerequisites for library

- CMSIS and CMSIS-Atmel headers from ARM and Microchip respectively, arm-none-eabi-gcc toolchain (an installation of the Arduino IDE set up with support for the Adafruit Feather M4 provides these, or they can be obtained from upstream)

## Prerequisites for demo

- Arduino environment, trivially modifiable to not depend on it
- Adafruit Feather M4 or Circuitbrains Deluxe
- 8 MB PSRAM chip such as https://www.adafruit.com/product/4677 or https://www.pjrc.com/store/psram.html
