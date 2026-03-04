#include "emirResInfo.h"

// =============================================================================
// EmirResInfo
// =============================================================================

EmirResInfo::EmirResInfo()
    : _n1(-1), _n2(-1)
    , _llx(0.0f), _lly(0.0f), _urx(0.0f), _ury(0.0f)
    , _layerIdx(-1)
    , _resistance(0.0f), _current(0.0f)
    , _avgPower(0.0f), _rmsPower(0.0f)
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

int EmirResInfo::n1() const { return _n1; }
int EmirResInfo::n2() const { return _n2; }
