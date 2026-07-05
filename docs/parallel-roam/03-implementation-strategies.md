# Implementation Strategies

## Classic CPU ROAM

### 目标

实现一个清晰、正确、接近经典教材逻辑的 CPU baseline。它的价值不是最快，而是可调试、可对照、能解释。

### 数据组织

初期可使用对象式节点与指针：

```cpp
struct ClassicTriNode
{
    ClassicTriNode* leftChild = nullptr;
    ClassicTriNode* rightChild = nullptr;

    ClassicTriNode* baseNeighbor = nullptr;
    ClassicTriNode* leftNeighbor = nullptr;
    ClassicTriNode* rightNeighbor = nullptr;

    TriangleDomain domainTriangle;
    float geometricError = 0.0f;
    uint16_t depth = 0;
    bool isLeaf = true;
    bool isActive = true;
};
```

后续可改成 index pool，但 Classic 版保留“传统对象式实现”特征，以凸显 Data-Oriented 版的差异。

### 更新流程

```text
Camera Update
    ↓
Recursive Error Evaluation
    ↓
Threshold / Priority-based Split-Merge Decision
    ↓
Neighbor-aware Split / Merge
    ↓
Leaf Triangle Traversal
    ↓
CPU Build Vertex / Index Data
    ↓
OpenGL Upload + Draw
```

### 实现重点

- 先保证 split 正确；
- 再加入 base neighbor；
- 再处理 forced split / diamond split；
- merge 可后置；
- 作为所有后续版本的视觉与误差对照基准。

### 验收标准

- 近处和地形变化大的区域明显细分；
- 远处保持粗网格；
- wireframe 可清晰展示 LOD；
- 基本无裂缝或裂缝风险可通过 debug view 定位；
- 能输出 active triangle count、split count、max depth。

## Data-Oriented CPU ROAM

### 目标

保留 ROAM 的核心语义，但用现代 CPU 友好的数据布局和任务划分重构。这里的 Data-Oriented 不是要求使用 Unity DOTS，而是强调连续内存、批处理和减少指针跳转。

### 关键原则

不是“每个三角形都是一个 ECS Entity”，而是：

> Terrain Chunk 是高层对象；Triangle Node 是 chunk 内部连续存储的数据。

首版可以不做 chunk，只做全局 node pool。chunk 更适合作为性能优化或大场景扩展。

### 节点数据布局

使用 index-based SoA：

```cpp
struct RoamNodeSoA
{
    std::vector<uint32_t> leftChild;
    std::vector<uint32_t> rightChild;

    std::vector<uint32_t> baseNeighbor;
    std::vector<uint32_t> leftNeighbor;
    std::vector<uint32_t> rightNeighbor;

    std::vector<TriangleDomain> domainTriangle;

    std::vector<float> geometricError;
    std::vector<float> screenError;

    std::vector<uint16_t> depth;

    std::vector<uint8_t> isActive;
    std::vector<uint8_t> isLeaf;
    std::vector<uint8_t> splitFlag;
    std::vector<uint8_t> mergeFlag;
    std::vector<uint8_t> forcedSplitFlag;
};
```

进阶优化：

- 将多个 flag 打包到 bitfield；
- 将 `TriangleDomain` 改为隐式坐标表达，减少内存占用；
- 预分配 node pool，避免运行期频繁 `new/delete`；
- 使用 thread-local buffer，避免共享 vector 锁竞争。

### 任务划分

```text
1. ErrorEvaluationSystem
   - 并行
   - 对 active leaf 计算 screen-space error

2. LodDecisionSystem
   - 并行
   - 标记 split / merge candidate

3. NeighborConstraintSystem
   - 分轮传播或分块处理
   - 标记 forced split
   - 限制相邻深度差

4. TopologyCommitSystem
   - 初版串行
   - 后期可按 Terrain Chunk 分区并行
   - 真正执行 split / merge，更新邻接关系

5. ActiveLeafCollectSystem
   - 并行
   - thread-local buffer 收集 leaf index
   - 最后合并为 render list

6. MeshBuildSystem
   - 并行构建顶点/索引或实例化数据
```

### 预期收益与限制

预期收益：

- 连续内存与更好 cache locality；
- error evaluation / candidate marking 可利用多核；
- 避免指针跳转；
- 通过 batch 处理减少函数调用与分支成本；
- 为 GPU SSBO 布局提供一致的数据模型。

限制：

- topology commit 仍受邻接关系和 split/merge 依赖限制；
- 小场景可能因为线程调度开销变慢；
- 多线程收益需要通过阶段计时证明，不能只看总 FPS。

### 线程数实验定位

线程数测试只作为 Data-Oriented 版内部的可扩展性分析，不作为项目主线。

建议测试：

```text
1 / 2 / 4 / 8 / 16 / 32 workers
```

主实验仍然是：

```text
Classic CPU vs Data-Oriented CPU vs GPU ROAM-like
```

## GPU ROAM-like

### 目标

将最适合大规模并行的阶段迁移到 GPU，研究 ROAM 在 GPU 管线下的适配边界。

GPU 版不必执着于“原样复刻经典 ROAM 的全局优先队列”。项目表述建议为：

> 保留 ROAM 的二叉三角树、视点相关自适应细分、屏幕空间误差和无裂缝约束思想，以 GPU-friendly 的批量阈值决策替代经典串行优先队列。

### 分级交付

```text
Level A：GPU error + CPU topology + GPU render
Level B：GPU error + GPU candidate + CPU topology + indirect draw 可选
Level C：GPU split-only
Level D：GPU split/merge + crack-free dependency propagation
```

建议主线目标定为 Level B。Level C/D 是冲刺，不应阻塞报告完成。

### GPU Phase 1：误差评估

```text
Input:
- Height Map Texture
- Triangle Node SSBO
- Camera Data UBO

Compute:
- active leaf triangle 的 screen-space error

Output:
- screenError buffer
```

### GPU Phase 2：候选标记

```text
Input:
- screenError
- splitThreshold / mergeThreshold

Compute:
- splitFlag / mergeFlag

Output:
- candidate flags
```

### GPU Phase 3：活动叶节点收集

```text
Input:
- isActive / isLeaf

Compute:
- SSBO append 或 atomic counter
- activeLeafIndices

Output:
- active leaf list
```

### GPU Phase 4：间接绘制

```text
activeLeafIndices
    ↓
GPU-generated draw command
    ↓
DrawArraysIndirect / DrawElementsIndirect
```

### GPU Phase 5：GPU 拓扑更新

这是冲刺目标：

- GPU side split / merge；
- GPU memory pool；
- 邻接依赖传播；
- forced split；
- 多轮 Compute Pass；
- 更完整的 GPU-resident tree。

### 预期难点

- 动态拓扑不规则；
- 线程间同步；
- 邻接依赖；
- node allocation / recycle；
- GPU debug 困难；
- merge 的一致性维护；
- CPU readback 会影响性能结论，需要在实验中单独标注。

只要 Level B 或 C 完成，就已经具备明确 GPU 化对比价值。

