#ifndef MERKLE_H
#define MERKLE_H

#include "SdFat.h"
#include "message.h"
#include "usha256.h"

namespace PDP {

typedef enum {
  MERKLE_HASH_KNOWN = (1 << 0),
  MERKLE_CHUNK_COMPLETE = (1 << 1),
} MerkleBlockFlags_t;

class MerkleFile {
public:
  MerkleFile();
  // Opens merkleFilename.
  //
  // If merkleFilename exists, its rootHash is compared to HashChainMessage. If
  // they do not match Error is set to ERROR_MERKLE_ROOT_MISMATCH
  //
  // If merkleFilename does not exist, creates a blank merkle file from
  // HashChainMessage
  void Open(FatFileSystem &fs, const char *merkleFilename,
            HashChainMessage &msg);
  // Opens merkleFilename.
  //
  // If merkleFilename does not exist, or has invalid header, creates it from
  // the data in FatFile
  void Open(FatFileSystem &fs, const char *merkleFilename, FatFile &src);
  void ReadHashBlock(uint16_t n);
  void WriteHashBlock(
      uint16_t n); // TODO private? only expose Save(HashChainMessage&)
  void ReadHeaderBlock();
  void ReadRootHashBlock();
  bool HashKnown(uint16_t chunk);
  void SetHash(uint16_t chunk, uint8_t *hash);
  bool ChunkComplete(uint16_t chunk);
  void SetChunkComplete(uint16_t chunk);
  // load the hash chain for the specified chunk into 'msg'
  void Load(HashChainMessage &msg, uint16_t chunk);
  // save the hash chain specified in 'msg'
  void Save(HashChainMessage &msg);
  // verify msg is a valid hash chain
  bool Verify(HashChainMessage &msg);
  // verify msg is a valid chunk. returns false if the correct hash for this
  // chunk is not yet known.
  bool Verify(DataChunkMessage &msg);
  // Check that the contents of f match this merkle file's hashes
  void Check(FatFile &f);
  // Fill in unknown hashes in this merkle file, if their children are known
  void Fill();
  struct {
    uint8_t type;
    union {
      struct {
        uint8_t flags;
        uint8_t hash[32];
      } HashBlock;
      struct {
        uint32_t fileSize;
        uint32_t chunkSize;
        uint16_t numChunks, numLeafs, numNodes;
        uint8_t treeDepth;
      } HeaderBlock;
    };
  } Block;
  Error_t Error;

private:
  void createFrom(FatFile &src, const uint32_t chunkSize);
  void scanChunks(FatFile &src, bool writeHashes, Sha256Context &ctx);
  void fill(Sha256Context &ctx);
  void readBlock(uint16_t n);
  void writeBlock(uint16_t n);
  void reset();
  FatFile m;
};

} // namespace PDP

#endif // MERKLE_H
