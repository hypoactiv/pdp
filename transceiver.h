#ifndef TRANSCEIVER_H
#define TRANSCEIVER_H

#include "RF24.h"
#include "merkle.h"
#include "message.h"
#include "multipart.h"
#include "stdint.h"

namespace PDP {

class TransceiverBase {
public:
  Error_t Error;
  YieldState_t YieldState;

protected:
  TransceiverBase(RF24 &radio, FatFileSystem &fs, FatFile &chunkFile);
  void LoadChunk(DataChunkMessage &msg, const uint16_t chunk);
  void SaveChunk(DataChunkMessage &msg);
  // buffers that are never used simutaneously
  union {
    uint8_t packet[32];
    char filename[MAX_FILENAME_LENGTH + 1];
  };
  // TODO rename to radio
  RF24 &_radio;
  FatFile &chunkFile;
  MerkleFile m;
  ChunkQueue missing;
  uint16_t treeDepth;
  uint16_t chunkSize;
  uint16_t numChunks;
  uint16_t lastScanned;
  // the merkle tree root of the file currently being received
  uint8_t rootHash[32];
  bool receivePacket(uint8_t *packet, const uint16_t timeout);
  void receiveMultipart(MultipartCombiner<32> &mpc, const uint16_t timeout);
  void broadcastMultipart(MultipartSplitter<32> &mps);
  void scanMissingChunks();
};
}

#endif // TRANSCEIVER_H
