#include "system/serial_guard.h"

#include <Arduino.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include "config/config.h"

namespace {

int discardLog(const char*, va_list) { return 0; }
void discardChar(char) {}

}  // namespace

void silenceSystemOutput() {
#if SF_SILENCE_SYSTEM_OUTPUT
  esp_log_level_set("*", ESP_LOG_NONE);
  esp_log_set_vprintf(&discardLog);
  // ROM printf (esp_rom_printf / ets_printf), used by some precompiled
  // driver libraries, goes to these two channels.
  esp_rom_install_channel_putc(1, &discardChar);
  esp_rom_install_channel_putc(2, &discardChar);
  Serial.setDebugOutput(false);
#endif
}
