#ifndef EMIR_INFO_MGR_H
#define EMIR_INFO_MGR_H

#include <vector>

#include "emirLayerInfo.h"

// =============================================================================
// EmirInfoMgr — global manager: layout bbox, layer table
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

    // layer table (indexed by EmirLayerInfo::_idx)
    const std::vector<EmirLayerInfo*>& layers() const;
    void addLayer(EmirLayerInfo* l);

    // debug output (fprintf-style, prints to stderr)
    void debug(const char* fmt, ...) const;

private:
    float _llx, _lly, _urx, _ury;
    std::vector<EmirLayerInfo*> _layers;
};

#endif // EMIR_INFO_MGR_H
