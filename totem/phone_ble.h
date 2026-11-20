#pragma once

#include <Arduino.h>
#include "phone_protocol.h"

// ============================================================================
// PHONE CONTROL OVER BLUETOOTH (totem only)
// ============================================================================
//
// crew_ble.cpp has the totem connect OUT to heart rate straps. This is the
// other direction: the totem also advertises as "Totem" and lets one phone
// connect IN, the way a strap lets the totem connect to it. The phone page
// (docs/remote/index.html, opened in the Bluefy browser on iPhone) writes
// commands (phone_protocol.h) and reads back a status line.
//
// Three characteristics on one service:
//   command  write   one command per write
//   status   read    JSON: who's connected, heart rates, what's playing
//   info     read    JSON: the phrase list and number of brightness levels
// Until the right PIN has been written on a connection, status and info read
// {"auth":0} and every other command is ignored. A phone that hasn't unlocked
// within PHONE_UNLOCK_S is disconnected, so a stranger can't sit on the slot.
//
// Takes one of NimBLE's connections (3 by default: your Whoop, one friend,
// the phone). Only one phone at a time: advertising stops while one is on.

// After bleSetup() (which starts NimBLE).
void phoneSetup();

// From loop(): the next command the unlocked phone sent, if any. Also
// disconnects a phone that hasn't unlocked in time.
bool phonePoll(phone::Command &out);

// From loop(): what status reads return from now on.
void phoneSetStatus(const char *json);
// Once, from setup(): what info reads return.
void phoneSetInfo(const char *json);

bool phoneConnected();   // a phone is connected and unlocked
