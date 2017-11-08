#ifndef DRIVER_H
#define DRIVER_H

#include "RF24.h"
#include "SdFat.h"
#include "stdint.h"

const uint8_t PIN_STATUS_LED = 13;

const uint8_t PIN_RADIO_CE = 6;
const uint8_t PIN_RADIO_CS = 7;

const uint8_t PIN_SD_MISO = 17;
const uint8_t PIN_SD_MOSI = 16;
const uint8_t PIN_SD_SCK = 15;
const uint8_t PIN_SD_CS = 14;

// The last channel to listen on
const uint8_t CHANNEL_MAX = 2;

// How long to listen on a channel before switching to the next (milliseconds)
const uint16_t CHANNEL_TIMEOUT = 1000;

void Run();

extern SdFatSoftSpi<PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_SCK> sd;
extern RF24 radio;

#endif // DRIVER_H
