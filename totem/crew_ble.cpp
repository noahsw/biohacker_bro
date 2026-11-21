#include "crew_ble.h"
#include "config.h"

#include <NimBLEDevice.h>

// NimBLE-Arduino, not the core's bundled BLE library, for the same reason as
// the chest display (see ble_heart_rate.cpp): the Whoop does its own MTU
// exchange, and connect(..., exchangeMTU = false) is the only way to skip ours.

#if defined(MYNEWT_VAL_BLE_MAX_CONNECTIONS) && CREW_ENABLED
#if MYNEWT_VAL_BLE_MAX_CONNECTIONS < 5
#warning "NimBLE is set for fewer than 5 connections, so not every friend can connect (default 3: you + 2 friends). See totem_config.h section 6."
#endif
#endif

volatile int currentBPM = 0;
volatile bool hrConnected = false;
CrewMember crew[CREW_MAX];
int crewCount = 0;

namespace {

// A strap we follow. Slot 0 is you; 1..crewCount are friends.
struct Peer {
  String mac;                 // uppercase
  NimBLEClient *client = nullptr;
  volatile bool connected = false;
  volatile int bpm = 0;
  volatile unsigned long lastNotifyMs = 0;
  volatile unsigned long retryAt = 0;   // after a failed connect: ignored by the scan until then
  bool noSlot = false;        // NimBLE ran out of connections for this one
};
const int MAX_PEERS = 1 + CREW_MAX;
Peer peers[MAX_PEERS];
int peerCount = 0;

// A strap is "gone quiet" if connected but no reading for this long.
const unsigned long STALE_MS = 5000;
// Connected but silent this long: the link is up but the readings stopped
// (the subscription got lost). Drop it so it reconnects and subscribes again,
// rather than showing no heart rate for the rest of the night.
const unsigned long SILENT_KICK_MS = 30000;
// After a failed connect, leave that strap alone this long. A connect that
// fails at once (no free connection) would otherwise loop scan/connect/fail
// as fast as the strap advertises, starving everyone else's reconnects.
const unsigned long RETRY_MS = 3000;

NimBLEAdvertisedDevice pendingDevice;
volatile int pendingPeer = -1;

void startScan() {
  if (!NimBLEDevice::getScan()->isScanning())
    NimBLEDevice::getScan()->start(0, false, true);   // fresh scan, see ble_heart_rate.cpp
}

bool allConnected() {
  for (int i = 0; i < peerCount; i++)
    if (!peers[i].connected && !peers[i].noSlot) return false;
  return true;
}

int peerForClient(NimBLEClient *c) {
  for (int i = 0; i < peerCount; i++)
    if (peers[i].client == c) return i;
  return -1;
}

void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t length, bool /*isNotify*/) {
  int i = peerForClient(chr->getRemoteService()->getClient());
  if (i < 0 || length < 2) return;
  int bpm;
  if (data[0] & 0x01) {           // 16-bit BPM
    if (length < 3) return;
    bpm = data[1] | (data[2] << 8);
  } else {
    bpm = data[1];
  }
  // Sensor contact: bit 2 = the strap reports it, bit 1 = on skin. A strap
  // taken off stays connected and keeps sending, often its last BPM, so treat
  // "reports contact, none detected" as nobody wearing it.
  // (The COOSPO HW807 armband doesn't report contact. Taken off, it keeps
  // reading a smoothly rising made-up rate for about a minute, then sends 0;
  // too much like a real rising rate to filter out.)
  if ((data[0] & 0x06) == 0x04) bpm = 0;
  peers[i].bpm = bpm;
  peers[i].lastNotifyMs = millis();
}

class ClientCallback : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) override {
    int i = peerForClient(client);
    if (i >= 0) {
      peers[i].lastNotifyMs = millis();   // the silent-link timer starts now, not at the last session's reading
      peers[i].connected = true;
    }
  }
  void onDisconnect(NimBLEClient *client, int reason) override {
    int i = peerForClient(client);
    if (i >= 0) {
      peers[i].connected = false;
      peers[i].bpm = 0;
      Serial.printf("%s disconnected (reason=%d), rescanning.\r\n", i == 0 ? "Your Whoop" : "A friend's strap", reason);
    }
    startScan();   // walked off and came back: pick them up again
  }
};
ClientCallback clientCallback;

class ScanCallback : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (pendingPeer >= 0) return;
    if (!dev->isAdvertisingService(NimBLEUUID((uint16_t)0x180D))) return;
    String seen = dev->getAddress().toString().c_str();
    seen.toUpperCase();
    for (int i = 0; i < peerCount; i++) {
      if (peers[i].connected || peers[i].noSlot || seen != peers[i].mac) continue;
      if ((long)(millis() - peers[i].retryAt) < 0) continue;   // failed just now: give it a moment
      pendingDevice = *dev;
      pendingPeer = i;
      NimBLEDevice::getScan()->stop();
      return;
    }
  }
};
ScanCallback scanCallback;

