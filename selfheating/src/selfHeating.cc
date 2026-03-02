// selfHeating.cc
//
// Implementation of the self-heating module for emir EM analysis.
// This module computes temperature rise (deltaT) on wire resistors
// due to self-heating effects from MOSFET devices underneath.
//
// Two main classes:
//   SelfHeatingDevMgr - manages MOSFET data with Uniform Grid spatial index
//   SelfHeatingMgr    - per-net processor for wire res deltaT computation

#include "selfheating/selfHeating.h"

#include <cstdio>
#include <ctime>

#include "emirMtmqMgr.h"
#include "emirMtmqTask.h"
#include "emirMtmqArg.h"
#include "emirUtils.h"

// =============================================================================
// SelfHeatingDevMgr
// =============================================================================

SelfHeatingDevMgr::SelfHeatingDevMgr(int debug)
    : _debug(debug), _originX(0.0f), _originY(0.0f), _cellSize(1.0f), _nx(0), _ny(0)
{}

SelfHeatingDevMgr::~SelfHeatingDevMgr()
{}

// Look up or create a numeric layer_id for the given layer name.
// This avoids storing a std::string per device (millions of devices).
short SelfHeatingDevMgr::_getOrCreateLayerId(const std::string& name) {
    std::map<std::string, short>::iterator it = _layerNameToId.find(name);
    if (it != _layerNameToId.end()) {
        return it->second;
    }
    short id = (short)_layerNames.size();
    _layerNames.push_back(name);
    _layerNameToId[name] = id;
    return id;
}

// Convert 2D grid coordinates to a flat index.
int SelfHeatingDevMgr::_cellIndex(int gx, int gy) const {
    return gy * _nx + gx;
}

