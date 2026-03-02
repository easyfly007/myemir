// emirUtils.cc
//
// Lightweight debug helpers for emir modules.
// C++03 compatible.

#include "emirUtils.h"

#include <cstdio>
#include <ctime>

// ----------------------------------------------------------------------------
// getNowStr — current local time as "YYYY-MM-DD HH:MM:SS"
// ----------------------------------------------------------------------------

const char* getNowStr() {
    static char buf[32];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    if (t) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    } else {
        snprintf(buf, sizeof(buf), "(unknown)");
    }
    return buf;
}

// ----------------------------------------------------------------------------
// getRssStr — current process RSS as human-readable string
// ----------------------------------------------------------------------------

const char* getRssStr() {
    static char buf[64];
    long rss_kb = 0;

    // Linux: parse VmRSS from /proc/self/status
    FILE* fp = fopen("/proc/self/status", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "VmRSS: %ld kB", &rss_kb) == 1)
                break;
        }
        fclose(fp);
    }

    if (rss_kb <= 0) {
        snprintf(buf, sizeof(buf), "N/A");
    } else if (rss_kb < 1024) {
        snprintf(buf, sizeof(buf), "%ld KB", rss_kb);
    } else if (rss_kb < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", rss_kb / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%.2f GB", rss_kb / (1024.0 * 1024.0));
    }
    return buf;
}
