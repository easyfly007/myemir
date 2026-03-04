#include "emirNetInfo.h"

// =============================================================================
// EmirNetInfo
// =============================================================================

EmirNetInfo::EmirNetInfo()
    : _mgr(NULL)
{}

EmirNetInfo::~EmirNetInfo()
{}

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
