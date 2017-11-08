# PDP - Pocket Data Pigeon

PDP is an Arduino firmware that can automatically send and receive files over inexpensive NRF24L01+ radios. The files are sent from, or downloaded to, an SD card. PDP supports chunking of files and secure peer-to-peer exchange of chunks, similar to BitTorrent. Chunk integrity is ensured with [Merkle trees](https://en.wikipedia.org/wiki/Merkle_tree) and SHA256 sums via the [usha256](https://github.com/hypoactiv/usha256) library.

## Limitations

PDP is currently limited to small files (768KiB maximum) due to the small RAM limit of the ATmega MCU. Files over this limit are ignored and not broadcast. Transmission rates are also relatively slow. However, it is fit for the purpose of transmitting text files and small images.

## Parts List

You will want to build at least 2 PDPs, so that they can talk to eachother.

* 2x Arduino with ATmega328 MCU. Arduino Uno or Arduino Pro Mini (328) are most common.
* 2x NRF24L01+ radio modules
* 2x SD card modules. If you wish to power the SD card with 5v, make sure you buy modules with power regulators built in.
* 2x SD cards
* 2x USB power supplies, or other power supply for your Arduinos

## Connecting the NRF24L01+

| Radio Pin | Arduino Pin |
|-----------|-------------|
|     V+    |     3.3v    |
|    GND    |     GND     |
|    CSN    |      7      |
|     CE    |      6      |
|    MOSI   |      11     |
|    MISO   |      12     |
|    SCK    |      13     |

## Connecting the SD card module

| SD Pin |            Arduino Pin            |
|--------|:---------------------------------:|
|   CS   |                 A0                |
|   SCK  |                 A1                |
|  MOSI  |                 A2                |
|  MISO  |                 A3                |
|   VCC  | 3.3v or 5v depending on SD module |
|   GND  |                GND                |

## Compiling 

Make sure you have SdFat and RF24 libraries installed in your Arduino workspace. PDP uses software SPI to communicate with the SD card, which needs to be manually enabled in SdFat's `SdFatConfig.h` by changing the line

```
#define ENABLE_SOFTWARE_SPI_CLASS 0
```
to

```
#define ENABLE_SOFTWARE_SPI_CLASS 1
```

After this, run
```
make
```
to compile PDP using the included [Arduino makefile](https://github.com/sudar/Arduino-Makefile).

If you choose to compile using the Arduino workspace or any other method, make sure to disable LTO (link time optimization) as it breaks this project and will generate a broken binary.

## Usage

* Format both SD cards as FAT32.
* Place some files in the root of one SD card, insert it into one of the PDP units, and power it on.
* Leave the second SD card empty, insert it into the second unit, and power it on.
* After computing the Merkle trees for the files placed in the SD card root, they will be broadcast.
* You can monitor the progress on the Arduino serial terminal. Once the files have been received, you can power down the units and verify file integrity.

## TODO

PDP is still in its early stages. Many things need to be done to make it more user friendly and robust.

* Implement 1 or more status LEDs to report progress without the serial terminal.
* Better interference detection and channel hopping.
* An Arduino shield PCB to ease assembly and improve physical durability.
* Document wireless protocol
* Test with more than 3 units to test and improve chunk exchange algorithms.
