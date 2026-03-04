#include "emirInfoMgr.h"

#include <cstdarg>
#include <cstdio>

// =============================================================================
// EmirInfoMgr
// =============================================================================

EmirInfoMgr::EmirInfoMgr()
    : _llx(0.0f), _lly(0.0f), _urx(0.0f), _ury(0.0f)
{}

float EmirInfoMgr::llx() const { return _llx; }
float EmirInfoMgr::lly() const { return _lly; }
float EmirInfoMgr::urx() const { return _urx; }
float EmirInfoMgr::ury() const { return _ury; }
void EmirInfoMgr::setBBox(float llx, float lly, float urx, float ury) {
    _llx = llx; _lly = lly; _urx = urx; _ury = ury;
}

const std::vector<EmirLayerInfo*>& EmirInfoMgr::layers() const { return _layers; }

void EmirInfoMgr::addLayer(EmirLayerInfo* l) {
    int idx = l->_idx;
    if (idx >= static_cast<int>(_layers.size()))
        _layers.resize(idx + 1, NULL);
    _layers[idx] = l;
}

void EmirInfoMgr::debug(const char* fmt, ...) const {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
