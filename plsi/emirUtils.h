// emirUtils.h
//
// Lightweight debug helpers for emir modules.
// All functions return pointers to internal static buffers —
// simple and allocation-free, but NOT thread-safe.
// Intended for debug/log printing only.
//
// C++03 compatible.

#ifndef EMIR_UTILS_H
#define EMIR_UTILS_H

// Return current local time as "YYYY-MM-DD HH:MM:SS".
// Points to a static buffer; overwritten on each call.
const char* getNowStr();

// Return current process RSS (Resident Set Size) as a human-readable
// string, e.g. "123.4 MB" or "1.2 GB".
// On Linux reads /proc/self/status; returns "N/A" on failure.
// Points to a static buffer; overwritten on each call.
const char* getRssStr();

#endif // EMIR_UTILS_H
