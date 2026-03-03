#ifndef EMIR_NET_INFO_H
#define EMIR_NET_INFO_H

#include <string>
#include <vector>

#include "emirNodeInfo.h"
#include "emirResInfo.h"

// =============================================================================
// Simplified emir data structures for selfheating module compilation.
// These are stubs mirroring the real emir interfaces.
// To be replaced by actual emir headers when integrating.
// =============================================================================

// =============================================================================
// EmirInfoMgr — manages all nets, holds global layout bbox
// =============================================================================

class EmirInfoMgr {
public:
    EmirInfoMgr();

    // global layout bounding box
    float llx() const;
    float lly() const;
    float urx() const;
    float ury() const;
    void setBBox(float llx, float lly, float urx, float ury);

    // debug output (fprintf-style, prints to stderr)
    void debug(const char* fmt, ...) const;

private:
    float _llx, _lly, _urx, _ury;
};

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

    // parent manager
    EmirInfoMgr* _mgr() const;
    void setMgr(EmirInfoMgr* m);

    // building helpers
    void addNode(EmirNodeInfo* n);
    void addRes(EmirResInfo* r);

private:
    EmirInfoMgr* _mgrData;
    std::vector<EmirNodeInfo*> _nodes;
    std::vector<EmirResInfo*> _reses;
    std::vector<ResEmParam> _resEmParams;  // same offset as _reses
};

#endif // EMIR_NET_INFO_H