// Phase 1: Copy external MOSFET data into compact storage and
// build the Uniform Grid spatial index for fast overlap queries.
//
// Steps:
//   1. Copy each SelfHeatingMosfet to SelfHeatingDevStr (32 bytes each)
//   2. Use caller-provided bbox (from EmirInfoMgr) as grid origin/extent
//   3. Dynamically determine grid resolution targeting ~8 dev/cell avg
//   4. Insert each device into all grid cells its bbox covers
void SelfHeatingDevMgr::init(const std::vector<SelfHeatingMosfet>& mosfets,
                             float bbox_llx, float bbox_lly,
                             float bbox_urx, float bbox_ury) {
    if (mosfets.empty()) return;

    clock_t t0 = clock();

    // Step 1: Copy data to compact _devices vector
    _devices.resize(mosfets.size());
    for (size_t i = 0; i < mosfets.size(); ++i) {
        SelfHeatingDevStr& d = _devices[i];
        d.llx = mosfets[i].llx;
        d.lly = mosfets[i].lly;
        d.urx = mosfets[i].urx;
        d.ury = mosfets[i].ury;
        d.power = mosfets[i].power;
        d.deltaT = 0.0f;
        d.finger_num = mosfets[i].finger_num;
        d.fin_num = mosfets[i].fin_num;
        d.layer_id = _getOrCreateLayerId(mosfets[i].layer_name);
        d._pad = 0;
    }

    // Step 2: Use caller-provided bbox as grid origin/extent
    _originX = bbox_llx;
    _originY = bbox_lly;

    // Step 3: Dynamic grid resolution targeting ~8 devices per cell
    float width = bbox_urx - bbox_llx;
    float height = bbox_ury - bbox_lly;
    float area = width * height;

    int num_devices = (int)_devices.size();
    int target_avg = 8;
    int target_cells = num_devices / target_avg;
    if (target_cells < 100) target_cells = 100;           // floor ~10x10
    if (target_cells > 25000000) target_cells = 25000000;  // cap ~5000x5000

    _cellSize = (float)sqrt((double)area / target_cells);
    if (_cellSize <= 0.0f) _cellSize = 1.0f;

    _nx = (int)((width / _cellSize) + 1);
    _ny = (int)((height / _cellSize) + 1);

    // Step 4: Build CSR (Compressed Sparse Row) grid — two-pass algorithm
    //
    // Replaces vector<vector<int>> which caused ~6M independent heap
    // allocations and poor cache locality. CSR packs all device indices
    // into one contiguous array (_gridData) with an offset table
    // (_gridOffsets), reducing malloc count from ~6M to 2.
    //
    // Pass 1 (count):  count how many device indices each cell receives
    // Pass 2 (fill):   write device indices into the correct positions

    int numCells = _nx * _ny;

    // _gridOffsets doubles as a count array during Pass 1, then becomes
    // the prefix-sum / offset array after conversion.
    _gridOffsets.assign(numCells + 1, 0);

    // --- Pass 1: count entries per cell ---
    for (int i = 0; i < (int)_devices.size(); ++i) {
        int gx0 = (int)((_devices[i].llx - _originX) / _cellSize);
        int gy0 = (int)((_devices[i].lly - _originY) / _cellSize);
        int gx1 = (int)((_devices[i].urx - _originX) / _cellSize);
        int gy1 = (int)((_devices[i].ury - _originY) / _cellSize);

        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
        if (gx1 >= _nx) gx1 = _nx - 1;
        if (gy1 >= _ny) gy1 = _ny - 1;

        for (int gy = gy0; gy <= gy1; ++gy) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                ++_gridOffsets[_cellIndex(gx, gy)];
            }
        }
    }

    // Convert counts to prefix sums (exclusive scan):
    //   _gridOffsets[i] = sum of counts for cells 0..i-1
    // After this, _gridOffsets[numCells] = total number of entries.
    {
        int running = 0;
        for (int c = 0; c < numCells; ++c) {
            int count = _gridOffsets[c];
            _gridOffsets[c] = running;
            running += count;
        }
        _gridOffsets[numCells] = running;
    }

    // Allocate the contiguous data array
    _gridData.resize(_gridOffsets[numCells]);

    // --- Pass 2: fill device indices using write cursors ---
    // We use _gridOffsets as write cursors: _gridOffsets[c] points to the
    // next write position for cell c. After filling, each _gridOffsets[c]
    // equals the original _gridOffsets[c+1], so we shift right to restore.
    for (int i = 0; i < (int)_devices.size(); ++i) {
        int gx0 = (int)((_devices[i].llx - _originX) / _cellSize);
        int gy0 = (int)((_devices[i].lly - _originY) / _cellSize);
        int gx1 = (int)((_devices[i].urx - _originX) / _cellSize);
        int gy1 = (int)((_devices[i].ury - _originY) / _cellSize);

        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
        if (gx1 >= _nx) gx1 = _nx - 1;
        if (gy1 >= _ny) gy1 = _ny - 1;

        for (int gy = gy0; gy <= gy1; ++gy) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                int ci = _cellIndex(gx, gy);
                _gridData[_gridOffsets[ci]] = i;
                ++_gridOffsets[ci];
            }
        }
    }

    // Shift _gridOffsets right by one to restore original prefix sums.
    // After Pass 2, _gridOffsets[c] == original _gridOffsets[c+1] for all c,
    // so we shift right and set [0] = 0.
    for (int c = numCells; c > 0; --c) {
        _gridOffsets[c] = _gridOffsets[c - 1];
    }
    _gridOffsets[0] = 0;

    // Debug output: grid statistics
    if (_debug >= 1) {
        int totalEntries = _gridOffsets[numCells];
        double avgDevPerCell = (numCells > 0)
            ? (double)totalEntries / numCells : 0.0;
        // Memory: _devices (32B each) + _gridData (4B each) + _gridOffsets (4B each)
        double memDevices = (double)_devices.size() * 32.0;
        double memGridData = (double)_gridData.size() * 4.0;
        double memGridOffsets = (double)_gridOffsets.size() * 4.0;
        double memTotal = memDevices + memGridData + memGridOffsets;

        fprintf(stderr, "[SH DevMgr] devices=%d  bbox=(%.4f,%.4f)-(%.4f,%.4f)\n",
                num_devices, bbox_llx, bbox_lly, bbox_urx, bbox_ury);
        fprintf(stderr, "[SH DevMgr] grid: nx=%d ny=%d cellSize=%.4f numCells=%d\n",
                _nx, _ny, _cellSize, numCells);
        fprintf(stderr, "[SH DevMgr] avg dev/cell=%.2f  gridData entries=%d\n",
                avgDevPerCell, totalEntries);
        double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
        fprintf(stderr, "[SH DevMgr] memory: devices=%.1fMB gridData=%.1fMB gridOffsets=%.1fMB total=%.1fMB\n",
                memDevices / (1024.0 * 1024.0),
                memGridData / (1024.0 * 1024.0),
                memGridOffsets / (1024.0 * 1024.0),
                memTotal / (1024.0 * 1024.0));
        fprintf(stderr, "[SH DevMgr] init time=%.3fs  %s %s\n",
                sec, getNowStr(), getRssStr());
    }
}

