#include "LvFailure.h"
#include <Arduino.h>

namespace {
void (*showFailure)(void*) = nullptr;
void* failureDisplay = nullptr;
}

void handheld_lvgl_failure_display(void (*show)(void*), void* display) {
    showFailure = show;
    failureDisplay = display;
}

void handheld_lvgl_fail(void) {
    Serial.println("[LVGL] UI stopped after an assertion; restart required");
    if (showFailure) showFailure(failureDisplay);
    // An allocation may fail in the middle of a widget mutation. Do not enter
    // LVGL again or automatically reboot into the same failure. Yield so the
    // service owner can finish accepted writes and other tasks can run.
    for (;;) delay(1000);
}