bool connectPeer(int i) {
  Peer &p = peers[i];
  if (p.client == nullptr) {
    // One client per strap, made once and reused: NimBLE's limit counts
    // client objects, so making a new one per reconnect would use it up.
    p.client = NimBLEDevice::createClient();
    if (p.client == nullptr) {
      p.noSlot = true;
      Serial.printf("No BLE connection left for strap %d (%s). Raise MYNEWT_VAL_BLE_MAX_CONNECTIONS, see totem_config.h.\r\n",
                    i, p.mac.c_str());
      return false;
    }
    p.client->setClientCallbacks(&clientCallback, false);
    p.client->setConnectTimeout(5000);   // ms; a friend at the edge of range shouldn't hold up the others
  }
  if (!p.client->connect(&pendingDevice, true, false, false)) return false;   // exchangeMTU = false

  NimBLERemoteService *svc = p.client->getService(NimBLEUUID((uint16_t)0x180D));
  NimBLERemoteCharacteristic *chr = svc ? svc->getCharacteristic(NimBLEUUID((uint16_t)0x2A37)) : nullptr;
  if (chr == nullptr || !chr->canNotify() || !chr->subscribe(true, onNotify)) {
    p.client->disconnect();
    return false;
  }
  return true;
}

void bleTask(void *) {
  for (;;) {
    int i = pendingPeer;
    if (i >= 0) {
      if (!connectPeer(i)) {
        peers[i].retryAt = millis() + RETRY_MS;
        Serial.printf("Couldn't connect strap %d, still scanning.\r\n", i);
      }
      pendingPeer = -1;
      if (!allConnected()) startScan();
    }

    unsigned long now = millis();

    // Scan watchdog. The scan is restarted from onDisconnect and after each
    // connect, but a start can fail (another connect in progress, a NimBLE
    // host reset), and then a strap that dropped would never be picked up
    // again. So: anyone missing and no scan running means start one.
    static unsigned long lastScanCheck = 0;
    if (now - lastScanCheck >= 2000) {
      lastScanCheck = now;
      if (pendingPeer < 0 && !allConnected() && !NimBLEDevice::getScan()->isScanning()) {
        Serial.println("BLE: a strap is missing and the scan had stopped, restarting it.");
        startScan();
      }
      for (int k = 0; k < peerCount; k++) {
        Peer &p = peers[k];
        // Signed: a reading can land after `now` was taken (another task).
        if (p.connected && p.client && (long)(now - p.lastNotifyMs) > (long)SILENT_KICK_MS) {
          Serial.printf("Strap %d connected but silent for %lu s, reconnecting.\r\n", k, SILENT_KICK_MS / 1000);
          p.lastNotifyMs = now;   // once, not every check, while it goes
          p.client->disconnect();
        }
      }
    }

    // Publish to the drawing code. A strap that's connected but silent for
    // STALE_MS (out of range but the link not yet dropped) shows as gone.
    for (int k = 0; k < peerCount; k++) {
      // Signed, as above: unsigned, a reading arriving just after `now` was
      // taken wrapped to ~49 days old and blanked the BPM for a cycle.
      bool live = peers[k].connected && peers[k].bpm > 0 && (long)(now - peers[k].lastNotifyMs) < (long)STALE_MS;
      int bpm = live ? peers[k].bpm : 0;
      if (k == 0) {
        hrConnected = peers[0].connected;
        currentBPM = peers[0].connected ? bpm : 0;
      } else {
        crew[k - 1].connected = live;
        crew[k - 1].bpm = bpm;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void addPeer(const char *mac) {
  peers[peerCount].mac = mac;
  peers[peerCount].mac.toUpperCase();
  peerCount++;
}

} // namespace

void bleSetup() {
  addPeer(TARGET_WHOOP_MAC);
#if CREW_ENABLED
  const char *macs[CREW_MAX] = {CREW_MAC_1, CREW_MAC_2, CREW_MAC_3, CREW_MAC_4};
  const char initials[CREW_MAX] = {CREW_INITIAL_1, CREW_INITIAL_2, CREW_INITIAL_3, CREW_INITIAL_4};
  for (int i = 0; i < CREW_MAX; i++) {
    if (macs[i][0] == 0) continue;
    crew[crewCount].initial = initials[i];
    crew[crewCount].bpm = 0;
    crew[crewCount].connected = false;
    crewCount++;
    addPeer(macs[i]);
  }
#endif

  NimBLEDevice::init("BiohackerBro");
  NimBLEScan *scanner = NimBLEDevice::getScan();
  scanner->setScanCallbacks(&scanCallback, false);
  // Callbacks only, keep nothing. NimBLE's default keeps every advertiser it
  // has ever seen, and this scan runs all night whenever a strap is missing:
  // in a crowd (phones, watches, earbuds, all rotating random addresses) that
  // list grows by thousands an hour until the heap runs out, and every
  // advert is a linear search of it on the Bluetooth core.
  scanner->setMaxResults(0);
  scanner->setActiveScan(true);
  startScan();
  xTaskCreatePinnedToCore(bleTask, "crew_ble", 6144, nullptr, 1, nullptr, 0);
  Serial.printf("Following your Whoop + %d friend(s).\r\n", crewCount);
}