// Phase 2: Compute deltaT for each device.
// Formula: deltaT = power * Rth / finger_effect / fin_effect
// If the device layer is not found in params, deltaT is set to 0.
//
// Optimization: pre-build layer_id -> DeviceLayerParams* lookup table
// so each device does O(1) array access instead of O(log M) string map find.
void SelfHeatingDevMgr::build(
    const std::map<std::string, DeviceLayerParams>& device_layers)
{
    // Build layer_id -> DeviceLayerParams* table for O(1) lookup
    std::vector<const DeviceLayerParams*> dlpTable(_layerNames.size(), NULL);
    for (size_t lid = 0; lid < _layerNames.size(); ++lid) {
        std::map<std::string, DeviceLayerParams>::const_iterator it =
            device_layers.find(_layerNames[lid]);
        if (it != device_layers.end()) {
            dlpTable[lid] = &it->second;
        }
    }

    for (int i = 0; i < (int)_devices.size(); ++i) {
        SelfHeatingDevStr& dev = _devices[i];

        const DeviceLayerParams* lp = dlpTable[dev.layer_id];
        if (!lp) {
            dev.deltaT = 0.0f;
            continue;
        }

        // Apply effect functions; default to 1.0 if not specified
        double finger_eff = lp->finger_effect
                            ? lp->finger_effect(dev.finger_num) : 1.0;
        double fin_eff = lp->fin_effect
                         ? lp->fin_effect(dev.fin_num) : 1.0;

        dev.deltaT = (float)(dev.power * lp->Rth / finger_eff / fin_eff);
    }
}

// Find all devices whose bbox overlaps the query rectangle.
// Uses the Uniform Grid for fast spatial filtering, then does
// exact bbox intersection check on candidates.
//
// Results are returned via output parameter to allow caller reuse
// (avoids repeated heap allocation across ~300M queries).
//
// Why 'visited' bitmap is needed for deduplication:
// A device whose bbox spans multiple grid cells is inserted into every cell
// it covers during init(). When a query rectangle also covers multiple cells,
// that same device index appears in each overlapping cell, producing duplicates
// in results. The visited bitmap provides O(1) per-element dedup, replacing
// the previous std::sort + std::unique which had significant fixed overhead
// accumulated over ~250M calls.
void SelfHeatingDevMgr::queryOverlap(
    float llx, float lly, float urx, float ury,
    std::vector<int>& results,
    std::vector<bool>& visited) const
{
    results.clear();

    if (_nx == 0 || _ny == 0) return;

    // Map query bbox to grid cell range
    int gx0 = (int)((llx - _originX) / _cellSize);
    int gy0 = (int)((lly - _originY) / _cellSize);
    int gx1 = (int)((urx - _originX) / _cellSize);
    int gy1 = (int)((ury - _originY) / _cellSize);

    // Clamp to grid bounds
    if (gx0 < 0) gx0 = 0;
    if (gy0 < 0) gy0 = 0;
    if (gx1 >= _nx) gx1 = _nx - 1;
    if (gy1 >= _ny) gy1 = _ny - 1;

    // Check each candidate in covered cells via CSR offset access,
    // deduplicate via bitmap.
    // For cell ci, device indices are _gridData[ _gridOffsets[ci] .. _gridOffsets[ci+1] )
    for (int gy = gy0; gy <= gy1; ++gy) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            int ci = _cellIndex(gx, gy);
            for (int k = _gridOffsets[ci]; k < _gridOffsets[ci + 1]; ++k) {
                int idx = _gridData[k];
                const SelfHeatingDevStr& dev = _devices[idx];
                // Exact bbox overlap test
                if (dev.urx > llx && dev.llx < urx &&
                    dev.ury > lly && dev.lly < ury) {
                    if (!visited[idx]) {
                        visited[idx] = true;
                        results.push_back(idx);
                    }
                }
            }
        }
    }

    // Reset only the entries we touched (typically 0-5 elements)
    for (size_t i = 0; i < results.size(); ++i)
        visited[results[i]] = false;
}

int SelfHeatingDevMgr::deviceCount() const {
    return (int)_devices.size();
}

const SelfHeatingDevStr& SelfHeatingDevMgr::getDevice(int idx) const {
    return _devices[idx];
}

SelfHeatingDevStr& SelfHeatingDevMgr::getDevice(int idx) {
    return _devices[idx];
}

