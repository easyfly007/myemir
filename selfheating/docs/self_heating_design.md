# Self-Heating 模块设计文档

## 1. 概述

### 1.1 项目背景

emir 是一个 EM/IR 分析工具，用于检查芯片互连线的电迁移（EM）和 IR drop 问题。

在实际芯片中，MOSFET 工作时产生的热量会通过介质向上传导到金属互连线，导致局部温度升高（self-heating effect）。温度升高会使允许的最大电流密度 Jmax 降低，从而影响 EM 判定结果。现有的 EM 检查使用统一的环境温度，未考虑 self-heating 导致的局部温升。

### 1.2 模块目标

Self-Heating 模块计算每条 wire res 在考虑自加热效应后的温升 deltaT，并将结果写入已有的 EM 参数结构（`ResEmParam._deltaT`），供后续 EM check 使用新温度重新判定。

### 1.3 当前状态

| 部分 | 状态 |
|------|------|
| 设计文档 | 完成 |
| emir 桩数据结构（emirnetinfo.h/.cc） | 完成 |
| SelfHeatingDevStr + SelfHeatingDevMgr 声明 | 完成（selfHeating.h） |
| SelfHeatingMosfet / SelfHeatingParams / SelfHeatingMgr 声明 | 待实现 |
| 所有 .cc 实现 | 待实现 |

## 2. 架构定位

selfheating 模块位于 EmirInfoMgr **之下**，由上层调用者传入数据：

- 直接使用 emir 已有的 `EmirNetInfo` / `EmirResInfo` / `EmirNodeInfo`
- MOSFET 数据来自独立的 manager，selfheating 通过 `SelfHeatingMosfet` 拷贝所需字段
- 计算结果写入已有的 `ResEmParam._deltaT`，与 EM check 解耦
- 模块不感知 EmirInfoMgr 或 MOSFET manager 的存在

```
上层调用者
  ├── EmirInfoMgr        — 管理所有 EmirNetInfo*（nets/res/node）
  ├── MosfetMgr（独立）  — 管理所有 MOSFET device
  └── 调用 selfheating，把两边的数据传进去
```

## 3. 代码约束

### 3.1 C++03 严格兼容

- 不用 `auto`、range-for、`override`、`= default` / `= delete`
- 不用 `std::function`、`unordered_map` / `unordered_set`
- 不用 `nullptr`（用 `NULL`）
- 不用 `std::unique_ptr` / `std::shared_ptr`（用 raw pointer）
- 不用 initializer lists、`constexpr`、move semantics
- 模板嵌套 `>>` 写成 `> >`（中间加空格）

### 3.2 命名规范

- 文件名：小写，`.h` + `.cc`（不用 `.cpp`）
- class 名：首字母大写（如 `SelfHeatingDevMgr`）
- class 成员：以 `_` 开头（如 `_devices`、`_net`）

### 3.3 文件结构

```
selfheating/
  docs/self_heating_design.md        — 本设计文档
  include/selfheating/
    emirnetinfo.h                    — emir 桩数据结构（简化版，集成时替换）
    selfHeating.h                    — selfheating 所有 struct/class 声明
  src/
    emirnetinfo.cc                   — emir 桩数据结构实现
    selfHeating.cc                   — selfheating 所有实现
```

## 4. 整体 Flow

```
路径1: SPICE 仿真
  → 1.1 计算 device 功耗
  → 1.2 计算 device 的 deltaT

路径2: Power net 仿真
  → 2.1 计算 wire res 的 deltaT（依赖 device deltaT）
  → 2.2 用新温度重新计算 wire res 的 EM limitation
```

两条路径在 Step 2.1 汇合：device 的 deltaT 作为输入参与 wire res deltaT 的计算。

### 计算步骤

