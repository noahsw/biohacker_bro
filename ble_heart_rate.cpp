#include "ble_heart_rate.h"
#include "config.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

volatile int currentBPM = 0;
volatile bool hrConnected = false;

namespace {

BLEClient *bleClient = nullptr;
BLERemoteCharacteristic *hrCharacteristic = nullptr;
bool doConnect = false;
BLEAdvertisedDevice *foundDevice = nullptr;

class HRNotifyCallback {
public:
  static void onNotify(BLERemoteCharacteristic *pChar, uint8_t *data, size_t length, bool isNotify) {
    if (length < 2) return;
    uint8_t flags = data[0];
    int bpm;
    if (flags & 0x01) {
      // 16-bit BPM value
      bpm = data[1] | (data[2] << 8);
    } else {
      // 8-bit BPM value
      bpm = data[1];
    }
    currentBPM = bpm;
  }
};

class ClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *client) override {
    hrConnected = true;
  }
  void onDisconnect(BLEClient *client) override {
    hrConnected = false;
    currentBPM = 0;
  }
};

class ScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    // Only care about devices advertising the standard Heart Rate service
    if (!advertisedDevice.isAdvertisingService(BLEUUID((uint16_t)0x180D))) {
      return;
    }
    // MAC filter — this is the part that ignores your friend's Whoop
    String seenMac = advertisedDevice.getAddress().toString().c_str();
    seenMac.toUpperCase();
    String target = TARGET_WHOOP_MAC;
    target.toUpperCase();
    if (seenMac != target) {
      return; // not your Whoop — ignore it
    }
    BLEDevice::getScan()->stop();
    foundDevice = new BLEAdvertisedDevice(advertisedDevice);
    doConnect = true;
  }
};

bool connectToWhoop() {
  bleClient = BLEDevice::createClient();
  bleClient->setClientCallbacks(new ClientCallback());

  if (!bleClient->connect(foundDevice)) {
    return false;
  }

  BLERemoteService *hrService = bleClient->getService(BLEUUID((uint16_t)0x180D));
  if (hrService == nullptr) {
    bleClient->disconnect();
    return false;
  }

  hrCharacteristic = hrService->getCharacteristic(BLEUUID((uint16_t)0x2A37));
  if (hrCharacteristic == nullptr) {
    bleClient->disconnect();
    return false;
  }

  if (hrCharacteristic->canNotify()) {
    hrCharacteristic->registerForNotify(HRNotifyCallback::onNotify);
  }

  return true;
}

} // namespace

void bleSetup() {
  BLEDevice::init("BiohackerBro");
  BLEScan *scanner = BLEDevice::getScan();
  scanner->setAdvertisedDeviceCallbacks(new ScanCallback());
  scanner->setActiveScan(true);
  scanner->start(0, nullptr, false); // scan indefinitely until we find our Whoop
}

void bleLoop() {
  if (doConnect) {
    doConnect = false;
    if (!connectToWhoop()) {
      Serial.println("Failed to connect to Whoop — resuming scan.");
      BLEDevice::getScan()->start(0, nullptr, false);
    }
  }
}
