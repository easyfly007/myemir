// selfHeating.h
//
// Self-heating module for emir EM analysis.
// Computes temperature rise (deltaT) on wire resistors due to
// MOSFET self-heating effects and writes results to ResEmParam._deltaT.
//
// Usage (upper-level calling flow):
//   1. SelfHeatingDevMgr::init()  - load MOSFET data + build spatial index
//   2. SelfHeatingDevMgr::build() - compute device deltaT
//   3. Per net: SelfHeatingMgr::buildViaConn() + compute()
//   4. Existing EM check reads ResEmParam._deltaT
//
// All code is C++03 compatible.

#ifndef SELFHEATING_H
#define SELFHEATING_H

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

#include "selfheating/emirNetInfo.h"

// =============================================================================
// SelfHeatingDevStr — compact MOSFET storage (32 bytes)
//
// Copied from external MOSFET manager during init(). Uses compact types
// to minimize memory footprint (~96 MB for 3 million devices).
// =============================================================================

struct SelfHeatingDevStr {
    float llx, lly, urx, ury;  // 16 bytes - bounding box for overlap query
    float power;                // 4 bytes  - device power dissipation
    float deltaT;               // 4 bytes  - temperature rise (computed by build)
    short finger_num;           // 2 bytes  - number of fingers
    short fin_num;              // 2 bytes  - number of fins
    short layer_id;             // 2 bytes  - maps to layer name via DevMgr
    short _pad;                 // 2 bytes  - padding to 32 bytes total
};

// =============================================================================
// SelfHeatingMosfet — MOSFET input from external manager
//
// Tentative structure. Will be adjusted when integrating with the actual
// external MosfetMgr interface.
// =============================================================================

struct SelfHeatingMosfet {
    float llx, lly, urx, ury;  // bounding box
    float power;                // power dissipation
    short finger_num;           // number of fingers
    short fin_num;              // number of fins
    std::string layer_name;     // device layer name (e.g. "OD")
};

// =============================================================================
// Techfile parameters
// =============================================================================

// Function pointer type for finger/fin effect formulas.
// Takes the count (finger_num or fin_num), returns the effect multiplier.
typedef double (*EffectFunc)(int);

// Per device-layer parameters (e.g. "OD")
struct DeviceLayerParams {
    double Rth;                 // thermal resistance
    EffectFunc finger_effect;   // finger effect function, NULL = default 1.0
    EffectFunc fin_effect;      // fin effect function, NULL = default 1.0

    DeviceLayerParams(): Rth(0.0), finger_effect(NULL), fin_effect(NULL) {}
};

// Per metal-layer parameters (e.g. "M1", "M2")
struct MetalLayerParams {
    double Rth;                 // thermal resistance
    double alpha_connecting;    // coupling coeff when via-connected to MOSFET
    double alpha_overlapping;   // coupling coeff when only bbox overlapping

    MetalLayerParams(): Rth(0.0), alpha_connecting(0.0), alpha_overlapping(0.0) {}
};

// Top-level parameter container, populated from techfile
struct SelfHeatingParams {
    double K_SH_Scale;                  // global deltaT scaling factor
    double beta_c1, beta_c2, beta_c3;   // beta formula coefficients
    double T_ambient;                    // ambient temperature
    std::map<std::string, DeviceLayerParams> device_layers;
    std::map<std::string, MetalLayerParams> metal_layers;

    SelfHeatingParams():
        K_SH_Scale(1.0), beta_c1(0.0), beta_c2(0.0), beta_c3(0.0),
        T_ambient(25.0) {}
};

// =============================================================================
// SelfHeatingDevMgr — MOSFET manager with Uniform Grid spatial index
//
// Manages all MOSFET device data and provides fast bbox overlap queries
// via an internal Uniform Grid (~1000x1000 cells).
//
// Two-phase usage:
//   init()  - copy data from external manager + build grid
//   build() - compute each device's deltaT from techfile params
// Then queryOverlap() is called per wire res during SelfHeatingMgr::compute().
// =============================================================================