1. **`devMgr.init(mosfets)`**：从外部 manager 拷贝 MOSFET 到紧凑结构 + 建 Uniform Grid 空间索引
2. **`devMgr.build(deviceLayerParams)`**：计算每个 device 的 deltaT
3. **Per-net 处理**：对每个 net 临时构造 `SelfHeatingMgr`，内部建 via 连接集合 + 计算所有 wire res 的 deltaT，结果写入 `ResEmParam._deltaT`
4. **EM check**（已有流程）：读 `ResEmParam` 做 EM 计算，自动拿到新的 deltaT

### 上层调用代码

```cpp
// =============================================
// 输入
// =============================================
// nets              - vector<EmirNetInfo*>，来自 EmirInfoMgr
// mosfets           - vector<SelfHeatingMosfet>，来自外部 MosfetMgr
// params            - SelfHeatingParams，来自 techfile 解析

// =============================================
// Phase 1: 建立 device 数据 + 空间索引
// =============================================
SelfHeatingDevMgr devMgr;
devMgr.init(mosfets);    // 拷贝到 SelfHeatingDevStr + 建 Uniform Grid

// =============================================
// Phase 2: 计算每个 device 的 deltaT
// =============================================
devMgr.build(params.device_layers);

// =============================================
// Phase 3: Per-net 处理 wire res
// =============================================
for (size_t n = 0; n < nets.size(); ++n) {
    SelfHeatingMgr mgr(nets[n]);
    mgr.buildViaConn();                 // 扫描本 net 的 via res，建连接集合
    mgr.compute(devMgr, params);        // 计算所有 wire res 的 deltaT，写入 ResEmParam._deltaT
}

// =============================================
// Phase 4: EM check（已有流程）
// =============================================
// 已有的 EM 计算逻辑读 ResEmParam，其中 _deltaT 字段已由 selfheating 填入
// 不做 selfheating 时 _deltaT 默认为 0，不影响已有逻辑
```

## 5. 输入数据

### 5.1 来自 emir 的数据（直接使用已有数据结构）

selfheating 模块使用 emir 已有的三个核心数据结构。本项目中提供了简化的桩实现（`emirnetinfo.h` / `emirnetinfo.cc`），集成时替换为真实 emir 头文件。

#### EmirNetInfo（每个 net）

```cpp
class EmirNetInfo {
public:
    const std::vector<EmirNodeInfo*>& nodes() const;
    const std::vector<EmirResInfo*>& reses() const;
    std::vector<ResEmParam>& resEmParams();     // 与 reses() 一一对应，相同 offset
    const std::vector<ResEmParam>& resEmParams() const;

    void addNode(EmirNodeInfo* n);
    void addRes(EmirResInfo* r);    // 同时向 _resEmParams push 一个默认值，保持同步
};
```

#### EmirResInfo（wire res 或 via res）

```cpp
class EmirResInfo {
public:
    float llx() const;              // bbox
    float lly() const;
    float urx() const;
    float ury() const;
    const std::string& layer() const;
    float resistance() const;
    float current() const;
    float avgPower() const;
    float rmsPower() const;
    EmirNodeInfo* n1() const;       // 两端节点
    EmirNodeInfo* n2() const;
    bool isVia() const;             // true = via res, false = wire res
};
```

#### EmirNodeInfo

```cpp
class EmirNodeInfo {
public:
    float x() const;
    float y() const;
    const std::string& layer() const;
    char type() const;              // 'I' = instance/MOSFET pin, 'N' = normal node
};
```

#### ResEmParam（EM 参数，per res）

已有结构，selfheating 在其中新增 `_deltaT` 字段：

```cpp
struct ResEmParam {
    float _td;
    float _duty;    // duty ratio
    float _deltaT;  // self-heating temperature rise（新增）
    ResEmParam():
        _td(std::numeric_limits<float>::quiet_NaN()),
        _duty(std::numeric_limits<float>::quiet_NaN()),
        _deltaT(0.0f){}
};
```

- `_deltaT` 默认 0，不做 self-heating 时对 EM 计算无影响
- selfheating 模块通过 `SelfHeatingMgr::compute` 写入此字段

### 5.2 MOSFET 数据（从外部 manager 拷贝）

