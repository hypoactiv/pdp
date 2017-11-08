#include "message.h"
#include "string.h"

namespace PDP {

HashChainMessage::HashChainMessage() : Verified(false), VerifyDepth(0) {}

#if 0 // debug code -- disable to avoid Serial dependency
void HashChainMessage::Print() {
  uint8_t i, treeDepth = this->Message.Header.treeDepth;
  Serial.print(F("Filename: "));
  Serial.println(this->Message.Header.filename);
  Serial.println(F("Chunk hash: "));
  PrintHash(this->Message.Header.chunkHash);
  Serial.println(F("Hash chain: "));
  for (i = 0; i < treeDepth; i++) {
    PrintHash(this->Message.chain[i]);
  }
  Serial.println(F("Root hash: "));
  PrintHash(this->Message.Header.rootHash);
}
#endif 0

ListenForReqMessage::ListenForReqMessage() {}

ListenForReqMessage::ListenForReqMessage(ChunkQueue &q)
    : Message{PROTOCOL_VERSION, sizeof(this->Message), {}, {}, ChunkQueue(q)} {}

// TODO figure out how to combine the following two methods
void ReqMessage::SetRootHash(uint8_t *rootHash) {
  memcpy(this->Message.rootHash, rootHash, 32);
}

void ListenForReqMessage::SetRootHash(uint8_t *rootHash) {
  memcpy(this->Message.rootHash, rootHash, 32);
}

void DataChunkMessage::SetRootHash(uint8_t *rootHash) {
  memcpy(this->Message.Header.rootHash, rootHash, 32);
}

ChunkQueue::ChunkQueue() : len(0) {}

uint8_t ChunkQueue::Length() { return this->len; }

bool ChunkQueue::Verify() {
  if (this->len > REQ_QUEUE_LEN) {
    this->len = 0;
    return false;
  }
  return true;
}

bool ChunkQueue::Push(uint16_t chunk) {
  if (this->len >= REQ_QUEUE_LEN) {
    return false;
  }
  for (uint8_t i = 0; i < this->len; i++) {
    if (this->reqs[i] == chunk) {
      return true;
    }
  }
  this->reqs[this->len] = chunk;
  this->len++;
  return true;
}

bool ChunkQueue::Push(ChunkQueue &q) {
  for (uint8_t i = 0; i < q.len; i++) {
    if (!this->Push(q.reqs[i])) {
      return false;
    }
  }
  return true;
}

uint16_t ChunkQueue::Pop() {
  if (this->len == 0) {
    // queue is empty
    return 0;
  }
  this->len--;
  return this->reqs[this->len];
}

void ChunkQueue::Remove(uint16_t chunk) {
  for (uint8_t i = 0; i < this->len; i++) {
    if (this->reqs[i] == chunk) {
      this->len--;
      memmove(&this->reqs[i], &this->reqs[i + 1],
              (this->len - i) * sizeof(this->reqs[0]));
      return;
    }
  }
}

void ChunkQueue::Clear() { this->len = 0; }

/*
void ChunkQueue::Print() {
  for (uint8_t i = 0; i < this->len; i++) {
    Serial.print(this->reqs[i]);
    Serial.print(F(" "));
  }
  Serial.println();
}
*/

ReqMessage::ReqMessage() {}

ReqMessage::ReqMessage(ChunkQueue &q)
    : Message{PROTOCOL_VERSION, sizeof(this->Message), {}, ChunkQueue(q)} {}

bool ReqMessage::Verify() { return this->Message.q.Verify(); }

} // namespace PDP
