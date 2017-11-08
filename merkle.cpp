#include "merkle.h"
#include "stackmon.h"
#include "util.h"

namespace PDP {

// Returns true if 'c' is a legal character for a fat filename
//
// Copied from SdFat/src/FatLib/FatFileLFN.cpp
inline bool FatLegalChar(char c) {
  if (c == '/' || c == '\\' || c == '"' || c == '*' || c == ':' || c == '<' ||
      c == '>' || c == '?' || c == '|') {
    return false;
  }
  return 0x1F < c && c < 0x7F;
}

MerkleFile::MerkleFile() : Error(ERROR_NONE) {}

// Returns true if the chunk's hash is known. On return, the chunk's hash block
// is loaded
bool MerkleFile::HashKnown(uint16_t chunk) {
  // TODO check error? what to do if error? return false, or true?
  this->ReadHashBlock(chunk);
  return (this->Block.HashBlock.flags & MERKLE_HASH_KNOWN);
}

void MerkleFile::SetHash(uint16_t chunk, uint8_t *hash) {
  this->ReadHashBlock(chunk);
  memcpy(this->Block.HashBlock.hash, hash, 32);
  this->Block.HashBlock.flags |= MERKLE_HASH_KNOWN;
  this->WriteHashBlock(chunk);
}

// Returns true if the chunk is marked complete
bool MerkleFile::ChunkComplete(uint16_t chunk) {
  this->ReadHashBlock(chunk);
  return (this->Block.HashBlock.flags & MERKLE_CHUNK_COMPLETE);
}

// Marks the chunk as complete
void MerkleFile::SetChunkComplete(uint16_t chunk) {
  uint8_t i, treeDepth;
  uint16_t j = 0, n = chunk;
  this->ReadHeaderBlock();
  treeDepth = this->Block.HeaderBlock.treeDepth;
  this->ReadHashBlock(chunk);
  if (this->Block.HashBlock.flags & MERKLE_CHUNK_COMPLETE) {
    // this chunk is already marked complete, nothing to do
    return;
  }
  for (i = 0; i < treeDepth; i++) {
    this->Block.HashBlock.flags |= MERKLE_CHUNK_COMPLETE;
    this->writeBlock(n + 1);
    if (!this->ChunkComplete(n ^ 0x01)) {
      // neighbor is not complete, stop here
      return;
    }
    // neighbor is also complete, continue up tree and set parent's complete
    // flag
    j += 1 << (treeDepth - i);
    chunk >>= 1;
    n = j + chunk;
    this->ReadHashBlock(n);
  }
  // chunk == 0
  // entire file is complete, set root node's complete flag.
  // since the root node has no neighbor, it needs a special case
  this->ReadHashBlock(j); // TODO superfluous?
  this->Block.HashBlock.flags |= MERKLE_CHUNK_COMPLETE;
  this->WriteHashBlock(j);
}

void MerkleFile::Open(FatFileSystem &fs, const char *merkleFilename,
                      FatFile &src) {
  this->reset();
  this->Error = OpenFile(fs, merkleFilename, this->m, O_RDWR);
  if (!this->Error) {
    this->ReadHeaderBlock();
  }
  if (this->Error) {
    // error opening merkle tree or reading header block, create it from
    // srcFilename
    this->Error = ERROR_NONE;
    this->Error =
        OpenFile(fs, merkleFilename, this->m, O_CREAT | O_TRUNC | O_RDWR);
    if (this->Error) {
      // give up
      return;
    }
    this->createFrom(src, MAX_CHUNK_SIZE);
    if (!this->Error) {
      this->ReadHeaderBlock();
    }
  }
}

void MerkleFile::Open(FatFileSystem &fs, const char *merkleFilename,
                      HashChainMessage &msg) {
  this->reset();
  this->Error = OpenFile(fs, merkleFilename, this->m, O_RDWR);
  if (!this->Error) {
    this->ReadHeaderBlock();
  }
  if (!this->Error) {
    // Verify merkle file's root hash matches msg's root hash
    this->ReadRootHashBlock();
    if (!this->Error &&
        memcmp(this->Block.HashBlock.hash, msg.Message.Header.rootHash, 32)) {
      // hashes do not match
      this->m.close();
      this->Error = ERROR_MERKLE_ROOT_MISMATCH;
      return;
    }
  }
  if (this->Error) {
    uint16_t N, i;
    // error opening merkle tree, create a blank file from msg
    this->Error = ERROR_NONE;
    if (this->m.isOpen()) {
      this->m.close();
    }
    this->Error =
        OpenFile(fs, merkleFilename, this->m, O_CREAT | O_TRUNC | O_RDWR);
    if (this->Error) {
      // give up
      return;
    }
    // create blank merkle file of appropriate size
    memset(&this->Block, 0, sizeof(this->Block));
    // compute total number of tree nodes (internal and leafs)
    N = (1 << (msg.Message.Header.treeDepth + 1)) - 1;
    this->Block.type = BLOCK_HASH;
    for (i = 0; i <= N; i++) {
      this->writeBlock(i);
    }
    // write root hash
    this->SetHash(N - 1, msg.Message.Header.rootHash);
    // write header
    this->Block.type = BLOCK_HEADER;
    this->Block.HeaderBlock.fileSize = msg.Message.Header.fileSize;
    this->Block.HeaderBlock.chunkSize = msg.Message.Header.chunkSize;
    this->Block.HeaderBlock.numChunks = msg.Message.Header.numChunks;
    this->Block.HeaderBlock.numNodes = N;
    // compute number of leaf nodes
    N = (1 << msg.Message.Header.treeDepth);
    this->Block.HeaderBlock.numLeafs = N;
    this->Block.HeaderBlock.treeDepth = msg.Message.Header.treeDepth;
    this->writeBlock(0);
    // mark empty leaf nodes as complete
    for (i = msg.Message.Header.numChunks; i < N; i++) {
      this->SetChunkComplete(i);
    }
    // merkle file created ok
    this->m.sync();
    this->ReadHeaderBlock();
  }
}

/*void MerkleFile::Open(FatFileSystem &fs, const char *merkleFilename) {
  this->reset();
  this->Error = OpenFile(fs, merkleFilename, this->m, O_RDWR);
  this->ReadHeaderBlock();
}*/

bool MerkleFile::Verify(DataChunkMessage &msg) {
  Sha256Context ctx;
  uint8_t hash[32];
  // verify message header fields
  this->ReadHeaderBlock();
  if (this->Error) {
    // merkle file error, reject
    Serial.println(F("DRJ MK ERR"));
    return false;
  }
  if (msg.Message.Header.chunkSize > MAX_CHUNK_SIZE ||
      msg.Message.Header.chunk >= (1 << this->Block.HeaderBlock.treeDepth)) {
    // reject
    Serial.println(F("DRJ HDR ERR"));
    return false;
  }
  // verify message's root hash
  this->ReadRootHashBlock();
  if (this->Error) {
    // merkle file error, reject
    Serial.println(F("DRJ MK ERR"));
    return false;
  }
  if (memcmp(msg.Message.Header.rootHash, this->Block.HashBlock.hash, 32)) {
    // reject
    Serial.println(F("DRJ ROOT ERR"));
    return false;
  }
  // compute chunk hash and compare to merkle file
  if (!this->HashKnown(msg.Message.Header.chunk)) {
    // don't know the hash for this chunk yet, impossible to verify
    Serial.println(F("DRJ NOHASH ERR"));
    return false;
  }
  sha256_init(&ctx);
  sha256_update(&ctx, msg.Message.chunk, msg.Message.Header.chunkSize);
  sha256_final(&ctx, hash);
  if (memcmp(hash, this->Block.HashBlock.hash, 32)) {
    // hash mismatch, reject
    Serial.println(StackCount());
    Serial.println(F("DRJ CORRUPT ERR"));
    return false;
  }
  return true;
}
// Scans chunks of src, comparing them to the stored hashes
// in this MerkleFile.
// If writeHashes is false, computed hashes that match those in this MerkleFile
// are marked complete.
// If writeHashes is true, computed hashes are written to this MerkleFile and
// marked as complete.
void MerkleFile::scanChunks(FatFile &src, bool writeHashes,
                            Sha256Context &ctx) {
  uint8_t buf[MAX_CHUNK_SIZE];
  this->ReadHeaderBlock();
  const uint32_t chunkSize = this->Block.HeaderBlock.chunkSize;
  const uint16_t numLeafs = this->Block.HeaderBlock.numLeafs;
  const uint16_t numChunks = this->Block.HeaderBlock.numChunks;
  void *hashTarget = writeHashes ? this->Block.HashBlock.hash : buf;
  uint16_t i;
  src.seekSet(0);
  for (i = 0; i < numChunks; i++) {
    uint32_t len = src.read(buf, chunkSize);
    if (len == 0) {
      this->Error = ERROR_IO_READCHUNK;
      return;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, buf, len);
    sha256_final(&ctx, hashTarget);
    if (writeHashes) {
      this->Block.HashBlock.flags = MERKLE_HASH_KNOWN;
      this->WriteHashBlock(i);
    } else {
      this->ReadHashBlock(i);
    }
    if (writeHashes || !memcmp(hashTarget, this->Block.HashBlock.hash, 32)) {
      this->SetChunkComplete(i);
    }
  }
  // remainder of leaf nodes are empty, so they're always complete
  for (; i < numLeafs; i++) {
    Serial.println(this->Error);
    this->SetChunkComplete(i);
  }
}

void MerkleFile::createFrom(FatFile &src, const uint32_t chunkSize) {
  Sha256Context ctx;
  uint16_t numChunks, numLeafs, numNodes, i;
  uint32_t srcSize = src.fileSize();
  uint8_t treeDepth = 0;
  if (srcSize == 0 || srcSize > MAX_FILE_SIZE) {
    this->Error = ERROR_CHUNKFILE_EMPTY_OR_TOO_BIG;
    return;
  }
  numChunks = (srcSize - 1) / chunkSize + 1;
  // compute treeDepth = ceil(log_2(numChunks)) and numLeafs = 2^treeDepth
  while ((numLeafs = (1 << treeDepth)) < numChunks) {
    treeDepth++;
  }
  // compute number of internal and leaf nodes in merkle tree
  numNodes = (1 << (treeDepth + 1)) - 1;
  // write header block
  this->Block.type = BLOCK_HEADER;
  this->Block.HeaderBlock.fileSize = srcSize;
  this->Block.HeaderBlock.chunkSize = chunkSize;
  this->Block.HeaderBlock.numChunks = numChunks;
  this->Block.HeaderBlock.numLeafs = numLeafs;
  this->Block.HeaderBlock.numNodes = numNodes;
  this->Block.HeaderBlock.treeDepth = treeDepth;
  this->writeBlock(0);
  // Initialize hash blocks with zeroes
  memset(&this->Block, 0, sizeof(this->Block));
  for (i = 0; i < numNodes; i++) {
    this->WriteHashBlock(i);
  }
  // scan src file and write hashes to leaf nodes
  this->scanChunks(src, true, ctx);
  // fill in internal hash nodes
  this->fill(ctx);
  this->m.sync();
}

void MerkleFile::ReadHashBlock(uint16_t n) {
  this->Block.type = BLOCK_INVALID;
  this->readBlock(n + 1);
  if (!this->Error && this->Block.type != BLOCK_HASH) {
    this->Error = ERROR_MERKLE_BLOCK_TYPE_INVALID;
  }
}

void MerkleFile::ReadHeaderBlock() {
  this->Block.type = BLOCK_INVALID;
  this->readBlock(0);
  if (!this->Error && this->Block.type != BLOCK_HEADER) {
    this->Error = ERROR_MERKLE_BLOCK_TYPE_INVALID;
  }
}

void MerkleFile::ReadRootHashBlock() {
  this->ReadHeaderBlock();
  this->ReadHashBlock(this->Block.HeaderBlock.numNodes - 1);
}

void MerkleFile::WriteHashBlock(uint16_t n) {
  this->Block.type = BLOCK_HASH;
  this->writeBlock(n + 1);
}

void MerkleFile::writeBlock(uint16_t n) {
  if (this->Error) {
#ifdef DEBUG
    Serial.print(F("E CODE: "));
    Serial.println(this->Error);
    Serial.println(F("MWR HALT"));
    while (1)
      ;
#else
    // merkle file locked until error flag is cleared
    return;
#endif
  }
  if (!m.seekSet(((uint32_t)n) * sizeof(this->Block))) {
    this->Error = ERROR_IO_SEEK;
    return;
  }
  if (m.write(&this->Block, sizeof(this->Block)) != sizeof(this->Block)) {
    // short write
    this->Error = ERROR_IO_WRITE;
    return;
  }
}

void MerkleFile::readBlock(uint16_t n) {
  this->Block.type = BLOCK_INVALID;
  if (this->Error) {
#ifdef DEBUG
    Serial.print(F("E CODE: "));
    Serial.println(this->Error);
    Serial.println(F("MRD HALT"));
    while (1)
      ;
#else
    // merkle file locked until error flag is cleared
    return;
#endif
  }
  if (!m.seekSet(((uint32_t)n) * sizeof(this->Block))) {
    this->Error = ERROR_IO_SEEK;
    return;
  }
  if (m.read(&this->Block, sizeof(this->Block)) != sizeof(this->Block)) {
    // short read
    this->Error = ERROR_IO_READ;
    return;
  }
}

void MerkleFile::reset() {
  this->Error = ERROR_NONE;
  if (this->m.isOpen()) {
    m.close();
  }
}

void MerkleFile::Load(HashChainMessage &msg, uint16_t chunk) {
  uint8_t i, treeDepth;
  uint16_t j;
  // Initialize message header
  this->ReadHeaderBlock();
  msg.Message.Header.version = PROTOCOL_VERSION;
  msg.Message.Header.chunk = chunk;
  msg.Message.Header.filename[MAX_FILENAME_LENGTH] =
      0; // filename set using SetFilename
  msg.Message.Header.numChunks = this->Block.HeaderBlock.numChunks;
  msg.Message.Header.fileSize = this->Block.HeaderBlock.fileSize;
  msg.Message.Header.chunkSize = this->Block.HeaderBlock.chunkSize;
  treeDepth = msg.Message.Header.treeDepth = this->Block.HeaderBlock.treeDepth;
  msg.Message.Header.messageLength =
      sizeof(msg.Message.Header) + 32 * treeDepth;
  j = 0;
  this->ReadRootHashBlock();
  memcpy(msg.Message.Header.rootHash, this->Block.HashBlock.hash, 32);
  this->ReadHashBlock(chunk);
  memcpy(msg.Message.Header.chunkHash, this->Block.HashBlock.hash, 32);
  for (i = 0; i < treeDepth; i++) {
    this->ReadHashBlock((j + (chunk >> i)) ^ 0x01);
    if (this->Error) {
      return;
    }
    memcpy(msg.Message.chain[i], this->Block.HashBlock.hash, 32);
    j += 1 << (treeDepth - i);
  }
}

void MerkleFile::Save(HashChainMessage &msg) {
  uint8_t i;
  uint16_t treeLayerWidth = (1 << msg.Message.Header.treeDepth);
  uint16_t j = 0, chunk = msg.Message.Header.chunk;
  if (!msg.Verified) {
    // refuse to save unverified message
    // TODO this should halt, as it is a bug
    return;
  }
  if (this->HashKnown(chunk)) {
    // this hash chain is already saved, don't save it again
    Serial.print(F("HDP "));
    Serial.println(chunk);
    return;
  }
  Serial.print(F("HOK "));
  Serial.println(chunk);
  this->SetHash(chunk, msg.Message.Header.chunkHash);
  for (i = 0; i <= msg.VerifyDepth; i++) {
    uint16_t n = (j + (chunk >> i)) ^ 0x01;
    if (this->HashKnown(n)) {
      // remainder of chain already known, stop here
      this->m.sync();
      return;
    }
    this->SetHash(n, msg.Message.chain[i]);
    j += treeLayerWidth;
    treeLayerWidth >>= 1;
  }
  this->m.sync();
}

// Returns true if the hash chain is valid
bool MerkleFile::Verify(HashChainMessage &msg) {
  Sha256Context ctx;
  uint8_t hash[32];
  uint8_t i;
  uint16_t j = 0;
  bool isOpen = this->m.isOpen();
  msg.Verified = false;
  if (!isOpen) {
    // merkle file is not yet open, verify message header fields are within
    // limits
    if ((msg.Message.Header.filename[MAX_FILENAME_LENGTH] != '\0') ||
        (msg.Message.Header.treeDepth > MAX_TREE_DEPTH) ||
        (msg.Message.Header.fileSize > MAX_FILE_SIZE) ||
        (msg.Message.Header.chunkSize > MAX_CHUNK_SIZE) ||
        (msg.Message.Header.numChunks > (1 << msg.Message.Header.treeDepth)) ||
        (msg.Message.Header.chunk >= msg.Message.Header.numChunks)) {
      // reject
      return false;
    }
    // verify filename is legal
    for (i = 0;
         i < MAX_FILENAME_LENGTH && msg.Message.Header.filename[i] != '\0';
         i++) {
      if (!FatLegalChar(msg.Message.Header.filename[i])) {
        // reject
        Serial.print(F("bad filename: "));
        Serial.println(msg.Message.Header.filename);
        return false;
      }
    }
    // i == strlen(msg.Message.Header.filename)
    if (i == 0) {
      // reject empty filename
      return false;
    }
    // header fields ok to create a merkle file
  } else {
    // merkle file is open, verify message header fields match local copy
    this->ReadHeaderBlock();
    if (this->Error) {
      // merkle file error, reject
      return false;
    }
    if ((msg.Message.Header.fileSize != this->Block.HeaderBlock.fileSize) ||
        (msg.Message.Header.chunkSize != this->Block.HeaderBlock.chunkSize) ||
        (msg.Message.Header.treeDepth != this->Block.HeaderBlock.treeDepth) ||
        (msg.Message.Header.numChunks != this->Block.HeaderBlock.numChunks) ||
        (msg.Message.Header.chunk >= this->Block.HeaderBlock.numChunks)) {
      // reject
      return false;
    }
    // verify root hashes match
    this->ReadRootHashBlock();
    if (this->Error) {
      return false;
    }
    if (memcmp(msg.Message.Header.rootHash, this->Block.HashBlock.hash, 32)) {
      // reject
      return false;
    }
    // don't care about the filename, since the file is already open, but null
    // it to be safe
    msg.Message.Header.filename[0] = '\0';
  }
  // verify hash chain proper
  memcpy(hash, msg.Message.Header.chunkHash, 32);
  // TODO remove
  memset(&ctx, 0, sizeof(ctx));
  for (i = 0; i < msg.Message.Header.treeDepth; i++) {
    sha256_init(&ctx);
    if (msg.Message.Header.chunk & (1 << i)) {
      sha256_update(&ctx, msg.Message.chain[i], 32);
      sha256_update(&ctx, hash, 32);
    } else {
      sha256_update(&ctx, hash, 32);
      sha256_update(&ctx, msg.Message.chain[i], 32);
    }
    sha256_final(&ctx, hash);
    if (isOpen) {
      j += 1 << (msg.Message.Header.treeDepth - i);
      if (this->HashKnown(j + (msg.Message.Header.chunk >> (i + 1)))) {
        // reached an already known hash. compare to this known hash instead of
        // computing all the way to the tree root
        msg.Verified = (memcmp(hash, this->Block.HashBlock.hash, 32) == 0);
        msg.VerifyDepth = i;
        return msg.Verified;
      }
    }
  }
  // accept if message's rootHash matches computed hash
  msg.Verified = (memcmp(hash, msg.Message.Header.rootHash, 32) == 0);
  msg.VerifyDepth = i - 1; // i == msg.Message.Header.treeDepth
  return msg.Verified;
}

void MerkleFile::Check(FatFile &f) {
  Sha256Context ctx;
  this->ReadHeaderBlock();
  uint16_t numNodes = this->Block.HeaderBlock.numNodes;
  // clear chunk complete flags
  for (uint16_t i = 0; i < numNodes; i++) {
    this->ReadHashBlock(i);
    this->Block.HashBlock.flags &= ~MERKLE_CHUNK_COMPLETE;
    this->writeBlock(i + 1);
  }
  // reset chunk complete flags on validated chunks
  this->scanChunks(f, false, ctx);
}

void MerkleFile::Fill() {
  Sha256Context ctx;
  this->fill(ctx);
}

void MerkleFile::fill(Sha256Context &ctx) {
  this->ReadHeaderBlock();
  const uint16_t numLeafs = this->Block.HeaderBlock.numLeafs;
  const uint16_t numChunks = this->Block.HeaderBlock.numChunks;
  const uint16_t numNodes = this->Block.HeaderBlock.numNodes;
  uint8_t flags;
  uint16_t i;
  // Hashes of empty leaf nodes are always known and complete
  sha256_init(&ctx);
  sha256_final(&ctx, this->Block.HashBlock.hash);
  this->Block.HashBlock.flags = MERKLE_HASH_KNOWN | MERKLE_CHUNK_COMPLETE;
  for (i = numChunks; i < numLeafs; i++) {
    this->WriteHashBlock(i);
  }
  // Scan internal nodes and compute their hashes if both child hashes are known
  for (; i < numNodes; i++) {
    if (this->HashKnown(i)) {
      continue;
    }
    // save flags so a re-read isn't needed later
    flags = this->Block.HashBlock.flags | MERKLE_HASH_KNOWN;
    sha256_init(&ctx);
    if (!this->HashKnown(2 * (i - numLeafs))) {
      // left child hash missing
      continue;
    }
    // this->ReadHashBlock(2 * (i - numLeafs)); // already read by HashKnown
    sha256_update(&ctx, this->Block.HashBlock.hash, 32);
    if (!this->HashKnown(2 * (i - numLeafs) + 1)) {
      // right child hash missing
      continue;
    }
    // this->ReadHashBlock(2 * (i - numLeafs) + 1); // already read by HashKnown
    // both children of this hash block have known hashes. compute hash for
    // hash block i
    sha256_update(&ctx, this->Block.HashBlock.hash, 32);
    sha256_final(&ctx, this->Block.HashBlock.hash);
    this->Block.HashBlock.flags = flags;
    this->WriteHashBlock(i);
  }
}

} // namespace PDP
