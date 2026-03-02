#ifndef EMIR_NET_INFO_H
#define EMIR_NET_INFO_H

#include <string>
#include <vector>
#include <limits>

// =============================================================================
// Simplified emir data structures for selfheating module compilation.
// These are stubs mirroring the real emir interfaces.
// To be replaced by actual emir headers when integrating.
// =============================================================================

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
// EmirNodeInfo
// =============================================================================

class EmirNodeInfo {
public:
    EmirNodeInfo();

    float x() const;
    float y() const;
    void setX(float v);
    void setY(float v);

    const std::string& layer() const;
    void setLayer(const std::string& l);

    // 'I' = instance/MOSFET pin, 'N' = normal node, etc.
    char type() const;
    void setType(char t);

private:
    float _x, _y;
    std::string _layer;
    char _type;
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

    // electrical properties
    float resistance() const;
    void setResistance(float v);
    float current() const;
    void setCurrent(float v);
    float avgPower() const;
    void setAvgPower(float v);
    float rmsPower() const;
    void setRmsPower(float v);

    // connected nodes
    EmirNodeInfo* n1() const;
    EmirNodeInfo* n2() const;
    void setN1(EmirNodeInfo* n);
    void setN2(EmirNodeInfo* n);

    // via or wire
    bool isVia() const;
    void setIsVia(bool v);

private:
    float _llx, _lly, _urx, _ury;
    std::string _layer;
    float _resistance;
    float _current;
    float _avgPower;
    float _rmsPower;
    EmirNodeInfo* _n1;
    EmirNodeInfo* _n2;
    bool _isVia;
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

    // building helpers
    void addNode(EmirNodeInfo* n);
    void addRes(EmirResInfo* r);

    // debug output (fprintf-style, prints to stderr)
    void debug(const char* fmt, ...) const;

private:
    std::vector<EmirNodeInfo*> _nodes;
    std::vector<EmirResInfo*> _reses;
    std::vector<ResEmParam> _resEmParams;  // same offset as _reses
};

#endif // EMIR_NET_INFO_H
