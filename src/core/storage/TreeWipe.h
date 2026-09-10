#pragma once

#include <Arduino.h>
#include <cstring>

namespace handheld::storage {

// One traversal policy for the SD namespace wipe and the final internal reset.
// An empty File is not proof of EOF: callers must additionally remove the
// emptied directory, or use a checked root-directory verification. Keep each
// recursive frame/path bounded; an unsupported tree remains recovery data.
template<class Store>
bool wipeTreeContents(Store& store, const char* path,
                      bool (*preserve)(const char*) = nullptr, unsigned depth = 0) {
    if (depth >= 16) return false;
    File dir = store.openDir(path);
    if (!dir || !dir.isDirectory()) return false;
    for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        const char* name = entry.name();
        if (preserve && preserve(name)) continue;
        char child[256];
        const int length = snprintf(child, sizeof(child), "%s%s%s", path,
                                   strcmp(path, "/") ? "/" : "", name);
        const bool directory = entry.isDirectory();
        entry.close();
        if (length < 0 || length >= int(sizeof(child))) return false;
        if (directory) {
            if (!wipeTreeContents(store, child, nullptr, depth + 1) ||
                !store.removeDir(child)) return false;
        } else if (!store.remove(child)) return false;
        yield();
    }
    return true;
}

} // namespace handheld::storage
