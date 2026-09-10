#pragma once
#include <Preferences.h>
#include "SelectionState.h"

namespace launcher {
// Existing schema: rslaunch/last, 0=Standalone, 1=RNode; other values fall back.
inline Choice loadLastChoice() {
    Preferences prefs;
    uint8_t value = 0;
    if (prefs.begin("rslaunch", true)) {
        value = prefs.getUChar("last", 0);
        prefs.end();
    }
    return choiceFromValue(value);
}
inline bool saveLastChoice(Choice choice) {
    Preferences prefs;
    if (!prefs.begin("rslaunch", false)) return false;
    const bool saved = prefs.putUChar("last", choiceValue(choice)) == 1;
    prefs.end();
    return saved;
}
} // namespace launcher
