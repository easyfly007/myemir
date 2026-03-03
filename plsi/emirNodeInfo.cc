#include "emirNodeInfo.h"

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
