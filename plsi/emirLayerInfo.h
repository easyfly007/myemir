#ifndef EMIR_LAYER_INFO_H
#define EMIR_LAYER_INFO_H

#include <string>

// =============================================================================
// EmirLayerInfo
// =============================================================================

class EmirLayerInfo {
public:
    EmirLayerInfo(int idx, const char* name) {
        _idx          = idx;
        _gdsidx       = -1;
        _name         = name;
        _type         = -1;  // 0 for metal, 1 for via, -1 for unknown
        _res_cnt      = 0;
        _in_techfile  = 0;   // if this layer can be found in the techfile
        _is_wire_layer   = 0;
        _valid_inode_cnt = 0; // inode(connected to transistor) count
    }

    int  getType() const          { return _type; }
    void setType(int type)        { _type = type; }
    bool isViaLayer()   const     { return _type == 1; }
    bool isMetalLayer() const     { return _type == 0; }
    bool isUnknown()    const     { return _type == -1; }
    const std::string& name() const { return _name; }

    ~EmirLayerInfo() {}

    int         _idx;
    int         _gdsidx;        // from gds ap
    std::string _name;
    int         _type;
    int         _res_cnt;
    int         _in_techfile;
    int         _is_wire_layer;  // bwires, mwires, rwires, etc
    int         _valid_inode_cnt; // valid inode count, inode connected to transistor
};

#endif // EMIR_LAYER_INFO_H
