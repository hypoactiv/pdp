#define DEBUG // TODO hack

#include "transmitter.h"
#include "stackmon.h"
#include "power.h"

namespace PDP {

Transmitter::Transmitter(RF24 &radio, FatFileSystem &fs, FatFile &chunkFile)
    : TransceiverBase(radio, fs, chunkFile), HeardRequest(false),
      yieldRxStationId(0){};

void Transmitter::Broadcast() {
  uint8_t sinceHeard = 0;
  uint16_t numChunks, i;
  if (this->Error) {
    return;
  }
  uint16_t interference; // TODO remove
  if ((interference = this->ListenForInterference(1000)) > 3) {
    // channel interference detected
    this->Error = ERROR_TX_INTERFERENCE;
    Serial.println(F("channel not quiet"));
    Serial.println(interference);
    return;
  }
  this->_radio.stopListening();
  // broadcast hash chains
  m.ReadHeaderBlock();
  numChunks = m.Block.HeaderBlock.numChunks;
  for (i = 0; i < numChunks; i++) {
    while (this->transmit.Length() > 0) {
      uint16_t ch = this->transmit.Pop();
      if (ch > numChunks) {
        // invalid chunk index
        continue;
      }
      this->m.ReadHashBlock(ch);
      uint8_t flags = this->m.Block.HashBlock.flags;
      if (flags != (MERKLE_CHUNK_COMPLETE | MERKLE_HASH_KNOWN)) {
        this->missing.Push(ch);
      }
      Serial.println(ch);
      if (flags & MERKLE_HASH_KNOWN) {
        Serial.println(F("Chain"));
        this->broadcastHashChain(ch);
        if (flags & MERKLE_CHUNK_COMPLETE) {
          Serial.println(F("chunk"));
          this->broadcastDataChunk(ch);
        }
        if (this->ListenForInterference(100) > 3) {
          // channel interference detected
          this->Error = ERROR_TX_INTERFERENCE;
          return;
        }
      }
      CheckPowerSwitch();
      if (FlagShutdown) {
        return;
      }
    }
    this->transmit.Push(i);
    // signal intention to listen
    this->broadcastListenForReqs();
    // enter listen period
    if (this->listenForRequests()) {
      sinceHeard = 0;
      this->HeardRequest = true;
      if (this->yieldRxStationId > 0 && this->missing.Length() > 0) {
        // there are missing pieces and a station is requesting channel yield
        // TODO decide if yield should be granted
        if (i > (numChunks >> 2)) {
          this->YieldState = YIELD_GRANTED;
        }
      }
    } else {
      sinceHeard++;
      // TODO hack. some better logic is needed here
      // end transmission if no requests have been heard in a while
      if (sinceHeard > 10) {
        return;
      }
    }
    this->m.ReadRootHashBlock();
    if (!(this->m.Block.HashBlock.flags & MERKLE_CHUNK_COMPLETE)) {
      // at least one chunk missing, scan
      this->scanMissingChunks();
    }
    if (this->YieldState == YIELD_GRANTED) {
      MultipartHeader *header = (MultipartHeader *)this->packet;
      // TODO move to its own method? doChannelYield?
      for (uint8_t yieldTries = 0; yieldTries < 5; yieldTries++) {
        this->broadcastListenForReqs();
        this->_radio.startListening();
        while (this->receivePacket(this->packet, 1000)) {
          // TODO if we don't get a SEQ_CHAIN_START, but keep getting packets,
          // we'll be stuck here forever
          if (header->sequenceNum == SEQ_CHAIN_START) {
            // another station is now broadcasting, channel yield successful
            return;
          }
        }
        // other station has not started broadcasting, retry yield
        this->_radio.stopListening();
      }
      // channel yield failed
      this->YieldState = YIELD_NONE;
    }
  }
}

void Transmitter::broadcastHashChain(uint16_t chunk) {
  HashChainMessage msg;
  m.Load(msg, chunk);
  this->chunkFile.getName(msg.Message.Header.filename, MAX_FILENAME_LENGTH + 1);
  MultipartSplitter<32> mp(&msg.Message, msg.Message.Header.messageLength,
                           SEQ_CHAIN_START, chunk);
  this->broadcastMultipart(mp);
}

void Transmitter::broadcastDataChunk(uint16_t chunk) {
  DataChunkMessage msg;
#ifdef DEBUG_VERIFY_TX
  memset(msg.Message.chunk, 0xAA, MAX_CHUNK_SIZE);
#endif
  this->m.ReadRootHashBlock();
  msg.SetRootHash(m.Block.HashBlock.hash);
  this->LoadChunk(msg, chunk);
#ifdef DEBUG_VERIFY_TX
  if (!this->m.Verify(msg)) {
    Serial.println(StackCount());
    Serial.println(this->Error);
    Serial.println(F("Tried to transmit bad data. Halt."));
    while (1)
      ;
  } else {
    Serial.println(F("Data ok!"));
  }
#endif
  MultipartSplitter<32> mp(&msg.Message, msg.Message.Header.messageLength,
                           SEQ_CHUNK_START, chunk);
  this->broadcastMultipart(mp);
}

void Transmitter::broadcastListenForReqs() {
  ListenForReqMessage msg(this->missing);
  m.ReadRootHashBlock();
  msg.SetRootHash(m.Block.HashBlock.hash);
  if (this->YieldState == YIELD_GRANTED) {
    msg.Message.listenDuration = 0;
    Serial.print(F("offer yeild: "));
    Serial.println(this->yieldRxStationId);
    msg.Message.yieldToRxId = this->yieldRxStationId;
  } else {
    msg.Message.listenDuration = LISTEN_DURATION;
    msg.Message.yieldToRxId = 0;
  }
  MultipartSplitter<32> mp(&msg.Message, msg.Message.messageLength,
                           SEQ_TAKING_REQS, SEQ_TAKING_REQS);
  this->broadcastMultipart(mp);
}

bool Transmitter::listenForRequests() {
  unsigned long startTime;
  uint16_t remaining;
  bool heard = false;
  ReqMessage msg;
  MultipartHeader *header = (MultipartHeader *)this->packet;
  m.ReadRootHashBlock();
  this->_radio.startListening();
  startTime = millis();
  // TODO millis overflow?
  while (millis() - startTime < LISTEN_DURATION) {
    remaining = LISTEN_DURATION - (millis() - startTime);
    if (this->receivePacket(this->packet, remaining)) {
      if (header->sequenceNum == SEQ_MAKING_REQ) {
        // receiving a request
        if (header->messageLength > sizeof(msg.Message)) {
          // message too long, reject
          continue;
        }
        MultipartCombiner<32> mpc(&msg.Message, header->messageLength,
                                  SEQ_MAKING_REQ);
        this->receiveMultipart(mpc, remaining);
        if (mpc.Error) {
          // dropped packet or timeout, reject
          continue;
        }
        if (!msg.Verify()) {
          // reject
          continue;
        }
        if (memcmp(msg.Message.rootHash, this->m.Block.HashBlock.hash, 32)) {
          // reject
          continue;
        }
        Serial.println(F("REQ RX"));
        if (msg.Message.yieldRequest > 0) {
          // an RX station is requesting the channel
          this->yieldRxStationId = msg.Message.yieldRequest;
        }
        // request received ok
        // chunk numbers are validated on transmit
        this->transmit.Push(msg.Message.q);
        heard = true;
      }
    }
  }
  this->_radio.stopListening();
  return heard;
}

uint8_t Transmitter::ListenForInterference(uint16_t duration) {
  uint8_t ret = 0;
  unsigned long timeoutAt = millis() + duration;
  while (millis() < timeoutAt) {
    this->_radio.startListening();
    delay(10);
    this->_radio.stopListening();
    if (this->_radio.testCarrier()) {
      ret++;
      if (ret == 0xff) {
        break;
      }
    }
    // clear any received packet
    if (this->_radio.available()) {
      Serial.println(F("flushed packet!"));
      this->_radio.read(this->packet, 32);
    }
  }
  return ret;
}

} // namespace PDP
