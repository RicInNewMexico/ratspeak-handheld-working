#pragma once

#include "config/UserConfig.h"

enum class RadioApply : uint8_t { Applied, Pending, RebootRequired, Unavailable };

template<class Radio>
bool radioSettingsMatch(Radio& radio, const UserSettings& settings) {
    return radio.getFrequency() == settings.loraFrequency &&
           radio.getSpreadingFactor() == settings.loraSF &&
           radio.getSignalBandwidth() == settings.loraBW &&
           radio.getCodingRate4() == settings.loraCR &&
           radio.getTxPower() == settings.loraTxPower &&
           radio.getPreambleLength() == settings.loraPreamble;
}

template<class Radio>
void writeRadioSettings(Radio& radio, const UserSettings& settings) {
    radio.setFrequency(settings.loraFrequency);
    radio.setSpreadingFactor(settings.loraSF);
    radio.setSignalBandwidth(settings.loraBW);
    radio.setCodingRate4(settings.loraCR);
    radio.setTxPower(settings.loraTxPower);
    radio.setPreambleLength(settings.loraPreamble);
    radio.receive();
}

// Boot-only: no interface may own traffic yet. Live saves use the owner gate below.
template<class Radio>
void applyRadioSettings(Radio& radio, const UserSettings& settings) {
    if (!settings.loraEnabled) { radio.sleep(); return; }
    if (!radioSettingsMatch(radio, settings)) writeRadioSettings(radio, settings);
}

// The durable settings object remains the sole requested tuple. Refused new
// traffic stays with its existing owner while already accepted driver work drains.
// Enabling/disabling the transport is a reboot setting; never sleep a live owner.
template<class Radio, class Owner>
RadioApply applyLiveRadioSettings(Radio& radio, Owner& owner, const UserSettings& settings,
                                 bool accepting = true) {
    if (!accepting) { owner.pauseForReconfigure(false); return RadioApply::Unavailable; }
    if (!radio.isRadioOnline()) { owner.pauseForReconfigure(false); return RadioApply::Unavailable; }
    if (!owner.isOnline()) {
        owner.pauseForReconfigure(false);
        return settings.loraEnabled ? RadioApply::RebootRequired : RadioApply::Unavailable;
    }
    if (radioSettingsMatch(radio, settings)) {
        owner.pauseForReconfigure(false);
        return settings.loraEnabled ? RadioApply::Applied : RadioApply::RebootRequired;
    }
    owner.pauseForReconfigure(true);
    if (!owner.canReconfigure()) return RadioApply::Pending;
    // Apply the live tuple independently of the saved reboot-only enable flag.
    writeRadioSettings(radio, settings);
    owner.pauseForReconfigure(false);
    if (!radio.isRadioOnline()) return RadioApply::Unavailable;
    return settings.loraEnabled ? RadioApply::Applied : RadioApply::RebootRequired;
}
