#include "mic.h"
#include "config.h"

#include <Arduino.h>

void micSetup() {
  if (!MIC_CONFIGURED) {
    Serial.println("Mic pins not set yet — dB readout will be a placeholder.");
    return;
  }
  // TODO once pins are confirmed: configure I2S peripheral here to read
  // from the ES7210 codec's audio output, e.g.:
  //
  //   i2s_config_t i2s_config = { ... };
  //   i2s_pin_config_t pin_config = {
  //     .bck_io_num = MIC_BCLK_PIN,
  //     .ws_io_num = MIC_WS_PIN,
  //     .data_out_num = I2S_PIN_NO_CHANGE,
  //     .data_in_num = MIC_DATA_PIN
  //   };
  //   i2s_driver_install(...);
  //   i2s_set_pin(...);
}

int micLoop() {
  if (!MIC_CONFIGURED) {
    // Placeholder: gentle fake wobble so the display isn't blank/static
    // while you wait on the real mic pins. Replace once I2S is wired up.
    return 30 + (millis() / 100) % 20;
  }

  // TODO once pins are confirmed: read a buffer of I2S samples, compute
  // RMS, and scale it to a 0-100 relative loudness value, e.g.:
  //
  //   int32_t samples[256];
  //   size_t bytesRead;
  //   i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, portMAX_DELAY);
  //   // compute RMS over `samples`, map to 0-100
  //
  return 0;
}
