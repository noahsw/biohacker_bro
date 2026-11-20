#include "phone_ble.h"
#include "totem_config.h"

#include <NimBLEDevice.h>

#if PHONE_ENABLED

namespace {

NimBLEServer *server = nullptr;
NimBLECharacteristic *statusChr = nullptr, *infoChr = nullptr;
QueueHandle_t commands = nullptr;

// One phone at a time (advertising stops while it's connected).
volatile bool phoneOn = false, unlocked = false;
volatile uint16_t phoneHandle = 0;
volatile unsigned long connectedAt = 0, lockedUntil = 0;

// Written by loop() on core 1, read by NimBLE's task on core 0.
portMUX_TYPE textLock = portMUX_INITIALIZER_UNLOCKED;
char statusText[512] = "{}";
char infoText[512] = "{}";

const char *LOCKED_JSON = "{\"auth\":0}";
const char *WAIT_JSON = "{\"auth\":0,\"wait\":1}";

void serve(NimBLECharacteristic *chr, const char *text) {
  // Copied out under the lock, handed to NimBLE outside it.
  static char buf[512];
  portENTER_CRITICAL(&textLock);
  strlcpy(buf, text, sizeof(buf));
  portEXIT_CRITICAL(&textLock);
  chr->setValue((const uint8_t *)buf, strlen(buf));
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    phoneHandle = info.getConnHandle();
    unlocked = false;
    connectedAt = millis();
    phoneOn = true;
    Serial.printf("phone: connected (%s), waiting for the PIN\r\n", info.getAddress().toString().c_str());
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    phoneOn = false;
    unlocked = false;
    Serial.printf("phone: disconnected (reason=%d)\r\n", reason);
    // NimBLE starts advertising again by itself (advertiseOnDisconnect).
  }
};
ServerCallbacks serverCallbacks;

class ReadCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    if (!unlocked) {
      const char *j = millis() < lockedUntil ? WAIT_JSON : LOCKED_JSON;
      chr->setValue((const uint8_t *)j, strlen(j));
      return;
    }
    serve(chr, chr == statusChr ? statusText : infoText);
  }
};
ReadCallbacks readCallbacks;

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    NimBLEAttValue v = chr->getValue();
    phone::Command cmd;
    if (!phone::parse(v.data(), v.size(), cmd)) {
      Serial.println("phone: didn't understand a command");
      return;
    }
    if (cmd.kind == phone::Command::PIN) {
      if (millis() < lockedUntil) return;   // a wrong one just now: wait
      if (phone::pinMatches(cmd.text, PHONE_PIN)) {
        unlocked = true;
        Serial.println("phone: unlocked");
      } else {
        // A pause after each wrong one: 10,000 four-digit codes take 8 hours.
        lockedUntil = millis() + 3000;
        Serial.println("phone: wrong PIN");
      }
      return;
    }
    if (!unlocked) return;
    xQueueSend(commands, &cmd, 0);   // full (a flood of taps): drop it
  }
};
CommandCallbacks commandCallbacks;

} // namespace

void phoneSetup() {
  commands = xQueueCreate(6, sizeof(phone::Command));
  NimBLEDevice::init("BiohackerBro");   // same name as bleSetup(); its own init is then a no-op

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);

  NimBLEService *svc = server->createService(PHONE_SERVICE_UUID);
  NimBLECharacteristic *cmdChr = svc->createCharacteristic(PHONE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE);
  cmdChr->setCallbacks(&commandCallbacks);
  statusChr = svc->createCharacteristic(PHONE_STATUS_UUID, NIMBLE_PROPERTY::READ);
  statusChr->setCallbacks(&readCallbacks);
  infoChr = svc->createCharacteristic(PHONE_INFO_UUID, NIMBLE_PROPERTY::READ);
  infoChr->setCallbacks(&readCallbacks);


  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(PHONE_SERVICE_UUID);   // what the page looks for
  adv->setName(PHONE_NAME);                  // in the scan response: the UUID fills the packet
  adv->enableScanResponse(true);
  // Every ~0.5 s rather than NimBLE's ~0.1 s: the radio is also scanning and
  // keeping the straps' connections, and the phone still finds it in a second.
  adv->setMinInterval(800);    // x 0.625 ms
  adv->setMaxInterval(1200);
  adv->start();
  Serial.printf("phone: advertising as \"%s\"%s\r\n", PHONE_NAME,
                strcmp(PHONE_PIN, "1234") == 0 ? " (default PIN 1234: set PHONE_PIN in secrets.h)" : "");
}

bool phonePoll(phone::Command &out) {
  // Not unlocked in time: free the slot. From here rather than a timer
  // because loop() runs every frame anyway.
  if (phoneOn && !unlocked && millis() - connectedAt > PHONE_UNLOCK_S * 1000UL) {
    Serial.println("phone: no PIN in time, disconnecting");
    connectedAt = millis();   // ask once, not every frame, while it goes
    server->disconnect(phoneHandle);
  }
  return commands && xQueueReceive(commands, &out, 0) == pdTRUE;
}

void phoneSetStatus(const char *json) {
  portENTER_CRITICAL(&textLock);
  strlcpy(statusText, json, sizeof(statusText));
  portEXIT_CRITICAL(&textLock);
}

void phoneSetInfo(const char *json) {
  portENTER_CRITICAL(&textLock);
  strlcpy(infoText, json, sizeof(infoText));
  portEXIT_CRITICAL(&textLock);
}

bool phoneConnected() { return phoneOn && unlocked; }

#else

void phoneSetup() {}
bool phonePoll(phone::Command &) { return false; }
void phoneSetStatus(const char *) {}
void phoneSetInfo(const char *) {}
bool phoneConnected() { return false; }

#endif
