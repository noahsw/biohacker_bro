/*
  ============================================================================
  FIND WHOOP MAC — one-time helper sketch
  ============================================================================
  What this does:
    Flash this to the ESP32-S3 board instead of the main biohacker-bro.ino
    sketch. It scans for nearby BLE devices and prints each one's address,
    advertised name (if any), and whether it's broadcasting the standard
    Bluetooth Heart Rate service (0x180D) — which is what a Whoop in HR
    Broadcast mode advertises. Any hit flagged "LIKELY YOUR WHOOP" is what
    you want to copy into TARGET_WHOOP_MAC in the main sketch.

  How to use:
    1. Put your Whoop in HR Broadcast mode (Whoop app settings) and make
       sure it's the only Whoop nearby (quiet room, no friends' straps on).
    2. Open this sketch in Arduino IDE, select the same board settings you
       use for the main sketch, and flash it.
    3. Open Serial Monitor at 115200 baud.
    4. Watch for a line flagged "LIKELY YOUR WHOOP" and copy that MAC
       address (format AA:BB:CC:DD:EE:FF) into TARGET_WHOOP_MAC in
       biohacker-bro.ino.
    5. Re-flash the main sketch once you have it — this scan sketch is
       just a throwaway tool, not part of the costume firmware.
  ============================================================================
*/

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

class ScanPrintCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    bool isHeartRate = advertisedDevice.isAdvertisingService(BLEUUID((uint16_t)0x180D));

    Serial.print(advertisedDevice.getAddress().toString().c_str());
    Serial.print("  RSSI=");
    Serial.print(advertisedDevice.getRSSI());

    if (advertisedDevice.haveName()) {
      Serial.print("  name=\"");
      Serial.print(advertisedDevice.getName().c_str());
      Serial.print("\"");
    }

    if (isHeartRate) {
      Serial.print("  <-- advertises Heart Rate service (0x180D) — LIKELY YOUR WHOOP");
    }

    Serial.println();
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Scanning for BLE devices... put your Whoop in HR Broadcast mode now.");
  Serial.println("(Make sure no one else's Whoop is broadcasting nearby.)");
  Serial.println();

  BLEDevice::init("BiohackerBro-Scanner");
  BLEScan *scanner = BLEDevice::getScan();
  scanner->setAdvertisedDeviceCallbacks(new ScanPrintCallback());
  scanner->setActiveScan(true);
  scanner->start(0, nullptr, false); // scan indefinitely — just watch Serial Monitor
}

void loop() {
  delay(1000);
}
