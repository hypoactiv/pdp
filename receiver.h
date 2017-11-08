#ifndef RECEIVER_H
#define RECEIVER_H

#include "RF24.h"
#include "SdFat.h"
#include "merkle.h"
#include "transceiver.h"

namespace PDP {

class Receiver : public TransceiverBase {
public:
  // If chunkFile is open, Receiver will only receive that file. All other
  // files will be ignored.
  //
  // If chunkFile is not open, Receiver will listen on the current channel
  // for a file being transmit. If a broadcast is found, chunkFile will
  // be opened to the received file.
  Receiver(RF24 &radio, FatFileSystem &fs, FatFile &chunkFile,
           const uint16_t timeout);
  // Begin listening.
  //
  // Returns with ERROR_RX_TIMEOUT if timeout elapses with no progress made, but
  // file is still incomplete.
  // (driver: new channel, close file, listen again. all channels tried:
  // switch to broadcast)
  //
  // Returns with ERROR_NONE and YieldState YIELD_NONE if the file being
  // received is complete, and the TX is not missing any chunks, or TX is
  // ignoring channel yield request.
  // (driver: new channel? close file, listen again)
  //
  // Returns with ERROR_NONE and YieldState YIELD_GRANTED if the TX is missing
  // chunks and the channel has been yielded to
  // this station.
  // (driver: keep channel, keep file, construct tx, broadcast)
  void Listen();

private:
  uint16_t stationId;
  FatFileSystem &_fs;
  const uint16_t _timeout;
  // true if a valid hash chain has been received. if true, rootHash and
  // treeDepth fields are valid
  bool knowRoot;
  // If true, TX is missing chunks.
  bool txIncomplete;
  // number of times we've asked for a yield
  uint16_t yieldRequests;
  // Receive the hash chain currently being broadcast
  void receiveHashChain(uint16_t messageLength);
  // Receive the data chunk currently being broadcast
  void receiveDataChunk(uint16_t messageLength);
  // Receive the listen period start message, returning the duration
  // of the listen period in listenDuration
  void receiveListenForReq(uint16_t messageLength, uint16_t &listenDuration);
  // Broadcast a request for missing chunks
  void broadcastRequests();
};

} // namespace PDP

#endif // RECEIVER_H
