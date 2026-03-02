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

#include <set>
#include <cstdio>

#include "emirMtmqMgr.h"
#include "emirMtmqTask.h"
#include "emirMtmqArg.h"

// =============================================================================
// SelfHeatingDevMgr
// =============================================================================

SelfHeatingDevMgr::SelfHeatingDevMgr()
    : _originX(0.0f), _originY(0.0f), _cellSize(1.0f), _nx(0), _ny(0)
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
//   2. Compute the overall layout bounding box from all device bboxes
//   3. Determine grid cell size targeting ~1000x1000 cells
//   4. Insert each device into all grid cells its bbox covers
void SelfHeatingDevMgr::init(const std::vector<SelfHeatingMosfet>& mosfets) {
    if (mosfets.empty()) return;

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

    // Step 2: Compute layout bounding box from all devices
    float min_x = _devices[0].llx, min_y = _devices[0].lly;
    float max_x = _devices[0].urx, max_y = _devices[0].ury;
    for (size_t i = 1; i < _devices.size(); ++i) {
        if (_devices[i].llx < min_x) min_x = _devices[i].llx;
        if (_devices[i].lly < min_y) min_y = _devices[i].lly;
        if (_devices[i].urx > max_x) max_x = _devices[i].urx;
        if (_devices[i].ury > max_y) max_y = _devices[i].ury;
    }
    _originX = min_x;
    _originY = min_y;

    // Step 3: Determine grid cell size, targeting ~1000x1000 cells
    float width = max_x - min_x;
    float height = max_y - min_y;
    _cellSize = width / 1000.0f;
    if (height / 1000.0f > _cellSize) _cellSize = height / 1000.0f;
    if (_cellSize <= 0.0f) _cellSize = 1.0f;

    _nx = (int)((width / _cellSize) + 1);
    _ny = (int)((height / _cellSize) + 1);

    // Step 4: Populate grid cells with device indices
    _gridCells.clear();
    _gridCells.resize(_nx * _ny);
    for (int i = 0; i < (int)_devices.size(); ++i) {
        // Map device bbox to grid cell range
        int gx0 = (int)((_devices[i].llx - _originX) / _cellSize);
        int gy0 = (int)((_devices[i].lly - _originY) / _cellSize);
        int gx1 = (int)((_devices[i].urx - _originX) / _cellSize);
        int gy1 = (int)((_devices[i].ury - _originY) / _cellSize);

        // Clamp to grid bounds
        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
        if (gx1 >= _nx) gx1 = _nx - 1;
        if (gy1 >= _ny) gy1 = _ny - 1;

        // Insert device index into all covered cells
        for (int gy = gy0; gy <= gy1; ++gy) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                _gridCells[_cellIndex(gx, gy)].push_back(i);
            }
        }
    }
}

// Phase 2: Compute deltaT for each device.
// Formula: deltaT = power * Rth / finger_effect / fin_effect
// If the device layer is not found in params, deltaT is set to 0.
void SelfHeatingDevMgr::build(
    const std::map<std::string, DeviceLayerParams>& device_layers)
{
    for (int i = 0; i < (int)_devices.size(); ++i) {
        SelfHeatingDevStr& dev = _devices[i];
        const std::string& layer = _layerNames[dev.layer_id];

        std::map<std::string, DeviceLayerParams>::const_iterator it =
            device_layers.find(layer);
        if (it == device_layers.end()) {
            dev.deltaT = 0.0f;
            continue;
        }
        const DeviceLayerParams& lp = it->second;

        // Apply effect functions; default to 1.0 if not specified
        double finger_eff = lp.finger_effect
                            ? lp.finger_effect(dev.finger_num) : 1.0;
        double fin_eff = lp.fin_effect
                         ? lp.fin_effect(dev.fin_num) : 1.0;

        dev.deltaT = (float)(dev.power * lp.Rth / finger_eff / fin_eff);
    }
}

// Find all devices whose bbox overlaps the query rectangle.
// Uses the Uniform Grid for fast spatial filtering, then does
// exact bbox intersection check on candidates.
//
// Results are returned via output parameter to allow caller reuse
// (avoids repeated heap allocation across ~300M queries).
// Results are sorted and deduplicated since a device spanning
// multiple grid cells would otherwise appear multiple times.
void SelfHeatingDevMgr::queryOverlap(
    float llx, float lly, float urx, float ury,
    std::vector<int>& results) const
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

    // Check each candidate in covered cells
    for (int gy = gy0; gy <= gy1; ++gy) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            const std::vector<int>& cell = _gridCells[_cellIndex(gx, gy)];
            for (size_t k = 0; k < cell.size(); ++k) {
                int idx = cell[k];
                const SelfHeatingDevStr& dev = _devices[idx];
                // Exact bbox overlap test
                if (dev.urx > llx && dev.llx < urx &&
                    dev.ury > lly && dev.lly < ury) {
                    results.push_back(idx);
                }
            }
        }
    }

    // Deduplicate (a device may appear in multiple grid cells)
    std::sort(results.begin(), results.end());
    results.erase(std::unique(results.begin(), results.end()), results.end());
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

