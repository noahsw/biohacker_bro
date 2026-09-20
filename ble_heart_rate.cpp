#include "ble_heart_rate.h"
#include "config.h"

#include <NimBLEDevice.h>

// ============================================================================
// Why NimBLE-Arduino instead of the ESP32 core's bundled "BLE" library:
//
// The bundled library unconditionally calls ble_gattc_exchange_mtu() the
// moment the connection comes up. The Whoop performs the MTU exchange itself
// the instant the link is established, so that call returns BLE_HS_EALREADY
// (status=2). The bundled library treats that as a fatal connection failure
// and bails out — while leaving the BLE link open, so every retry then fails
// with "Client busy" against its own live connection. Confirmed on hardware
// against the real strap; the link reported MTU=247 already negotiated.
//
// NimBLE-Arduino exposes connect(..., exchangeMTU) so we can skip the
// redundant exchange. That is the whole reason for the dependency.
// Install: Library Manager -> "NimBLE-Arduino" by h2zero (tested on 2.5.1).
// ============================================================================

volatile int currentBPM = 0;
volatile bool hrConnected = false;

namespace {

NimBLEAdvertisedDevice foundDevice;
volatile bool doConnect = false;

void startScan() {
  // Third arg restarts a fresh scan rather than resuming a cached one;
  // without it the de-dupe cache can hide the Whoop on a reconnect.
  NimBLEDevice::getScan()->start(0, false, true);
}

void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t length, bool isNotify) {
  if (length < 2) return;
  uint8_t flags = data[0];
  int bpm;
  if (flags & 0x01) {
    // 16-bit BPM — only valid if the packet actually carries the second byte.
    if (length < 3) return;
    bpm = data[1] | (data[2] << 8);
  } else {
    bpm = data[1];
  }
  currentBPM = bpm;
}

class ClientCallback : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) override {
    hrConnected = true;
  }
  void onDisconnect(NimBLEClient *client, int reason) override {
    hrConnected = false;
    currentBPM = 0;
    Serial.printf("Whoop disconnected (reason=%d) — rescanning.\r\n", reason);
    startScan(); // a dropped strap at the party should recover on its own
  }
};
ClientCallback clientCallback;

class ScanCallback : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    // Only care about devices advertising the standard Heart Rate service
    if (!advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0x180D))) {
      return;
    }
    // MAC filter — this is the part that ignores your friend's Whoop
    String seenMac = advertisedDevice->getAddress().toString().c_str();
    seenMac.toUpperCase();
    String target = TARGET_WHOOP_MAC;
    target.toUpperCase();
    if (seenMac != target) {
      return; // not your Whoop — ignore it
    }
    foundDevice = *advertisedDevice;
    doConnect = true;
    NimBLEDevice::getScan()->stop();
  }
};
ScanCallback scanCallback;

bool connectToWhoop() {
  NimBLEClient *client = NimBLEDevice::getDisconnectedClient();
  if (client == nullptr) {
    client = NimBLEDevice::createClient();
  }
  client->setClientCallbacks(&clientCallback, false);

  // exchangeMTU = false — see the note at the top of this file.
  if (!client->connect(&foundDevice, true, false, false)) {
    return false;
  }

  NimBLERemoteService *hrService = client->getService(NimBLEUUID((uint16_t)0x180D));
  if (hrService == nullptr) {
    client->disconnect();
    return false;
  }

  NimBLERemoteCharacteristic *hrCharacteristic =
      hrService->getCharacteristic(NimBLEUUID((uint16_t)0x2A37));
  if (hrCharacteristic == nullptr) {
    client->disconnect();
    return false;
  }

  if (!hrCharacteristic->canNotify() || !hrCharacteristic->subscribe(true, onNotify)) {
    client->disconnect();
    return false;
  }

  return true;
}

} // namespace

void bleSetup() {
  NimBLEDevice::init("BiohackerBro");
  NimBLEScan *scanner = NimBLEDevice::getScan();
  scanner->setScanCallbacks(&scanCallback, false);
  scanner->setActiveScan(true);
  startScan(); // scan indefinitely until we find our Whoop
}

void bleLoop() {
  if (doConnect) {
    doConnect = false;
    if (!connectToWhoop()) {
      Serial.println("Failed to connect to Whoop — resuming scan.");
      startScan();
    }
  }
}