const std::string& SelfHeatingDevMgr::layerName(short layer_id) const {
    return _layerNames[layer_id];
}

// =============================================================================
// SelfHeatingMgr — per-net wire res processor
//
// Lifetime: created temporarily per net in the outer loop, destroyed after use.
// Two-phase usage:
//   1. buildViaConn() - identify which wire res are connected to MOSFET pins
//   2. compute()      - calculate deltaT for each wire res, write to ResEmParam
// =============================================================================

SelfHeatingMgr::SelfHeatingMgr(EmirNetInfo* net, int debug, int numThreads)
    : _net(net), _debug(debug), _numThreads(numThreads < 1 ? 1 : numThreads)
{}

// Build the via-connectivity flags: which wire res are connected to MOSFET
// pins via one-hop via traversal within the same net.
//
// Algorithm:
//   Pass 1: Scan all via res. If one endpoint has type 'I' (MOSFET pin),
//           mark the other endpoint's node index in isConnNode bitmap.
//   Pass 2: Scan all wire res. If either endpoint (n1/n2) is marked in
//           the bitmap, set _isConnected[i] = true.
//
// Uses vector<bool> indexed by node idx for O(1) lookup, replacing
// std::set which had O(log N) per lookup with poor cache locality.
//
// This determines which alpha coefficient to use:
//   connected wire res    -> alpha_connecting   (stronger coupling)
//   non-connected wire res -> alpha_overlapping  (weaker coupling)
void SelfHeatingMgr::buildViaConn() {
    const std::vector<EmirNodeInfo*>& nodes = _net->nodes();
    const std::vector<EmirResInfo*>& reses = _net->reses();
    _isConnected.assign(reses.size(), false);

    clock_t t0 = clock();

    // Pass 1: Mark nodes reachable from MOSFET pins through a single via
    std::vector<bool> isConnNode(nodes.size(), false);
    int viaCount = 0;
    int connNodeCount = 0;
    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (!res->isVia()) continue;
        ++viaCount;

        const EmirNodeInfo* n1 = res->n1();
        const EmirNodeInfo* n2 = res->n2();

        if (n1->type() == 'I' && !isConnNode[n2->idx()]) {
            isConnNode[n2->idx()] = true;
            ++connNodeCount;
        }
        if (n2->type() == 'I' && !isConnNode[n1->idx()]) {
            isConnNode[n1->idx()] = true;
            ++connNodeCount;
        }
    }

    clock_t t1 = clock();

    // Pass 2: Mark wire res whose endpoints touch a connected node (O(1) lookup)
    int wireCount = 0;
    int connectedCount = 0;
    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (res->isVia()) continue;
        ++wireCount;

        if (isConnNode[res->n1()->idx()] ||
            isConnNode[res->n2()->idx()]) {
            _isConnected[i] = true;
            ++connectedCount;
        }
    }

    clock_t t2 = clock();

    if (_debug >= 1) {
        double pass1_sec = (double)(t1 - t0) / CLOCKS_PER_SEC;
        double pass2_sec = (double)(t2 - t1) / CLOCKS_PER_SEC;
        fprintf(stderr, "[SH] buildViaConn: reses=%d (via=%d wire=%d) connNodes=%d connWire=%d\n",
                (int)reses.size(), viaCount, wireCount,
                connNodeCount, connectedCount);
        fprintf(stderr, "[SH] buildViaConn: pass1=%.3fs pass2=%.3fs total=%.3fs  %s %s\n",
                pass1_sec, pass2_sec, pass1_sec + pass2_sec,
                getNowStr(), getRssStr());
    }
}

