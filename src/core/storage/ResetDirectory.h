#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace handheld::storage::reset {

enum class Presence { Absent, Directory, Other, Unavailable };

// This narrow recovery boundary deliberately bypasses Arduino File::exists /
// openNextFile: their path allocations can conceal an I/O failure as absence.
// The pinned ESP32 LittleFS VFS preserves stat errno and distinguishes a failed
// lfs_dir_read from EOF in readdir_r. See the LC02 SDK/source receipt.
inline Presence probe(const char* mount, const char* path) {
    char absolute[128];
    const int size = snprintf(absolute, sizeof(absolute), "%s%s", mount, path);
    if (size < 0 || size >= int(sizeof(absolute))) return Presence::Unavailable;
    struct stat state {};
    errno = 0;
    if (::stat(absolute, &state) == 0)
        return S_ISDIR(state.st_mode) ? Presence::Directory : Presence::Other;
    return errno == ENOENT ? Presence::Absent : Presence::Unavailable;
}

inline bool containsOnly(const char* mount, bool (*allowed)(const char*)) {
    DIR* directory = ::opendir(mount);
    if (!directory) return false;
    bool complete = false;
    for (;;) {
        // Pinned readdir delegates to the checked readdir_r implementation and
        // preserves its errno on failure. Clear it before each call so a prior
        // successful operation cannot masquerade as an enumeration error.
        errno = 0;
        const struct dirent* entry = ::readdir(directory);
        if (!entry) { complete = errno == 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!allowed || !allowed(entry->d_name)) break;
    }
    return ::closedir(directory) == 0 && complete;
}

} // namespace handheld::storage::reset