外部提供每个 MOSFET 的 bbox、layer、power、finger_num、fin_num。selfheating 模块通过 `SelfHeatingMosfet` 接收，拷贝到内部紧凑结构 `SelfHeatingDevStr`，不绑定外部数据结构。

### 5.3 来自 Techfile (param.sh) 的参数

```
self_heat_parameters {

    // 全局参数
    K_SH_Scale = 1.23          // 最终 deltaT 的全局缩放因子

    beta_c1 = 0.0012           // beta 公式系数（全局，不分 layer）
    beta_c2 = 0.0023
    beta_c3 = 0.0034

    // FEOL device layer 参数
    device_layers {
        layer "OD" {
            Rth = 1000                                        // 热阻
            finger_effect = 2.0 * (1-exp(-0.3 * finger_num)) // finger 效应公式
            fin_effect = 1.0 - (0.018 * (10 - fin_num))      // fin 效应公式
        }
    }

    // Metal layer 参数
    layer "M1" {
        Rth = ...                    // 热阻
        alpha_connecting = 0.50      // 有 via 直接连接时的系数
        alpha_overlapping = 0.40     // 仅 bbox 交叠时的系数
    }

    layer "M2" {
        Rth = ...
        alpha_connecting = ...
        alpha_overlapping = ...
    }
}
```

## 6. 数据结构设计

### 6.1 SelfHeatingDevStr — MOSFET 紧凑存储

从外部 manager 拷贝 selfheating 所需字段。使用紧凑类型节省内存。

```cpp
struct SelfHeatingDevStr {
    float llx, lly, urx, ury;  // 16 bytes - bbox，用于 overlap 查询
    float power;                // 4 bytes
    float deltaT;               // 4 bytes（build 阶段计算写入）
    short finger_num;           // 2 bytes
    short fin_num;              // 2 bytes
    short layer_id;             // 2 bytes（映射到 layer name）
    short _pad;                 // 2 bytes padding -> 32 bytes total
};
```

- `layer_id` 通过 `std::vector<std::string>` 做 id → name 映射，避免 300 万个 string 分配
- 300 万个 SelfHeatingDevStr ≈ 96 MB

### 6.2 SelfHeatingMosfet — MOSFET 输入结构（暂定）

外部 manager 传入 selfheating 模块的 MOSFET 数据。此结构为暂定设计，后续根据外部 MosfetMgr 的实际接口进行调整。

```cpp
// 暂定结构，待对接外部 MosfetMgr 后调整
struct SelfHeatingMosfet {
    float llx, lly, urx, ury;  // bbox
    float power;                // 功耗
    short finger_num;
    short fin_num;
    std::string layer_name;     // device layer 名称
};
```

### 6.3 SelfHeatingDevMgr — MOSFET 管理器

管理所有 MOSFET 数据，内部维护 Uniform Grid 空间索引。对外只暴露 `init`、`build`、`queryOverlap`、`getDevice` 等接口。

#### 公开接口

```cpp
class SelfHeatingDevMgr {
public:
    SelfHeatingDevMgr();
    ~SelfHeatingDevMgr();

    // Phase 1: 拷贝外部 MOSFET 数据 + 建 Uniform Grid 空间索引
    void init(const std::vector<SelfHeatingMosfet>& mosfets);

    // Phase 2: 用 device layer 参数计算每个 device 的 deltaT
    void build(const std::map<std::string, DeviceLayerParams>& device_layers);

    // 查询与给定 bbox 重叠的 device 索引（调用方复用 vector）
    void queryOverlap(float llx, float lly, float urx, float ury,
                      std::vector<int>& results) const;

    // 访问 device
    int deviceCount() const;
    const SelfHeatingDevStr& getDevice(int idx) const;
    SelfHeatingDevStr& getDevice(int idx);
    const std::string& layerName(short layer_id) const;
};
```

#### 内部成员

