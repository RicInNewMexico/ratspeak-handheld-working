#pragma once
#include "hal/SharedSPIBus.h"
#include "LvFailure.h"

namespace handheld {
template<class Gfx> void showLvglFailure(void* display) {
    // The service task may still own SPI. Refuse a stuck bus rather than
    // leaving the failure task in another unbounded acquisition.
    SharedSPILock bus(pdMS_TO_TICKS(100));
    if (!bus.locked()) return; // Serial remains the fallback diagnostic.
    auto& gfx = *static_cast<Gfx*>(display);
    gfx.wakeup();
    gfx.setBrightness(128);
    gfx.fillScreen(0);
    gfx.setFont(&lgfx::fonts::Font0);
    gfx.setTextSize(2);
    gfx.setTextColor(0xffff, 0);
    gfx.setCursor(12, 24);
    gfx.print("Screen unavailable\n\n Restart the device");
}
}
