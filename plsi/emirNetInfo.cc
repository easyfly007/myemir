#include "emirNetInfo.h"
#include <cstdarg>
#include <cstdio>

// =============================================================================
// EmirNodeInfo
// =============================================================================

EmirNodeInfo::EmirNodeInfo()
    : _x(0.0f), _y(0.0f), _type('N'), _idx(-1)
{}

float EmirNodeInfo::x() const { return _x; }
float EmirNodeInfo::y() const { return _y; }
void EmirNodeInfo::setX(float v) { _x = v; }
void EmirNodeInfo::setY(float v) { _y = v; }

const std::string& EmirNodeInfo::layer() const { return _layer; }
void EmirNodeInfo::setLayer(const std::string& l) { _layer = l; }

char EmirNodeInfo::type() const { return _type; }
void EmirNodeInfo::setType(char t) { _type = t; }
int EmirNodeInfo::idx() const { return _idx; }
void EmirNodeInfo::setIdx(int i) { _idx = i; }

// =============================================================================
// EmirResInfo
// =============================================================================

EmirResInfo::EmirResInfo()
    : _llx(0.0f), _lly(0.0f), _urx(0.0f), _ury(0.0f)
    , _layerIdx(-1)
    , _resistance(0.0f), _current(0.0f)
    , _avgPower(0.0f), _rmsPower(0.0f)
    , _n1(NULL), _n2(NULL)
    , _isVia(false)
{}

float EmirResInfo::llx() const { return _llx; }
float EmirResInfo::lly() const { return _lly; }
float EmirResInfo::urx() const { return _urx; }
float EmirResInfo::ury() const { return _ury; }
void EmirResInfo::setBBox(float llx, float lly, float urx, float ury) {
    _llx = llx; _lly = lly; _urx = urx; _ury = ury;
}

const std::string& EmirResInfo::layer() const { return _layer; }
void EmirResInfo::setLayer(const std::string& l) { _layer = l; }
int EmirResInfo::layerIdx() const { return _layerIdx; }
void EmirResInfo::setLayerIdx(int idx) { _layerIdx = idx; }

float EmirResInfo::resistance() const { return _resistance; }
void EmirResInfo::setResistance(float v) { _resistance = v; }
float EmirResInfo::current() const { return _current; }
void EmirResInfo::setCurrent(float v) { _current = v; }
float EmirResInfo::avgPower() const { return _avgPower; }
void EmirResInfo::setAvgPower(float v) { _avgPower = v; }
float EmirResInfo::rmsPower() const { return _rmsPower; }
void EmirResInfo::setRmsPower(float v) { _rmsPower = v; }

EmirNodeInfo* EmirResInfo::n1() const { return _n1; }
EmirNodeInfo* EmirResInfo::n2() const { return _n2; }
void EmirResInfo::setN1(EmirNodeInfo* n) { _n1 = n; }
void EmirResInfo::setN2(EmirNodeInfo* n) { _n2 = n; }

bool EmirResInfo::isVia() const { return _isVia; }
void EmirResInfo::setIsVia(bool v) { _isVia = v; }

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

