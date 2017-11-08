#include "RF24.h"
#include "SdFat.h"
#include "driver.h"
#include "merkle.h"
#include "message.h"
#include "multipart.h"
#include "pdp.h"
#include "receiver.h"
#include "stackmon.h"
#include "transmitter.h"
#include "usha256.h"
#include "util.h"
#include "power.h"
#include <SPI.h>

#include <avr/wdt.h>
#include <avr/sleep.h>

SdFatSoftSpi<PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_SCK> sd;
RF24 radio(PIN_RADIO_CE, PIN_RADIO_CS);

using namespace PDP;

const uint32_t CHUNK_SIZE = MAX_CHUNK_SIZE;

static uint8_t mode;

void makeTestFile(FatFileSystem &fs) {
  uint8_t buf[CHUNK_SIZE];
  char c;
  /*
  while (1) {
    Serial.println(F("Type 'Y' to create test files."));
    while (!Serial.available()) {
      SysCall::yield();
    }
    c = Serial.read();
    if (c != 'Y') {
      return;
    } else {
      break;
    }
  }
  */
  Serial.print(F("test.txt create "));
  FatFile file = fs.open("test.txt", O_CREAT | O_RDWR | O_TRUNC);
  if (!file.isOpen()) {
    Serial.println(F("error"));
    return;
  }
  for (uint16_t i = 0; i < (1 << 7); i++) { // TODO test file size here
    memset(buf, i, CHUNK_SIZE);
    file.write(buf, CHUNK_SIZE);
  }
  file.close();
  Serial.println(F("ok!"));
}

void testMultipartHandler() {
  HashChainMessage msg1;
  uint8_t buf2[sizeof(msg1.Message)];
  uint8_t packet[32];
  MultipartSplitter<32> mp((uint8_t *)&msg1.Message, 256, SEQ_CHAIN_START, 222);
  MultipartCombiner<32> mpc(buf2, 256, SEQ_CHAIN_START);
  memset(buf2, 11, sizeof(buf2));
  memset(&msg1.Message, 0, sizeof(msg1.Message));
  while (mp.More()) {
    memset(packet, 0, 32);
    mp.Get(packet);
    mpc.Put(packet);
    PrintHash(packet);
  }
  for (uint16_t i = 0; i < 256; i += 32) {
    PrintHash(buf2 + i);
  }
  Serial.println(sizeof(mp));
}

/*
void testHashChainMessage(PDP::MerkleFile& m) {
  HashChainMessage msg;
  uint8_t packet[32];
  msg.Load(m, 7);
  MultipartSplitter<32> mp((uint8_t*)&msg.Message,
msg.Message.Header.messageLength, SEQ_CHAIN_START, 7);
  msg.Print();
  while (mp.More()) {
    memset(packet, 0, 32);
    mp.Get(packet);
    PrintHash(packet);
  }
}
*/
/*
void testDataChunkMessage(FatFileSystem &fs, PDP::MerkleFile &m) {
  ChunkFile f;
  DataChunkMessage msg;
  uint8_t packet[32];
  m.ReadHeaderBlock();
  f.Open(fs, "test.txt", m.Block.HeaderBlock.chunkSize);
  f.Load(msg, 7);
  m.ReadRootHashBlock();
  msg.SetRootHash(m.Block.HashBlock.hash);
  MultipartSplitter<32> mp((uint8_t *)&msg.Message,
                           msg.Message.Header.messageLength, SEQ_CHAIN_START,
                           7);
  while (mp.More()) {
    memset(packet, 0, 32);
    mp.Get(packet);
    PrintHash(packet);
  }
}
*/
/*
void testTransmitter(RF24 &r, FatFileSystem &fs) {
  Transmitter t(r, fs);
  while (1) {
    t.Broadcast("test.txt", "test.mkl");
  }
}
*/

bool yieldflag;
FatFile rxf;

void testReceiver(RF24 &radio, FatFileSystem &fs) {
  Receiver r(radio, fs, rxf, 10000);
  //  while(1) {
  r.Listen();
  if (r.Error = ERROR_RX_TIMEOUT) {
    Serial.println(F("listen timeout"));
  }
  yieldflag = (r.YieldState == YIELD_GRANTED);
  //  }
}

void promptDelete(FatFileSystem &fs) {
  char c;
  unsigned long timeout = millis() + 5000;
  Serial.println(F("Type 'Y' to delete merkle files."));
  while (!Serial.available()) {
    SysCall::yield();
    if (millis() > timeout) {
      return;
    }
  }
  c = Serial.read();
  if (c == 'Y') {
    fs.remove("test.mkl");
    fs.remove("yui.mkl");
    fs.remove("yukari1.mkl");
    fs.remove("yukari2.mkl");
    fs.remove("yuzuku.mkl");
  }
}
/*
bool getNextMerkleChunkPair(FatFile &d, ) {
  FatFile f;
  if (!f.openNext(&d, O_READ)) {
    return false;
  }
  f.getName(filename, size);
  return true;
}
*/