```cpp
private:
    // Device 存储
    std::vector<SelfHeatingDevStr> _devices;

    // Layer name <-> id 映射
    std::vector<std::string> _layerNames;          // id -> name
    std::map<std::string, short> _layerNameToId;   // name -> id

    // Uniform Grid
    std::vector<std::vector<int> > _gridCells;  // _gridCells[cell_idx] = device indices
    float _originX, _originY;                   // 版图左下角
    float _cellSize;                            // cell 边长
    int _nx, _ny;                               // grid 列数、行数

    // 内部方法
    short _getOrCreateLayerId(const std::string& name);
    int _cellIndex(int gx, int gy) const;
```

#### init 实现逻辑

```cpp
void SelfHeatingDevMgr::init(const std::vector<SelfHeatingMosfet>& mosfets) {
    // 1. 拷贝数据到 _devices
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

    // 2. 计算版图范围
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

    // 3. 确定 grid 参数（~1000x1000）
    float width = max_x - min_x;
    float height = max_y - min_y;
    _cellSize = width / 1000.0f;
    if (height / 1000.0f > _cellSize) _cellSize = height / 1000.0f;
    if (_cellSize <= 0.0f) _cellSize = 1.0f;

    _nx = (int)((width / _cellSize) + 1);
    _ny = (int)((height / _cellSize) + 1);

    // 4. 建 grid，插入每个 device
    _gridCells.resize(_nx * _ny);
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
                _gridCells[_cellIndex(gx, gy)].push_back(i);
            }
        }
    }
}
```

#### build 实现逻辑

```cpp
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

        double finger_eff = lp.finger_effect
                            ? lp.finger_effect(dev.finger_num) : 1.0;
        double fin_eff = lp.fin_effect
                         ? lp.fin_effect(dev.fin_num) : 1.0;

        dev.deltaT = (float)(dev.power * lp.Rth / finger_eff / fin_eff);
    }
}
```

#### queryOverlap 实现逻辑

```cpp
void SelfHeatingDevMgr::queryOverlap(
    float llx, float lly, float urx, float ury,
    std::vector<int>& results) const
{
    results.clear();

    int gx0 = (int)((llx - _originX) / _cellSize);
    int gy0 = (int)((lly - _originY) / _cellSize);
    int gx1 = (int)((urx - _originX) / _cellSize);
    int gy1 = (int)((ury - _originY) / _cellSize);

    if (gx0 < 0) gx0 = 0;
    if (gy0 < 0) gy0 = 0;
    if (gx1 >= _nx) gx1 = _nx - 1;
    if (gy1 >= _ny) gy1 = _ny - 1;

    for (int gy = gy0; gy <= gy1; ++gy) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            const std::vector<int>& cell = _gridCells[_cellIndex(gx, gy)];
            for (size_t k = 0; k < cell.size(); ++k) {
                int idx = cell[k];
                const SelfHeatingDevStr& dev = _devices[idx];
                // 精确 bbox 交叠检查
                if (dev.urx > llx && dev.llx < urx &&
                    dev.ury > lly && dev.lly < ury) {
                    results.push_back(idx);
                }
            }
        }
    }

    // 去重（同一 device 可能出现在多个 cell 中）
    std::sort(results.begin(), results.end());
    results.erase(std::unique(results.begin(), results.end()), results.end());
}
```

#### Uniform Grid 设计说明

为大规模 overlap 查询设计（~3 亿 wire res × ~300 万 MOSFET）。

**原理**：
1. `init` 阶段确定版图范围，划分为均匀网格
2. 将每个 MOSFET 插入其 bbox 覆盖的所有 grid cell（一次性）
3. `queryOverlap` 时算出查询 bbox 覆盖哪些 grid cell，只检查这些 cell 里的 MOSFET

**关键参数**：
- cell 总数限制在 ~100 万（如 1000×1000）
- `cell_size = max(版图宽/1000, 版图高/1000)`
- 版图范围从 SelfHeatingDevStr 的 bbox 自动推算

**内存**：
- grid cell 存储：每个 cell 一个 `std::vector<int>`（device 索引），~24 MB
- 总内存（SelfHeatingDevStr + Grid）约 120-150 MB

### 6.4 SelfHeatingParams — Techfile 参数