// Build the set of wire res that are connected to MOSFET pins via one-hop
// via traversal within the same net.
//
// Algorithm:
//   Pass 1: Scan all via res. If one endpoint has type 'I' (MOSFET pin),
//           add the other endpoint to connectedNodes set.
//   Pass 2: Scan all wire res. If either endpoint (n1/n2) is in
//           connectedNodes, add the wire res to _connectedRes.
//
// This determines which alpha coefficient to use:
//   connected wire res    -> alpha_connecting   (stronger coupling)
//   non-connected wire res -> alpha_overlapping  (weaker coupling)
void SelfHeatingMgr::buildViaConn() {
    const std::vector<EmirResInfo*>& reses = _net->reses();
    _isConnected.assign(reses.size(), false);

    std::set<const EmirNodeInfo*> connectedNodes;

    // Pass 1: Collect nodes reachable from MOSFET pins through a single via
    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (!res->isVia()) continue;

        const EmirNodeInfo* n1 = res->n1();
        const EmirNodeInfo* n2 = res->n2();

        if (n1->type() == 'I') connectedNodes.insert(n2);
        if (n2->type() == 'I') connectedNodes.insert(n1);
    }

    // Pass 2: Mark wire res whose endpoints touch a connected node
    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (res->isVia()) continue;

        if (connectedNodes.count(res->n1()) ||
            connectedNodes.count(res->n2())) {
            _isConnected[i] = true;
        }
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
    const std::vector<bool>& isConnected,
    int debug, EmirNetInfo* net)
{
    std::vector<int> overlap;

    for (size_t r = begin; r < end; ++r) {
        EmirResInfo* res = reses[r];
        if (res->isVia()) continue;

        const std::string& wire_layer = res->layer();
        std::map<std::string, MetalLayerParams>::const_iterator mlp_it =
            params.metal_layers.find(wire_layer);
        if (mlp_it == params.metal_layers.end()) continue;
        const MetalLayerParams& mlp = mlp_it->second;

        double deltaT_self = mlp.Rth * res->avgPower();

        double deltaT_feol = 0.0;

        double res_area = (double)(res->urx() - res->llx())
                        * (res->ury() - res->lly());
        if (res_area <= 0.0) continue;

        if (debug >= 2) {
            net->debug("[SH] res[%zu] layer=%s bbox=(%.4f,%.4f)-(%.4f,%.4f) area=%.6g\n",
                       r, wire_layer.c_str(),
                       res->llx(), res->lly(), res->urx(), res->ury(), res_area);
        }

        devMgr.queryOverlap(res->llx(), res->lly(),
                            res->urx(), res->ury(), overlap);

        // Hoist alpha lookup before the device j-loop (P0 fix)
        double alpha = isConnected[r]
                       ? mlp.alpha_connecting
                       : mlp.alpha_overlapping;

        for (int j = 0; j < (int)overlap.size(); ++j) {
            const SelfHeatingDevStr& dev = devMgr.getDevice(overlap[j]);

            float inter_llx = (res->llx() > dev.llx) ? res->llx() : dev.llx;
            float inter_lly = (res->lly() > dev.lly) ? res->lly() : dev.lly;
            float inter_urx = (res->urx() < dev.urx) ? res->urx() : dev.urx;
            float inter_ury = (res->ury() < dev.ury) ? res->ury() : dev.ury;
            double inter_area = (double)(inter_urx - inter_llx)
                              * (inter_ury - inter_lly);
            double overlap_ratio = inter_area / res_area;

            double beta = params.beta_c1 * dev.deltaT
                        + params.beta_c2 * res->rmsPower()
                        + params.beta_c3;

            double contribution = overlap_ratio * alpha * beta * dev.deltaT;
            if (debug >= 2) {
                net->debug("[SH]   dev[%d] overlap_ratio=%.6g alpha=%.6g beta=%.6g contrib=%.6g\n",
                           overlap[j], overlap_ratio, alpha, beta, contribution);
            }

            deltaT_feol += contribution;
        }

        double deltaT_total = (deltaT_self + deltaT_feol) * params.K_SH_Scale;
        emParams[r]._deltaT = (float)deltaT_total;

        if (debug >= 1) {
            net->debug("[SH] res[%zu] deltaT_self=%.6g deltaT_feol=%.6g deltaT_total=%.6g\n",
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
                     *a->isConnected, a->debug, a->net);
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

    if (_numThreads <= 1) {
        // Single-threaded path
        computeRange(0, reses.size(), reses, emParams, devMgr, params,
                     _isConnected, _debug, _net);
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
}
