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
    SelfHeatingMgr mgr(nets[n], 0, numThreads);  // debug=0, numThreads 控制并行度
    mgr.buildViaConn();                 // 扫描本 net 的 via res，建连接标记
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

    // 查询与给定 bbox 重叠的 device 索引（调用方复用 results 和 visited）
    void queryOverlap(float llx, float lly, float urx, float ury,
                      std::vector<int>& results,
                      std::vector<bool>& visited) const;

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

    // Uniform Grid — CSR (Compressed Sparse Row) format
    // All device indices packed into one contiguous array (_gridData).
    // _gridOffsets[i] = start index of cell i in _gridData.
    // Cell i owns _gridData[ _gridOffsets[i] .. _gridOffsets[i+1] ).
    std::vector<int> _gridData;      // device indices, all cells contiguous
    std::vector<int> _gridOffsets;   // size = numCells + 1
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

    // 4. 建 CSR grid（两遍扫描）
    //    Pass 1: 统计每个 cell 收到多少 device index → 转为 prefix sum 得到 _gridOffsets
    //    Pass 2: 再遍历所有 device，用 write cursor 写入 _gridData
    int numCells = _nx * _ny;
    _gridOffsets.assign(numCells + 1, 0);

    // Pass 1: count
    for (int i = 0; i < (int)_devices.size(); ++i) {
        // ... 计算 gx0/gy0/gx1/gy1 + clamp（同前）...
        for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx)
                ++_gridOffsets[_cellIndex(gx, gy)];
    }

    // 转为 prefix sum
    { int running = 0;
      for (int c = 0; c < numCells; ++c) {
          int count = _gridOffsets[c];
          _gridOffsets[c] = running;
          running += count;
      }
      _gridOffsets[numCells] = running;
    }

    _gridData.resize(_gridOffsets[numCells]);

    // Pass 2: fill（用 _gridOffsets 作为 write cursor）
    for (int i = 0; i < (int)_devices.size(); ++i) {
        // ... 计算 gx0/gy0/gx1/gy1 + clamp（同前）...
        for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx) {
                int ci = _cellIndex(gx, gy);
                _gridData[_gridOffsets[ci]++] = i;
            }
    }

    // 右移 _gridOffsets 恢复原始 prefix sum
    for (int c = numCells; c > 0; --c)
        _gridOffsets[c] = _gridOffsets[c - 1];
    _gridOffsets[0] = 0;
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
    std::vector<int>& results,
    std::vector<bool>& visited) const
{
    results.clear();

    int gx0 = (int)((llx - _originX) / _cellSize);
    // ... grid range + clamp 同前 ...

    for (int gy = gy0; gy <= gy1; ++gy) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            int ci = _cellIndex(gx, gy);
            // CSR access: cell ci owns _gridData[ _gridOffsets[ci] .. _gridOffsets[ci+1] )
            for (int k = _gridOffsets[ci]; k < _gridOffsets[ci + 1]; ++k) {
                int idx = _gridData[k];
                const SelfHeatingDevStr& dev = _devices[idx];
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

    // 只清理本次标记的位置（通常 0-5 个）
    for (size_t i = 0; i < results.size(); ++i)
        visited[results[i]] = false;
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

**内存**（CSR 格式）：
- `_gridData`：~50M × 4B = ~200 MB（所有 cell 的 device 索引连续存放）
- `_gridOffsets`：(numCells+1) × 4B ≈ 4 MB
- 堆分配仅 2 次（`_gridData` + `_gridOffsets`），无碎片化
- 总内存（SelfHeatingDevStr + Grid）约 300 MB

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
    // debug: 0=off, 1=summary, 2=verbose
    // numThreads: 1=serial, >1=parallel via mtmq thread pool
    SelfHeatingMgr(EmirNetInfo* net, int debug = 0, int numThreads = 1);

    // 扫描本 net 的 via res，建连接标记
    void buildViaConn();

    // 计算所有 wire res 的 deltaT，写入 ResEmParam._deltaT
    // numThreads>1 时将 res 范围分块，通过 mtmq RD 模式并行执行
    void compute(const SelfHeatingDevMgr& devMgr,
                 const SelfHeatingParams& params);

private:
    EmirNetInfo* _net;
    int _debug;
    int _numThreads;
    std::vector<bool> _isConnected;  // _isConnected[r] = true 表示 wire res r 通过 via 连接到 MOSFET
};
```

#### buildViaConn 实现逻辑

One-hop 检测：扫描本 net 所有 via res，如果 via res 一端的 node type 为 'I'（MOSFET pin），则将另一端标记为 connected node。再扫描所有 wire res，如果 n1 或 n2 在 connected node 集合中，将 `_isConnected[i]` 设为 true。

```cpp
void SelfHeatingMgr::buildViaConn() {
    const std::vector<EmirResInfo*>& reses = _net->reses();
    _isConnected.assign(reses.size(), false);

    // 1. 收集所有通过 via 一跳可达 type 'I' 节点的 node
    std::set<const EmirNodeInfo*> connectedNodes;

    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (!res->isVia()) continue;

        const EmirNodeInfo* n1 = res->n1();
        const EmirNodeInfo* n2 = res->n2();

        if (n1->type() == 'I') connectedNodes.insert(n2);
        if (n2->type() == 'I') connectedNodes.insert(n1);
    }

    // 2. 扫描所有 wire res，如果 n1 或 n2 在 connectedNodes 中，标记 _isConnected[i]
    for (size_t i = 0; i < reses.size(); ++i) {
        EmirResInfo* res = reses[i];
        if (res->isVia()) continue;

        if (connectedNodes.count(res->n1()) ||
            connectedNodes.count(res->n2())) {
            _isConnected[i] = true;
        }
    }
}
```

#### compute 实现逻辑

compute() 分为两条路径：单线程直接调用 `computeRange()`，多线程通过 mtmq RD 模式将 res 范围分块并行处理。

**静态辅助函数 computeRange()**：封装了单个 res 范围 [begin, end) 的 deltaT 计算逻辑。每次调用有独立的 `overlap` vector（多线程时各线程各自持有）。alpha 查找已提到 device j 循环外（P0 修复）。

```cpp
static void computeRange(
    size_t begin, size_t end,
    const std::vector<EmirResInfo*>& reses,
    std::vector<ResEmParam>& emParams,
    const SelfHeatingDevMgr& devMgr,
    const SelfHeatingParams& params,
    const std::vector<bool>& isConnected,
    int debug, EmirNetInfo* net)
{
    std::vector<int> overlap;  // per-thread local

    for (size_t r = begin; r < end; ++r) {
        EmirResInfo* res = reses[r];
        if (res->isVia()) continue;

        // ... metal layer lookup, deltaT_self 计算 ...

        // alpha 提到 j 循环外（P0 修复：同一 wire res 只查一次）
        double alpha = isConnected[r]
                       ? mlp.alpha_connecting
                       : mlp.alpha_overlapping;

        for (int j = 0; j < (int)overlap.size(); ++j) {
            // ... overlap_ratio, beta, contribution 计算 ...
        }

        emParams[r]._deltaT = (float)deltaT_total;
    }
}
```

**mtmq 辅助类型**（匿名 namespace）：

```cpp
struct SHComputeJob { size_t begin; size_t end; };

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
    virtual void run(void* job, EmirMtmqArg* arg) {
        SHComputeJob* j = static_cast<SHComputeJob*>(job);
        SHComputeArg* a = static_cast<SHComputeArg*>(arg);
        computeRange(j->begin, j->end, *a->reses, *a->emParams,
                     *a->devMgr, *a->params, *a->isConnected, a->debug, a->net);
    }
};
```

**compute() 主函数**：

```cpp
void SelfHeatingMgr::compute(
    const SelfHeatingDevMgr& devMgr,
    const SelfHeatingParams& params)
{
    const std::vector<EmirResInfo*>& reses = _net->reses();
    std::vector<ResEmParam>& emParams = _net->resEmParams();
    if (reses.empty()) return;

    if (_numThreads <= 1) {
        // 单线程：直接调用
        computeRange(0, reses.size(), reses, emParams, devMgr, params,
                     _isConnected, _debug, _net);
    } else {
        // 多线程：分块 + mtmq RD 模式
        size_t n = reses.size();
        size_t nThreads = (size_t)_numThreads;
        size_t chunkSize = (n + nThreads - 1) / nThreads;

        std::vector<SHComputeJob> jobs(nThreads);
        // ... 初始化每个 job 的 begin/end ...

        SHComputeArg arg;
        // ... 设置共享参数指针 ...

        EmirMtmqMgr mtmq(nThreads - 1);  // nThreads-1 个 worker + main = nThreads 总线程
        for (size_t t = 0; t < nThreads; ++t)
            mtmq.addLeafJob(&jobs[t]);
        mtmq.setArgument(&arg);
        mtmq.start();

        SHComputeTask task;
        mtmq.run(&task);  // 阻塞直到所有 chunk 完成
    }
}
```

**线程安全分析**：
- 共享只读：`devMgr`（const ref）、`params`（const ref）、`reses`（const vector）、`_isConnected`（const vector\<bool\>）
- Per-thread local：`overlap` vector、循环变量
- 写入无竞争：各线程写入 `emParams[r]._deltaT` 的索引范围不重叠

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

### 9.1 CPU 性能瓶颈

#### P0 — 必须优先解决

- [x] **~~去掉 compute() 中的无条件 debug 输出~~**（已完成）
  - 已通过 `_debug` flag 控制，`_debug=0` 时零开销

- [x] **~~`_connectedRes.count(res)` 在 overlap 内循环中冗余调用~~**（已完成）
  - 已将 alpha 查找提到 device j 循环外，改用 `_isConnected[r]`（O(1) vector\<bool\> 下标访问），每个 wire res 只查一次

- [x] **~~compute() 内部 wire res 并行化~~**（已完成）
  - 已通过 mtmq RD 模式实现。构造函数新增 `numThreads` 参数，`numThreads>1` 时将 res 范围均匀分块，各线程处理不重叠的 `[begin, end)` 区间
  - 核心逻辑提取到 `static computeRange()`，每个线程有独立的 `overlap` vector
  - 所有共享数据只读（devMgr、params、reses、_isConnected），写入 `emParams[r]._deltaT` 无竞争

#### P1 — 显著影响性能

- [x] **~~queryOverlap() 去重改用 bitmap 替代 sort+unique~~**（已完成）
  - 已将 queryOverlap 签名新增 `visited` 参数（`vector<bool>&`），调用方复用避免重复分配
  - 在 bbox 检查通过后用 `visited[idx]` 做 O(1) 去重，查询结束只清理 results 中的标记位
  - computeRange 中 per-thread 创建 visited（大小 = deviceCount），循环复用

- [x] **~~_connectedRes 从 std::set 改为 O(1) 查找结构~~**（已完成）
  - 已改为 `std::vector<bool> _isConnected`，下标 = res 在 net 中的 offset，O(1) 查找
  - 同时消除了 set 树节点的堆分配和内存开销

- [ ] **Uniform Grid 密度不足导致扫描过多 candidate**
  - 位置：`selfHeating.cc:86-91`
  - 问题：固定 1000×1000 grid（100 万 cell），5000 万 device → 平均 50 dev/cell。实际芯片 device 分布不均，热点 cell 可能有数千个 device。每次 queryOverlap 线性扫描 cell 内所有 device，预计 2.5 亿 query × 50 candidate = **125 亿次** bbox 比较
  - 方案：动态调整 grid 分辨率使平均 ~5-10 dev/cell。如 5000 万 device 用 3000×3000 grid（900 万 cell，平均 ~5.5 dev/cell），扫描量降低约 10 倍

#### P2 — 有改善空间

- [ ] **metal_layers 查找从 string map 改为 layer_id + 数组**
  - 位置：`selfHeating.cc:293-295`
  - 问题：每个 wire res 做一次 `std::map<string,...>::find`，metal_layers ~10 个 entry → 每次 ~3-4 次 string 比较。2.5 亿 × 3 = **7.5 亿次** string 比较
  - 方案：为 wire res 也建 layer_id 映射（类似 SelfHeatingDevStr），用数组下标 O(1) 访问 MetalLayerParams

- [ ] **buildViaConn() 的 connectedNodes std::set 操作**（部分改善）
  - `_connectedRes` 已改为 `vector<bool>`（见 P1），Pass 2 写入变为 O(1)
  - 剩余问题：`connectedNodes`（std::set\<EmirNodeInfo\*\>）仍为红黑树，Pass 1 的 insert 和 Pass 2 的 count 仍为 O(log N)
  - 方案：`connectedNodes` 改用排序 vector + binary_search

### 9.2 内存瓶颈

#### P0 — 可能导致 OOM

- [ ] **init() 峰值内存：输入与内部存储同时在内存**（非本模块可修复，仅记录）
  - 位置：`selfHeating.cc:52`
  - 量化：

    | 数据结构 | 计算 | 大小 |
    |----------|------|------|
    | 输入 `vector<SelfHeatingMosfet>` | 50M × (~20B POD + ~48B string) | ~3.4 GB |
    | `_devices` | 50M × 32B | 1.6 GB |
    | **init() 峰值** | 两者同时存在 | **~5.0 GB** |

  - C++03 下 `std::string` 无 SSO 保证，50M 个 `SelfHeatingMosfet.layer_name` = 50M 次堆分配
  - 说明：输入参数 `SelfHeatingMosfet` 为当前桩实现的暂定结构，实际集成时由上层 MosfetMgr 决定接口形式和数据生命周期，非 selfheating 模块所能控制。此处仅记录潜在风险，供集成时参考

#### P1 — 碎片化影响性能

- [x] **~~gridCells 从 vector\<vector\<int\>\> 改为 CSR 格式~~**（已完成）
  - 已改为 CSR 格式：`_gridData`（连续 device 索引）+ `_gridOffsets`（前缀和偏移表）
  - init() 使用两遍扫描（count → prefix sum → fill），堆分配从 ~600 万次降为 2 次
  - queryOverlap() 通过 `_gridOffsets[ci]..._gridOffsets[ci+1]` 范围遍历，数据连续存放，cache 友好

#### P2 — emir 侧问题（供参考）

- [ ] **EmirResInfo 的 `std::string _layer` — 5 亿个 string**
  - 位置：`emirNetInfo.h:95`（emir 数据，非 selfheating 拥有）
  - 量化：

    | 数据 | 计算 | 大小 |
    |------|------|------|
    | 500M `EmirResInfo` 对象 | 500M × ~90B（含 string） | ~45 GB |
    | 其中 `_layer` string | 500M × ~48B（C++03 string + heap） | ~24 GB |

  - 方案（emir 侧）：`EmirResInfo` 改用 layer_id (short) 代替 string，500M × 2B = 1 GB，节省 ~23 GB

#### P3 — 小影响

- [x] **~~_connectedRes / connectedNodes 的 set 树节点开销~~**（已完成）
  - `_connectedRes` 已改为 `vector<bool>`，树节点开销已消除
  - `connectedNodes` 仍为 `std::set`（仅 buildViaConn 中临时使用，规模为 connected node 数，远小于 res 数）

### 9.3 汇总表

| 优先级 | 类型 | 瓶颈 | 量化影响 |
|--------|------|------|----------|
| ~~P0~~ | ~~CPU~~ | ~~debug 无条件输出~~ | ~~已修复~~ |
| ~~P0~~ | ~~CPU~~ | ~~`_connectedRes.count` 内循环冗余~~ | ~~已修复：alpha 提到 j 循环外~~ |
| ~~P0~~ | ~~CPU~~ | ~~compute() 内 wire res 串行~~ | ~~已修复：mtmq RD 模式并行~~ |
| ~~P1~~ | ~~CPU~~ | ~~queryOverlap sort+unique~~ | ~~已修复：bitmap 去重替代 sort+unique~~ |
| ~~P1~~ | ~~CPU~~ | ~~`_connectedRes` 用 std::set~~ | ~~已修复：改为 vector\<bool\>~~ |
| P1 | CPU | grid 平均 50 dev/cell | 125 亿次 bbox 比较 |
| P2 | CPU | metal_layers string map | 7.5 亿次 string 比较 |
| P2 | CPU | buildViaConn set 操作 | 10 亿次 O(log N) + 堆分配 |
| P0 | 内存 | init() 峰值（非本模块可修复） | ~5 GB（上层接口决定） |
| ~~P1~~ | ~~内存~~ | ~~gridCells 碎片化~~ | ~~已修复：CSR 格式，2 次 malloc~~ |
| P2 | 内存 | emir 侧 500M string | ~24 GB（emir 侧） |
| ~~P3~~ | ~~内存~~ | ~~set 树节点 per-net~~ | ~~已修复：_connectedRes 改为 vector\<bool\>~~ |

## 10. 未来扩展方向

- 支持电热迭代收敛（温度 → 电阻更新 → 重新求解 → 新温度 → ...）
- Wire res 之间的热传递
- 瞬态温度分析
