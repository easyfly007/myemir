#ifndef EMIR_UNION_FIND_H
#define EMIR_UNION_FIND_H

#include <vector>
#include <map>
#include <cstdio>

class EmirNetInfo;

class EmirUnionFind {
public:
    EmirUnionFind(int n);
    int find(int x);
    void unionSet(int x, int y);
    bool isConnected(int x, int y);
    std::vector<int>& getGroups();
    void finalize();
    void dump(FILE* f, EmirNetInfo* net);

    int getGroupCnt() { return _nodeVecByGrp.size(); }
    std::vector<int>& getGroupNodeVec(int gidx) { return _nodeVecByGrp[gidx]; }

private:
    std::vector<int> _parents;
    std::vector<int> _rank;
    std::vector<int> _groups;     // unique root node indices (built by getGroups)
    std::vector<std::vector<int>> _nodeVecByGrp;
    std::map<int, int> _root2idx; // group root node idx ==> group idx (the offset in _nodeVecByGrp)
};

#endif // EMIR_UNION_FIND_H
