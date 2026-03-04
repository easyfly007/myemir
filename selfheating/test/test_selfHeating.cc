#include "../../plsi/selfHeating.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cassert>

// =============================================================================
// Test helpers
// =============================================================================

static int g_testsPassed = 0;
static int g_testsFailed = 0;

static void check(bool cond, const char* msg, int line) {
    if (cond) {
        ++g_testsPassed;
    } else {
        ++g_testsFailed;
        fprintf(stderr, "FAIL (line %d): %s\n", line, msg);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_NEAR(a, b, eps) check(fabs((a)-(b)) < (eps), #a " ~= " #b, __LINE__)

// dummy effect functions for testing
static double testFingerEffect(int finger_num) {
    return 2.0 * (1.0 - exp(-0.3 * finger_num));
}

static double testFinEffect(int fin_num) {
    return 1.0 - (0.018 * (10 - fin_num));
}

// =============================================================================
// Test: SelfHeatingDevMgr init + basic accessors
// =============================================================================

static void testDevMgrInit() {
    fprintf(stdout, "--- testDevMgrInit ---\n");

    std::vector<SelfHeatingMosfet> mosfets(3);

    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 0.5f; mosfets[0].finger_num = 4; mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "OD";

    mosfets[1].llx = 20; mosfets[1].lly = 20; mosfets[1].urx = 30; mosfets[1].ury = 30;
    mosfets[1].power = 1.0f; mosfets[1].finger_num = 2; mosfets[1].fin_num = 6;
    mosfets[1].layer_name = "OD";

    mosfets[2].llx = 50; mosfets[2].lly = 50; mosfets[2].urx = 60; mosfets[2].ury = 60;
    mosfets[2].power = 0.8f; mosfets[2].finger_num = 3; mosfets[2].fin_num = 10;
    mosfets[2].layer_name = "POLY";

    SelfHeatingDevMgr mgr;
    mgr.init(mosfets, 0, 0, 60, 60);

    CHECK(mgr.deviceCount() == 3);
    CHECK_NEAR(mgr.getDevice(0).power, 0.5f, 1e-6);
    CHECK_NEAR(mgr.getDevice(1).power, 1.0f, 1e-6);
    CHECK_NEAR(mgr.getDevice(2).power, 0.8f, 1e-6);
    CHECK(mgr.getDevice(0).finger_num == 4);
    CHECK(mgr.getDevice(2).fin_num == 10);
    CHECK(mgr.layerName(mgr.getDevice(0).layer_id) == "OD");
    CHECK(mgr.layerName(mgr.getDevice(2).layer_id) == "POLY");
}

// =============================================================================
// Test: SelfHeatingDevMgr build (deltaT computation)
// =============================================================================

static void testDevMgrBuild() {
    fprintf(stdout, "--- testDevMgrBuild ---\n");

    std::vector<SelfHeatingMosfet> mosfets(1);
    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 2.0f;
    mosfets[0].finger_num = 4;
    mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "OD";

    SelfHeatingDevMgr mgr;
    mgr.init(mosfets, 0, 0, 10, 10);

    std::map<std::string, DeviceLayerParams> dlp;
    DeviceLayerParams od;
    od.Rth = 1000.0;
    od.finger_effect = testFingerEffect;
    od.fin_effect = testFinEffect;
    dlp["OD"] = od;

    mgr.build(dlp);

    double expected_finger = 2.0 * (1.0 - exp(-0.3 * 4));
    double expected_fin = 1.0 - (0.018 * (10 - 8));
    double expected_deltaT = 2.0 * 1000.0 / expected_finger / expected_fin;

    CHECK_NEAR(mgr.getDevice(0).deltaT, expected_deltaT, 0.01);
}

// =============================================================================
// Test: SelfHeatingDevMgr build — unknown layer defaults to deltaT=0
// =============================================================================

static void testDevMgrBuildUnknownLayer() {
    fprintf(stdout, "--- testDevMgrBuildUnknownLayer ---\n");

    std::vector<SelfHeatingMosfet> mosfets(1);
    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 2.0f;
    mosfets[0].finger_num = 4;
    mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "UNKNOWN";

    SelfHeatingDevMgr mgr;
    mgr.init(mosfets, 0, 0, 10, 10);

    std::map<std::string, DeviceLayerParams> dlp;
    mgr.build(dlp);

    CHECK_NEAR(mgr.getDevice(0).deltaT, 0.0f, 1e-6);
}

// =============================================================================
// Test: SelfHeatingDevMgr queryOverlap
// =============================================================================

static void testDevMgrQueryOverlap() {
    fprintf(stdout, "--- testDevMgrQueryOverlap ---\n");

    // 3 devices at different locations
    std::vector<SelfHeatingMosfet> mosfets(3);

    mosfets[0].llx = 0;  mosfets[0].lly = 0;  mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 1.0f; mosfets[0].finger_num = 1; mosfets[0].fin_num = 1;
    mosfets[0].layer_name = "OD";

    mosfets[1].llx = 5;  mosfets[1].lly = 5;  mosfets[1].urx = 15; mosfets[1].ury = 15;
    mosfets[1].power = 1.0f; mosfets[1].finger_num = 1; mosfets[1].fin_num = 1;
    mosfets[1].layer_name = "OD";

    mosfets[2].llx = 100; mosfets[2].lly = 100; mosfets[2].urx = 110; mosfets[2].ury = 110;
    mosfets[2].power = 1.0f; mosfets[2].finger_num = 1; mosfets[2].fin_num = 1;
    mosfets[2].layer_name = "OD";

    SelfHeatingDevMgr mgr;
    mgr.init(mosfets, 0, 0, 110, 110);

    std::vector<int> results;
    std::vector<bool> visited(mgr.deviceCount(), false);

    // Query overlapping device 0 and 1
    mgr.queryOverlap(3, 3, 12, 12, results, visited);
    CHECK(results.size() == 2);

    // Query overlapping only device 2
    mgr.queryOverlap(99, 99, 111, 111, results, visited);
    CHECK(results.size() == 1);
    CHECK(results[0] == 2);

    // Query with no overlap
    mgr.queryOverlap(50, 50, 60, 60, results, visited);
    CHECK(results.size() == 0);
}

// =============================================================================
// Test: SelfHeatingMgr buildViaConn
// =============================================================================

static void testMgrBuildViaConn() {
    fprintf(stdout, "--- testMgrBuildViaConn ---\n");

    // Build a simple net:
    //   node_inst (type 'I') --[via_res]--> node_mid (type 'N') --[wire_res_0]--> node_end (type 'N')
    //   node_other (type 'N') --[wire_res_1]--> node_far (type 'N')  (not connected to MOSFET)

    EmirNodeInfo node_inst, node_mid, node_end, node_other, node_far;
    node_inst.setType('I'); node_inst.setX(0); node_inst.setY(0);
    node_mid.setType('N');  node_mid.setX(5);  node_mid.setY(5);
    node_end.setType('N');  node_end.setX(10); node_end.setY(10);
    node_other.setType('N'); node_other.setX(50); node_other.setY(50);
    node_far.setType('N');  node_far.setX(60); node_far.setY(60);

    EmirInfoMgr infoMgr;
    EmirLayerInfo via_layer(1, "VIA1");
    via_layer._type = 1;  // via layer
    infoMgr.addLayer(&via_layer);

    EmirNetInfo net;
    net._mgr = &infoMgr;
    net.addNode(&node_inst);   // idx 0
    net.addNode(&node_mid);    // idx 1
    net.addNode(&node_end);    // idx 2
    net.addNode(&node_other);  // idx 3
    net.addNode(&node_far);    // idx 4

    // via res: node_inst -> node_mid
    EmirResInfo via_res;
    via_res._n1 = node_inst.idx();
    via_res._n2 = node_mid.idx();
    via_res.setLayerIdx(1);
    via_res.setBBox(0, 0, 5, 5);

    // wire_res_0: node_mid -> node_end (should be connected)
    EmirResInfo wire_res_0;
    wire_res_0._n1 = node_mid.idx();
    wire_res_0._n2 = node_end.idx();
    wire_res_0.setLayerIdx(0);
    wire_res_0.setBBox(0, 0, 10, 10);


    // wire_res_1: node_other -> node_far (not connected)
    EmirResInfo wire_res_1;
    wire_res_1._n1 = node_other.idx();
    wire_res_1._n2 = node_far.idx();
    wire_res_1.setLayerIdx(0);
    wire_res_1.setBBox(50, 50, 60, 60);
    net.addRes(&via_res);
    net.addRes(&wire_res_0, 0.01f, 0.02f);
    net.addRes(&wire_res_1, 0.01f, 0.02f);

    SelfHeatingMgr mgr(&net);
    mgr.buildViaConn();

    // wire_res_0 has node_mid which is connected via via_res to MOSFET pin
    // wire_res_1 has no connection to MOSFET pin
    // We can verify by running compute and checking alpha difference
    // For now we just verify it doesn't crash and the net has correct structure
    CHECK(net.reses().size() == 3);
    CHECK(net.reses().size() == 3);
}

// =============================================================================
// Test: SelfHeatingMgr compute (end-to-end)
// =============================================================================

static void testMgrComputeEndToEnd() {
    fprintf(stdout, "--- testMgrComputeEndToEnd ---\n");

    // --- Setup devices ---
    std::vector<SelfHeatingMosfet> mosfets(1);
    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 2.0f;
    mosfets[0].finger_num = 4;
    mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "OD";

    SelfHeatingDevMgr devMgr;
    devMgr.init(mosfets, 0, 0, 10, 10);

    std::map<std::string, DeviceLayerParams> dlp;
    DeviceLayerParams od;
    od.Rth = 1000.0;
    od.finger_effect = testFingerEffect;
    od.fin_effect = testFinEffect;
    dlp["OD"] = od;
    devMgr.build(dlp);

    float dev_deltaT = devMgr.getDevice(0).deltaT;
    CHECK(dev_deltaT > 0.0f);

    // --- Setup params ---
    SelfHeatingParams params;
    params.K_SH_Scale = 1.5;
    params.beta_c1 = 0.001;
    params.beta_c2 = 0.002;
    params.beta_c3 = 0.003;
    params.T_ambient = 25.0;
    params.device_layers = dlp;

    MetalLayerParams m1;
    m1.Rth = 500.0;
    m1.alpha_connecting = 0.5;
    m1.alpha_overlapping = 0.3;
    params.metal_layers["M1"] = m1;

    // --- Setup net ---
    // node_inst (type 'I') --[via_res]--> node_mid --[wire_res_conn]--> node_end
    // node_other --[wire_res_noconn]--> node_far  (no via to MOSFET)
    // Both wire res overlap the device at (0,0)-(10,10)

    EmirNodeInfo node_inst, node_mid, node_end, node_other, node_far;
    node_inst.setType('I');  node_inst.setX(0);  node_inst.setY(0);
    node_mid.setType('N');   node_mid.setX(5);   node_mid.setY(5);
    node_end.setType('N');   node_end.setX(10);  node_end.setY(10);
    node_other.setType('N'); node_other.setX(2);  node_other.setY(2);
    node_far.setType('N');   node_far.setX(8);   node_far.setY(8);

    EmirInfoMgr infoMgr;
    EmirLayerInfo m1_layer(0, "M1");  m1_layer._type = 0;
    EmirLayerInfo via_layer(1, "VIA1"); via_layer._type = 1;
    infoMgr.addLayer(&m1_layer);
    infoMgr.addLayer(&via_layer);

    EmirNetInfo net;
    net._mgr = &infoMgr;
    net.addNode(&node_inst);   // idx 0
    net.addNode(&node_mid);    // idx 1
    net.addNode(&node_end);    // idx 2
    net.addNode(&node_other);  // idx 3
    net.addNode(&node_far);    // idx 4

    EmirResInfo via_res;
    via_res._n1 = node_inst.idx();
    via_res._n2 = node_mid.idx();
    via_res.setLayerIdx(1);
    via_res.setBBox(0, 0, 5, 5);

    float avg_power = 0.01f;
    float rms_power = 0.02f;

    // wire_res_conn: connected to MOSFET via via -> alpha_connecting
    EmirResInfo wire_res_conn;
    wire_res_conn._n1 = node_mid.idx();
    wire_res_conn._n2 = node_end.idx();
    wire_res_conn.setLayerIdx(0);
    wire_res_conn.setBBox(0, 0, 10, 10);

    // wire_res_noconn: not connected -> alpha_overlapping
    EmirResInfo wire_res_noconn;
    wire_res_noconn._n1 = node_other.idx();
    wire_res_noconn._n2 = node_far.idx();
    wire_res_noconn.setLayerIdx(0);
    wire_res_noconn.setBBox(0, 0, 10, 10);

    net.addRes(&via_res);                              // index 0
    net.addRes(&wire_res_conn,    avg_power, rms_power); // index 1
    net.addRes(&wire_res_noconn, avg_power, rms_power); // index 2

    // --- Run ---
    SelfHeatingMgr mgr(&net);
    mgr.buildViaConn();
    mgr.compute(devMgr, params);

    // --- Verify ---
    // via_res (index 0): skipped, deltaT should remain 0
    CHECK_NEAR(net.getResEmParam(0)->_deltaT, 0.0f, 1e-6);

    // wire_res_conn (index 1): connected -> alpha_connecting = 0.5
    // overlap_ratio = 1.0 (wire res and device have identical bbox)
    double overlap_ratio_conn = 1.0;
    double beta_conn = params.beta_c1 * dev_deltaT
                     + params.beta_c2 * rms_power
                     + params.beta_c3;
    double deltaT_self_conn = m1.Rth * rms_power;
    double deltaT_feol_conn = overlap_ratio_conn * m1.alpha_connecting * beta_conn * dev_deltaT;
    double deltaT_total_conn = (deltaT_self_conn + deltaT_feol_conn) * params.K_SH_Scale;

    CHECK_NEAR(net.getResEmParam(1)->_deltaT, deltaT_total_conn, 0.01);

    // wire_res_noconn (index 2): not connected -> alpha_overlapping = 0.3
    // overlap_ratio = 1.0 (wire res and device have identical bbox)
    double overlap_ratio_noconn = 1.0;
    double beta_noconn = params.beta_c1 * dev_deltaT
                       + params.beta_c2 * rms_power
                       + params.beta_c3;
    double deltaT_self_noconn = m1.Rth * rms_power;
    double deltaT_feol_noconn = overlap_ratio_noconn * m1.alpha_overlapping * beta_noconn * dev_deltaT;
    double deltaT_total_noconn = (deltaT_self_noconn + deltaT_feol_noconn) * params.K_SH_Scale;

    CHECK_NEAR(net.getResEmParam(2)->_deltaT, deltaT_total_noconn, 0.01);

    // Connected should have higher deltaT than non-connected (alpha_connecting > alpha_overlapping)
    CHECK(net.getResEmParam(1)->_deltaT > net.getResEmParam(2)->_deltaT);

    fprintf(stdout, "  wire_res_conn  deltaT = %f (expected %f)\n",
            net.getResEmParam(1)->_deltaT, deltaT_total_conn);
    fprintf(stdout, "  wire_res_noconn deltaT = %f (expected %f)\n",
            net.getResEmParam(2)->_deltaT, deltaT_total_noconn);
}

// =============================================================================
// Test: partial overlap — overlap_ratio < 1.0
// =============================================================================

static void testMgrComputePartialOverlap() {
    fprintf(stdout, "--- testMgrComputePartialOverlap ---\n");

    // Device at (0,0)-(10,10)
    std::vector<SelfHeatingMosfet> mosfets(1);
    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 2.0f;
    mosfets[0].finger_num = 4;
    mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "OD";

    SelfHeatingDevMgr devMgr;
    devMgr.init(mosfets, 0, 0, 20, 10);

    std::map<std::string, DeviceLayerParams> dlp;
    DeviceLayerParams od;
    od.Rth = 1000.0;
    od.finger_effect = testFingerEffect;
    od.fin_effect = testFinEffect;
    dlp["OD"] = od;
    devMgr.build(dlp);

    float dev_deltaT = devMgr.getDevice(0).deltaT;

    SelfHeatingParams params;
    params.K_SH_Scale = 1.5;
    params.beta_c1 = 0.001;
    params.beta_c2 = 0.002;
    params.beta_c3 = 0.003;
    params.T_ambient = 25.0;
    params.device_layers = dlp;

    MetalLayerParams m1;
    m1.Rth = 500.0;
    m1.alpha_connecting = 0.5;
    m1.alpha_overlapping = 0.3;
    params.metal_layers["M1"] = m1;

    // Wire res at (5,0)-(20,10): partially overlaps device (0,0)-(10,10)
    // Intersection: (5,0)-(10,10) = 5*10 = 50
    // Wire res area: (20-5)*(10-0) = 15*10 = 150
    // overlap_ratio = 50/150 = 1/3
    EmirNodeInfo n1, n2;
    n1.setType('N'); n1.setX(5);  n1.setY(0);
    n2.setType('N'); n2.setX(20); n2.setY(10);

    EmirInfoMgr infoMgr;
    EmirLayerInfo m1_layer(0, "M1");  m1_layer._type = 0;
    infoMgr.addLayer(&m1_layer);
    EmirNetInfo net;
    net._mgr = &infoMgr;
    net.addNode(&n1);  // idx 0
    net.addNode(&n2);  // idx 1

    EmirResInfo wire_res;
    wire_res._n1 = n1.idx();
    wire_res._n2 = n2.idx();
    wire_res.setLayerIdx(0);
    wire_res.setBBox(5, 0, 20, 10);
    float avg_power = 0.01f;
    float rms_power = 0.02f;
    net.addRes(&wire_res, avg_power, rms_power);

    SelfHeatingMgr mgr(&net);
    mgr.buildViaConn();
    mgr.compute(devMgr, params);

    double overlap_ratio = 50.0 / 150.0;  // 1/3
    double beta = params.beta_c1 * dev_deltaT
                + params.beta_c2 * rms_power
                + params.beta_c3;
    double deltaT_self = m1.Rth * rms_power;
    double deltaT_feol = overlap_ratio * m1.alpha_overlapping * beta * dev_deltaT;
    double deltaT_total = (deltaT_self + deltaT_feol) * params.K_SH_Scale;

    CHECK_NEAR(net.getResEmParam(0)->_deltaT, deltaT_total, 0.01);

    // Verify overlap_ratio < 1.0 actually reduces FEOL contribution vs full overlap
    double deltaT_feol_full = 1.0 * m1.alpha_overlapping * beta * dev_deltaT;
    CHECK(deltaT_feol < deltaT_feol_full);

    fprintf(stdout, "  overlap_ratio = %f\n", overlap_ratio);
    fprintf(stdout, "  deltaT = %f (expected %f)\n",
            net.getResEmParam(0)->_deltaT, deltaT_total);
    fprintf(stdout, "  deltaT_feol partial = %f vs full = %f\n",
            deltaT_feol, deltaT_feol_full);
}

// =============================================================================
// Test: empty input
// =============================================================================

static void testEmptyInput() {
    fprintf(stdout, "--- testEmptyInput ---\n");

    // Empty mosfets
    std::vector<SelfHeatingMosfet> mosfets;
    SelfHeatingDevMgr devMgr;
    devMgr.init(mosfets, 0, 0, 100, 100);
    CHECK(devMgr.deviceCount() == 0);

    std::vector<int> results;
    std::vector<bool> visited;
    devMgr.queryOverlap(0, 0, 10, 10, results, visited);
    CHECK(results.size() == 0);

    // Empty net
    EmirInfoMgr infoMgr;
    EmirNetInfo net;
    net._mgr = &infoMgr;
    SelfHeatingMgr mgr(&net);
    mgr.buildViaConn();

    SelfHeatingParams params;
    mgr.compute(devMgr, params);
    CHECK(net.reses().size() == 0);
}

// =============================================================================
// Test: wire res with no metal layer params (should be skipped)
// =============================================================================

static void testMissingMetalLayer() {
    fprintf(stdout, "--- testMissingMetalLayer ---\n");

    std::vector<SelfHeatingMosfet> mosfets(1);
    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 1.0f; mosfets[0].finger_num = 1; mosfets[0].fin_num = 1;
    mosfets[0].layer_name = "OD";

    SelfHeatingDevMgr devMgr;
    devMgr.init(mosfets, 0, 0, 10, 10);

    std::map<std::string, DeviceLayerParams> dlp;
    DeviceLayerParams od;
    od.Rth = 1000.0;
    dlp["OD"] = od;
    devMgr.build(dlp);

    // params with no metal layer for "M2"
    SelfHeatingParams params;
    params.device_layers = dlp;
    MetalLayerParams m1;
    m1.Rth = 500.0;
    m1.alpha_connecting = 0.5;
    m1.alpha_overlapping = 0.3;
    params.metal_layers["M1"] = m1;
    // No "M2" entry

    EmirNodeInfo n1, n2;
    n1.setType('N'); n1.setX(0); n1.setY(0);
    n2.setType('N'); n2.setX(10); n2.setY(10);

    EmirInfoMgr infoMgr;
    EmirNetInfo net;
    net._mgr = &infoMgr;
    net.addNode(&n1);  // idx 0
    net.addNode(&n2);  // idx 1

    EmirResInfo wire_res;
    wire_res._n1 = n1.idx();
    wire_res._n2 = n2.idx();
    wire_res.setLayerIdx(1);
    wire_res.setBBox(0, 0, 10, 10);
    net.addRes(&wire_res, 0.01f, 0.02f);

    SelfHeatingMgr mgr(&net);
    mgr.buildViaConn();
    mgr.compute(devMgr, params);

    // deltaT should remain 0 since M2 is not in metal_layers
    CHECK_NEAR(net.getResEmParam(0)->_deltaT, 0.0f, 1e-6);
}

// =============================================================================
// Test: SelfHeatingMgr compute multi-threaded (same setup as end-to-end)
// =============================================================================

static void testMgrComputeMultiThread() {
    fprintf(stdout, "--- testMgrComputeMultiThread ---\n");

    // --- Setup devices (same as end-to-end) ---
    std::vector<SelfHeatingMosfet> mosfets(1);
    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 2.0f;
    mosfets[0].finger_num = 4;
    mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "OD";

    SelfHeatingDevMgr devMgr;
    devMgr.init(mosfets, 0, 0, 10, 10);

    std::map<std::string, DeviceLayerParams> dlp;
    DeviceLayerParams od;
    od.Rth = 1000.0;
    od.finger_effect = testFingerEffect;
    od.fin_effect = testFinEffect;
    dlp["OD"] = od;
    devMgr.build(dlp);

    float dev_deltaT = devMgr.getDevice(0).deltaT;

    // --- Setup params ---
    SelfHeatingParams params;
    params.K_SH_Scale = 1.5;
    params.beta_c1 = 0.001;
    params.beta_c2 = 0.002;
    params.beta_c3 = 0.003;
    params.T_ambient = 25.0;
    params.device_layers = dlp;

    MetalLayerParams m1;
    m1.Rth = 500.0;
    m1.alpha_connecting = 0.5;
    m1.alpha_overlapping = 0.3;
    params.metal_layers["M1"] = m1;

    // --- Setup net (same as end-to-end) ---
    EmirNodeInfo node_inst, node_mid, node_end, node_other, node_far;
    node_inst.setType('I');  node_inst.setX(0);  node_inst.setY(0);
    node_mid.setType('N');   node_mid.setX(5);   node_mid.setY(5);
    node_end.setType('N');   node_end.setX(10);  node_end.setY(10);
    node_other.setType('N'); node_other.setX(2);  node_other.setY(2);
    node_far.setType('N');   node_far.setX(8);   node_far.setY(8);

    EmirInfoMgr infoMgr;
    EmirLayerInfo m1_layer(0, "M1");  m1_layer._type = 0;
    EmirLayerInfo via_layer(1, "VIA1"); via_layer._type = 1;
    infoMgr.addLayer(&m1_layer);
    infoMgr.addLayer(&via_layer);

    EmirNetInfo net;
    net._mgr = &infoMgr;
    net.addNode(&node_inst);   // idx 0
    net.addNode(&node_mid);    // idx 1
    net.addNode(&node_end);    // idx 2
    net.addNode(&node_other);  // idx 3
    net.addNode(&node_far);    // idx 4

    EmirResInfo via_res;
    via_res._n1 = node_inst.idx();
    via_res._n2 = node_mid.idx();
    via_res.setLayerIdx(1);
    via_res.setBBox(0, 0, 5, 5);

    float avg_power = 0.01f;
    float rms_power = 0.02f;

    EmirResInfo wire_res_conn;
    wire_res_conn._n1 = node_mid.idx();
    wire_res_conn._n2 = node_end.idx();
    wire_res_conn.setLayerIdx(0);
    wire_res_conn.setBBox(0, 0, 10, 10);

    EmirResInfo wire_res_noconn;
    wire_res_noconn._n1 = node_other.idx();
    wire_res_noconn._n2 = node_far.idx();
    wire_res_noconn.setLayerIdx(0);
    wire_res_noconn.setBBox(0, 0, 10, 10);

    net.addRes(&via_res);
    net.addRes(&wire_res_conn,    avg_power, rms_power);
    net.addRes(&wire_res_noconn, avg_power, rms_power);

    // --- Run with numThreads=2 ---
    SelfHeatingMgr mgr(&net, 0, 2);
    mgr.buildViaConn();
    mgr.compute(devMgr, params);

    // --- Verify (same expected values as end-to-end) ---
    CHECK_NEAR(net.getResEmParam(0)->_deltaT, 0.0f, 1e-6);

    double overlap_ratio_conn = 1.0;
    double beta_conn = params.beta_c1 * dev_deltaT
                     + params.beta_c2 * rms_power
                     + params.beta_c3;
    double deltaT_self_conn = m1.Rth * rms_power;
    double deltaT_feol_conn = overlap_ratio_conn * m1.alpha_connecting * beta_conn * dev_deltaT;
    double deltaT_total_conn = (deltaT_self_conn + deltaT_feol_conn) * params.K_SH_Scale;

    CHECK_NEAR(net.getResEmParam(1)->_deltaT, deltaT_total_conn, 0.01);

    double overlap_ratio_noconn = 1.0;
    double beta_noconn = params.beta_c1 * dev_deltaT
                       + params.beta_c2 * rms_power
                       + params.beta_c3;
    double deltaT_self_noconn = m1.Rth * rms_power;
    double deltaT_feol_noconn = overlap_ratio_noconn * m1.alpha_overlapping * beta_noconn * dev_deltaT;
    double deltaT_total_noconn = (deltaT_self_noconn + deltaT_feol_noconn) * params.K_SH_Scale;

    CHECK_NEAR(net.getResEmParam(2)->_deltaT, deltaT_total_noconn, 0.01);

    CHECK(net.getResEmParam(1)->_deltaT > net.getResEmParam(2)->_deltaT);

    fprintf(stdout, "  [MT] wire_res_conn  deltaT = %f (expected %f)\n",
            net.getResEmParam(1)->_deltaT, deltaT_total_conn);
    fprintf(stdout, "  [MT] wire_res_noconn deltaT = %f (expected %f)\n",
            net.getResEmParam(2)->_deltaT, deltaT_total_noconn);
}

// =============================================================================
// Test: SelfHeatingDevMgr build multi-threaded
// =============================================================================

static void testDevMgrBuildMultiThread() {
    fprintf(stdout, "--- testDevMgrBuildMultiThread ---\n");

    // Create multiple devices across two layers
    std::vector<SelfHeatingMosfet> mosfets(4);

    mosfets[0].llx = 0; mosfets[0].lly = 0; mosfets[0].urx = 10; mosfets[0].ury = 10;
    mosfets[0].power = 2.0f; mosfets[0].finger_num = 4; mosfets[0].fin_num = 8;
    mosfets[0].layer_name = "OD";

    mosfets[1].llx = 20; mosfets[1].lly = 20; mosfets[1].urx = 30; mosfets[1].ury = 30;
    mosfets[1].power = 1.5f; mosfets[1].finger_num = 2; mosfets[1].fin_num = 6;
    mosfets[1].layer_name = "OD";

    mosfets[2].llx = 40; mosfets[2].lly = 40; mosfets[2].urx = 50; mosfets[2].ury = 50;
    mosfets[2].power = 3.0f; mosfets[2].finger_num = 4; mosfets[2].fin_num = 8;
    mosfets[2].layer_name = "OD";

    mosfets[3].llx = 60; mosfets[3].lly = 60; mosfets[3].urx = 70; mosfets[3].ury = 70;
    mosfets[3].power = 0.5f; mosfets[3].finger_num = 1; mosfets[3].fin_num = 10;
    mosfets[3].layer_name = "POLY";

    std::map<std::string, DeviceLayerParams> dlp;
    DeviceLayerParams od;
    od.Rth = 1000.0;
    od.finger_effect = testFingerEffect;
    od.fin_effect = testFinEffect;
    dlp["OD"] = od;

    DeviceLayerParams poly;
    poly.Rth = 800.0;
    poly.finger_effect = NULL;
    poly.fin_effect = NULL;
    dlp["POLY"] = poly;

    // Single-threaded reference (debug=0, numThreads=1)
    SelfHeatingDevMgr mgrST(0, 1);
    mgrST.init(mosfets, 0, 0, 70, 70);
    mgrST.build(dlp);

    // Multi-threaded (debug=0, numThreads=2)
    SelfHeatingDevMgr mgrMT(0, 2);
    mgrMT.init(mosfets, 0, 0, 70, 70);
    mgrMT.build(dlp);

    // Results must match exactly
    for (int i = 0; i < 4; ++i) {
        CHECK_NEAR(mgrMT.getDevice(i).deltaT, mgrST.getDevice(i).deltaT, 1e-6);
    }

    // Verify POLY device gets Rth=800 with default effects (1.0)
    CHECK_NEAR(mgrST.getDevice(3).deltaT, static_cast<float>(0.5 * 800.0), 1e-3);

    fprintf(stdout, "  ST vs MT deltaT: [%.4f, %.4f, %.4f, %.4f]\n",
            mgrST.getDevice(0).deltaT, mgrST.getDevice(1).deltaT,
            mgrST.getDevice(2).deltaT, mgrST.getDevice(3).deltaT);
}

// =============================================================================
// main
// =============================================================================

int main() {
    fprintf(stdout, "=== Self-Heating Test Suite ===\n\n");

    testDevMgrInit();
    testDevMgrBuild();
    testDevMgrBuildUnknownLayer();
    testDevMgrQueryOverlap();
    testMgrBuildViaConn();
    testMgrComputeEndToEnd();
    testMgrComputePartialOverlap();
    testEmptyInput();
    testMissingMetalLayer();
    testMgrComputeMultiThread();
    testDevMgrBuildMultiThread();

    fprintf(stdout, "\n=== Results: %d passed, %d failed ===\n",
            g_testsPassed, g_testsFailed);

    return (g_testsFailed > 0) ? 1 : 0;
}
