#pragma once

#include "MaintenanceBarrier.h"
#include "ServiceMessages.h"

class ProtocolBackend;
class UserConfig;
class IdentityManager;
class FlashStore;
class SDStore;
class AnnounceManager;

namespace handheld {
struct MaintenanceResult {
    bool ok = false;
    const char* detail = "Maintenance not ready";
};

// Called only for the controller's one-shot Perform action. These synchronous
// final flushes/actions begin after all worker and callback owners settle.
// Hardware restart/power-off remains the caller's explicit result action.
MaintenanceResult performMaintenance(MaintenanceBarrier& barrier, const Request& request,
    ProtocolBackend& backend, UserConfig& config, IdentityManager& identities,
    FlashStore& flash, SDStore& sd, AnnounceManager* nodes);
}
