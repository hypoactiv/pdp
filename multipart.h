#ifndef MULTIPART_H
#define MULTIPART_H

#include "consts.h"
#include "stdint.h"

namespace PDP {

typedef struct {
  uint8_t sequenceNum;
  uint16_t messageId;
  uint8_t protocolVersion;
  uint32_t messageLength;
} MultipartHeader;

// MultipartSplitter creates multipart messages from a buffer, to be
// recombined using a MultipartCombiner on the receiving side.
template <uint16_t PartSize> class MultipartSplitter {
public:
  MultipartSplitter(void *src, uint16_t len, uint8_t seq, uint16_t mid);
  // Returns true if there are more parts to be emitted
  bool More();
  // Gets the next size PartSize part of the multipart message into dst
  //
  // Returns true if there are more parts to be emitted
  bool Get(uint8_t *dst);

private:
  uint8_t *src;
  uint16_t len;
  uint8_t seq;
  uint16_t mid, cursor;
};

template <uint16_t PartSize>
MultipartSplitter<PartSize>::MultipartSplitter(void *src, uint16_t len,
                                               uint8_t seq, uint16_t mid)
    : src((uint8_t *)src), len(len), seq(seq), mid(mid), cursor(0) {}

template <uint16_t PartSize> bool MultipartSplitter<PartSize>::More() {
  return (this->cursor < this->len);
}

template <uint16_t PartSize>
bool MultipartSplitter<PartSize>::Get(uint8_t *dst) {
  MultipartHeader *mpHeader = (MultipartHeader *)dst;
  mpHeader->sequenceNum = this->seq;
  mpHeader->messageId = this->mid;
  memcpy(dst + 3, this->src + this->cursor,
         (this->cursor + PartSize - 3 < this->len) ? PartSize - 3
                                                   : this->len - this->cursor);
  this->seq++;
  this->cursor += PartSize - 3;
  return this->More();
}

// MultipartCombiner reassembles the parts emitted by a MultipartSplitter
template <uint16_t PartSize> class MultipartCombiner {
public:
  MultipartCombiner(void *dst, uint16_t len, uint8_t seq);
  // Returns true if there are more parts to be combined
  bool More();
  // Puts the size PartSize part of the multipart message stored in src
  //
  // Returns true if there are more parts to be combined
  bool Put(uint8_t *src);
  // Returns true if there was a sequence number error
  uint16_t Remaining();
  Error_t Error;

private:
  uint8_t *dst;
  uint16_t len;
  uint8_t seq;
  uint16_t cursor, mid;
  bool knowMid;
};

template <uint16_t PartSize>
MultipartCombiner<PartSize>::MultipartCombiner(void *dst, uint16_t len,
                                               uint8_t seq)
    : Error(ERROR_NONE), dst((uint8_t *)dst), len(len), seq(seq), cursor(0),
      knowMid(false) {}

template <uint16_t PartSize> bool MultipartCombiner<PartSize>::More() {
  if (this->Error) {
    // previous error, stop combining
    return false;
  }
  return (this->cursor < this->len);
}

template <uint16_t PartSize>
bool MultipartCombiner<PartSize>::Put(uint8_t *src) {
  MultipartHeader *mpHeader = (MultipartHeader *)src;
  if (this->Error || !this->More()) {
    return false;
  }
  // compare src's sequence number to expected value
  if (mpHeader->sequenceNum == this->seq - 1) {
    // duplicate part, ignore
    return true;
  }
  if (mpHeader->sequenceNum != this->seq) {
    // invalid sequence number, error
    this->Error = ERROR_MULTIPART_BAD_SEQUENCE;
    return false;
  }
  if (!this->knowMid) {
    this->knowMid = true;
    this->mid = mpHeader->messageId;
  } else {
    if (mpHeader->messageId != this->mid) {
      // part of some other message, ignore
      return true;
    }
  }
  memcpy(this->dst + this->cursor, src + 3,
         (this->Remaining() > PartSize - 3) ? PartSize - 3 : this->Remaining());
  this->seq++;
  this->cursor += PartSize - 3;
  return this->More();
}

template <uint16_t PartSize> uint16_t MultipartCombiner<PartSize>::Remaining() {
  return this->len - this->cursor;
}

} // namespace PDP

#endif // MULTIPART_H