// Static helper: compute deltaT for wire res in range [begin, end).
// Called directly in single-threaded mode, or per-chunk in multi-threaded mode.
// Each invocation has its own local 'overlap' vector (per-thread when parallel).
static void computeRange(
    size_t begin, size_t end,
    const std::vector<EmirResInfo*>& reses,
    std::vector<ResEmParam>& emParams,
    const SelfHeatingDevMgr& devMgr,
    const SelfHeatingParams& params,
    const std::vector<const MetalLayerParams*>& mlpTable,
    const std::vector<bool>& isConnected,
    int debug, EmirNetInfo* net)
{
    std::vector<int> overlap;
    std::vector<bool> visited(devMgr.deviceCount(), false);

    // Cache params constants to local variables (avoid repeated struct access)
    const double beta_c1 = params.beta_c1;
    const double beta_c2 = params.beta_c2;
    const double beta_c3 = params.beta_c3;
    const double K_SH_Scale = params.K_SH_Scale;

    for (size_t r = begin; r < end; ++r) {
        EmirResInfo* res = reses[r];
        if (res->isVia()) continue;

        // O(1) layer params lookup via layerIdx (replaces string map find)
        int lidx = res->layerIdx();
        if (lidx < 0 || lidx >= (int)mlpTable.size() || !mlpTable[lidx]) continue;
        const MetalLayerParams& mlp = *mlpTable[lidx];

        double deltaT_self = mlp.Rth * res->avgPower();

        double deltaT_feol = 0.0;

        double res_area = (double)(res->urx() - res->llx())
                        * (res->ury() - res->lly());
        if (res_area <= 0.0) continue;

        if (debug >= 2) {
            net->mgr()->debug("[SH] res[%zu] layer=%s bbox=(%.4f,%.4f)-(%.4f,%.4f) area=%.6g\n",
                       r, res->layer().c_str(),
                       res->llx(), res->lly(), res->urx(), res->ury(), res_area);
        }

        devMgr.queryOverlap(res->llx(), res->lly(),
                            res->urx(), res->ury(), overlap, visited);

        // Hoist alpha and rmsPower before j-loop (loop-invariant)
        double alpha = isConnected[r]
                       ? mlp.alpha_connecting
                       : mlp.alpha_overlapping;
        double rms_power = res->rmsPower();
        double beta_c2_rms = beta_c2 * rms_power;

        for (int j = 0; j < (int)overlap.size(); ++j) {
            const SelfHeatingDevStr& dev = devMgr.getDevice(overlap[j]);

            float inter_llx = (res->llx() > dev.llx) ? res->llx() : dev.llx;
            float inter_lly = (res->lly() > dev.lly) ? res->lly() : dev.lly;
            float inter_urx = (res->urx() < dev.urx) ? res->urx() : dev.urx;
            float inter_ury = (res->ury() < dev.ury) ? res->ury() : dev.ury;
            double inter_area = (double)(inter_urx - inter_llx)
                              * (inter_ury - inter_lly);
            double overlap_ratio = inter_area / res_area;

            double beta = beta_c1 * dev.deltaT
                        + beta_c2_rms
                        + beta_c3;

            double contribution = overlap_ratio * alpha * beta * dev.deltaT;
            if (debug >= 2) {
                net->mgr()->debug("[SH]   dev[%d] overlap_ratio=%.6g alpha=%.6g beta=%.6g contrib=%.6g\n",
                           overlap[j], overlap_ratio, alpha, beta, contribution);
            }

            deltaT_feol += contribution;
        }

        double deltaT_total = (deltaT_self + deltaT_feol) * K_SH_Scale;
        emParams[r]._deltaT = (float)deltaT_total;

        if (debug >= 1) {
            net->mgr()->debug("[SH] res[%zu] deltaT_self=%.6g deltaT_feol=%.6g deltaT_total=%.6g\n",
                       r, deltaT_self, deltaT_feol, deltaT_total);
        }
    }
}

// mtmq helper types for multi-threaded compute
namespace {

struct SHComputeJob {
    size_t begin;
    size_t end;
};

struct SHComputeArg : public EmirMtmqArg {
    const std::vector<EmirResInfo*>* reses;
    std::vector<ResEmParam>* emParams;
    const SelfHeatingDevMgr* devMgr;
    const SelfHeatingParams* params;
    const std::vector<const MetalLayerParams*>* mlpTable;
    const std::vector<bool>* isConnected;
    int debug;
    EmirNetInfo* net;
};

class SHComputeTask : public EmirMtmqRDtask {
public:
    SHComputeTask() : EmirMtmqRDtask("SHCompute") {}
    virtual void run(void* job, EmirMtmqArg* arg) {
        SHComputeJob* j = static_cast<SHComputeJob*>(job);
        SHComputeArg* a = static_cast<SHComputeArg*>(arg);
        computeRange(j->begin, j->end,
                     *a->reses, *a->emParams, *a->devMgr, *a->params,
                     *a->mlpTable, *a->isConnected, a->debug, a->net);
    }
};

} // anonymous namespace

