#pragma once
#include <stddef.h>

namespace handheld {
enum class ReleaseCheckResult { Failed, Current, Available };

// Informational check only. HTTPS trust/hostname verification is mandatory;
// the fixed GitHub API endpoint is never followed onto an HTTP redirect.
ReleaseCheckResult checkFirmwareRelease(const char* repository, const char* installed,
                                       char* version, size_t capacity);
}