/*
void doTransmission(const char *filename, ChunkFile &c, MerkleFile &m) {
  Transmitter t(radio, filename, c, m);
  Serial.print(F("now broadcasting: "));
  Serial.println(filename);
  t.Broadcast();
}
*/

void transmitFile(FatFileSystem &fs, FatFile &src) {
  Transmitter t(radio, fs, src);
  t.Broadcast();
  Serial.print(F("TX EXIT "));
  Serial.println(t.Error);
}

void testTransmitter(FatFileSystem &fs) {
  FatFile src, d;
  d.openRoot(&fs);
  d.rewind();
  while (src.openNext(&d, O_READ)) {
    /*
    ToMerkleFilename(filename);
    m.Open(fs, filename, src);
    if (m.Error) {
      // error creating or opening merkle file
      goto nextFile;
    }
    c.Set(src, m.Block.HeaderBlock.chunkSize);
    // reload original filename
    src.getName(filename, MAX_FILENAME_LENGTH + 1);
    doTransmission(filename, c, m);
    */
    transmitFile(fs, src);
    src.close();
  }
}

void makeTestFilename(char *fn, int n) {
  uint8_t i;
  for (i = 0; i < n; i++) {
    fn[i] = '0' + (i % 10);
  }
  if (n >= 10) {
    fn[n - 10] = '.';
  }
  fn[n] = '\0';
}

/*
void testChunkFile(FatFileSystem& fs) {
  uint8_t i;
  DataChunkMessage msg;
  ChunkFile f;
  MerkleFile m;
  f.Open(fs, "test.txt", CHUNK_SIZE);
  if (f.Error) {
    Serial.println(F("open chunk file error"));
    Serial.println(f.Error);
    while(1);
  }
  m.Open(fs, "test.mkl");
  if (m.Error) {
    Serial.println(F("open merkle error"));
    Serial.println(m.Error);
    while(1);
  }
  m.ReadRootHashBlock();
  msg.SetRootHash(m.Block.HashBlock.hash);
  Serial.println(F("files open ok"));
  for (i = 0; i < (1 << 5); i++) {
    f.Load(msg, i);
    if (f.Error) {
      Serial.println(f.Error);
      while(1);
    }
    if (msg.Verify(m)) {
      Serial.print(F("Chunk validated: "));
    } else {
      Serial.print(F("Chunk not valid: "));
    }
    Serial.println(i);
  }
}
*/

/*
void testChunkComplete(FatFileSystem &fs) {
  uint16_t numChunks;
  uint8_t treeDepth;
  MerkleFile m;
  m.Open(fs, "test.mkl");
  if (m.Error) {
    Serial.print(F("error"));
    Serial.println(m.Error);
    return;
  }
  m.ReadHeaderBlock();
  numChunks = m.Block.HeaderBlock.numChunks;
  treeDepth = m.Block.HeaderBlock.treeDepth;
  uint16_t i;
  for (i = 0; i < numChunks / 4; i++) {
    m.SetChunkComplete(i);
    if (m.Error) {
      Serial.print(F("error "));
      Serial.println(m.Error);
      return;
    }
  }
  for (i = 0; i < (1 << (treeDepth + 1)) - 1; i++) {
    m.ReadHashBlock(i);
    if (!(m.Block.HashBlock.flags & MERKLE_CHUNK_COMPLETE)) {
      Serial.print(F("not complete: "));
      Serial.println(i);
    }
  }
}
*/

// Change a byte of f
void corruptFile(FatFile &f) {
  uint8_t b;
  f.seekSet(128);
  f.read(&b, 1);
  Serial.println(b);
  b++;
  Serial.println(b);
  f.seekSet(128);
  f.write(&b, 1);
  f.read(&b, 1);
  Serial.println(b);
}

volatile bool PowerSwitch;

void setup() {
  MCUSR = 0;
  InitPowerSwitch();
  Serial.begin(115200);
  Serial.println(F("BOOT"));

  if (!sd.begin(PIN_SD_CS)) {
    // TODO blink an error LED
    Serial.println(F("SDERR"));
    while(1);
  }
  if (!radio.begin()) {
    // TODO blink an error LED
    Serial.println(F("RDOERR"));
    while (1)
      ;
  }
  // Radio setup
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(0);
  radio.setAutoAck(false);
  radio.disableDynamicPayloads();
  radio.setCRCLength(RF24_CRC_8);
  radio.openWritingPipe((uint8_t *)"BROAD");
  radio.openReadingPipe(1, (uint8_t *)"BROAD");

  CollectEntropy(sd.card(), radio);
  
  Run();
  // Should never reach here
  radio.powerDown();
  PowerOff();
}

void loop() {}
