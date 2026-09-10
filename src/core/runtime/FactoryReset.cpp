#include "FactoryReset.h"
#include "storage/FlashStore.h"
#include "storage/SDStore.h"
#include <nvs_flash.h>
#include <nvs.h>

namespace handheld {

const char* ResetResult::detail() const {
    switch (step) {
    case ResetStep::Complete: return "Reset complete";
    case ResetStep::Intent: return "Reset intent could not be saved";
    case ResetStep::Scope: return "Reset scope needs confirmation";
    case ResetStep::SDUnavailable: return "Insert SD to finish reset";
    case ResetStep::SD: return "SD erase failed";
    case ResetStep::NvsErase: return "NVS erase failed";
    case ResetStep::NvsInit: return "NVS restart failed";
    case ResetStep::NvsVerify: return "NVS erase could not be verified";
    case ResetStep::Internal: return "Internal data erase failed";
    case ResetStep::Finish: return "Reset completion failed";
    }
    return "Reset failed";
}

ResetResult performFactoryReset(FlashStore& flash, SDStore& sd, bool replaceInvalidScope) {
    bool includesSD = sd.isReady();
    if (flash.resetState() == FlashStore::ResetState::Pending && !flash.resetScope(includesSD) &&
        !replaceInvalidScope) return {ResetStep::Scope};
    if (!flash.prepareReset(includesSD, replaceInvalidScope)) return {ResetStep::Intent};
    if (includesSD && !sd.isReady()) return {ResetStep::SDUnavailable};
    if (includesSD && !sd.wipeRsDeck()) return {ResetStep::SD};
    if (nvs_flash_erase() != ESP_OK) return {ResetStep::NvsErase};
    if (nvs_flash_init() != ESP_OK) return {ResetStep::NvsInit};
    nvs_stats_t stats{};
    if (nvs_get_stats(nullptr, &stats) != ESP_OK || stats.used_entries || stats.namespace_count)
        return {ResetStep::NvsVerify};
    if (!flash.wipeForReset()) return {ResetStep::Internal};
    if (!flash.completeReset()) return {ResetStep::Finish};
    return {ResetStep::Complete};
}

} // namespace handheld