```cpp
typedef double (*EffectFunc)(int);

struct DeviceLayerParams {
    double Rth;
    EffectFunc finger_effect;   // NULL 则默认 1.0
    EffectFunc fin_effect;      // NULL 则默认 1.0
};

struct MetalLayerParams {
    double Rth;
    double alpha_connecting;
    double alpha_overlapping;
};

struct SelfHeatingParams {
    double K_SH_Scale;
    double beta_c1, beta_c2, beta_c3;
    double T_ambient;
    std::map<std::string, DeviceLayerParams> device_layers;
    std::map<std::string, MetalLayerParams> metal_layers;
};
```

### 6.5 SelfHeatingMgr — Per-Net Wire Res 处理器

per-net 的临时处理器，构造时接收 `EmirNetInfo*`，内部完成 via 连接检测和 wire res deltaT 计算。

#### 设计说明

- **生命周期**：在上层 for 循环中临时构造，处理完一个 net 后销毁
- **via 连接关系是 net 内部的**：net 之间不会连接，因此 via 检测是 per-net 独立的
- **结果写入 `ResEmParam._deltaT`**：通过 `_reses` 和 `_resEmParams` 的相同 offset 对应

#### 公开接口

```cpp
class SelfHeatingMgr {
public:
    SelfHeatingMgr(EmirNetInfo* net);

    // 扫描本 net 的 via res，建连接集合
    void buildViaConn();

    // 计算所有 wire res 的 deltaT，写入 ResEmParam._deltaT
    void compute(const SelfHeatingDevMgr& devMgr,
                 const SelfHeatingParams& params);

private:
    EmirNetInfo* _net;
    std::set<const EmirResInfo*> _connectedRes;  // 通过 via 连接到 MOSFET 的 wire res 集合
};
```

#### buildViaConn 实现逻辑

One-hop 检测：扫描本 net 所有 via res，如果 via res 一端的 node type 为 'I'（MOSFET pin），则将另一端标记为 connected node。再扫描所有 wire res，如果 n1 或 n2 在 connected node 集合中，标记该 wire res 为 connected。

```cpp
void SelfHeatingMgr::buildViaConn() {
    _connectedRes.clear();

    // 1. 收集所有通过 via 一跳可达 type 'I' 节点的 node
    std::set<const EmirNodeInfo*> connectedNodes;
    const std::vector<EmirResInfo*>& reses = _net->reses();

    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (!res->isVia()) continue;

        const EmirNodeInfo* n1 = res->n1();
        const EmirNodeInfo* n2 = res->n2();

        if (n1->type() == 'I') connectedNodes.insert(n2);
        if (n2->type() == 'I') connectedNodes.insert(n1);
    }

    // 2. 扫描所有 wire res，如果 n1 或 n2 在 connectedNodes 中，标记为 connected
    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (res->isVia()) continue;

        if (connectedNodes.count(res->n1()) ||
            connectedNodes.count(res->n2())) {
            _connectedRes.insert(res);
        }
    }
}
```

#### compute 实现逻辑

