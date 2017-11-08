#ifndef CONSTS_H
#define CONSTS_H

#include "stdint.h"

namespace PDP {

typedef enum {
  ERROR_NONE = 0,
  ERROR_MERKLE_FILE_INVALID,
  ERROR_MERKLE_BLOCK_TYPE_INVALID,
  ERROR_MERKLE_ROOT_MISMATCH,
  ERROR_CHUNKFILE_EMPTY_OR_TOO_BIG,
  ERROR_CHUNK_UNSET,
  ERROR_CHUNK_SIZE_INVALID,
  ERROR_IO_OPEN,
  ERROR_IO_CREATE,
  ERROR_IO_READ,
  ERROR_IO_WRITE,
  ERROR_IO_SEEK,
  ERROR_IO_READCHUNK,
  ERROR_IO_ALREADY_EXISTS,
  ERROR_RX_TIMEOUT,
  ERROR_TX_INTERFERENCE,
  ERROR_MULTIPART_BAD_SEQUENCE,
  ERROR_REFUSE_MERKLE,
} Error_t;

const uint32_t MAX_CHUNK_SIZE = 384;
const uint8_t MAX_TREE_DEPTH = 11;
const uint32_t MAX_FILE_SIZE =
    (1 << MAX_TREE_DEPTH) *
    MAX_CHUNK_SIZE; // TODO test with files in range [0,MAX_FILE_SIZE) or ]
const uint8_t RETRANSMITS = 3;
const uint8_t PROTOCOL_VERSION = 1;
const uint8_t MAX_FILENAME_LENGTH = 32;

const uint16_t MAX_STATION_ID = 0xffff;

typedef enum {
  // In RX context: don't want to request yield
  // In TX context: file is complete and don't want to offer yield
  YIELD_NONE = 0,
  // In RX context: have a chunk TX is missing, and requesting yield
  // In TX context: unused
  YIELD_REQUEST,
  // In RX context: TX has granted us the channel
  // In TX context: channel has been granted to RX
  YIELD_GRANTED,
} YieldState_t;

// TODO rename MAX_CHUNKQUEUE_LEN
const uint8_t REQ_QUEUE_LEN = 8;

// TX request listen duration in milliseconds
const uint16_t LISTEN_DURATION = 100;

const uint8_t BLOCK_INVALID = 0;
const uint8_t BLOCK_HEADER = 1;
const uint8_t BLOCK_HASH = 2;

const uint8_t MODE_TRANSMIT = 1;
const uint8_t MODE_RECEIVE = 2;

const uint8_t SEQ_CHAIN_START = 32;
const uint8_t SEQ_CHAIN_FINAL = 32 + MAX_TREE_DEPTH - 1;
const uint8_t SEQ_CHUNK_START = 64;
const uint8_t SEQ_TAKING_REQS = 0; // TX is entering request listen period
const uint8_t SEQ_MAKING_REQ =
    8; // RX is making a request during TX's listen period

} // namespace PDP

#endif // CONSTS_H
