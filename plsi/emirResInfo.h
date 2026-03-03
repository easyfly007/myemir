#ifndef EMIR_RES_INFO_H
#define EMIR_RES_INFO_H

#include <string>
#include <limits>

// =============================================================================
// ResEmParam — EM parameters per res
// =============================================================================

struct ResEmParam {
    float _td;
    float _duty;    // duty ratio
    float _deltaT;  // self-heating temperature rise
    ResEmParam():
        _td(std::numeric_limits<float>::quiet_NaN()),
        _duty(std::numeric_limits<float>::quiet_NaN()),
        _deltaT(0.0f){}
};

// =============================================================================
// EmirResInfo
// =============================================================================

class EmirResInfo {
public:
    EmirResInfo();

    // bbox
    float llx() const;
    float lly() const;
    float urx() const;
    float ury() const;
    void setBBox(float llx, float lly, float urx, float ury);

    // layer
    const std::string& layer() const;
    void setLayer(const std::string& l);
    int layerIdx() const;
    void setLayerIdx(int idx);

    // electrical properties
    float resistance() const;
    void setResistance(float v);
    float current() const;
    void setCurrent(float v);
    float avgPower() const;
    void setAvgPower(float v);
    float rmsPower() const;
    void setRmsPower(float v);

    // via or wire
    bool isVia() const;
    void setIsVia(bool v);

    // connected node indices (offsets into EmirNetInfo::_nodes)
    int n1() const;
    int n2() const;

    int _n1;  // index into EmirNetInfo::_nodes
    int _n2;  // index into EmirNetInfo::_nodes

private:
    float _llx, _lly, _urx, _ury;
    std::string _layer;
    int _layerIdx;
    float _resistance;
    float _current;
    float _avgPower;
    float _rmsPower;
    bool _isVia;
};

#endif // EMIR_RES_INFO_H