```cpp
void SelfHeatingMgr::compute(
    const SelfHeatingDevMgr& devMgr,
    const SelfHeatingParams& params)
{
    const std::vector<EmirResInfo*>& reses = _net->reses();
    std::vector<ResEmParam>& emParams = _net->resEmParams();
    std::vector<int> overlap;  // 复用

    for (size_t r = 0; r < reses.size(); ++r) {
        EmirResInfo* res = reses[r];
        if (res->isVia()) continue;

        const std::string& wire_layer = res->layer();
        std::map<std::string, MetalLayerParams>::const_iterator mlp_it =
            params.metal_layers.find(wire_layer);
        if (mlp_it == params.metal_layers.end()) continue;
        const MetalLayerParams& mlp = mlp_it->second;

        // 1. 自身焦耳热
        double deltaT_self = mlp.Rth * res->avgPower();

        // 2. 底层 FEOL device 热传导
        double deltaT_feol = 0.0;

        double res_area = (double)(res->urx() - res->llx())
                        * (res->ury() - res->lly());
        if (res_area <= 0.0) continue;  // degenerate res, skip

        devMgr.queryOverlap(res->llx(), res->lly(),
                            res->urx(), res->ury(), overlap);

        for (int j = 0; j < (int)overlap.size(); ++j) {
            const SelfHeatingDevStr& dev = devMgr.getDevice(overlap[j]);

            // 计算交叠面积比例
            float inter_llx = (res->llx() > dev.llx) ? res->llx() : dev.llx;
            float inter_lly = (res->lly() > dev.lly) ? res->lly() : dev.lly;
            float inter_urx = (res->urx() < dev.urx) ? res->urx() : dev.urx;
            float inter_ury = (res->ury() < dev.ury) ? res->ury() : dev.ury;
            double inter_area = (double)(inter_urx - inter_llx)
                              * (inter_ury - inter_lly);
            double overlap_ratio = inter_area / res_area;

            // alpha: connected vs overlap-only
            double alpha = (_connectedRes.count(res))
                           ? mlp.alpha_connecting
                           : mlp.alpha_overlapping;

            // beta
            double beta = params.beta_c1 * dev.deltaT
                        + params.beta_c2 * res->rmsPower()
                        + params.beta_c3;

            deltaT_feol += overlap_ratio * alpha * beta * dev.deltaT;
        }

        // 3. 总温升，写入 ResEmParam
        double deltaT_total = (deltaT_self + deltaT_feol) * params.K_SH_Scale;
        emParams[r]._deltaT = (float)deltaT_total;
    }
}
```

## 7. 计算公式

### 7.1 计算 FEOL Device 的 deltaT

```
deltaT_device = P_device * Rth(device_layer) / finger_effect(finger_num) / fin_effect(fin_num)
```

- 由 `SelfHeatingDevMgr::build` 执行，结果存入 `SelfHeatingDevStr.deltaT`

### 7.2 计算 Wire Res 的 deltaT

#### 自身焦耳热

```
deltaT_self = Rth(wire_layer) * P_avg
```

#### 底层 FEOL Device 的热传导

对当前 wire res 通过 `queryOverlap` 查询到的每个 overlap MOSFET 求和：

```
deltaT_FEOL_total = Σ (overlap_ratio_i * alpha_i * beta_i * deltaT_device_i)
```

**overlap_ratio**（交叠面积比例）：
```
inter_area = (min(res_urx, dev_urx) - max(res_llx, dev_llx)) * (min(res_ury, dev_ury) - max(res_lly, dev_lly))
overlap_ratio = inter_area / res_area
```
- `res_area` = wire res 的 bbox 面积，在 overlap 循环前预计算；面积 ≤ 0 则跳过
- `overlap_ratio` 范围 (0, 1]，部分交叠时 < 1.0

**alpha**（由 `SelfHeatingMgr::buildViaConn` 判定）：
- `alpha_connecting`：wire res 通过 via 一跳连接到 MOSFET pin
- `alpha_overlapping`：仅 bbox 交叠

**beta**：
```
beta_i = beta_c1 * deltaT_device_i + beta_c2 * rmsPower_wire + beta_c3
```

#### 总 deltaT

```
deltaT_total = (deltaT_self + deltaT_FEOL_total) * K_SH_Scale
T_new = T_ambient + deltaT_total
```

- `deltaT_total` 写入 `ResEmParam._deltaT`
- EM check 使用 `T_new` 重新计算 Jmax

## 8. 当前阶段的简化

- 不做电热迭代收敛，仅做单次 thermal correction
- 不更新电阻值重新做 IR drop（温度变化仅影响 EM 结果）
- 不实现 wire res 之间的热传递
- MOSFET 之间不互相影响热量

## 9. 性能优化 TODO（目标规模：5 亿 res / 5000 万 MOSFET）

### P0 — 必须优先解决

