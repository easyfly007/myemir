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

float EmirNetInfo::getResPwrAvg(int residx) const { return _resPwrAvg[residx]; }
float EmirNetInfo::getResPwrRms(int residx) const { return _resPwrRms[residx]; }

void EmirNetInfo::addRes(EmirResInfo* r, float pwrAvg, float pwrRms) {
    _reses.push_back(r);
    _resEmParams.push_back(ResEmParam());
    _resPwrAvg.push_back(pwrAvg);
    _resPwrRms.push_back(pwrRms);
}