// Compute deltaT for each wire res in this net and write to ResEmParam._deltaT.
//
// For each wire res:
//   1. deltaT_self = Rth(wire_layer) * avgPower  (self joule heating)
//   2. deltaT_feol = sum over overlapping devices of:
//        alpha * beta * device_deltaT
//      where:
//        alpha = alpha_connecting if wire is via-connected, else alpha_overlapping
//        beta  = beta_c1 * dev_deltaT + beta_c2 * rmsPower + beta_c3
//   3. deltaT_total = (deltaT_self + deltaT_feol) * K_SH_Scale
//
// Wire res whose layer is not in metal_layers params are skipped (deltaT = 0).
// Via res are skipped entirely.
//
// When _numThreads > 1, the res range is split into chunks and processed
// in parallel via mtmq RD mode. Each thread writes to disjoint emParams indices.
void SelfHeatingMgr::compute(
    const SelfHeatingDevMgr& devMgr,
    const SelfHeatingParams& params)
{
    const std::vector<EmirResInfo*>& reses = _net->reses();
    std::vector<ResEmParam>& emParams = _net->resEmParams();

    if (reses.empty()) return;

    clock_t t0 = clock();

    // Build mlpTable: layerIdx -> MetalLayerParams* for O(1) lookup
    //
    // Two-pass O(N) algorithm replacing O(N*M) nested loop:
    //   Pass 1: Scan wire res once to build layerIdx -> layerName mapping
    //   Pass 2: For each mapped layer, lookup metal_layers map once
    int maxLayerIdx = -1;
    for (size_t i = 0; i < reses.size(); ++i) {
        if (!reses[i]->isVia() && reses[i]->layerIdx() > maxLayerIdx)
            maxLayerIdx = reses[i]->layerIdx();
    }
    std::vector<const MetalLayerParams*> mlpTable(maxLayerIdx + 1, NULL);
    {
        // Pass 1: collect layerIdx -> layer name (first occurrence wins)
        std::vector<const std::string*> idxToName(maxLayerIdx + 1, NULL);
        for (size_t i = 0; i < reses.size(); ++i) {
            if (reses[i]->isVia()) continue;
            int lidx = reses[i]->layerIdx();
            if (lidx >= 0 && lidx <= maxLayerIdx && !idxToName[lidx]) {
                idxToName[lidx] = &reses[i]->layer();
            }
        }
        // Pass 2: lookup metal_layers map for each distinct layerIdx
        for (int lidx = 0; lidx <= maxLayerIdx; ++lidx) {
            if (!idxToName[lidx]) continue;
            std::map<std::string, MetalLayerParams>::const_iterator it =
                params.metal_layers.find(*idxToName[lidx]);
            if (it != params.metal_layers.end()) {
                mlpTable[lidx] = &it->second;
            }
        }
    }

    if (_numThreads <= 1) {
        // Single-threaded path
        computeRange(0, reses.size(), reses, emParams, devMgr, params,
                     mlpTable, _isConnected, _debug, _net);
    } else {
        // Multi-threaded path via mtmq
        size_t n = reses.size();
        size_t nThreads = (size_t)_numThreads;
        size_t chunkSize = (n + nThreads - 1) / nThreads;

        std::vector<SHComputeJob> jobs(nThreads);
        for (size_t t = 0; t < nThreads; ++t) {
            jobs[t].begin = t * chunkSize;
            jobs[t].end = jobs[t].begin + chunkSize;
            if (jobs[t].end > n) jobs[t].end = n;
        }

        SHComputeArg arg;
        arg.reses = &reses;
        arg.emParams = &emParams;
        arg.devMgr = &devMgr;
        arg.params = &params;
        arg.mlpTable = &mlpTable;
        arg.isConnected = &_isConnected;
        arg.debug = _debug;
        arg.net = _net;

        // _numThreads-1 worker threads + main thread = _numThreads total
        EmirMtmqMgr mtmq(nThreads - 1);
        for (size_t t = 0; t < nThreads; ++t) {
            if (jobs[t].begin < jobs[t].end) {
                mtmq.addLeafJob(&jobs[t]);
            }
        }
        mtmq.setArgument(&arg);
        mtmq.start();

        SHComputeTask task;
        mtmq.run(&task);
    }

    if (_debug >= 1) {
        clock_t t1 = clock();
        double sec = (double)(t1 - t0) / CLOCKS_PER_SEC;
        fprintf(stderr, "[SH] compute: reses=%d threads=%d time=%.3fs  %s %s\n",
                (int)reses.size(), _numThreads, sec,
                getNowStr(), getRssStr());
    }
}
