#include "receiver.h"
#include "multipart.h"
#include "util.h"

namespace PDP {

// TODO move
// change dataFilename's extension (portion after last '.') to "mkl"
// truncating dataFilename if neccessary. dataFilename is a pointer to
// at least MAX_FILENAME_LENGTH+1 bytes.
void ToMerkleFilename(char *dataFilename) {
  uint8_t i, lastDot = MAX_FILENAME_LENGTH;
  // find last '.' in filename, or terminating null character
  for (i = 0; i < MAX_FILENAME_LENGTH - 4 && dataFilename[i] != '\0'; i++) {
    if (dataFilename[i] == '.') {
      lastDot = i;
    }
  }
  if (lastDot == MAX_FILENAME_LENGTH) {
    // filename contains no dots before MAX_FILENAME_LENGTH-4, append .mkl to
    // end
    lastDot = i;
  }
  strcpy(dataFilename + lastDot, ".mkl");
}

void ToHashFilename(uint8_t hash[32], char *dataFilename) {
  uint8_t i, j, lastDot = MAX_FILENAME_LENGTH;
  // find last '.' in filename, or terminating null character
  for (i = 0; dataFilename[i] != '\0'; i++) {
    if (dataFilename[i] == '.') {
      lastDot = i;
      // lastDot == i <= strlen(dataFilename)
    }
  }
  // i == strlen(dataFilename)
  // TODO use memmove! maybe? this might be smaller, since we always move right
  // move extension (portion after last '.') to end of dataFilename buffer
  for (j = MAX_FILENAME_LENGTH; i > lastDot; j--, i--) {
    dataFilename[j] = dataFilename[i];
  }
  dataFilename[j] = dataFilename[i];
  lastDot = j;
  // replace dataFilename with HEX of hash up to, but not including,
  // dataFilename[lastDot].
  for (i = 0; i < lastDot; i += 2) {
    uint8_t nybble;
    char hex;
    // high nybble
    nybble = (hash[i >> 1] & 0xf0) >> 4;
    if (nybble <= 9) {
      hex = '0' + nybble;
    } else {
      hex = 'a' + (nybble - 10);
    }
    dataFilename[i] = hex;
    if (i + 1 < lastDot) {
      // low nybble
      nybble = (hash[i >> 1] & 0x0f);
      if (nybble <= 9) {
        hex = '0' + nybble;
      } else {
        hex = 'a' + (nybble - 10);
      }
      dataFilename[i + 1] = hex;
    }
  }
}

Receiver::Receiver(RF24 &radio, FatFileSystem &fs, FatFile &chunkFile,
                   const uint16_t timeout)
    : stationId(random(1, MAX_STATION_ID)),
      TransceiverBase(radio, fs, chunkFile), _fs(fs), _timeout(timeout),
      txIncomplete(false), yieldRequests(0) {
  this->knowRoot = chunkFile.isOpen();
}

void Receiver::Listen() {
  MultipartHeader *header = (MultipartHeader *)this->packet;
  this->_radio.startListening();
  while (1) {
    if (!this->receivePacket(this->packet, this->_timeout)) {
      this->Error = ERROR_RX_TIMEOUT;
      return;
    }
    if (header->protocolVersion != PROTOCOL_VERSION) {
      // invalid protocol version, ignore message
      //Serial.print(F("Bad ver: "));
      //Serial.println(header->protocolVersion);
      continue;
    }
    switch (header->sequenceNum) {
    case SEQ_CHAIN_START:
      // start of hash chain
      Serial.println(F("H"));
      this->receiveHashChain(header->messageLength);
      break;
    case SEQ_CHUNK_START:
      // start of data chunk
      Serial.println(F("D"));
      this->receiveDataChunk(header->messageLength);
      break;
    case SEQ_TAKING_REQS:
      // TX is listening for requests
      // TODO check RX-only flag
      Serial.println(F("R"));
      uint16_t listenDuration;
      this->receiveListenForReq(header->messageLength, listenDuration);
      if (listenDuration > 0) {
        delay(random(0, listenDuration));
        this->broadcastRequests();
      }
      break;
    default:
      // unknown packet
      Serial.print(F("Bad seq: "));
      Serial.println(header->sequenceNum);
      break;
    }
    if (this->knowRoot) {
      m.ReadRootHashBlock();
      if (m.Block.HashBlock.flags & MERKLE_CHUNK_COMPLETE) {
        // File is complete.
        // If TX isn't missing chunks, exit.
        // If TX is ignoring yield request, exit.
        if (!this->txIncomplete ||
            this->yieldRequests > 25) { // TODO configurable
          Serial.println("RX Exit");
          Serial.println(this->txIncomplete);
          Serial.println(this->yieldRequests);
          return;
        }
        /*
        m.Fill();
        m.Check(this->f);
        m.ReadRootHashBlock();
        if (m.Block.HashBlock.flags & MERKLE_CHUNK_COMPLETE) {
          Serial.println(F("file verified!"));
        } else {
          Serial.println(F("file NOT verified!"));
        }
        return;
        */
      }
    }
    if (this->YieldState == YIELD_GRANTED) {
      // stop listening and become TX
      break;
    }
  }
}

void Receiver::receiveHashChain(uint16_t msgLength) {
  HashChainMessage msg;
  if (msgLength > sizeof(msg.Message)) {
    Serial.println(F("Hash chain too long"));
    return;
  }
  MultipartCombiner<32> mc(&msg.Message, msgLength, SEQ_CHAIN_START);
  mc.Put(this->packet);
  if (this->knowRoot) {
    // only receive if this hash chain is new
    // only first 31 bytes of msg are valid at this point
    if (msg.Message.Header.chunk >= this->numChunks) {
      // invalid chunk index, reject
      return;
    }
    if (m.HashKnown(msg.Message.Header.chunk)) {
      // already have this chain, reject
      // use this free time to scan for missing chunks
      this->scanMissingChunks();
      return;
    }
    // don't have this chain, request the corresponding chunk later
    this->missing.Push(msg.Message.Header.chunk);
  }
  this->receiveMultipart(mc, this->_timeout);
  if (mc.Error) {
    // error receiving remainder of message (dropped packet)
    // TODO increment dropped packet counter
    return;
  }
  // verify hash chain
  if (!this->m.Verify(msg)) {
    // hash chain invalid, reject
    // TODO increment hash chain error counter
    return;
  }
  // hash chain is valid
  if (!this->knowRoot) {
    // not yet receiving a file, open or create the file described by this
    // hash chain
    this->treeDepth = msg.Message.Header.treeDepth;
    this->numChunks = msg.Message.Header.numChunks;
    this->chunkSize = msg.Message.Header.chunkSize;
    memcpy(this->rootHash, msg.Message.Header.rootHash, 32);
    strcpy(this->filename, msg.Message.Header.filename);
    // this->packet is now clobbered (unioned with filename)
    ToMerkleFilename(this->filename);
    this->m.Open(this->_fs, this->filename, msg);
    // TODO
    // disabling file renaming for now, since if A.JPG gets renamed to
    // 1234.JPG, then when TX yields channel and RX becomes TX, the same
    // file will be broadcast under 1234.JPG, but the new RX (formerly TX)
    // won't know to open A.JPG, unless some rootHash to filename lookup is
    // implemented.
    /*
    if (this->m.Error == ERROR_MERKLE_ROOT_MISMATCH) {
      // merkle file has different root hash, must be for a different file
      // try again replacing filename with root hash hex
      this->m.Error = ERROR_NONE;
      ToHashFilename(msg.Message.Header.rootHash, msg.Message.Header.filename);
      strcpy(this->filename, msg.Message.Header.filename);
      ToMerkleFilename(this->filename);
      this->m.Open(fs, this->filename, msg);
    }
    */
    if (this->m.Error) {
      // error opening merkle file, or root hash mismatch. discard.
      return;
    }
    // merkle file opened, open chunk file
    if (OpenFile(this->_fs, msg.Message.Header.filename, this->chunkFile,
                 O_RDWR)) {
      // couldn't open chunk file, create it
      if (this->Error =
              Create(this->_fs, this->chunkFile, msg.Message.Header.filename,
                     msg.Message.Header.fileSize)) {
        // error creating file, give up
        return;
      }
    }
    this->knowRoot = true;
  }
  // hash chain is valid and for file being received. save chain if it is not
  // known already.
  this->m.Save(msg);
}

void Receiver::receiveDataChunk(uint16_t msgLength) {
  DataChunkMessage msg;
  if (!this->knowRoot) {
    // don't know the root hash yet, reject
    return;
  }
  if (msgLength > sizeof(msg.Message)) {
    // reject
    return;
  }
  memset(&msg.Message, 0, sizeof(msg.Message));
  MultipartCombiner<32> mc(&msg.Message, msgLength, SEQ_CHUNK_START);
  mc.Put(this->packet);
  this->receiveMultipart(mc, this->_timeout);
  if (mc.Error) {
    // error receiving multipart message (dropped packet)
    Serial.print(F("DRP"));
    Serial.println(msg.Message.Header.chunk);
    return;
  }
  // only take the time to verify if this chunk is new
  // and hash of chunk is known
  // TODO do these checks after first part of message is received, like in
  // recieveHashChain?
  if (msg.Message.Header.chunk >= (1 << this->treeDepth)) {
    // invalid chunk index, reject
    return;
  }
  if (!m.HashKnown(msg.Message.Header.chunk)) {
    // don't know the hash for this chunk, reject
    Serial.print(F("NHS "));
    Serial.println(msg.Message.Header.chunk);
    return;
  }
  if (m.ChunkComplete(msg.Message.Header.chunk)) {
    // already have this chunk, reject
    Serial.print(F("DUP "));
    Serial.println(msg.Message.Header.chunk);
    // TODO this shouldn't be needed... never request a chunk we already have
    this->missing.Remove(msg.Message.Header.chunk);
    return;
  }
  if (this->m.Verify(msg)) {
    this->SaveChunk(msg);
    m.SetChunkComplete(msg.Message.Header.chunk);
    Serial.print(F("DOK "));
    this->missing.Remove(msg.Message.Header.chunk);
  } else {
    Serial.print(F("DRJ "));
  }
  Serial.println(msg.Message.Header.chunk);
}

void Receiver::receiveListenForReq(uint16_t messageLength,
                                   uint16_t &listenDuration) {
  ListenForReqMessage msg;
  MultipartCombiner<32> mc(&msg.Message, messageLength, SEQ_TAKING_REQS);
  listenDuration = 0;
  if (!this->knowRoot) {
    return;
  }
  memset(&msg.Message, 0, sizeof(msg.Message));
  mc.Put(this->packet);
  this->receiveMultipart(mc, this->_timeout);
  if (mc.Error) {
    return;
  }
  this->m.ReadRootHashBlock();
  if (memcmp(msg.Message.rootHash, this->m.Block.HashBlock.hash, 32)) {
    // root hash mismatch
    return;
  }
  // TODO bound listen duration to something reasonable
  listenDuration = msg.Message.listenDuration;
  if (msg.Message.yieldToRxId == this->stationId) {
    this->YieldState = YIELD_GRANTED;
  } else {
    // TODO scanning the merkle file may take longer than listen period length?
    this->YieldState = YIELD_NONE;
    if (msg.Message.q.Verify() && this->knowRoot) {
      // check if this station has any pieces the TX station is missing
      this->txIncomplete = (msg.Message.q.Length() > 0);
      while (msg.Message.q.Length() > 0) {
        uint16_t ch = msg.Message.q.Pop();
        Serial.print(F("TX asking for "));
        Serial.println(ch);
        if (ch > this->numChunks) {
          // invalid chunk index
          continue;
        }
        if (this->m.ChunkComplete(ch)) {
          // begin channel yield request
          this->YieldState = YIELD_REQUEST;
          // return;
        }
      }
    }
  }
}

void Receiver::broadcastRequests() {
  if (!this->knowRoot ||
      (this->missing.Length() == 0 && this->YieldState != YIELD_REQUEST)) {
    // nothing to request
    return;
  }
  this->_radio.stopListening();
  ReqMessage msg(this->missing);
  MultipartSplitter<32> mps(&msg.Message, msg.Message.messageLength,
                            SEQ_MAKING_REQ, (uint16_t)random());
  this->m.ReadRootHashBlock();
  msg.SetRootHash(this->m.Block.HashBlock.hash);
  if (this->YieldState == YIELD_REQUEST) {
    // request TX station to yield channel to this RX station
    msg.Message.yieldRequest = this->stationId;
    this->yieldRequests++;
  } else {
    // no yield request
    msg.Message.yieldRequest = 0;
    this->yieldRequests = 0;
  }
  Serial.print(F("asking for "));
  Serial.println(msg.Message.q.Length());
  this->broadcastMultipart(mps);
  this->_radio.startListening();
}

} // namespace PDP
