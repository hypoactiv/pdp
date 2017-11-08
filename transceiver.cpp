#include "transceiver.h"
#include "pdp.h"
#include "util.h"
#include "power.h"

namespace PDP {

TransceiverBase::TransceiverBase(RF24 &radio, FatFileSystem &fs,
                                 FatFile &chunkFile)
    : Error(ERROR_NONE), YieldState(YIELD_NONE), _radio(radio),
      chunkFile(chunkFile), chunkSize(0), lastScanned(0) {
  if (this->chunkFile.isOpen()) {
    // chunkFile is already open. Open the corresponding merkle file.
    uint8_t filenameLen;
    this->chunkFile.getName(this->filename, MAX_FILENAME_LENGTH + 1);
    filenameLen = strlen(this->filename);
    if (filenameLen >= 3 && !strcmp(this->filename + filenameLen - 3, "mkl")) {
      // refuse to transmit/receive merkle files
      this->Error = ERROR_REFUSE_MERKLE;
      return;
    }
    ToMerkleFilename(this->filename);
    this->m.Open(fs, this->filename, chunkFile);
    this->Error = this->m.Error;
    if (!this->Error) {
      // header block is loaded after calling Open
      this->treeDepth = this->m.Block.HeaderBlock.treeDepth;
      this->chunkSize = this->m.Block.HeaderBlock.chunkSize;
      this->numChunks = this->m.Block.HeaderBlock.numChunks;
      this->m.ReadRootHashBlock();
      memcpy(this->rootHash, this->m.Block.HashBlock.hash, 32);
    }
  }
}

void TransceiverBase::LoadChunk(DataChunkMessage &msg, const uint16_t chunk) {
  uint32_t bytePosition;
#ifdef DEBUG
  if (this->chunkSize == 0 || this->chunkSize > MAX_CHUNK_SIZE) {
    Serial.println(F("IE01"));
    while (1)
      ;
  }
#endif
  bytePosition = chunk;
  bytePosition *= this->chunkSize;
  // TODO refuse to load if in error state? (as in save)
  if (bytePosition >= this->chunkFile.fileSize()) {
    // requesting chunk after end of file, return empty chunk
    msg.Message.Header.chunkSize = 0;
  } else {
    if (!chunkFile.seekSet(bytePosition)) {
      this->Error = ERROR_IO_SEEK;
      return;
    }
    msg.Message.Header.chunkSize =
        this->chunkFile.read(msg.Message.chunk, this->chunkSize);
    if (msg.Message.Header.chunkSize == 0) {
      this->Error = ERROR_IO_READ;
      return;
    }
  }
  msg.Message.Header.version = PROTOCOL_VERSION;
  msg.Message.Header.chunk = chunk;
  msg.Message.Header.messageLength =
      msg.Message.Header.chunkSize + sizeof(msg.Message.Header);
}

void TransceiverBase::SaveChunk(DataChunkMessage &msg) {
  uint32_t bytePosition;
#ifdef DEBUG
  if (this->chunkSize == 0 || this->chunkSize > MAX_CHUNK_SIZE) {
    Serial.println(F("IE02"));
    while (1)
      ;
  }
#endif
  bytePosition = msg.Message.Header.chunk;
  bytePosition *= this->chunkSize;
  if (this->Error) {
    Serial.println(this->Error);
    Serial.println(F("CHUNKFILE ERR HALT"));
    while (1)
      ;
  }
  if (!this->chunkFile.seekSet(bytePosition)) {
    this->Error = ERROR_IO_SEEK;
    return;
  }
  if (this->chunkFile.write(msg.Message.chunk, msg.Message.Header.chunkSize) !=
      msg.Message.Header.chunkSize) {
    this->Error = ERROR_IO_WRITE;
    return;
  }
  this->chunkFile.sync();
}

void TransceiverBase::broadcastMultipart(MultipartSplitter<32> &mps) {
  while (mps.More()) {
    mps.Get(this->packet);
    for (uint8_t i = 0; i < RETRANSMITS; i++) {
      this->_radio.write(this->packet, 32, true);
    }
  }
}

bool TransceiverBase::receivePacket(uint8_t *packet, const uint16_t timeout) {
  uint32_t timeoutAt = millis() + timeout;
  while (!this->_radio.available()) {
    CheckPowerSwitch();
    if (FlagShutdown) {
      return false;
    }
    if (millis() > timeoutAt) {
      return false;
    }
  }
  this->_radio.read(this->packet, 32);
  return true;
}

void TransceiverBase::receiveMultipart(MultipartCombiner<32> &mpc,
                                       const uint16_t timeout) {
  while (mpc.More()) {
    if (!this->receivePacket(this->packet, timeout)) {
      mpc.Error = ERROR_RX_TIMEOUT;
      return;
    }
    mpc.Put(this->packet);
  }
}

void TransceiverBase::scanMissingChunks() {
  // stop scanning after 10 milliseconds
  unsigned long timeoutAt = millis() + 10; // TODO configure timeout?
  uint16_t numChunks = this->numChunks;
  for (uint16_t i = 0; i < numChunks; i++) {
    if (millis() > timeoutAt) {
      // out of scan time
      return;
    }
    if (!this->m.ChunkComplete(lastScanned)) {
      if (!this->missing.Push(lastScanned)) {
        // queue full, stop now
        return;
      }
    }
    lastScanned = (lastScanned + 1) % numChunks;
  }
}

} // namespace PDP
