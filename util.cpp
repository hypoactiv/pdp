#include "RF24.h"
#include "SdFat.h"
#include "usha256.h"

#include "util.h"

namespace PDP {

// Print the 32-byte array 'hash' to Serial in hex
void PrintHash(uint8_t *hash) {
  for (uint8_t i = 0; i < 32; i++) {
    if (hash[i] < 0x10) {
      Serial.print('0'); // print leading 0 for this byte
    }
    Serial.print(hash[i], HEX);
  }
  Serial.println();
  Serial.flush();
}

// Collect entropy from the filesystem, radio, and analog pin, and use it
// to seed the PRNG.
void CollectEntropy(SdSpiCard *card, RF24 &radio) {
  uint8_t buf[512];
  Sha256Context ctx;
  sha256_init(&ctx);
  // hash MBR of card's FAT filesystem, which includes the volume id,
  // which (presumably) is unique to this card.
  card->readBlock(0, buf);
  sha256_update(&ctx, buf, 512);
  // perform a channel scan, and hash the results
  for (uint8_t ch = 0; ch < 126; ch++) {
    radio.setChannel(ch);
    buf[ch] = 0;
    for (uint8_t i = 0; i < 10; i++) {
      radio.startListening();
      delayMicroseconds(128);
      // clear any received packet
      // TODO experiment
      if (radio.available()) {
        radio.read(buf, 32);
      }
      radio.stopListening();
      buf[ch] += radio.testCarrier() ? 0 : 1;
    }
  }
  sha256_update(&ctx, buf, 128);
  // read A5, an unconnected analog pin, and hash the results
  for (uint8_t i = 0; i < 100; i++) {
    int analog;
    analog = analogRead(5);
    sha256_update(&ctx, analog, sizeof(analog));
  }
  radio.setChannel(0);
  sha256_final(&ctx, buf);
  // Seed the PRNG with the first 4 bytes of the hash
  randomSeed(*(long *)buf);
}

Error_t OpenFile(FatFileSystem &fs, const char *filename, FatFile &f,
                 uint8_t flag) {
  f = fs.open(filename, flag);
  if (!f.isOpen()) {
    return ERROR_IO_OPEN;
  }
  return ERROR_NONE;
}

// Create a filename on fs of size filesize, filled with 0's, with
// its opened handle in f
Error_t Create(FatFileSystem &fs, FatFile &f, char *filename,
               uint32_t filesize) {
  uint8_t buf[32];
  if (fs.exists(filename)) {
    // refuse to overwrite
    return ERROR_IO_ALREADY_EXISTS;
  }
  if (OpenFile(fs, filename, f, O_CREAT | O_TRUNC | O_RDWR)) {
    return ERROR_IO_OPEN;
  }
  memset(buf, 0, 32);
  while (filesize > 0) {
    uint32_t b;
    b = (filesize > 32) ? 32 : filesize;
    b = f.write(buf, b);
    if (b == 0) {
      f.close();
      return ERROR_IO_WRITE;
    }
    filesize -= b;
  }
  f.sync();
  return ERROR_NONE;
}

} // namespace PDP
