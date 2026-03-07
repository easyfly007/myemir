#include "emirUnionFind.h"
#include "emirNetInfo.h"
#include "emirNodeInfo.h"

#include <cassert>
#include <set>

// -----------------------------------------------------------------------------
// EmirUnionFind(int n)
//   Initialize n elements, each in its own set.
// -----------------------------------------------------------------------------
EmirUnionFind::EmirUnionFind(int n)
    : _parents(n)
    , _rank(n, 0)
{
    for (int i = 0; i < n; ++i) {
        _parents[i] = i;
    }
}

// -----------------------------------------------------------------------------
// find(int x)
//   Path-compression find: returns the root of x's set.
// -----------------------------------------------------------------------------
int EmirUnionFind::find(int x)
{
    assert(x >= 0 && x < static_cast<int>(_parents.size()));
    while (_parents[x] != x) {
        _parents[x] = _parents[_parents[x]]; // path halving
        x = _parents[x];
    }
    return x;
}

// -----------------------------------------------------------------------------
// unionSet(int x, int y)
//   Merge the sets containing x and y (union by rank).
// -----------------------------------------------------------------------------
void EmirUnionFind::unionSet(int x, int y)
{
    int rx = find(x);
    int ry = find(y);
    if (rx == ry) return;

    if (_rank[rx] < _rank[ry]) {
        _parents[rx] = ry;
    } else if (_rank[rx] > _rank[ry]) {
        _parents[ry] = rx;
    } else {
        _parents[ry] = rx;
        ++_rank[rx];
    }
}

// -----------------------------------------------------------------------------
// isConnected(int x, int y)
//   Return true if x and y belong to the same set.
// -----------------------------------------------------------------------------
bool EmirUnionFind::isConnected(int x, int y)
{
    return find(x) == find(y);
}

// -----------------------------------------------------------------------------
// getGroups()
//   Return a vector of unique root node indices (one per group).
//   Note: result is valid only after all unionSet() calls are done.
// -----------------------------------------------------------------------------
std::vector<int>& EmirUnionFind::getGroups()
{
    // Collect unique roots
    std::set<int> roots;
    int n = static_cast<int>(_parents.size());
    for (int i = 0; i < n; ++i) {
        roots.insert(find(i));
    }

    _groups.clear();
    _groups.assign(roots.begin(), roots.end());
    return _groups;
}

// -----------------------------------------------------------------------------
// finalize()
//   Build _nodeVecByGrp and _root2idx from the current union-find state.
//   After this call, getGroupCnt() and getGroupNodeVec() become usable.
// -----------------------------------------------------------------------------
void EmirUnionFind::finalize()
{
    _nodeVecByGrp.clear();
    _root2idx.clear();

    int n = static_cast<int>(_parents.size());
    for (int i = 0; i < n; ++i) {
        int root = find(i);

        std::map<int, int>::iterator it = _root2idx.find(root);
        if (it == _root2idx.end()) {
            int gidx = static_cast<int>(_nodeVecByGrp.size());
            _root2idx[root] = gidx;
            _nodeVecByGrp.push_back(std::vector<int>());
            _nodeVecByGrp[gidx].push_back(i);
        } else {
            _nodeVecByGrp[it->second].push_back(i);
        }
    }
}

// -----------------------------------------------------------------------------
// dump(FILE* f, EmirNetInfo* net)
//   Print group information to file for debugging.
// -----------------------------------------------------------------------------
void EmirUnionFind::dump(FILE* f, EmirNetInfo* net)
{
    if (!f) return;

    fprintf(f, "UnionFind: %d nodes, %d groups\n",
            static_cast<int>(_parents.size()),
            static_cast<int>(_nodeVecByGrp.size()));

    const std::vector<EmirNodeInfo*>& nodes = net ? net->nodes()
                                                  : std::vector<EmirNodeInfo*>();

    for (int g = 0; g < static_cast<int>(_nodeVecByGrp.size()); ++g) {
        const std::vector<int>& nv = _nodeVecByGrp[g];
        fprintf(f, "  group[%d]: %d nodes\n", g, static_cast<int>(nv.size()));
        for (int j = 0; j < static_cast<int>(nv.size()); ++j) {
            int ni = nv[j];
            if (net && ni < static_cast<int>(nodes.size())) {
                const EmirNodeInfo* node = nodes[ni];
                fprintf(f, "    node[%d] (%s, %.2f, %.2f)\n",
                        ni, node->layer().c_str(), node->x(), node->y());
            } else {
                fprintf(f, "    node[%d]\n", ni);
            }
        }
    }
}
