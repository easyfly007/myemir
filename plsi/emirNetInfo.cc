#include "emirNetInfo.h"
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

void EmirInfoMgr::debug(const char* fmt, ...) const {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// =============================================================================
// EmirNetInfo
// =============================================================================

EmirNetInfo::EmirNetInfo()
    : _mgrData(NULL)
{}

EmirNetInfo::~EmirNetInfo()
{}

EmirInfoMgr* EmirNetInfo::_mgr() const { return _mgrData; }
void EmirNetInfo::setMgr(EmirInfoMgr* m) { _mgrData = m; }

const std::vector<EmirNodeInfo*>& EmirNetInfo::nodes() const { return _nodes; }
const std::vector<EmirResInfo*>& EmirNetInfo::reses() const { return _reses; }
std::vector<ResEmParam>& EmirNetInfo::resEmParams() { return _resEmParams; }
const std::vector<ResEmParam>& EmirNetInfo::resEmParams() const { return _resEmParams; }

void EmirNetInfo::addNode(EmirNodeInfo* n) {
    n->setIdx(static_cast<int>(_nodes.size()));
    _nodes.push_back(n);
}

void EmirNetInfo::addRes(EmirResInfo* r) {
    _reses.push_back(r);
    _resEmParams.push_back(ResEmParam());  // keep same offset
}