class SelfHeatingDevMgr {
public:
    SelfHeatingDevMgr();
    ~SelfHeatingDevMgr();

    // Phase 1: copy external MOSFET data + build Uniform Grid spatial index
    void init(const std::vector<SelfHeatingMosfet>& mosfets);

    // Phase 2: compute each device's deltaT using device layer params
    // Formula: deltaT = power * Rth / finger_effect / fin_effect
    void build(const std::map<std::string, DeviceLayerParams>& device_layers);

    // Find device indices whose bbox overlaps the query rectangle.
    // Caller should reuse results AND visited vectors to avoid repeated allocation.
    // visited must be sized to deviceCount() and initialized to all false.
    //
    // Why visited is needed: a device spanning multiple grid cells gets inserted
    // into each cell during init(). Without dedup, the same device index would
    // appear multiple times in results. The bitmap provides O(1) dedup per query.
    void queryOverlap(float llx, float lly, float urx, float ury,
                      std::vector<int>& results,
                      std::vector<bool>& visited) const;

    int deviceCount() const;
    const SelfHeatingDevStr& getDevice(int idx) const;
    SelfHeatingDevStr& getDevice(int idx);
    const std::string& layerName(short layer_id) const;

private:
    std::vector<SelfHeatingDevStr> _devices;    // all devices, compact storage

    std::vector<std::string> _layerNames;       // layer_id -> layer name
    std::map<std::string, short> _layerNameToId; // layer name -> layer_id

    // Uniform Grid — CSR (Compressed Sparse Row) format
    //
    // All device indices for all grid cells stored in one contiguous array
    // (_gridData). Each cell's portion is located via _gridOffsets:
    //   cell i owns _gridData[ _gridOffsets[i] .. _gridOffsets[i+1] )
    //
    // This replaces vector<vector<int>> which required ~1M independent heap
    // allocations, causing memory fragmentation and poor cache locality
    // during ~250M queryOverlap() calls.
    //
    // Memory layout example (3 cells with 2, 0, 3 devices):
    //   _gridOffsets: [0, 2, 2, 5]   (size = numCells + 1)
    //   _gridData:    [7, 3, 12, 0, 5]
    //                  ^^^  ^^  ^^^^^^^
    //                 cell0  cell1  cell2
    std::vector<int> _gridData;      // device indices, all cells contiguous
    std::vector<int> _gridOffsets;   // _gridOffsets[i] = start of cell i
    float _originX, _originY;   // layout lower-left corner
    float _cellSize;            // grid cell edge length
    int _nx, _ny;               // grid dimensions (columns, rows)

    short _getOrCreateLayerId(const std::string& name);
    int _cellIndex(int gx, int gy) const;
};

// =============================================================================
// SelfHeatingMgr — per-net wire res processor
//
// Temporary object created per net. Handles:
//   1. Via connectivity detection (one-hop from MOSFET pin)
//   2. Wire res deltaT computation using overlapping devices
//
// Results are written directly to ResEmParam._deltaT (same offset as res).
// Via connectivity is net-internal (nets don't connect to each other).
// =============================================================================

class SelfHeatingMgr {
public:
    // Constructor takes the net to process, optional debug level, and thread count.
    //   debug: 0 = no debug output (default), 1 = summary, 2 = verbose
    //   numThreads: 1 = serial (default), >1 = parallel via mtmq thread pool
    SelfHeatingMgr(EmirNetInfo* net, int debug = 0, int numThreads = 1);

    // Scan via res in this net to identify wire res connected to MOSFET pins.
    // Must be called before compute().
    void buildViaConn();

    // Compute deltaT for all wire res and write to ResEmParam._deltaT.
    // Uses devMgr for spatial overlap queries and device deltaT values.
    void compute(const SelfHeatingDevMgr& devMgr,
                 const SelfHeatingParams& params);

private:
    EmirNetInfo* _net;
    int _debug;
    int _numThreads;
    std::vector<bool> _isConnected; // _isConnected[r] = true if wire res r is via-connected to MOSFET
};

#endif // SELFHEATING_H
