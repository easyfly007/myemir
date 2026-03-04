#ifndef EMIR_NET_INFO_H
#define EMIR_NET_INFO_H

#include <vector>

#include "emirInfoMgr.h"
#include "emirNodeInfo.h"
#include "emirResInfo.h"

// =============================================================================
// Simplified emir data structures for selfheating module compilation.
// These are stubs mirroring the real emir interfaces.
// To be replaced by actual emir headers when integrating.
// =============================================================================

// =============================================================================
// EmirNetInfo
// =============================================================================

class EmirNetInfo {
public:
    EmirNetInfo();
    ~EmirNetInfo();

    const std::vector<EmirNodeInfo*>& nodes() const;
    const std::vector<EmirResInfo*>& reses() const;
    std::vector<ResEmParam>& resEmParams();
    const std::vector<ResEmParam>& resEmParams() const;

    // res power (indexed by res offset, same as reses())
    float getResPwrAvg(int residx) const;
    float getResPwrRms(int residx) const;

    // building helpers
    void addNode(EmirNodeInfo* n);
    void addRes(EmirResInfo* r, float pwrAvg = 0.0f, float pwrRms = 0.0f);

    EmirInfoMgr* _mgr;  // parent manager

private:
    std::vector<EmirNodeInfo*> _nodes;
    std::vector<EmirResInfo*> _reses;
    std::vector<ResEmParam> _resEmParams;  // same offset as _reses
    std::vector<float> _resPwrAvg;         // same offset as _reses
    std::vector<float> _resPwrRms;         // same offset as _reses
};

#endif // EMIR_NET_INFO_H
