#ifndef UTIL_H
#define UTIL_H

#include "consts.h"

namespace PDP {

void PrintHash(uint8_t *hash);
void CollectEntropy(SdSpiCard *card, RF24 &radio);

// TODO make the argument order on these two the same
Error_t Create(FatFileSystem &fs, FatFile &f, char *filename,
               uint32_t filesize);
Error_t OpenFile(FatFileSystem &fs, const char *filename, FatFile &f,
                 uint8_t flag);
void ToMerkleFilename(char *dataFilename);

} // namespace PDP

#endif // UTIL_H
