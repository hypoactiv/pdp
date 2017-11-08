#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include "RF24.h"
#include "SdFat.h"
#include "merkle.h"
#include "transceiver.h"

namespace PDP {

class Transmitter : public TransceiverBase {
public:
  // chunkFile must be the opened file to be transmit.
  Transmitter(RF24 &radio, FatFileSystem &fs, FatFile &chunkFile);
  // Broadcast the hash chains and file chunks.
  //
  // Returns with ERROR_NONE and YIELD_NONE after the file has been broadcast at
  // least once, and no requests
  // heard recently (TODO define recently).
  // (driver: keep channel, close file, select new file)
  //
  // Returns with ERROR_NONE and YIELD_GRANTED if the channel is yielded to
  // another station.
  // (driver: keep channel, keep file, construct receiver, listen)
  //
  // Returns with ERROR_TX_INTERFERENCE if interference has been detected on the
  // channel
  // (driver: new channel, keep file, broadcast again)
  void Broadcast();
  // at least one valid request was received during broadcast
  bool HeardRequest;

  uint8_t ListenForInterference(uint16_t duration);

private:
  // Station ID of most recent RX station requesting channel yield
  uint16_t yieldRxStationId;
  ChunkQueue transmit;
  void broadcastHashChain(uint16_t chunk);
  void broadcastDataChunk(uint16_t chunk);
  void broadcastListenForReqs();
  bool listenForRequests();
};

} // namespace PDP

#endif // TRANSMITTER_H
