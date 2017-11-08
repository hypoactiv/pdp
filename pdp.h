// Glossary
//
// A 'chunk' is a piece of a file's contents of size at most CHUNK_SIZE. The
// hashes of the chunks make up the leaf nodes of the Merkle tree.
//
// A 'hash chain' is the list of SHA256 hashes needed to verify a chunk belongs
// to a known Merkle tree root hash. It consists of the chunk's sibling's hash,
// the chunk's parent's sibling's hash, and so on.
//
// The 'tree depth' is the number of layers below the root node, not including
// the root node's layer. Equivalently, it is the length of a hash chain, not
// including the root node hash or the leaf node hash being verified.

#ifndef PDP_H
#define PDP_H

// Configuation
#define DEBUG

#include "RF24.h"
#include "SdFat.h"
#include "consts.h"
#include "stdint.h"

namespace PDP {} // namespace PDP

#include "receiver.h"
#include "transmitter.h"

#endif // PDP_H
