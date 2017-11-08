#ifndef MESSAGE_H
#define MESSAGE_H

#include "consts.h"

namespace PDP {

class HashChainMessage {
public:
  HashChainMessage();
  void Print();
  void SetFilename(const char *filename);
  struct __attribute__((__packed__)) {   // Message
    struct __attribute__((__packed__)) { // Header
      // TODO split these first 2 fields into a MultipartHeader struct?
      // TODO hash the header fields into rootHash to verify they're received
      // correctly?
      // Protocol version
      uint8_t version;
      // Length of this message, including verion and messageSize, in bytes
      uint32_t messageLength;
      // Index of chunk this hash chain describes
      uint16_t chunk;
      // Total number of chunks in this file
      uint16_t numChunks;
      // Tree depth, and also the length of the hash chain
      uint8_t treeDepth;
      // Maximum chunk size in bytes. Only last chunk may be smaller.
      uint32_t chunkSize;
      // File size in bytes
      uint32_t fileSize;
      // Hash of chunk
      uint8_t chunkHash[32];
      // Merkle tree root hash
      uint8_t rootHash[32];
      // To ensure filename is null-terminated, filename[MAX_FILENAME_LENGTH]
      // must equal '\0'.
      char filename[MAX_FILENAME_LENGTH + 1];
    } Header;
    uint8_t chain[MAX_TREE_DEPTH][32];
  } Message;
  bool Verified;
  uint8_t VerifyDepth;
};

// TODO rename ChunkMessage
class DataChunkMessage {
public:
  void SetRootHash(uint8_t *rootHash);
  struct __attribute__((__packed__)) {   // Message
    struct __attribute__((__packed__)) { // Header
      // Protocol version
      uint8_t version;
      // Length of this message, including verion and messageSize, in bytes
      uint32_t messageLength;
      // Index of chunk
      uint16_t chunk;
      // Merkle tree root hash
      uint8_t rootHash[32];
      // Size of this chunk in bytes
      uint32_t chunkSize;
    } Header;
    uint8_t chunk[MAX_CHUNK_SIZE];
  } Message;
};

class __attribute__((__packed__)) ChunkQueue {
public:
  ChunkQueue();
  uint8_t Length();
  bool Verify();
  // Add a chunk index to the queue if it does not already exist. If the
  // ChunkQueue is full, false is returned, otherwise true is returned.
  bool Push(uint16_t chunk);
  bool Push(ChunkQueue &q);
  // Remove and return the last added chunk index from the queue
  uint16_t Pop();
  // removes chunk if it is in the queue
  void Remove(uint16_t chunk);
  void Clear();
  void Print();

private:
  uint8_t len;
  uint16_t reqs[REQ_QUEUE_LEN];
  friend class ReqMessage;
};

// TODO rename ListenPeriodStartMessage
class ListenForReqMessage {
public:
  ListenForReqMessage();
  ListenForReqMessage(ChunkQueue &q);
  void SetRootHash(uint8_t *rootHash);
  struct __attribute__((__packed__)) { // Message
    // Protocol version
    uint8_t version;
    // Length of this message, including verion and messageSize, in bytes
    uint32_t messageLength;
    // Merkle tree root hash
    uint8_t rootHash[32];
    // Duration of TX's listen period in milliseconds
    uint16_t listenDuration;
    // chunks missing from TX station. If an RX station has at least one of
    // these chunks, it will request a channel yield so that RX can become the
    // TX station.
    ChunkQueue q;
    // If zero, the TX is not yielding the channel. If non-zero, contains
    // the RX station id that now owns the channel.
    uint16_t yieldToRxId;
  } Message;
};

class ReqMessage {
public:
  ReqMessage();
  ReqMessage(ChunkQueue &q);
  void SetRootHash(uint8_t *rootHash);
  // Returns true if this is a valid ReqMessage
  bool Verify();
  struct __attribute__((__packed__)) {
    // Protocol version
    uint8_t version;
    // Length of this message, including verion and messageSize, in bytes
    uint32_t messageLength;
    // Merkle tree root hash
    uint8_t rootHash[32];
    ChunkQueue q;
    // If zero, the RX station is not requesting a channel yield. If non-zero
    // contains the station id that wishes to own the channel.
    uint16_t yieldRequest;
  } Message;
};

} // namespace PDP

#endif // MESSAGE_H
