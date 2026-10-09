#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Link {
void begin();
void loop();
void fillStatus(JsonObject out);
}