- [ ] **去掉 compute() 中的无条件 debug 输出**
  - 位置：`selfHeating.cc` compute() 中的 3 处 `_net->debug(...)` 调用
  - 问题：5 亿 wire res + overlap pair → 10 亿+ 次 `vfprintf`，即使 stderr 重定向到 /dev/null，格式化解析的 CPU 开销仍可能占总时间 30-50%
  - 方案：加 debug flag 控制，默认关闭；或改用 buffered debug（参考 mtmq 的 EmirMtmqDebug）

- [ ] **Per-net 循环并行化**
  - 位置：上层调用代码 `for (size_t n = 0; n < nets.size(); ++n)` 串行循环
  - 问题：每个 net 独立处理，但完全串行，无法利用多核
  - 方案：接入 mtmq 线程池。`devMgr` 和 `params` 只读，天然线程安全；每个 net 的 `SelfHeatingMgr` 是独立临时对象，无竞争。注意 `queryOverlap` 的 `results` vector 需要 per-thread 复用

### P1 — 显著影响性能

- [ ] **queryOverlap() 去重改用 bitmap 替代 sort+unique**
  - 位置：`selfHeating.cc:191-192`
  - 问题：对 5 亿个 wire res 每个都调一次 sort+unique，累积开销巨大
  - 方案：分配一个 `vector<bool>` 或 bitmap（大小 = deviceCount），用 visited 标记去重，查询结束后清除标记（只清本次标记的位置，不 memset 全部）

- [ ] **_connectedRes 从 std::set 改为 O(1) 查找结构**
  - 位置：`selfHeating.h:175`，`selfHeating.cc:329`
  - 问题：`_connectedRes.count(res)` 在 overlap 内循环中调用，红黑树 O(log N) + cache miss。假设平均每个 wire res overlap 2 个 device，总调用 ~10 亿次
  - 方案：改用 `std::vector<bool>`（下标 = res 在 net 中的 offset），O(1) 查找

- [ ] **Uniform Grid 密度调整**
  - 位置：`selfHeating.cc:86-91`
  - 问题：固定 1000x1000 grid，5000 万 device → 平均 50 dev/cell；device 分布不均时热点 cell 可能有数千个 device，线性扫描退化
  - 方案：动态调整 grid 分辨率，使 `device_count / cell_count` 控制在 ~5-10。如 5000 万 device 可用 3000x3000 grid（900 万 cell）

### P2 — 有改善空间

- [ ] **metal_layers 查找从 string map 改为 layer_id + 数组**
  - 位置：`selfHeating.cc:293-295`
  - 问题：每个 wire res 做一次 `std::map<string,...>::find`，5 亿次 string 比较
  - 方案：为 wire res 也建 layer_id 映射（类似 SelfHeatingDevStr），用数组下标 O(1) 访问 MetalLayerParams

- [ ] **gridCells 从 vector<vector<int>> 改为 CSR 格式**
  - 位置：`selfHeating.h:139`
  - 问题：100 万个 `std::vector<int>` = 100 万次独立堆分配，内存碎片化严重，遍历 cache miss 高
  - 方案：两阶段构建——先统计每个 cell 的 device 数量，再用一个大的 `vector<int>` + offset 数组（CSR），堆分配从 100 万次降为 2 次

- [ ] **init() 接口优化降低峰值内存**
  - 位置：`selfHeating.cc:52`
  - 问题：5000 万 `SelfHeatingMosfet`（每个含 `std::string layer_name`）+ `_devices` vector 同时在内存，峰值 ~4-5 GB
  - 方案：改用 iterator/callback 接口逐个拷贝；或让 init 接受 raw pointer + size + layer name 数组，避免 5000 万个 string 对象

### P3 — 小改善

- [ ] **buildViaConn() 的 std::set 改为更高效结构**
  - 位置：`selfHeating.cc:239`
  - 问题：大 power net（百万级 res/node）下，`std::set<EmirNodeInfo*>` 的树操作 + 指针比较性能差
  - 方案：改用排序 vector + binary_search，或 hash set

## 10. 未来扩展方向

- 支持电热迭代收敛（温度 → 电阻更新 → 重新求解 → 新温度 → ...）
- Wire res 之间的热传递
- 瞬态温度分析
