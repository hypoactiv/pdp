#include "driver.h"
#include "power.h"
#include "pdp.h"

static bool IncrementChannel;
static uint8_t ChannelsTried;

static bool Transmitting;

// The file being broadcast or received. If not open, receive first transmission
// heard.
static FatFile f;

void DoChannelIncrement() {
  uint8_t nextChannel = radio.getChannel() + 1;
  if (nextChannel > CHANNEL_MAX) {
    nextChannel = 0;
  }
  ChannelsTried++;
  radio.setChannel(nextChannel);
  radio.openWritingPipe((uint8_t *)"BROAD");
  radio.openReadingPipe(1, (uint8_t *)"BROAD");
  Serial.print(F("Channel "));
  Serial.println(nextChannel);
}

void PrintFilename(FatFile &f) {
  char filename[PDP::MAX_FILENAME_LENGTH + 1];
  f.getName(filename, PDP::MAX_FILENAME_LENGTH + 1);
  Serial.println(filename);
}

void OpenNext() {
  static FatFile srcDir;
  if (!srcDir.isOpen()) {
    srcDir.openRoot(&sd);
    srcDir.rewind();
  }
  if (!f.openNext(&srcDir, O_READ)) {
    srcDir.rewind();
    f.openNext(&srcDir, O_READ);
  }
}

void Broadcast() {
  PDP::Transmitter t(radio, sd, f);
  Serial.print(F("TX "));
  PrintFilename(f);
  t.Broadcast();
  if (FlagShutdown) {
    return;
  }
  if (t.Error == PDP::ERROR_TX_INTERFERENCE) {
    // someone else on this channel. change channel and keep broadcasting.
    // random backoff
    Serial.println(F("interference"));
    if (random(0, 10) >= 1) {
      Transmitting = false;
      f.close();
    } else {
      delay(random(0, 1000));
      IncrementChannel = true;
      if (ChannelsTried > CHANNEL_MAX) {
        // all channels full, give up TX
        f.close();
        Transmitting = false;
        ChannelsTried = 0;
      }
    }
  } else if (t.Error == PDP::ERROR_REFUSE_MERKLE) {
    // Tried to transmit a merkle file, skip
    f.close();
  } else if (t.Error) {
    // unknown error, halt.
    Serial.println(F("Error during transmit."));
    Serial.println(t.Error);
    while (1)
      ; // TODO remove
  } else {
    // No error, file was broadcast at least once, or channel yielded
    if (t.YieldState == PDP::YIELD_GRANTED) {
      // Expect to receive this file on this channel
      Transmitting = false;
    } else {
      // Done broadcasting this file, resume listening
      Transmitting = false;
      f.close();
    }
  }
}

void Listen() {
  PDP::Receiver r(radio, sd, f, CHANNEL_TIMEOUT);
  Serial.print(F("RX "));
  if (f.isOpen()) {
    Serial.print(F("expecting: "));
    PrintFilename(f);
  } else {
    Serial.println(F("anything."));
  }
  r.Listen();
  if (r.Error == PDP::ERROR_RX_TIMEOUT) {
    // Nobody on this channel or TX went away before file was done
    f.close();
    IncrementChannel = true;
  } else if (r.Error) {
    // unknown error, halt
    Serial.println(F("Error during receive."));
    Serial.println(r.Error);
    while (1)
      ; // TODO remove
  } else {
    // No error, some progress was made
    if (r.YieldState == PDP::YIELD_GRANTED) {
      // TX is expecting us to begin transmitting this file
      Transmitting = true;
    } else {
      // Done receiving this file, try next channel
      f.close();
      IncrementChannel = true;
    }
  }
}

void Run() {
  radio.setChannel(random(0, CHANNEL_MAX)); // TODO experimental
  Serial.print(F("Channel "));
  Serial.println(radio.getChannel());
  radio.openWritingPipe((uint8_t *)"BROAD");
  radio.openReadingPipe(1, (uint8_t *)"BROAD");
  while (true) {
    if (IncrementChannel) {
      DoChannelIncrement();
      if (ChannelsTried > CHANNEL_MAX) {
        // Listened on all channels without receiving anything, switch to TX
        ChannelsTried = 0;
        Transmitting = true;
      }
      IncrementChannel = false;
    }
    if (Transmitting) {
      // If f is not already open, pick a file and open it. Broadcast f.
      if (!f.isOpen()) {
        OpenNext();
      }
      // TODO is f always open here?
      if (f.isOpen()) {
        Broadcast();
      }
    } else {
      // Listen and receive anything on this channel
      Listen();
    }
    if (FlagShutdown) {
      // Shutdown requested. Close file, if any is open, and power off radio,
      // and power off.
      f.close();
      radio.powerDown();
      PowerOff();
    }
  }
}
