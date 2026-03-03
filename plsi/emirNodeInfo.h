#ifndef EMIR_NODE_INFO_H
#define EMIR_NODE_INFO_H

#include <string>

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

    // index in EmirNetInfo::_nodes vector (set by addNode)
    int idx() const;
    void setIdx(int i);

private:
    float _x, _y;
    std::string _layer;
    char _type;
    int _idx;
};

#endif // EMIR_NODE_INFO_H
