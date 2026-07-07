# 基于现代 CPU / GPU 的 ROAM 地形 LOD 算法实现与性能分析报告

## 第一章 绪论

### 1.1 项目背景

我最开始选择这个题目，是因为地形 LOD 这个问题看起来很“经典”，但它其实并没有过时。很多传统地形算法是在单核 CPU 或者早期图形硬件环境下提出的，当时的主要目标是尽量少画三角形、尽量减少 CPU 计算量，让程序能在当时的机器上跑起来。可是现在的硬件环境已经变化很大了：桌面 CPU 已经普遍进入多核心时代，高端 CPU 不只是频率更高，也有更多核心、更复杂的缓存层级；GPU 的发展更明显，现代 GPU 可以用成百上千个执行单元去处理大量结构相似的任务。也就是说，一个算法即使在传统意义上是“优秀算法”，也不代表它天然就能很好地利用现代硬件。

地形 LOD 仍然是现代游戏开发里很重要的性能问题。开放世界游戏越来越常见，玩家也越来越习惯在一个大地图里自由移动。开放世界意味着可见范围大、地形面积大、内容密度高，渲染系统必须在“看起来足够细”和“跑得足够快”之间做平衡。近年的开放世界游戏研究也说明，AAA 开放世界内容规模非常大，有论文对 20 个 AAA 开放世界游戏、约 2200 个任务进行了结构分析，这从另一个角度说明开放世界游戏已经不只是“地图大”，而是地图、任务、探索和内容组织都变得更复杂。这样的趋势会继续放大地形、植被、建筑、阴影和流式加载等系统的压力。

ROAM，也就是实时最优自适应网格（Real-time Optimally Adapting Meshes），是一个很有代表性的连续地形 LOD 算法。它通过二叉三角树不断分裂和合并，让靠近相机或者误差较大的地方保留更多三角形，远处或者平坦区域减少三角形。它的思想很优雅，也很符合“按需细分”的直觉。不过 ROAM 的经典实现里有很多指针、递归、邻接关系和拓扑修补，这些部分在现代 CPU 缓存和 GPU 并行模型下不一定友好。换句话说，ROAM 本身不是问题，问题在于传统写法是否还适合今天的硬件。

与此同时，数据导向设计，也就是 DOD，是现代游戏开发中非常重要的一种工程思想。它和传统面向对象设计不同，不是先从“对象是什么”出发，而是先考虑“数据如何被连续访问、如何被批量处理、如何减少缓存未命中”。在游戏引擎里，ECS 和 SoA 数据布局都和这种思想关系很深。它的目标不是让代码看起来更像现实世界，而是让 CPU 更舒服地读取数据、让多线程更容易分工。

GPU 编程则是另一个方向。OpenGL 4.3 开始正式把计算着色器和着色器存储缓冲区对象等能力放到图形 API 里，这让开发者可以不通过传统顶点/片元管线，也能把通用计算任务放到 GPU 上执行。对于重复性强、数据量大、分支相对可控的任务，计算着色器很适合。ROAM 中的误差计算、候选标记、活跃叶节点扫描和网格输出都看起来有一定并行潜力，所以我也想试试看，把 ROAM 中适合并行的阶段迁移到 GPU 后，实际系统性能会不会真的变好。

### 1.2 选题意义

本项目不是想证明“ROAM 已经过时”，也不是想声称我完全发明了一个新的地形 LOD 算法。我的目标更具体：我想把经典 CPU ROAM、数据导向 CPU ROAM 和 GPU 类 ROAM 三个版本放在同一个工程、同一套参数、同一条相机路径下进行对比，观察 ROAM 这个传统算法在现代硬件上的不同表现。

我比较关心的问题有三个。第一个问题是，经典 ROAM 的传统语义到底能不能稳定实现，它作为基准版本是否足够可靠。第二个问题是，如果把基于指针的节点改成基于索引的节点池，并把活跃叶节点扫描、误差评估、候选标记和网格输出等阶段改成更适合 CPU 缓存与多线程的方式，DOD 版本到底能获得多少收益。第三个问题是，GPU 真的能让 ROAM 变快吗，还是说 GPU 计算本身很快，但 CPU-GPU 数据交界会抵消收益。

这个对比的意义在于，它不是只看“最终帧率谁高”，而是拆开看算法不同阶段的代价。比如 ROAM 的误差评估可以并行，网格输出也可以并行，但分裂 / 合并的拓扑约束和无裂缝邻接修复就不一定适合无脑并行。通过这样的实验，我希望报告最后能给出一个比较朴素但有用的结论：传统地形 LOD 算法不是简单地“搬到 GPU 就会更快”，而是要看算法的哪些阶段真的适合现代硬件。

### 1.3 国内外研究现状

ROAM 的原始论文《ROAMing Terrain: Real-time Optimally Adapting Meshes》发表于 1997 年。它提出的核心思想是把规则网格地形组织成二叉三角树，用优先队列驱动分裂和合并，并通过菱形结构维护相邻三角形之间的连续性。这个算法在当时非常有意义，因为它能用相对少的三角形表达复杂地形，并且能根据视点变化动态调整网格。

后来地形 LOD 继续发展，出现了分块 LOD、几何 Clipmap、CDLOD、基于 Clipmap 的地形渲染等方案。和 ROAM 相比，这些方法往往更偏向块级处理或者规则结构，牺牲一部分局部最优细分能力，换取更好的批处理、更稳定的 GPU 渲染和更简单的数据流。也正因为如此，ROAM 在现代实时游戏引擎里不是最流行的地形方案，但它作为研究对象仍然很有价值，因为它把“误差驱动细分”和“拓扑连续性维护”这两个问题展示得很清楚。

DOD 和 ECS 的相关研究主要关注数据布局、并行执行和缓存友好性。近年的 ECS 研究会讨论任务调度、并发执行和组件数据访问方式，这和我项目中的 DOD ROAM 思路是相通的。我的 DOD 版本并不是完整 ECS，但它吸收了类似思想：节点不再主要靠对象指针互相连接，而是放在基于索引的节点池中；一些阶段不再一个节点一个节点地递归处理，而是把活跃叶节点、候选项和输出网格当作批量数据来处理。

GPU 方向上，计算着色器给图形程序提供了更灵活的计算入口。Khronos 在 OpenGL 4.3 发布说明中明确把计算着色器和着色器存储缓冲区对象作为重要增强能力，这正好对应本项目 GPU 版本中使用的 SSBO、计数缓冲区、计时查询和间接绘制。GPU 的优势是大规模并行吞吐，但限制是 CPU-GPU 同步、数据上传、读回等待和拓扑写冲突。我的 GPU 类 ROAM 版本也正是在这些限制中不断调整出来的：它证明了一些阶段能迁移到 GPU，但也暴露了完整 ROAM 拓扑并不容易完全 GPU 化。

### 1.4 主要功能概述

本项目最终实现的是一个可以实时运行的 C++ OpenGL 地形渲染程序。我基于 SDL2 创建窗口和 OpenGL 上下文，用 GLM 处理相机和矩阵，用 Dear ImGui 做运行时参数面板，用 OpenGL 完成地形网格绘制。程序支持高度图地形加载、贴图采样、基础光照、线框显示、LOD 调试着色显示，也可以在运行时切换经典、DOD 和 GPU 三种算法。

算法方面，我实现了经典 CPU ROAM、数据导向 CPU ROAM 和 GPU 类 ROAM 三个版本。经典版本保留传统二叉三角树、分裂 / 合并、基边邻居、菱形强制分裂和拓扑验证。DOD 版本把节点池改成基于索引的结构，并对误差评估、候选收集、部分拓扑提交和网格输出做多线程优化。GPU 版本在 DOD CPU 拓扑基准的基础上，加入 GPU 活跃叶节点压缩、误差评估、候选标记、仅分裂实验、GPU 网格输出和间接绘制。

为了让性能分析更可靠，我还实现了运行时基准测试。这个测试会让相机沿固定路径移动，按经典、DOD、GPU 的顺序运行同一组参数，并自动输出 Markdown 和 CSV。输出指标包括帧耗时、LOD 耗时、CPU 更新耗时、CPU 上传耗时、GPU 计算耗时、GPU 快照构建耗时、缓冲区分配耗时、调度墙钟耗时、查询等待、读回等待、三角形数、节点数、CPU 工作线程数和 CPU 利用率。后面的第四章就是基于这些数据进行分析。

## 第二章 相关技术与理论基础

这一章只讨论项目背后的技术和理论，不展开具体代码修改过程。为了后面分析三种实现方式时不混乱，本章先把地形如何由高度图变成三维网格、ROAM 如何根据误差细分、DOD 为什么能改善 CPU 访问模式、计算着色器为什么适合批量阶段，以及渲染时用到的基本矩阵和光照公式说明清楚。

### 2.1 ROAM 的核心原理

ROAM 处理的地形一般可以抽象为一张高度图。设高度图的离散大小为 \(W \times H\)，第 \(i,j\) 个采样点的高度为 \(h_{ij}\)。为了让算法和具体图片分辨率解耦，可以先把地形定义在二维参数域

\[
\Omega = [0,1] \times [0,1]
\]

上。参数坐标 \(u,v\) 与高度图像素坐标之间的关系可以写成

\[
x = u(W-1), \qquad z = v(H-1)
\]

因为 \(x,z\) 不一定刚好落在整数像素上，所以高度值通常用双线性插值取得。令

\[
i=\lfloor x \rfloor,\quad j=\lfloor z \rfloor,\quad \alpha=x-i,\quad \beta=z-j
\]

则高度函数可以近似为

\[
\begin{aligned}
H(u,v) = &(1-\alpha)(1-\beta)h_{ij}
        + \alpha(1-\beta)h_{i+1,j} \\
        &+ (1-\alpha)\beta h_{i,j+1}
        + \alpha\beta h_{i+1,j+1}
\end{aligned}
\]

把二维参数域映射到三维世界空间时，可以设地形边长为 \(L\)，高度缩放为 \(A\)，于是一个点的世界坐标为

\[
P(u,v)=
\begin{bmatrix}
(u-\frac{1}{2})L\\
A \cdot H(u,v)\\
(v-\frac{1}{2})L
\end{bmatrix}
\]

这里把 \(u,v\) 都减去 \(\frac{1}{2}\)，是为了让地形中心落在世界坐标原点附近。这个映射也说明了 ROAM 的一个特点：算法维护的三角形拓扑首先是在二维参数域里的三角形，真正绘制时才通过高度图采样变成起伏的三维地形。

ROAM 一开始不需要把整张高度图的所有小格子都画出来，而是用两个根三角形覆盖整个正方形参数域。可以把它们写成

\[
T_0 = ((0,0),(1,0),(0,1)), \qquad
T_1 = ((1,1),(0,1),(1,0))
\]

这两个三角形合起来刚好覆盖 \(\Omega\)。之后每个三角形都可以沿基边二分。设三角形的基边端点为 \(b_0,b_1\)，顶点为 \(a\)，基边中点为

\[
m=\frac{b_0+b_1}{2}
\]

则一次分裂会生成两个子三角形：

\[
T_L=(a,b_0,m), \qquad T_R=(b_1,a,m)
\]

每分裂一次，三角形面积约减半。若根三角形深度为 \(0\)，深度为 \(d\) 的三角形面积大约是根三角形面积的

\[
A_d = \frac{A_0}{2^d}
\]

所以 ROAM 的二叉三角树本质上是在用“局部继续二分”的方式，把细节集中到更需要的区域，而不是全局统一提高网格分辨率。

决定一个三角形是否需要继续分裂，关键是误差估计。最基础的想法是比较“真实高度图表面”和“当前三角形线性近似表面”的差距。设三角形 \(T\) 的三个二维顶点为 \(q_0,q_1,q_2\)，任意一点 \(q \in T\) 都可以用重心坐标表示：

\[
q=\lambda_0q_0+\lambda_1q_1+\lambda_2q_2,\qquad
\lambda_0+\lambda_1+\lambda_2=1,\quad \lambda_i\ge 0
\]

如果只看当前三角形的三个顶点，那么它对 \(q\) 点高度的线性估计为

\[
\hat{H}(q)=\lambda_0H(q_0)+\lambda_1H(q_1)+\lambda_2H(q_2)
\]

真实高度是 \(H(q)\)，所以三角形在参数域内的几何误差可以定义为

\[
e_g(T)=\max_{q\in T}|H(q)-\hat{H}(q)|
\]

这个定义最完整，但实际计算不可能每次遍历三角形内的无限多个点。因此常见做法是取有限采样点近似，比如三条边的中点和三角形重心。设采样集合为

\[
S_T=\left\{\frac{q_0+q_1}{2},\frac{q_1+q_2}{2},\frac{q_2+q_0}{2},\frac{q_0+q_1+q_2}{3}\right\}
\]

则近似误差为

\[
\tilde{e}_g(T)=\max_{q\in S_T}|H(q)-\hat{H}(q)|
\]

几何误差还不是最终视觉误差，因为同样的高度差在近处更明显，在远处可能几乎看不出来。透视投影下，一个世界空间长度 \(\Delta y\) 在屏幕上的近似像素高度可以由相似三角形推出。若相机到该点的视空间深度为 \(z_e\)，垂直视场角为 \(\theta\)，屏幕高度为 \(R_y\)，则投影比例为

\[
f_y=\frac{R_y}{2\tan(\theta/2)}
\]

世界空间高度误差为

\[
e_w(T)=A\cdot \tilde{e}_g(T)
\]

于是屏幕空间误差近似为

\[
e_s(T)\approx \frac{e_w(T)\cdot f_y}{\max(z_e,\epsilon)}
\]

其中 \(\epsilon\) 是一个很小的正数，用来避免相机贴近地形时分母趋近于零。这个公式的直观含义是：误差越大，越应该分裂；离相机越近，也越应该分裂。

只使用高度误差时还有一个小问题：如果某片区域非常平坦，\(\tilde{e}_g(T)\) 会很小，即使它离相机很近，也可能长期保持很大的三角形。对于地形渲染来说，近处三角形过大可能导致线框调试效果粗糙，也可能影响光照法线和视觉连续性。因此还可以把三角形边长的投影规模加入判断。设三角形最长边世界长度为

\[
l_w(T)=\max(\|P(q_0)-P(q_1)\|,\|P(q_1)-P(q_2)\|,\|P(q_2)-P(q_0)\|)
\]

则它的屏幕投影长度近似为

\[
l_s(T)\approx \frac{l_w(T)\cdot f_y}{\max(z_e,\epsilon)}
\]

综合误差可以写成

\[
E(T)=\max(w_h e_s(T), w_l l_s(T))
\]

其中 \(w_h\) 控制高度误差权重，\(w_l\) 控制近处网格密度权重。这个公式不是为了替代 ROAM 的原始思想，而是把“地形起伏”和“屏幕尺寸”统一成一个更容易比较的分裂分数。

和本项目的具体实现对齐时，上面的公式被进一步简化为一个不直接依赖屏幕像素高度的评分。设三角形中心到相机的世界距离为 \(d\)，距离权重为 \(s_d\)，代码中的最小距离保护为 \(\epsilon=0.05\)，最长边权重为 \(0.20\)，则实际评分可以理解为

\[
score(T)=\max\left(
\frac{A\tilde{e}_g(T)\cdot s_d}{\max(d,0.05)},
\frac{0.20\cdot l_w(T)}{\max(d,0.05)}
\right)
\]

这个式子保留了屏幕误差的核心趋势：高度误差越大，分数越高；三角形离相机越近，分数越高；近处平坦区域虽然高度误差小，但最长边项仍然会推动它继续细分。后面第四章中“距离权重”对三角形规模影响明显，原因也可以从这个分母和权重项里看出来。

ROAM 还要处理分裂和合并的稳定性。若只设置一个阈值 \(\tau\)，当 \(E(T)\) 在阈值附近上下波动时，三角形可能一帧分裂、下一帧合并，画面会出现抖动。更稳定的方式是设置两个阈值：

\[
\tau_{merge}<\tau_{split}
\]

判断规则可以写成

\[
\begin{cases}
E(T)>\tau_{split}, & \text{分裂}\\
E(T)<\tau_{merge}, & \text{合并}\\
\tau_{merge}\le E(T)\le \tau_{split}, & \text{保持上一帧状态}
\end{cases}
\]

这就是迟滞控制。它牺牲了一点点即时响应性，但换来更稳定的 LOD 变化。

最后还要说明 ROAM 的无裂缝约束。两个相邻三角形共享一条边时，如果其中一个三角形沿这条边产生了中点，而另一个三角形没有对应中点，就会形成 T 形裂缝。设三角形 \(T\) 的基边邻居为 \(B(T)\)。当 \(T\) 准备沿基边分裂时，合法条件可以写成

\[
B(T)=\varnothing \quad \text{或} \quad T \text{ 与 } B(T) \text{ 构成可同时分裂的菱形}
\]

如果这个条件不满足，就需要先递归处理基边邻居，使两侧共享同一个中点。也就是说，ROAM 的分裂并不是一个完全独立的局部操作，而是带有拓扑依赖：

\[
Split(T)\Rightarrow Split(B(T)) \quad \text{当基边邻居层级不匹配时}
\]

这也是 ROAM 比普通“按距离细分”更复杂的地方。误差公式决定“哪里需要更多细节”，而基边邻居和菱形关系决定“怎样细分才不会裂开”。

### 2.2 数据导向设计的理论基础

数据导向设计，也就是 DOD，关注的不是先把程序抽象成很多对象，而是先看数据会怎样被读取、怎样被写入、怎样批量处理。对于 ROAM 来说，一个节点可能包含三角形顶点、深度、父子关系、邻居关系、误差值、是否叶节点等字段。传统对象式写法常常把这些字段放在同一个节点对象里，多个节点再通过指针连接。这样表达关系很直观，但 CPU 实际执行时，指针跳转会让访问位置变得分散。

本项目的数据导向版本正是按这个思路组织节点池。一个节点不再主要表现为“一个对象里包着所有字段”，而是被拆成多个同下标数组：

\[
\text{节点池}=\{\text{定义域}[],\text{父节点}[],\text{左子节点}[],\text{右子节点}[],\text{基边邻居}[],\text{左邻居}[],\text{右邻居}[],\text{几何误差}[],\text{屏幕误差}[],\text{深度}[],\text{标记}[]\}
\]

如果第 \(i\) 个节点需要参与误差评估，就读取 \(\text{定义域}[i]\)、\(\text{几何误差}[i]\)、\(\text{深度}[i]\) 等字段；如果需要提交拓扑，就再读取和写入 \(\text{父节点}[i]\)、子节点数组和邻居数组。这样写不是为了让数学表达变复杂，而是为了让不同阶段只访问自己真正需要的数据。

为了说明这种差别，可以把传统节点布局近似看成“数组中的结构体”，把数据导向布局看成“结构化数组”。若一个节点对象大小为 \(S_{node}\)，而一次误差评估实际只需要读顶点、深度和误差缓存等字段，字段总大小为 \(S_{use}\)，通常有

\[
S_{use}<S_{node}
\]

扫描 \(N\) 个节点时，传统布局可能需要把大量暂时用不到的字段也一起带进缓存，近似内存读取量为

\[
B_{object}\approx N\cdot S_{node}
\]

而结构化数组只扫描当前阶段需要的数组，读取量更接近

\[
B_{soa}\approx N\cdot S_{use}
\]

当 \(S_{node}\) 明显大于 \(S_{use}\) 时，后者更容易利用缓存和预取机制。再从缓存行角度看，假设一条缓存行为 \(C\) 字节，一个浮点误差值为 \(4\) 字节，那么连续误差数组一条缓存行最多能容纳

\[
n_{soa}=\left\lfloor \frac{C}{4}\right\rfloor
\]

个误差值；如果误差值被包在一个较大的节点对象里，同一条缓存行能覆盖的有效节点数大约是

\[
n_{object}=\left\lfloor \frac{C}{S_{node}}\right\rfloor
\]

当 \(S_{node}\) 较大时，\(n_{object}\) 会明显变小。这个推导虽然简化，但能解释为什么连续数组扫描往往比追踪指针更适合现代 CPU。

DOD 的另一个理论基础是任务可以按数据范围切分。设当前活跃叶节点集合为

\[
A=\{a_0,a_1,\ldots,a_{N-1}\}
\]

误差评估可以写成对每个叶节点独立执行的函数：

\[
score_i=f(a_i,camera,settings)
\]

如果把 \(A\) 切成 \(k\) 个不相交区间，

\[
A=A_0\cup A_1\cup \cdots \cup A_{k-1},\qquad A_i\cap A_j=\varnothing
\]

那么不同工作线程就可以分别处理不同区间。只要每个线程只写自己负责的节点误差槽位，就不需要在每个节点上加锁。理想情况下，并行耗时接近

\[
T_k\approx \frac{T_1}{k}+T_{\text{调度}}+T_{\text{合并}}
\]

其中 \(T_1\) 是单线程耗时，\(T_{\text{调度}}\) 是任务调度成本，\(T_{\text{合并}}\) 是合并局部结果的成本。实际加速不会无限接近 \(k\)，可以用 Amdahl 定律理解：

\[
S(k)=\frac{1}{(1-p)+\frac{p}{k}+o(k)}
\]

这里 \(p\) 表示可并行部分比例，\(o(k)\) 表示线程调度、同步和缓存争用等额外开销。这个公式能说明一个朴素事实：DOD 的收益不是“开线程就一定快”，而是要看算法里到底有多少阶段能变成稳定的批量扫描。

ROAM 中比较适合 DOD 的阶段包括活跃叶节点扫描、误差评估、候选标记和网格输出。以网格输出为例，如果每个叶节点都输出一个三角形，并且每个三角形写入 3 个顶点、3 个索引，那么第 \(r\) 个叶节点的写入位置可以直接写成

\[
vertexOffset_r=3r,\qquad indexOffset_r=3r
\]

这样每个线程只要知道自己负责的叶节点排名，就可以写入固定区间，不需要争抢同一个输出数组尾部。如果每个叶节点输出的顶点数不固定，也可以先做前缀和：

\[
offset_i=\sum_{j=0}^{i-1} count_j
\]

再让第 \(i\) 个任务写入 \([offset_i, offset_i+count_i)\) 这段范围。

但是 ROAM 的拓扑更新不能简单看成普通数组扫描。分裂一个节点时，除了写它自己的子节点，还会影响父子关系、基边邻居、左右邻居和菱形配对。可以把一个候选节点 \(c\) 会影响的拓扑邻域记为

\[
N(c)=\{c,parent(c),children(c),baseNeighbor(c),leftNeighbor(c),rightNeighbor(c)\}
\]

如果两个候选 \(c_i,c_j\) 满足

\[
N(c_i)\cap N(c_j)=\varnothing
\]

它们就比较适合并行提交；如果交集不为空，就可能同时修改同一组拓扑关系，必须串行化、加锁，或者使用更复杂的冲突解决。这个模型解释了为什么 DOD 对 ROAM 的优化重点通常放在批量读取和批量输出阶段，而不是把所有分裂、合并和邻接修复都直接并行化。

为了把这种“互不影响”的条件落到可执行的判断上，可以把地形参数域划分成规则分块。设分块数量为 \(K\times K\)，一个三角形中心为 \((u_c,v_c)\)，则它所在分块可以写成

\[
chunk(T)=\lfloor K u_c\rfloor + K\lfloor K v_c\rfloor
\]

本项目中 \(K=8\)。如果一个候选节点和它可能影响的子节点、邻居节点都落在同一个内部分块里，就可以把它视作比较安全的并行候选；如果它跨越分块边界，就更适合回到串行路径。这个规则不是 ROAM 原论文里的理论要求，而是把拓扑邻域冲突条件转成工程上可判断的近似条件。

### 2.3 计算着色器与 GPU 迁移原理

GPU 的优势是大规模并行吞吐。计算着色器把任务组织成很多工作组，每个工作组里又有多个调用实例。设每个工作组大小为 \(L\)，工作组编号为 \(g\)，组内编号为 \(l\)，则一维任务中的全局编号可以写成

\[
id=g\cdot L+l
\]

如果需要处理 \(N\) 个元素，工作组数量一般取

\[
G=\left\lceil \frac{N}{L}\right\rceil
\]

本项目里的几个计算着色器都采用一维调度，并把 \(L\) 设为 128，因此实际调度规模可以写成

\[
G=\left\lceil \frac{N}{128}\right\rceil
\]

每个调用实例先判断

\[
id<N
\]

成立时才处理对应元素。这个执行模型很适合“一个叶节点对应一次误差评估”“一个候选对应一次标记”“一个叶节点对应一次网格输出”这类任务。

计算着色器能够直接读写着色器存储缓冲区对象，也就是 SSBO。为了减少中英夹杂，这里把节点缓冲区记为 \(B_n\)，活跃叶节点缓冲区记为 \(B_l\)，误差缓冲区记为 \(B_e\)，候选缓冲区记为 \(B_c\)。误差评估阶段可以抽象为

\[
nodeIndex=B_l[id]
\]

\[
B_e[id]=E(B_n[nodeIndex])
\]

这里误差写入的是活跃叶节点序号 \(id\) 对应的位置，而不是全局节点编号对应的位置。这个细节和项目里的 GPU 误差评估保持一致：候选标记阶段会按同一个活跃叶节点序号读取 \(B_e[id]\)，避免为整个节点容量开一份稀疏误差结果。

候选标记阶段可以进一步写成

\[
splitFlag_i =
\begin{cases}
1, & E_i>\tau_{split}\ \text{且}\ depth_i<D_{max}\\
0, & \text{其他情况}
\end{cases}
\]

\[
mergeFlag_i =
\begin{cases}
1, & E_i<\tau_{merge}\ \text{且子节点满足可合并条件}\\
0, & \text{其他情况}
\end{cases}
\]

这里的每个 \(i\) 都可以由不同 GPU 调用实例处理，所以误差评估和候选标记天然具有较高并行度。

活跃叶节点压缩也是 GPU 中常见的并行问题。设每个节点是否为活跃叶节点的判断结果为

\[
a_i\in\{0,1\}
\]

理论上可以通过前缀和把稀疏节点集合压缩成连续数组：

\[
p_i=\sum_{j=0}^{i-1}a_j
\]

当 \(a_i=1\) 时，把节点编号 \(i\) 写入

\[
B_l[p_i]=i
\]

如果不做完整前缀和，也可以用原子加法取得写入位置：

\[
p_i=atomicAdd(counter,1)
\]

这种方式实现简单，但当很多调用实例同时追加结果时，原子计数器会形成一定争用。所以 GPU 并行并不是没有代价，只是它把代价从 CPU 串行扫描变成了 GPU 侧的并发写入和同步问题。

网格输出阶段也适合写成 GPU 批量任务。假设压缩后共有 \(M\) 个活跃叶节点，第 \(r\) 个叶节点对应一个三角形，那么它可以写入

\[
V_{3r},V_{3r+1},V_{3r+2}
\]

三个顶点，并写入

\[
I_{3r}=3r,\quad I_{3r+1}=3r+1,\quad I_{3r+2}=3r+2
\]

三个索引。只要每个调用实例负责不同的 \(r\)，输出缓冲区就不会互相覆盖。之后绘制阶段可以使用间接绘制命令，让 GPU 根据输出计数决定画多少索引，从而减少 CPU 逐项提交的压力。

不过，GPU 适合批量阶段，并不等于完整 ROAM 拓扑天然适合完全放到 GPU。原因还是拓扑依赖。若两个 GPU 调用实例同时试图分裂相邻三角形，它们可能同时写同一个邻居指针、同一个父节点状态，或者同一个菱形关系。这个问题可以抽象成并发写冲突：

\[
WriteSet(c_i)\cap WriteSet(c_j)\ne\varnothing
\]

当交集不为空时，就必须使用原子锁、分阶段提交、图着色分批，或者回退到更保守的串行顺序。否则即使误差计算本身正确，拓扑也可能出现不一致。

GPU 迁移还必须考虑数据传输和同步。一个混合 CPU-GPU 地形 LOD 管线的总耗时可以粗略写成

\[
T_{\text{GPU总}}=T_{\text{准备}}+T_{\text{上传}}+T_{\text{调度}}+T_{\text{计算}}+T_{\text{同步}}+T_{\text{读回}}+T_{\text{绘制}}
\]

其中 \(T_{\text{计算}}\) 是 GPU 真正运行计算着色器的时间，\(T_{\text{上传}}\) 和 \(T_{\text{读回}}\) 是 CPU 与 GPU 之间的数据交界成本，\(T_{\text{同步}}\) 是缓冲区可见性和等待成本。GPU 版本要真正比 CPU 版本快，需要满足

\[
T_{\text{GPU总}}<T_{\text{CPU总}}
\]

也就是说，不能只看计算着色器本身是不是很快，还要看上传、读回、同步和绘制提交是否抵消了并行收益。这个理论关系也是后面分析 GPU 类 ROAM 实验结果时的重要前提。

### 2.4 地形渲染中的变换与光照基础

地形网格生成之后，还要经过标准图形渲染变换才能显示到屏幕上。一个顶点从模型空间到裁剪空间的过程可以写成

\[
p_{clip}=P_{proj}P_{view}P_{model}
\begin{bmatrix}
x\\y\\z\\1
\end{bmatrix}
\]

其中 \(P_{model}\) 是模型矩阵，负责把局部坐标放到世界空间；\(P_{view}\) 是观察矩阵，负责把世界坐标转换到相机坐标系；\(P_{proj}\) 是投影矩阵，负责透视投影。裁剪空间坐标还需要做透视除法：

\[
p_{ndc}=
\begin{bmatrix}
x_{clip}/w_{clip}\\
y_{clip}/w_{clip}\\
z_{clip}/w_{clip}
\end{bmatrix}
\]

然后再映射到屏幕像素。这个公式和前面屏幕空间误差推导是一致的：物体离相机越远，投影后占据的屏幕面积越小，因此同样的世界空间误差在屏幕上越不明显。

地形光照还需要法线。因为地形由高度函数定义，所以可以从高度图梯度推导法线。由

\[
P(u,v)=((u-\frac{1}{2})L,A H(u,v),(v-\frac{1}{2})L)
\]

可得两个切向量近似为

\[
P_u=(L,A\frac{\partial H}{\partial u},0),\qquad
P_v=(0,A\frac{\partial H}{\partial v},L)
\]

因此法线可以写成

\[
n=\frac{P_v\times P_u}{\|P_v\times P_u\|}
\]

高度图是离散数据，所以偏导数通常用中心差分近似：

\[
\frac{\partial H}{\partial u}\approx \frac{H(u+\Delta u,v)-H(u-\Delta u,v)}{2\Delta u}
\]

\[
\frac{\partial H}{\partial v}\approx \frac{H(u,v+\Delta v)-H(u,v-\Delta v)}{2\Delta v}
\]

有了法线以后，可以使用基础漫反射或 Blinn-Phong 光照模型。设单位法线为 \(n\)，单位光照方向为 \(l\)，视线方向为 \(v\)，半程向量为

\[
h=\frac{l+v}{\|l+v\|}
\]

则颜色可以写成

\[
C=C_a k_a + C_d k_d\max(n\cdot l,0)+C_s k_s\max(n\cdot h,0)^\gamma
\]

其中第一项是环境光，第二项是漫反射，第三项是高光，\(\gamma\) 控制高光集中程度。对于本项目的地形展示来说，光照公式不追求复杂材质效果，主要作用是让地形起伏更容易被观察，也让不同 LOD 状态下的三角形表面变化更直观。

## 第三章 系统设计与实现

### 3.1 项目概览

本项目使用 C++20 编写，使用 Git 做版本控制，使用 CMake 管理构建。窗口和输入层使用 SDL2，数学计算和相机矩阵使用 GLM，调试界面使用 Dear ImGui，渲染核心使用 OpenGL。项目支持 Debug、Release 和 RelWithDebInfo 构建，其中正式基准测试使用 RelWithDebInfo，因为它既保留了一定调试信息，也更接近实际性能表现。

截至本报告撰写时，项目 Git 提交数为 48。按不包含 `tools`、`third_party` 和 `build` 的自写工程口径统计，项目文件约 141 个，其中 `src` 下 C++ 源码与头文件 75 个，核心源码约 14413 行。去掉空行后，代码行约 11157 行，注释行约 1405 行，注释覆盖率约 11.18%。这些数据不是为了证明代码很多，而是为了说明这个项目不是只写了一个单文件演示程序，而是把窗口、渲染、地形加载、算法、GPU 阶段、基准测试和文档都拆成了独立模块。

图3-1 是项目总体架构图。它从上到下展示了应用主循环如何协调 SDL2、输入相机、Dear ImGui、地形渲染器、高度图、三种 LOD 算法和运行时基准测试。

![图3-1：项目总体架构图](report-assets/architecture-overview.svg)

### 3.2 三种算法子架构

经典 CPU ROAM 的子架构如图3-2 所示。这个版本最接近传统 ROAM 语义，核心是持久化二叉三角树、分裂优先队列、合并菱形队列、强制分裂和拓扑验证。我把它作为正确性基准版本，因为后面的 DOD 和 GPU 都需要知道自己有没有偏离 ROAM 的基本语义。

![图3-2：经典 CPU ROAM 子架构](report-assets/architecture-classic-roam.svg)

DOD ROAM 的子架构如图3-3 所示。这个版本把节点组织成基于索引的节点池，尽量把可批处理阶段改成并行阶段。误差评估、活跃叶节点收集、候选标记和网格输出都可以比较自然地并行；拓扑提交则采用保守策略，能安全分块的内部候选项并行处理，不能保证安全的部分回到串行路径。

![图3-3：数据导向 CPU ROAM 子架构](report-assets/architecture-dod-roam.svg)

GPU 类 ROAM 的子架构如图3-4 所示。这个版本目前仍然保留 DOD CPU 拓扑基准版本，然后把活跃叶节点压缩、误差评估、候选标记、仅分裂实验和网格输出放到 GPU 上。这样做不是最终理想形态，但它让我能一步一步验证 ROAM 哪些阶段适合 GPU，哪些阶段仍然卡在拓扑同步和数据交界上。

![图3-4：GPU 类 ROAM 子架构](report-assets/architecture-gpu-roam.svg)

### 3.3 模块划分

项目入口只负责解析命令行参数，比如普通运行、冒烟测试、GPU 冒烟测试、运行时基准测试和基准测试参数覆盖。真正的主循环由 `Application` 类承担，它持有窗口、输入、相机、渲染器、界面层和基准测试状态机。这样入口文件不会堆太多逻辑，主循环也能集中处理“每一帧先读输入、再更新相机、再更新地形、再画界面、最后交换缓冲区”的流程。

窗口模块负责 SDL2 窗口、OpenGL 上下文、垂直同步设置和窗口尺寸刷新。输入模块维护键盘、鼠标和窗口事件状态，相机控制模块根据输入更新观察位置和朝向。渲染模块的核心类是 `TerrainRenderer`，它统一接收界面设置和算法输出，并负责网格上传、着色器统一变量、绘制调用和统计数据。地形加载模块由 `HeightMap` 类承担，支持 PGM 和 PNG 资源。

算法模块都实现同一个抽象接口 `ITerrainLodAlgorithm`。这样地形渲染器不需要知道内部到底是经典版本、DOD 版本还是 GPU 版本，只要传入高度图、相机位置和 LOD 参数，就能得到统一的渲染数据包。经典算法的核心类是 `ClassicRoamMeshBuilder`，DOD 算法的核心类是 `DataOrientedRoamMeshBuilder`，GPU 算法的核心类是 `GpuRoamTerrainLodAlgorithm` 和 `GpuRoamMeshBuilder`。我觉得这个统一接口很重要，因为它让三种算法能在同一个基准测试里公平对比，而不是每种算法写一套单独测试逻辑。

基准测试模块分为两类。第一类是无窗口算法层基准测试，适合做冒烟测试和基础回归；第二类是真实应用内运行时基准测试，它会在 OpenGL 上下文存在的情况下运行，所以能测到 GPU、网格上传、帧耗时和界面参数。第四章的数据主要来自运行时基准测试。

### 3.4 关键代码片段解析

这一节不按源码文件顺序介绍，而是按项目里最核心的执行逻辑来讲。我把代码解析分成四块：经典 CPU ROAM、数据导向 CPU ROAM、GPU 类 ROAM，以及它们共同依赖的支撑代码。三种算法各自对应一个核心模块，支撑代码则负责统一输入输出、渲染分流和基准测试采样。

下面的代码不是完整源码的机械粘贴，而是从项目真实类和函数中整理出的关键逻辑。我删去了部分日志、错误字符串和界面细节，但保留判断条件、状态更新和数据流转，并在代码里补了注释。这样写的目的不是展示“写了哪些函数”，而是解释一帧中数据怎样一步一步经过算法，最后变成可以绘制和统计的结果。

#### 3.4.1 经典 CPU ROAM：用二叉三角树维护无裂缝拓扑

经典版本的核心类是 `ClassicRoamMeshBuilder`，属于经典 ROAM 算法模块。它内部保存两棵根三角树，两个根三角形共同覆盖整张高度图。每一帧更新时，算法不是把树全部删掉再重新生成，而是在已有拓扑上先尝试合并远处低误差节点，再把当前视角下误差较高的叶节点继续分裂。这样做更接近传统 ROAM 的跨帧维护语义，也能让迟滞控制和合并逻辑真正发挥作用。

先看它的一帧入口。这里的关键不是某一个数学公式，而是顺序：先判断是否需要重置拓扑，再合并，再分裂，最后在拓扑稳定后收集叶节点并输出网格。如果顺序反过来，比如先输出网格再合并，统计和画面就会对应不上。

```cpp
TerrainMeshData ClassicRoamMeshBuilder::Build(
    const HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const glm::vec3& cameraPosition,
    const ClassicRoamSettings& settings)
{
    // 记录本帧输入。相机位置只影响屏幕误差，不一定要求清空整棵树。
    _heightMap = &heightMap;
    _cameraPosition = cameraPosition;
    _terrainSize = terrainSize;
    _heightScale = heightScale;
    _settings = settings;

    // 如果高度图、缩放或最大深度不再兼容，旧拓扑里的误差缓存就不能复用。
    if (NeedsTopologyReset(heightMap, terrainSize, heightScale, settings))
    {
        ResetTopology();
    }

    // 先回收远处低误差细节，避免旧细节一直残留在地形远处。
    MergeWithDiamondQueue();

    // 再根据当前相机位置，把误差大的叶节点继续向下分裂。
    RefineWithSplitQueue(_rootA, _rootB);

    // 验证只负责统计问题，不在这里偷偷修拓扑，避免掩盖算法错误。
    if (_settings.EnableTopologyValidation)
    {
        ValidateTopology();
    }

    // 拓扑稳定后再收集叶节点；这些叶节点就是本帧真正要画的三角形。
    CollectLeafNodes(_activeLeaves);
    EmitLeafTriangles(meshData, _activeLeaves);
    AccumulateLeafStats(meshData, _activeLeaves);
    return meshData;
}
```

这段入口逻辑对应了经典 ROAM 的完整生命周期。`NeedsTopologyReset` 只在输入资源或拓扑上限变化时清空树，相机普通移动不会重置，这是为了保留跨帧节点身份。`MergeWithDiamondQueue` 放在前面，是因为上一帧近处的细节在这一帧可能已经变成远处，如果不先合并，后面的分裂只会不断增加节点，拓扑规模会越来越大。`RefineWithSplitQueue` 再接着运行，把新的细节补到当前相机真正需要的位置。最后统一收集叶节点，是为了保证网格输出、统计面板和基准测试看到的是同一份最终拓扑。

下面这段代码展示的是分裂阶段的核心流程。它没有直接递归“看见一个能分裂就分裂”，而是先把所有活跃叶节点按屏幕误差放进优先队列。这样误差最大的三角形会先处理，细节更集中在相机附近或地形变化更明显的位置。

```cpp
void ClassicRoamMeshBuilder::RefineWithSplitQueue(Node* rootA, Node* rootB)
{
    priority_queue<SplitCandidate> candidates;

    auto enqueueCandidate = [&](Node* node) {
        // 只有叶节点才代表当前可见网格中的一个三角形
        if (node == nullptr || !IsLeaf(node) || node->Depth >= _settings.MaxDepth)
            return;

        // 屏幕误差越大，越应该优先分裂
        float score = ComputeScreenErrorScore(*node);
        if (score >= _settings.SplitThreshold)
            candidates.push({score, _sequence++, node});
    };

    auto enqueueActiveLeaves = [&](auto&& self, Node* node) -> void {
        if (node == nullptr)
            return;

        if (IsLeaf(node))
        {
            enqueueCandidate(node);
            return;
        }

        // 根节点可能早已分裂，所以必须深入到当前活跃叶节点
        self(self, node->LeftChild);
        self(self, node->RightChild);
    };

    enqueueActiveLeaves(enqueueActiveLeaves, rootA);
    enqueueActiveLeaves(enqueueActiveLeaves, rootB);

    while (!candidates.empty())
    {
        SplitCandidate c = candidates.top();
        candidates.pop();

        // 候选可能已被强制分裂消耗，弹出时要重新检查
        if (!IsLeaf(c.Node))
            continue;

        // 分裂会修改拓扑，因此弹出时重新计算一次误差
        if (ComputeScreenErrorScore(*c.Node) < _settings.SplitThreshold)
            continue;

        Node* oldBaseNeighbor = c.Node->BaseNeighbor;
        if (!SplitNode(c.Node, SplitReason::Requested, nullptr))
            continue;

        // 当前节点分裂后，新产生的两个子节点也可能继续满足细分条件
        enqueueCandidate(c.Node->LeftChild);
        enqueueCandidate(c.Node->RightChild);

        // 基边邻居可能因为裂缝修复被同步分裂，也要把它的子节点重新放回队列
        if (oldBaseNeighbor != nullptr && !IsLeaf(oldBaseNeighbor))
        {
            enqueueCandidate(oldBaseNeighbor->LeftChild);
            enqueueCandidate(oldBaseNeighbor->RightChild);
        }
    }
}
```

这段代码可以分成三步理解。第一步是收集候选。因为经典 ROAM 的拓扑是持久化的，根节点在第一帧之后通常已经不是叶节点，所以算法不能只把两个根节点放进队列，而是要递归找到当前真正参与渲染的叶节点。每个叶节点代表当前网格中的一个三角形，只有它才需要重新计算屏幕误差。

第二步是优先队列驱动分裂。队列按照屏幕误差排序，误差越大的三角形越先被处理。这里弹出候选后还要重新检查一次，是因为 ROAM 的强制分裂会改变局部拓扑，某个候选可能在等待队列期间已经被邻居关系牵连处理掉。这个重新检查虽然多花一点计算，但能避免使用过期候选破坏拓扑。

第三步是分裂后的重新入队。一个节点分裂后会产生两个子节点，它们可能仍然离相机很近，需要继续分裂。与此同时，基边邻居也可能因为裂缝修复被同步分裂，所以它的新子节点也要重新进入候选队列。这样算法才能在一个更新过程中连续向更细层级推进，而不是每帧只分裂一小步。

真正保证无裂缝的是 `SplitNode`。它不是简单创建两个子三角形，而是先检查基边邻居是否处于兼容状态。如果对面的邻居还没有分裂，就先递归强制分裂邻居，让共享边两侧进入同一个菱形结构。

```cpp
bool ClassicRoamMeshBuilder::SplitNode(Node* node, SplitReason reason, Node* forcedFrom)
{
    if (!IsLeaf(node) || node->Depth >= _settings.MaxDepth)
        return false;

    Node* baseNeighbor = node->BaseNeighbor;

    if (_settings.EnableLocalConstraints && baseNeighbor != nullptr)
    {
        // 如果基边邻居还没有形成可兼容菱形，就先分裂邻居
        if (IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
        {
            SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node);
        }

        // 邻居分裂后，当前节点的邻接指针可能已被重连，需要刷新
        baseNeighbor = node->BaseNeighbor;
    }

    // 首次分裂时创建子节点；合并后再次分裂时可以复用子节点对象
    if (node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        TriangleDomain leftDomain;
        TriangleDomain rightDomain;
        SplitTriangleByBaseEdge(node->Domain, leftDomain, rightDomain);
        node->LeftChild = AddNode(leftDomain, node, node->Depth + 1);
        node->RightChild = AddNode(rightDomain, node, node->Depth + 1);
    }

    node->IsSplit = true;

    // 分裂后重新连接当前节点、子节点和基边邻居之间的邻接关系
    LinkSplitNeighbors(node, baseNeighbor);
    return true;
}
```

这段逻辑体现了经典 ROAM 最核心的地方：误差分数只决定“想不想分裂”，拓扑约束决定“能不能直接分裂”。如果当前三角形沿基边分裂，而基边邻居还是粗三角形，那么共享边两侧就会出现一边两条小边、一边一条大边的情况，也就是 T 形裂缝。因此我在分裂前先处理基边邻居，再创建子节点并重连邻接关系。`forcedFrom` 的作用是避免两个互为基边邻居的节点在强制分裂时来回递归。

经典版本最后还会做可选拓扑验证。拓扑验证不主动修复网格，而是把裂缝风险和邻接错误转成统计值，用来判断“画面看起来没问题”是否真的成立。

```cpp
for (LeafTriangle leaf : activeLeaves)
{
    // 检查粗边中间是否贴着其他叶节点端点，这是典型 T 形裂缝风险
    if (DetectTJunction(leaf, allLeafEdges))
        ++_stats.TjunctionCount;

    // 检查当前节点记录的邻居是否真的共享同一条边
    if (!ValidateNeighborLink(leaf.BaseNeighbor, leaf.BaseEdge))
        ++_stats.InvalidNeighborCount;
}
```

这部分之所以放进关键实现，是因为它保证经典版本能作为后面两个版本的正确性参照。如果经典版本只输出三角形数量，却不检查 T 形裂缝、非法邻居和非法拓扑，那么 DOD 或 GPU 版本即使跑得更快，也不知道是否还保留了 ROAM 的基本语义。

#### 3.4.2 数据导向 CPU ROAM：把节点关系改成可批处理的数据流

DOD 版本的核心类是 `DataOrientedRoamMeshBuilder`，属于数据导向 ROAM 算法模块。它仍然做 ROAM 的分裂、合并和裂缝约束，但内部不再主要依赖对象指针，而是把节点放进基于索引的节点池。节点的父子、邻居、深度、标记和误差分数都可以按数组连续访问。这个改动是后面并行扫描、候选标记、拓扑分块和 GPU 快照构建的基础。

```cpp
struct DataOrientedRoamNodePool
{
    // 每个数组的下标都是 nodeIndex，同一个下标描述同一个 ROAM 节点。
    // 这样遍历节点时可以连续读取某一类字段，而不是沿指针跳转。
    std::vector<TriangleDomain> Domains;
    std::vector<DataOrientedRoamNodeIndex> Parents;
    std::vector<DataOrientedRoamNodeIndex> LeftChildren;
    std::vector<DataOrientedRoamNodeIndex> RightChildren;
    std::vector<DataOrientedRoamNodeIndex> BaseNeighbors;
    std::vector<DataOrientedRoamNodeIndex> LeftNeighbors;
    std::vector<DataOrientedRoamNodeIndex> RightNeighbors;
    std::vector<DataOrientedRoamChunkId> InteriorChunkIds;
    std::vector<float> GeometricErrors;
    std::vector<float> ScreenErrors;
    std::vector<int> Depths;
    std::vector<std::uint8_t> IsSplits;
};
```

这段结构的重点不是“把指针换成整数”这么简单，而是把访问方式从“顺着对象跳来跳去”改成“按下标批量扫描”。例如收集活跃叶节点时，算法主要看 `IsSplits` 和 `Depths`；输出网格时，只需要用叶节点索引访问 `Domains`；构造 GPU 快照时，也可以把这些连续数组编码成缓冲区记录。它牺牲了一部分对象写法的直观性，但换来了更稳定的内存访问和更清楚的并行任务边界。

每帧更新时，DOD 版本先整理状态，再执行合并、分裂、验证、叶节点收集和网格输出。它和经典版本的算法顺序相似，但每个阶段尽量使用节点索引和批量数组。

```cpp
TerrainMeshData DataOrientedRoamMeshBuilder::BuildInternal(
    const HeightMap& heightMap,
    const glm::vec3& cameraPosition,
    bool emitCpuMesh)
{
    // state 是整个 DOD 模块的工作集，本帧所有阶段都通过它传递数据。
    ++state.BuildSequence;
    state.CameraPosition = cameraPosition;
    state.Stats = {};
    state.FinalActiveLeaves.clear();

    if (NeedsTopologyReset(state, heightMap, settings))
        ResetTopology(state);

    // 先合并低误差区域，避免旧细节一直留在远处
    MergeWithDiamondQueue(state);

    // 再按当前相机位置补充分裂，把细节分配到近处和高误差区域
    RefineWithSplitQueue(state);

    if (settings.EnableTopologyValidation)
        ValidateTopology(state);

    // 拓扑稳定后收集叶节点，后续统计、CPU 输出和 GPU 快照都复用它。
    CollectLeafNodes(state, state.FinalActiveLeaves);

    if (emitCpuMesh)
        EmitLeafTriangles(state, meshData, state.FinalActiveLeaves);

    AccumulateLeafStats(state, state.FinalActiveLeaves);
    return meshData;
}
```

这段代码和经典版本的入口很像，但内部数据流已经变了。经典版本的很多操作是“拿到一个节点指针，再沿邻居指针走”；DOD 版本则是“拿到节点下标，再到节点池的不同数组中取字段”。`emitCpuMesh` 这个参数也很关键：普通 DOD 算法会输出 CPU 网格，而 GPU 类 ROAM 只需要 DOD 维护拓扑，不需要它再生成一份 CPU 网格，所以可以跳过网格输出。这样 GPU 路径不会白白做一次 CPU 网格构建。

这段入口里有两个容易被忽略的顺序。第一，合并在分裂之前执行。这样远处或者低误差区域的旧细节会先被回收，然后分裂阶段再根据当前相机位置补回需要的细节。第二，叶节点快照一定要在拓扑稳定之后收集。因为网格输出、统计数据和 GPU 快照都依赖这份叶节点列表，如果边输出边改拓扑，很容易出现三角形数量和实际节点状态对不上的情况。

DOD 中最容易并行的是候选收集和误差评估。每个活跃叶节点的屏幕误差基本只依赖自身定义域、高度图和相机位置，不需要写邻居关系，所以我把活跃叶节点切成多个连续区间，让每个工作线程处理一段。

```cpp
void CollectSplitCandidates(State& state, vector<SplitCandidate>& outCandidates)
{
    // 每个工作线程一个局部候选数组，减少共享写入。
    vector<vector<SplitCandidate>> localCandidates(workerCount);

    RunWorkers(workerCount, [&](size_t workerIndex) {
        size_t begin = workerIndex * chunkSize;
        size_t end = min(begin + chunkSize, state.ActiveLeaves.size());

        for (size_t i = begin; i < end; ++i)
        {
            NodeIndex node = state.ActiveLeaves[i];
            float score = EvaluateScreenErrorForNode(state, node);

            // 每个叶节点只读自身定义域、误差缓存和相机位置，适合并行评分。
            if (ShouldSplitWithScore(state, node, score))
                localCandidates[workerIndex].push_back({score, i, node});
        }
    });

    // 主线程统一合并局部候选，并重新分配稳定顺序号。
    for (auto& local : localCandidates)
    {
        outCandidates.insert(outCandidates.end(), local.begin(), local.end());
    }
}
```

这里的逻辑关键是“局部写、最后合并”。如果所有工作线程都往同一个候选数组里追加元素，就必须加锁，候选越多锁竞争越明显，最后可能把并行收益抵消掉。我采用每个工作线程一个局部候选数组，扫描结束后再由主线程合并。这样每个工作线程只读共享状态、写自己的局部内存，数据竞争比较少，也更符合 DOD 的批处理思路。

真正麻烦的是拓扑提交。分裂和合并会修改父子关系、基边邻居、左邻居和右邻居，如果两个线程同时修改相邻三角形，就可能破坏无裂缝约束。因此 DOD 版本没有把所有候选都强行并行，而是先把候选按地形区域分块，只让完全落在安全内部分块里的候选并行提交，剩下的边界候选回到串行队列处理。

```cpp
void RefineWithSplitQueue(State& state)
{
    vector<SplitCandidate> initialCandidates;
    CollectSplitCandidates(state, initialCandidates);

    // 先找出互不相邻、比较安全的内部分块候选
    auto interiorChunks = BuildInteriorSplitChunks(state, initialCandidates);
    vector<CommittedSplit> committed = CommitInteriorSplitChunks(state, interiorChunks);

    priority_queue<SplitCandidate> queue;

    // 没有被并行批次消耗的候选，进入串行优先队列继续处理
    for (SplitCandidate c : initialCandidates)
    {
        if (state.IsValidNode(c.Node) && state.IsLeaf(c.Node))
            queue.push(c);
    }

    // 并行提交产生的新子节点，也要重新进入串行队列继续细分
    for (CommittedSplit s : committed)
    {
        EnqueueIfStillSplittable(queue, state.Nodes[s.Node].LeftChild);
        EnqueueIfStillSplittable(queue, state.Nodes[s.Node].RightChild);
    }

    // 串行收尾负责处理边界、过期候选和强制分裂传播
    while (!queue.empty())
    {
        SplitCandidate c = queue.top();
        queue.pop();

        if (!state.IsLeaf(c.Node))
            continue;

        if (SplitNode(state, c.Node, Requested))
        {
            EnqueueIfStillSplittable(queue, state.Nodes[c.Node].LeftChild);
            EnqueueIfStillSplittable(queue, state.Nodes[c.Node].RightChild);
        }
    }
}
```

这一段是 DOD 版本里最重要的工程取舍。纯粹从性能角度看，把所有分裂都并行提交最诱人，但 ROAM 的拓扑约束决定了相邻候选之间可能互相影响。我的做法是让“内部安全区域”先并行，尽量利用多核；再用串行队列做最终一致性收尾，处理边界候选、过期候选和强制分裂传播。这样它不是最激进的并行算法，但能在保持拓扑正确的前提下明显降低 CPU 更新时间。

最后是网格输出。DOD 版本在拓扑稳定后已经有一份活跃叶节点快照，每个叶节点最终都会输出一个三角形。只要提前确定输出数组大小，不同工作线程就可以写不同的三角形区间。

```cpp
void EmitLeafTriangles(State& state, MeshData& mesh, span<NodeIndex> leaves)
{
    mesh.Vertices.resize(leaves.size() * 3);
    mesh.Indices.resize(leaves.size() * 3);

    RunWorkers(workerCount, [&](size_t workerIndex) {
        size_t begin = workerIndex * chunkSize;
        size_t end = min(begin + chunkSize, leaves.size());

        // 每个叶节点对应固定三角形下标，所以不同线程不会写到同一槽位。
        for (size_t i = begin; i < end; ++i)
        {
            size_t triangleIndex = i;
            EmitOneLeafTriangle(state, mesh, leaves[i], triangleIndex);
        }
    });
}
```

这段代码解释了为什么三角形数量上来之后 DOD 的优势会变明显。经典版本需要沿指针树遍历并逐个输出三角形；DOD 版本已经有连续的叶节点列表，可以直接按区间切给不同线程。因为第 \(i\) 个叶节点固定写第 \(i\) 个三角形槽位，线程之间不需要抢锁。三角形越多，这种分段写入越能摊薄线程调度成本。

#### 3.4.3 GPU 类 ROAM：把并行阶段移到计算着色器

GPU 版本的核心类是 `GpuRoamTerrainLodAlgorithm` 和 `GpuRoamMeshBuilder`，属于 GPU 类 ROAM 算法模块。它目前不是完整 GPU 拓扑版本，而是一个混合管线：CPU 侧仍然用 DOD 版本维护无裂缝拓扑，GPU 侧负责活跃叶节点压缩、误差评估、候选标记、实验性的仅分裂流程、网格输出和间接绘制。这样设计的原因是 ROAM 的分裂 / 合并拓扑依赖比较强，而叶节点扫描和网格输出更适合 GPU 并行。

```cpp
bool GpuRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket)
{
    if (!GpuRoamIsSupported())
        return false;

    // 第一步：CPU DOD 先维护无裂缝拓扑，保证分裂和合并语义可靠。
    _cpuTopologyBuilder.UpdateTopology(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.CameraPosition,
        ToDataOrientedSettings(input.Settings));

    _stats = ToTerrainLodStats(_cpuTopologyBuilder.Stats());

    // 第二步：把 CPU 节点池和活跃叶节点编码成 GPU 可以读取的快照。
    GpuRoamBufferSnapshot snapshot =
        BuildGpuRoamBufferSnapshot(_cpuTopologyBuilder.State());

    // 第三步：GPU 负责并行计算和生成绘制缓冲区。
    return _gpuMeshBuilder.Build(snapshot, input, outPacket, _stats);
}
```

这段代码说明 GPU 版本的边界非常清楚：它先借用 DOD CPU 拓扑作为可靠基准，然后再把适合并行的部分交给 GPU。也正因为如此，第四章里我不会简单写“GPU 版本就是完整 GPU ROAM”，而是称它为 GPU 类 ROAM 或混合 GPU 管线。它能证明一部分阶段适合 GPU，但也暴露了 CPU 快照构建和数据上传仍然会拖慢完整系统。

快照构建的作用，是把 DOD 节点池转换成 GPU 侧 SSBO 可以直接读取的结构。它不仅复制节点定义域，还要标记哪些节点是当前活跃叶节点，否则 GPU 不知道应该输出哪些三角形。

```cpp
GpuRoamBufferSnapshot snapshot{};
snapshot.Nodes.resize(dodState.Nodes.size());
snapshot.ActiveLeafIndices = dodState.FinalActiveLeaves;

for (size_t i = 0; i < dodState.Nodes.size(); ++i)
{
    // 把 CPU 侧的索引节点编码成 GPU 侧紧凑记录。
    // 记录里包含三角形定义域、子节点、邻居、深度和状态标记。
    snapshot.Nodes[i] = EncodeGpuNodeRecord(dodState, i);
}
```

这段看起来简单，但它正是 GPU 版本当前的主要限制之一。只要每帧都要从 CPU DOD 状态构造完整快照，GPU 就不是真正长期持有拓扑。后面实验 5 中“快照构建”和“读回等待”仍然明显，就是这个结构带来的结果。

进入 GPU 后，`GpuRoamMeshBuilder` 会先上传快照，再依次执行多个计算着色器阶段。每个阶段读写不同的 SSBO，阶段之间用内存屏障保证数据可见。

```cpp
bool GpuRoamMeshBuilder::RunGpuComputePipeline(...)
{
    // 上传 CPU 快照：节点记录进入 SSBO，高度图进入纹理。
    UploadSnapshot(snapshot, *input.HeightMap, nodeCapacity);

    // 阶段1：扫描节点池，把活跃叶节点压缩到连续缓冲区。
    RunActiveLeafCompactionPass();
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 阶段2：对每个活跃叶节点计算屏幕误差。
    RunErrorEvaluationPass();
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 阶段3：根据误差标记候选，供实验性 GPU 分裂或调试统计使用。
    RunCandidateMarkingPass();
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 阶段4：把叶节点直接写成顶点、索引和间接绘制参数。
    RunMeshEmitPass();
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                    GL_ELEMENT_ARRAY_BARRIER_BIT |
                    GL_COMMAND_BARRIER_BIT);
}
```

这段代码里最重要的是阶段顺序和内存屏障。活跃叶节点压缩先把零散节点变成连续列表，后面的误差评估和网格输出才能按全局调用编号并行处理。误差评估写出的结果要被候选标记读取，网格输出写出的顶点、索引和间接绘制参数要被后续绘制命令读取，所以每个关键阶段后都需要 `glMemoryBarrier`。如果漏掉屏障，问题往往不是稳定崩溃，而是偶发数量错误、候选错乱或者长三角形，这也是 GPU 调试比较难的地方。

活跃叶节点压缩的计算着色器逻辑比较短，但它决定 GPU 后续阶段处理哪些三角形。这里我没有让 GPU 扫描整个缓冲区容量，而是使用当前有效节点数量和 GPU 侧已分配数量的较小值。这样可以避免缓冲区尾部未初始化内容被误当成叶节点。

```glsl
void main()
{
    uint nodeIndex = gl_GlobalInvocationID.x;

    // 只能读取当前有效节点范围，容量不等于有效数据量。
    uint readableNodeCount = min(uNodeCount, allocatedNodeCount);
    if (nodeIndex >= readableNodeCount)
    {
        return;
    }

    uint flags = nodes[nodeIndex].topology1.w;
    bool isActiveLeaf = (flags & activeLeafFlag) != 0u;
    bool alreadySplit = (flags & splitFlag) != 0u;

    // 只有“当前活跃，并且没有继续分裂”的节点才会输出成三角形。
    if (!isActiveLeaf || alreadySplit)
    {
        return;
    }

    // 原子加法分配输出槽位，把零散叶节点压缩成连续数组。
    uint outputIndex = atomicAdd(activeLeafCount, 1u);
    activeLeafIndices[outputIndex] = nodeIndex;
}
```

这段逻辑的核心是把“节点池扫描”变成“连续叶节点列表”。节点池里既有内部节点，也有已经合并后暂时不活跃的旧子节点，如果后续网格输出直接遍历整个节点池，就会把不该画的节点也画出来。压缩阶段先过滤出有效叶节点，后面的误差评估和网格输出就可以把第 \(i\) 个叶节点交给第 \(i\) 个 GPU 调用实例处理，整个任务形状更规整。

GPU 网格输出阶段则把叶节点直接写成顶点、索引和间接绘制参数。它还做了防御性检查：如果节点下标越界、节点不是活跃叶节点，或者三角形定义域不是合法的 \(0\sim1\) 坐标，就输出一个退化三角形，而不是继续把错误数据画成跨场景长三角形。

```glsl
void main()
{
    uint leafSlot = gl_GlobalInvocationID.x;
    uint emitLeafCount = min(activeLeafCount, uActiveLeafLimit);

    // 第一个调用实例写入间接绘制命令，后续渲染可以直接使用 GPU 端数量。
    if (leafSlot == 0u)
    {
        drawCommand[0] = emitLeafCount * 3u;
        drawCommand[1] = 1u;
        drawCommand[2] = 0u;
        drawCommand[3] = 0u;
        drawCommand[4] = 0u;
    }

    if (leafSlot >= emitLeafCount)
    {
        return;
    }

    uint nodeIndex = activeLeafIndices[leafSlot];
    if (nodeIndex >= min(allocatedNodeCount, uNodeCapacity))
    {
        writeDegenerateLeaf(leafSlot);
        return;
    }

    NodeRecord node = nodes[nodeIndex];
    vec2 uvs[3] = vec2[3](
        node.domainAAndB.xy,
        node.domainAAndB.zw,
        node.domainCAndErrors.xy);

    // 非活跃、已分裂或定义域异常的节点都不能继续输出真实三角形。
    if (!isActiveLeaf(node) || isSplit(node) || !isValidDomain(uvs))
    {
        writeDegenerateLeaf(leafSlot);
        return;
    }

    // 根据高度图采样生成位置、法线、调试颜色，再写入固定槽位。
    uint vertexBase = leafSlot * 3u;
    writeVertex(vertexBase + 0u, uvs[0], node);
    writeVertex(vertexBase + 1u, uvs[1], node);
    writeVertex(vertexBase + 2u, uvs[2], node);
    writeWindingCorrectedIndices(vertexBase, uvs);
}
```

这段输出逻辑和 CPU 网格输出的区别很明显。CPU 版本是把三角形数据交给渲染器再上传，GPU 版本则直接把顶点和索引写进 GPU 缓冲区。`leafSlot * 3` 决定了每个叶节点写入自己的三个顶点和三个索引，不需要不同调用实例抢同一段输出空间。最后的绕序修正也很重要，因为如果三角形顶点顺序和背面剔除规则相反，地形会出现大片消失，看起来像算法没生成三角形，但实际是被渲染管线剔除了。

GPU 版本还有一段很重要的资源管理逻辑。它把“缓冲区容量”和“本帧实际使用大小”分开处理：容量不够时才重新分配，容量足够时只更新本帧用到的数据前缀。这样算法阶段可以专注于读写有效节点和有效叶节点，资源层则负责避免不必要的缓冲区重建。

```cpp
bool UploadBufferRange(Buffer& buffer, const void* data, size_t usedBytes, size_t requiredCapacity)
{
    if (buffer.Capacity < requiredCapacity)
    {
        // 只有容量不足时才重新分配。
        glBindBuffer(buffer.Target, buffer.Id);
        glBufferData(buffer.Target, requiredCapacity, nullptr, GL_DYNAMIC_DRAW);
        buffer.Capacity = requiredCapacity;
    }

    // 容量足够时只更新本帧实际用到的数据前缀
    glBindBuffer(buffer.Target, buffer.Id);
    glBufferSubData(buffer.Target, 0, usedBytes, data);
    return true;
}
```

这段逻辑的判断顺序很简单，但对 GPU 管线很重要。`requiredCapacity` 表示为了容纳当前节点池和可能的 GPU 分裂结果，缓冲区至少要有多大；`usedBytes` 表示本帧真正要上传多少字节。两者分开后，节点数量小幅波动不会每帧触发重新分配。报告第四章分析 GPU 分项耗时时，也正是通过这些统计项区分“计算着色器运行时间”和“资源管理时间”。

计时和计数读回则使用环形槽位。当前帧提交 GPU 查询，之后轮转到旧槽位时再读取结果。这样统计系统仍然可以获得 GPU 计算耗时和计数器结果，但不会把每一帧都写成“提交后立刻等待”的形式。

```cpp
size_t slot = _state.TimingReadbackCursor % TimingSlotCount;

// 先处理旧槽位的结果，再把当前帧的查询写入同一个槽位。
ResolveTimingReadbackSlot(slot);

glBeginQuery(GL_TIME_ELAPSED, _state.TimingSlots[slot].TimerQueryId);
RunGpuComputePipelinePasses();
glEndQuery(GL_TIME_ELAPSED);

// 当前槽位标记为等待，之后轮转回来再读
_state.TimingSlots[slot].Pending = true;
_state.TimingReadbackCursor++;
```

这段代码的逻辑是把“本帧提交”和“旧帧读回”解耦。它没有取消统计，而是把统计放到更符合 GPU 异步执行特性的流程里。这样报告里仍然能看到 GPU 查询等待和读回等待，但这些等待会作为独立指标出现，而不是混在一个模糊的总耗时里。

#### 3.4.4 其他关键支撑代码：统一接入、渲染分流与基准测试

除了三种算法本身，项目里还有一块很关键的支撑代码，也就是统一算法接口、地形渲染器和运行时基准测试。它们不直接决定某个三角形要不要分裂，但决定三种算法能不能在同一个程序里被公平调用。如果每种算法都有自己单独的输入、输出和统计方式，后面做性能对比时就很难说明差异到底来自算法，还是来自外部测试流程。

公共接入层的核心类是 `ITerrainLodAlgorithm` 和 `TerrainRenderer`。前者定义算法边界，后者负责把界面参数整理成算法输入，并根据算法输出选择 CPU 网格上传或者 GPU 缓冲区绑定。这样经典、DOD 和 GPU 三个版本可以共享相机、光照、线框、调试着色、性能面板和基准测试路径。

```cpp
struct TerrainLodBuildInput
{
    // 三种算法都从同一份高度图、同一个相机位置和同一组参数开始。
    const HeightMap* HeightMap{nullptr};
    glm::vec3 CameraPosition{0.0f};
    TerrainLodSettings Settings;
};

struct TerrainLodRenderPacket
{
    // CPU 算法返回普通网格，GPU 算法返回已经准备好的 GPU 缓冲区。
    TerrainLodRenderMode Mode{TerrainLodRenderMode::CpuMesh};
    TerrainMeshData CpuMesh;
    uint32_t GpuVertexBufferId{0};
    uint32_t GpuIndexBufferId{0};
    uint32_t IndirectDrawBufferId{0};
    size_t ActiveTriangleCount{0};
    size_t IndexCount{0};
};

class ITerrainLodAlgorithm
{
public:
    // 算法只关心 LOD 输入和渲染输出，不直接处理窗口、界面和输入事件。
    virtual bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) = 0;

    virtual const TerrainLodStats& Stats() const = 0;
    virtual void Reset() = 0;
};
```

这段接口代码的作用是把算法模块和应用框架隔开。`TerrainLodBuildInput` 把算法需要的输入限制在高度图、相机位置和细节层次参数里；`TerrainLodRenderPacket` 则把输出分成 CPU 网格和 GPU 缓冲区两类。这样渲染器不需要知道当前算法内部是指针树、索引节点池还是计算着色器，只需要看输出模式并执行对应绘制路径。

渲染器收到算法输出后，会根据输出模式分流。经典和 DOD 版本生成 CPU 网格，所以渲染器负责上传顶点和索引；GPU 类 ROAM 已经在 GPU 侧写好了顶点、索引和间接绘制参数，所以渲染器只需要绑定这些缓冲区并绘制。

```cpp
bool TerrainRenderer::RebuildTerrainLod(const glm::vec3& cameraPosition)
{
    TerrainLodBuildInput input{};
    input.HeightMap = &_heightMap;
    input.CameraPosition = cameraPosition;
    input.Settings = BuildLodSettingsFromPanel();

    TerrainLodRenderPacket packet{};
    if (!_terrainLodAlgorithm->BuildRenderData(input, packet, &_lastError))
    {
        return false;
    }

    // 统计口径也从算法接口统一收口，后续界面和报告都读同一份数据。
    _terrainLodStats = _terrainLodAlgorithm->Stats();

    if (packet.Mode == TerrainLodRenderMode::CpuMesh)
    {
        // 经典和 DOD 走这里：算法返回三角形，渲染器再上传。
        _meshData = std::move(packet.CpuMesh);
        return UploadMesh();
    }

    // GPU 类 ROAM 走这里：算法已经生成 GPU 缓冲区。
    return BindGpuTerrainBuffers(packet);
}
```

这里的逻辑是先统一输入，再根据输出模式分流。这个结构对报告很重要，因为第四章比较三种算法时，外部主循环、相机、窗口、光照和绘制入口都是同一套，差异主要来自算法内部，而不是三套完全不同的渲染代码。统计信息也在这里统一收口，每个算法最后都要把活跃三角形数、节点数、CPU 更新时间、GPU 计算时间、上传和读回等字段写进 `TerrainLodStats`。

运行时基准测试也是支撑代码的一部分。它按固定顺序切换经典、DOD 和 GPU 三个版本，并在每次切换后重置算法状态。这样做是为了避免前一个算法留下的持久拓扑影响后一个算法，也避免手动飞行测试带来的路径差异。

```cpp
void Application::BeginRuntimeBenchmarkAlgorithm()
{
    TerrainLodAlgorithmId algorithm = CurrentBenchmarkAlgorithm();

    // 每个算法都从同一个起点、同一个朝向、同一套参数开始。
    _runtimeBenchmark.StartPosition = ComputePathStart();
    _runtimeBenchmark.EndPosition = ComputePathEnd();
    _camera.SetPose(_runtimeBenchmark.StartPosition,
                    _runtimeBenchmark.FixedYawPitch);

    _terrainPanelState.TerrainLodAlgorithm = algorithm;
    ApplyTerrainPanelSettings();

    // 切换算法后清空持久拓扑，保证三种算法不互相继承状态。
    _terrainRenderer.ResetTerrainLodAlgorithm();
    _terrainRenderer.RequestMeshRebuild();
}
```

采样阶段会保存当前帧的相机位置、算法名称、构建配置、地形参数和渲染统计。这里我没有只记录一个帧率数字，因为只看帧率无法解释瓶颈。样本里同时包含细节层次总耗时、CPU 更新时间、CPU 上传耗时、GPU 计算耗时、快照构建耗时、三角形数量、节点数量和拓扑错误计数，后面第四章才能把“整体快不快”和“具体慢在哪里”拆开讲。

```cpp
void Application::RecordRuntimeBenchmarkSample(
    const FrameTiming& frameTiming,
    const TerrainRenderStats& terrainStats,
    const glm::vec3& cameraPosition)
{
    RuntimeBenchmarkSample sample{};
    sample.AlgorithmName = CurrentRuntimeBenchmarkAlgorithmName();
    sample.TimeSeconds = _runtimeBenchmark.ElapsedSeconds;
    sample.FrameMilliseconds = frameTiming.FrameMilliseconds;
    sample.CameraPosition = cameraPosition;
    sample.Stats = terrainStats;

    // 每个算法一组样本，后续汇总时既可以看平均值，也可以看最大值。
    _runtimeBenchmark.Results.back().Samples.push_back(sample);
}
```

这段采样代码的价值在于，它把视觉运行、算法统计和实验报告连接到一起。逐帧明细适合画曲线和查异常帧，汇总表适合放进报告里做横向对比。我在样本里保留拓扑错误计数，是因为性能数据不能脱离正确性讨论：如果某个算法很快，但同时出现 T 形裂缝、非法邻居或非法拓扑，那么这个速度就没有可比意义。

### 3.5 项目开发阶段规划与里程碑

项目开发可以概括为四个阶段。第一阶段是基础框架阶段，我先把 SDL2 窗口、OpenGL 上下文、GLM 相机、着色器、高度图加载和 Dear ImGui 面板跑通。这个阶段的验收标准不是性能，而是程序能稳定打开窗口、显示地形、移动相机，并且能在界面里调整参数。

第二阶段是经典 ROAM 阶段。我实现二叉三角树、分裂 / 合并、基边邻居、强制分裂和拓扑验证。这个阶段最重要的是正确性，因为如果经典基准版本都不可信，后面对比 DOD 和 GPU 就没有意义。这个阶段我也遇到了很多拓扑问题，比如路径编号撞号、近距离不细分、持久化拓扑后不再继续细分等。

第三阶段是 DOD 优化阶段。我把经典版本的基于指针的结构改成基于索引的节点池，并逐步加入线程池、并行误差评估、并行候选标记、保守并发拓扑提交和并行网格输出。这个阶段的验收标准是三角形数量和拓扑验证要和经典版本保持一致，同时基准测试中 CPU 更新耗时要明显下降。

第四阶段是 GPU 类 ROAM 和基准测试阶段。我加入 GPU 能力检测、SSBO、计算着色器、活跃叶节点压缩、GPU 网格输出、间接绘制和运行时基准测试。这个阶段的重点不是简单追求 GPU 比 DOD 快，而是拆开看 GPU 管线的瓶颈，尤其是 CPU-GPU 交界、读回、缓冲区分配和查询等待。

### 3.6 问题解决记录

本项目里我花了比较多时间做问题排查和修复，所以这一节不是简单列几个错误，而是记录我真正遇到过、定位过、并且对项目结构产生影响的问题。ROAM 这类算法的麻烦点在于，很多错误不是一运行就崩溃，而是表现为“近处不细分”“偶尔出现长三角形”“性能数据看起来不对”这种比较隐蔽的现象。如果只看最终代码，很容易忽略中间这些调试过程，但我认为这部分恰好是本项目最能体现工作量的地方。

#### 问题1：经典 ROAM 的路径编号撞号

属于阶段：这个问题出现在第二阶段经典 CPU ROAM 中，具体是在我接入合并、迟滞控制和跨帧历史状态统计之后。这个阶段已经不只是把三角形分裂出来，而是需要让节点在多帧之间保留身份，因此节点编号是否稳定、是否唯一，开始直接影响算法状态。

现象：一开始这个问题不是通过画面直接暴露的，而是在我检查分裂和合并的历史状态时发现数据不稳定。某些节点的历史状态会被另一棵根三角树里的节点污染，导致迟滞控制和合并统计出现难以解释的结果。表面上看，地形还能正常显示，但内部状态已经不可靠，这类问题如果继续往后做，很容易在合并阶段变成更隐蔽的拓扑错误。

定位：我最后定位到问题来自路径编号的生成方式。最初根三角树 A 从 1 开始，根三角树 B 从 2 开始，而子节点使用 `parentPathId * 2` 这种类似二叉堆的方式派生。这样写看起来很自然，但根三角树 A 的左子节点也会得到 2，正好和根三角树 B 的根节点撞号。也就是说，两棵根三角树使用了同一个编号空间，后续只要用路径编号做历史表键值，就一定会出现状态混淆。

排查过程：我一开始以为是合并判断或者迟滞控制阈值的问题，因为现象出现在合并历史统计附近。后来我把根节点和子节点的路径编号打印出来，才发现问题比阈值更基础：编号本身已经重复了。这个过程让我意识到，ROAM 的节点编号不能只当成调试用的标签，它实际上会影响跨帧状态追踪。

解决方案：我把两棵根三角树的路径编号命名空间分开，让根三角树 A 和根三角树 B 落在不同区间，子节点仍然保持二叉堆式的派生关系。这样既保留了路径编号的层级含义，也避免两棵树互相撞号。修复之后，迟滞控制和合并历史状态能稳定对应到正确节点，后续做持久化拓扑时也更安全。

#### 问题2：经典 ROAM 近距离细分变化很小

属于阶段：这个问题出现在第二阶段经典 CPU ROAM 中，发生在我完成初步分裂流程、几何误差和屏幕误差估算之后。当时程序已经能显示 ROAM 地形，但视觉效果还不像真正的视点相关细节层次。

现象：我把相机靠近地形时，线框看起来仍然比较粗，活跃三角形数量变化也不明显。按 ROAM 的直觉，近处应该明显细分，远处应该保持粗网格，但实际效果更像是整个地形都维持在一个比较保守的细分程度。这个问题如果只看实体渲染不太明显，但一打开线框和统计面板就能看出来。

定位：我定位后发现，早期分裂分数过度依赖高度误差。也就是说，如果某个区域本身比较平坦，基边中点的真实高度和插值高度差很小，那么即使相机离它很近，分数也可能不够大。这样算法就更像是在找“地形起伏大的地方”，而不是找“屏幕上需要更多细节的地方”。

排查过程：我先排除了渲染器没有更新网格、界面统计没有刷新、相机位置没有传入算法这几个可能性。后来我把不同相机距离下的分裂次数、实际达到深度和活跃三角形数量对比起来看，发现相机距离变化没有足够影响分数。再回到评分函数后，才确认问题在误差模型太单一。

解决方案：我把几何误差从单一中点采样扩展到三条边中点和重心采样，并在屏幕误差分数中加入近距离投影边长权重，同时提高默认最大深度。修复之后，相机靠近地形时，活跃三角形、实际达到深度和分裂次数都会明显变化，线框也能看到近处细、远处粗的效果。这个问题让我感觉到，细节层次算法里的“误差”不能只理解成高度误差，还必须考虑它最后投到屏幕上的可见程度。

#### 问题3：帧率最低只显示 10，导致性能判断失真

属于阶段：这个问题出现在第一阶段界面面板和运行时统计接入期间。它本身不是算法错误，但会直接影响我对算法性能的判断，所以也属于必须修复的问题。

现象：在某些很卡的情况下，界面面板里的帧率最低只显示到 10 左右，不会继续下降。表面上看程序只是“比较卡”，但无法判断是 100 毫秒一帧、300 毫秒一帧，还是更严重。对于后面分析经典 ROAM 的全局裂缝修复成本、数据导向优化收益和 GPU 管线瓶颈来说，这个数字会误导判断。

定位：我最后发现原因在主循环的时间步长处理。为了防止调试断点、窗口拖动或突然卡顿导致相机瞬移，我把时间步长限制到 0.1 秒以内。这个处理对相机移动是合理的，但早期我把限制后的时间步长同时用于帧率计算，于是当真实帧时间超过 100 毫秒后，帧率就被固定成了 `1 / 0.1 = 10`。

排查过程：这个问题的排查比较像“怀疑工具本身”。我先以为是 Dear ImGui 显示精度或者刷新频率问题，后来通过输出原始帧耗时，发现真实帧时间已经超过界面显示的范围。继续检查 `Application::ComputeFrameTiming` 后，确认是相机用的时间步长和性能统计用的时间步长混用了同一个值。

解决方案：我把帧时间统计拆成原始时间步长和限制后时间步长两个字段。相机更新继续使用限制后时间步长，避免卡顿后视角跳飞；帧率和帧毫秒数显示则使用原始时间步长，真实反映卡顿程度。修复后，性能面板可以显示真正的低帧率和高帧耗时，也让后续基准测试指标更可信。

#### 问题4：持久化拓扑后，相机移动不再继续触发细分

属于阶段：这个问题出现在第二阶段后半段，也就是经典 ROAM 从“每帧临时重建”改成“跨帧持久化拓扑”的时候。它是我在经典版本里遇到的一个很典型的问题，因为它不是某一行代码写错，而是算法状态模型改变后，原来的入口逻辑不再成立。

现象：完成持久化拓扑后，程序第一帧可以生成 ROAM 网格，但后面移动相机时，地形细分几乎不再变化。界面里的本帧分裂数量长时间为 0，线框看起来也像停在第一次构建的状态。这个现象一开始很容易误判为统计口径变化，因为持久化后“本帧分裂”和“当前活跃分裂”确实不是同一个概念，但继续观察画面后可以确认，实际拓扑也没有继续响应相机移动。

定位：问题出在分裂优先队列的入口集合。早期每一帧都会从根节点重新构建拓扑，所以把根三角树 A 和根三角树 B 入队是正确的，因为它们每帧都是叶节点。改成持久化拓扑后，根节点第一帧分裂之后就已经不是叶节点了。如果后续仍然只尝试从两个根节点入队，`enqueueCandidate` 会直接拒绝内部节点，队列自然为空，当前真正活跃的叶节点根本没有机会重新计算屏幕误差。

排查过程：我先检查了渲染链路，确认网格上传和绘制调用正常；又检查了相机位置传入，确认 `BuildRenderData` 收到的位置确实在变化。然后我把注意力转回算法层，跟踪分裂队列的候选数量，发现第二帧以后队列入口就几乎为空。这个现象和“根节点已经不是叶节点”完全对应，才最终定位到入口集合没有随着持久化拓扑改变。

解决方案：我在 `RefineWithSplitQueue` 中新增了活跃叶节点遍历逻辑，从根三角树 A、根三角树 B 递归向下走，遇到内部节点就继续访问子节点，遇到活跃叶节点才调用原来的 `enqueueCandidate`。这样每次相机触发重建时，当前所有活跃叶节点都能重新进入候选队列，按照新的相机位置重新计算分数。修复之后，不同相机位置下活跃三角形、活跃分裂、本帧分裂和合并都会变化，经典 ROAM 才真正成为一个跨帧维护的动态细节层次算法。

#### 问题5：缺少统一的运行时性能测试流程

属于阶段：这个问题出现在第三阶段到第四阶段之间，也就是数据导向版本已经能跑、GPU 版本也开始接入之后。它不是某个算法的错误，而是实验方法本身的问题。

现象：一开始我主要靠手动移动相机观察界面，或者运行无窗口的算法层基准测试。这样可以发现局部问题，但很难写进报告。因为手动移动相机不能保证每次路径一样，无窗口基准测试又测不到真实渲染循环、网格上传、OpenGL 上下文和界面参数。经典、数据导向和 GPU 三个版本如果不是在同一条路径、同一组参数、同一段时间里跑，最后的性能对比就不够有说服力。

定位：问题的根源是项目缺少一个贯穿应用主循环、相机、地形渲染器和报告输出的标准流程。已有基准测试更偏算法层，运行时界面又只显示当前帧状态，两者都不能直接承担论文第四章的实验数据来源。另外，渲染器里还有相机位移缓存，如果直接用普通交互路径采样，会把“复用上一帧网格”和“真实重新构建网格”的帧混在一起。

排查过程：我回看前面几次性能问题的定位方式，发现每次都需要临时跑不同命令、手动观察不同指标，数据很难横向对齐。尤其是 GPU 版本接入后，如果没有真实 OpenGL 上下文，就无法测 GPU 阶段；如果只手动看界面，又没法稳定复现同一条路径。这个矛盾说明我需要先把实验工具做出来，而不是急着下性能结论。

解决方案：我实现了运行时基准测试。程序会保存当前界面参数和相机姿态，然后按经典、数据导向、GPU 的顺序运行同一条固定相机路径，每个算法采样约 10 秒，并自动输出 Markdown 和 CSV。采样字段包括帧耗时、细节层次耗时、CPU 更新耗时、CPU 上传耗时、GPU 计算、快照构建、读回等待、三角形数、节点数和 CPU 利用率。这个功能后来成为第四章实验分析的基础，也让我能把“感觉哪个快”改成“同一流程下数据怎么表现”。

#### 问题6：GPU 类 ROAM 偶发生成跨场景长三角形

属于阶段：这个问题出现在第四阶段 GPU 类 ROAM 接入活跃叶节点压缩、网格输出和间接绘制的时候。它属于 GPU 路径里比较严重的问题，因为它直接破坏画面，而且不是普通的细节层次裂缝。

现象：GPU 版本有时会在地形上生成一条特别长的异常三角形，看起来像从地面某个点被拉到很远的位置。这个现象不是传统 T 形裂缝，因为 T 形裂缝通常发生在相邻三角形边界，表现为小裂缝或接缝闪烁；这里是整个三角形的定义域数据已经不可信，属于更底层的 GPU 缓冲区或拓扑数据问题。

定位：我最后定位到两个原因叠在一起。第一，节点着色器存储缓冲区只上传了有效节点前缀，但活跃叶节点压缩扫描的是整个节点容量，缓冲区尾部未定义数据可能被误判为活跃叶节点。第二，GPU 分裂阶段早期是先增加已分配节点数量，再尝试提交分裂，如果并发提交失败，就可能留下已经计数但内容没有完整写好的节点槽位。网格输出读取这些非法叶节点后，就会把错误的定义域坐标转成世界坐标，最后画出跨场景长三角形。

排查过程：我先排除了背面剔除、绕序和着色器变换矩阵的问题，因为如果是这些问题，三角形会整体朝向错误或被剔除，而不是偶发从某个非法位置拉出去。然后我把注意力放到 GPU 缓冲区的有效范围上，检查活跃叶节点压缩的输出数量、已分配节点数量和网格输出输入索引，才发现它们没有使用同一个有效上界。这个问题也提醒我，GPU 缓冲区的容量和当前有效数量必须严格分开。

解决方案：我把活跃叶节点压缩改成以 GPU `allocatedNodeCount` 为有效上界，并排除已经分裂的内部节点。GPU 分裂阶段改成先原子锁定父节点，再分配子节点，避免失败时留下未完成节点。网格输出也加入防御性校验，对节点索引、叶节点标记和定义域坐标范围做检查，异常叶节点输出退化三角形，而不是继续生成越界几何。修复后，GPU 冒烟测试可以连续强制执行 GPU 拓扑、活跃叶节点压缩、网格输出和间接绘制，不再出现这类异常长三角形。

#### 问题7：GPU 类 ROAM 被资源上传、缓冲区重分配和同步读回拖慢

属于阶段：这个问题出现在第四阶段 GPU 性能分析期间。GPU 版本功能上已经能跑，但实验结果显示它没有超过数据导向版本，这时就需要拆分管线，而不能只看一个总耗时。

现象：运行时基准测试中，GPU 计算时间很低，但 GPU 类 ROAM 的完整细节层次耗时和帧耗时仍然高于数据导向版本。也就是说，着色器本身算得很快，但整个 GPU 版本并没有真正快起来。如果只看 `gpuComputeMilliseconds`，会觉得 GPU 很成功；如果看最终帧耗时，又会觉得 GPU 不如数据导向版本。这个矛盾说明瓶颈不在一个单独指标里。

定位：我检查 `GpuRoamMeshBuilder` 后发现，早期实现每帧都会重新上传高度图纹理，每帧对 SSBO、VBO、IBO 和间接绘制缓冲区调用 `glBufferData`，并且在 `glEndQuery` 后立刻读取 `GL_QUERY_RESULT`。这些操作都会引入 CPU-GPU 同步或驱动侧重分配。换句话说，GPU 计算阶段只是整条管线的一部分，真正拖慢的是资源生命周期和同步策略。

排查过程：我先把 GPU 统计拆成计算耗时、快照构建、缓冲区分配、调度墙钟耗时、查询等待和读回等待等字段，然后对比数据导向版本的 CPU 更新耗时和上传耗时。拆开之后就能看出，旧版 GPU 管线的很多时间花在 CPU-GPU 交界，而不是着色器算法本身。这个排查过程对我影响很大，因为它说明“把代码搬到 GPU”不等于“系统会变快”，还必须让数据留在合适的位置。

解决方案：我把高度图纹理改成只在加载或切换高度图时上传；SSBO、VBO、IBO 和间接绘制缓冲区改成容量复用，容量足够时只更新实际使用的前缀；计时查询和计数器读回改成四槽环形缓冲区，延迟几帧读取，尽量避免当前帧立刻等待 GPU；同时保留分项统计，方便继续观察瓶颈。修复后，缓冲区分配和查询等待明显降低，GPU 版本的瓶颈也变得更清楚：现在主要问题转向快照构建、读回等待和仍然存在的 CPU 拓扑基准版本。

#### 问题8：Windows 上 CPU 利用率统计被低估

属于阶段：这个问题出现在第四阶段运行时基准测试的跨平台性能分析期间。数据导向版本已经使用了多线程，但统计结果一开始没有正确反映出来。

现象：数据导向版本在 macOS 上曾经能显示 300% 以上的 CPU 利用率，这符合“一个逻辑核心满载为 100%，多线程可以超过 100%”的口径。但在 Windows RTX 5090 D 测试环境下，经典、数据导向和 GPU 的 CPU 利用率长期都接近 100%。这看起来像数据导向版本没有真正用上多核，可是工作线程数和实际耗时下降又说明多线程确实生效了。

定位：我最后定位到性能剖析接口的跨平台语义问题。早期实现用 `std::clock()` 估算进程 CPU 时间，但 Windows / MSVC 下它不能可靠表示进程所有线程累计 CPU 时间，结果会接近墙钟时间，导致多线程利用率被低估。问题不在线程池，也不在任务分发，而在统计工具本身。

排查过程：我先对比了数据导向版本的工作线程数量、CPU 更新耗时和经典版本的耗时，发现数据导向版本明显更快，而且工作线程数量能达到 8，这说明算法不是单线程跑的。随后我检查 `TerrainLodProfiling.h`，确认 CPU 利用率公式本身没问题，问题是输入的进程 CPU 时间不可靠。这个过程和问题3有点相似，都是性能分析工具本身影响了判断。

解决方案：我把 Windows 路径改成使用 `GetProcessTimes(GetCurrentProcess(), ...)` 读取进程内核态时间和用户态时间，再按 FILETIME 的 100ns tick 转成毫秒；macOS / Linux 路径则使用 `getrusage(RUSAGE_SELF)` 读取用户态和系统态时间。修复后，数据导向版本在正式基准测试中能显示 300% 以上 CPU 利用率，和工作线程数、性能提升都能对应起来。这个问题让我认识到，性能报告里不仅算法要正确，统计口径也必须可靠。

## 第四章 测试与结果分析

### 4.1 数据来源与可比口径

本章只使用项目基准测试已经落盘的数据进行分析。原始样本位于 `benchmark-output/report-experiments/experiment-*/*.csv`，每组实验的聚合表位于对应目录下的 `summary.csv` 和 `analysis_metrics.csv`，图表由 `scripts/generate_report_experiment_charts.py` 从这些表重新生成。正文中的平均值、P95、阶段耗时和占比都从这些表格推导，不再只凭单次运行窗口读数判断。

实验环境保持一致：CPU 为 AMD Ryzen 9 9950X3D，GPU 为 NVIDIA GeForce RTX 5090 D，OpenGL 渲染器为 `NVIDIA GeForce RTX 5090 D/PCIe/SSE2`，OpenGL 版本为 `4.3.0 NVIDIA 591.86`。程序使用 RelWithDebInfo 构建，窗口分辨率为 1280×720，基准测试期间关闭垂直同步。默认组使用 `Hm_Terrain_Test_129.pgm`，主要参数为最大深度 20、距离权重 80、分裂阈值 0.04、合并阈值 0.02。相机路径从地形 Z+ 边中点上方移动到地形中心上方，每种算法运行约 10 秒。

可比性主要看三点。第一，三种算法在同一 case 下使用同一高度图、相机路径和误差参数。第二，`summary.csv` 中各组 `maxTopologyIssues` 均为 0，说明参与统计的数据没有出现拓扑验证错误。第三，正文分析不只比较 LOD 总耗时，还同时比较平均三角形数、每万三角形耗时、CPU 利用率、`splitMilliseconds`、`mergeMilliseconds`、`emitMilliseconds`、`validateMilliseconds` 和 GPU 管线分项。

图4-1 到图4-3 用作视觉正确性佐证，不参与性能结论推导，后续排版时应放程序截图：图4-1 放实体地形渲染截图，图4-2 放线框模式下近细远粗的 LOD 截图，图4-3 放 LOD 调试着色或 Dear ImGui 性能面板截图。真正参与本章推导的是后面由脚本生成的 SVG 图表。

### 4.2 默认参数下的总体结果与阶段拆分

实验 1 是默认参数的三轮重复测试。三种算法的平均三角形数都在 3.12 万到 3.15 万之间，DOD 和 GPU 类版本相对经典版本的三角形数差异分别约为 0.61% 和 0.68%。因此这一组数据可以认为是在相近输出规模下比较构建成本，而不是某个版本靠显著减少几何量取胜。

| 算法 | 平均 LOD/ms | P95 LOD/ms | 平均三角形数 | CPU 利用率 | 每万三角形 LOD/ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Classic CPU ROAM | 24.65 | 35.39 | 31235 | 96.75% | 7.89 |
| Data-Oriented CPU ROAM | 10.43 | 14.09 | 31426 | 323.52% | 3.32 |
| GPU ROAM-like | 13.38 | 17.08 | 31449 | 230.13% | 4.25 |

![图4-4：实验1平均 LOD 耗时对比](../../benchmark-output/report-experiments/experiment-01-baseline/chart_avg_lod_ms.svg)

![图4-5：实验1平均三角形数对比](../../benchmark-output/report-experiments/experiment-01-baseline/chart_avg_triangles.svg)

![图4-6：实验1 LOD 时间组成](../../benchmark-output/report-experiments/experiment-01-baseline/chart_exp01_lod_composition.svg)

![图4-7：实验1 ROAM 阶段耗时组成](../../benchmark-output/report-experiments/experiment-01-baseline/chart_exp01_roam_stage_composition.svg)

从总耗时看，DOD 的平均 LOD 为 10.43 ms，比经典版本低 57.68%；GPU 类版本为 13.38 ms，比经典版本低 45.71%，但比 DOD 高 28.28%。这个排序和 CPU 利用率能对应起来：经典版本约 96.75%，基本是单核主导；DOD 达到 323.52%，说明多线程阶段确实吃到了并行；GPU 类版本为 230.13%，说明它并不是 CPU 完全退出，而是仍然保留了 CPU 拓扑和 CPU-GPU 交界工作。

阶段数据比总耗时更能解释差距。经典版本的 `emitMilliseconds` 平均为 12.17 ms，约占自身 LOD 总耗时的一半，是最大的单项成本；分裂和合并合计约 7.01 ms。DOD 的网格输出降到 3.14 ms，相比经典版本下降 74.20%；分裂从 4.00 ms 降到 2.08 ms，合并从 3.00 ms 降到 1.26 ms。这说明 DOD 的主要收益不是某个单点优化，而是同时降低了网格输出和拓扑维护的 CPU 成本，其中网格输出收益最明显。

GPU 类版本的 CPU `emitMilliseconds` 为 0，这是因为网格输出转移到 GPU 管线；但这并不等于输出阶段没有代价。图4-6 中可以看到，GPU 类版本新增了快照构建、上传、GPU dispatch、GPU compute 和读回等分项。换句话说，它把经典和 DOD 中的 CPU emit 成本换成了 GPU 管线成本。当前数据里这个交换是有效的，因为它仍然明显快于经典版本；但它还不足以超过 DOD，因为 CPU 拓扑和数据边界没有消失。

### 4.3 最大深度实验：上限参数只在未饱和前有效

实验 2 将最大深度设为 12、14、16、18、20，其余参数保持默认。这个实验最重要的不是“深度越高越慢”这句话，而是观察深度上限何时真正限制了工作量，何时已经不再是主导因素。

![图4-8：实验2最大深度对平均 LOD 耗时的影响](../../benchmark-output/report-experiments/experiment-02-max-depth/chart_exp02_depth_lod_lines.svg)

![图4-9：实验2最大深度对平均三角形数的影响](../../benchmark-output/report-experiments/experiment-02-max-depth/chart_exp02_depth_triangles_lines.svg)

深度 12 到 14 是最明显的跳变区间。平均三角形数从约 8116 个增加到约 26100 个，约为 3.2 倍；经典版本 LOD 从 3.65 ms 增加到 16.88 ms，DOD 从 2.24 ms 增加到 7.54 ms，GPU 类版本从 3.11 ms 增加到 10.09 ms。这里三角形数和耗时同时大幅上升，说明深度上限确实限制了可细分层级。

深度 16 之后，曲线进入平台区。经典版本在 16、18、20 时分别为 24.13 ms、25.27 ms、24.87 ms；DOD 分别为 9.80 ms、9.90 ms、9.77 ms；GPU 类版本分别为 13.22 ms、13.59 ms、13.32 ms。对应的平均三角形数基本停在 3.12 万到 3.15 万之间。`summary.csv` 也显示深度设置为 18 和 20 时，实际 `maxDepthReached` 最高仍为 17。由此可以推导出：在这条相机路径和这组阈值下，最大深度超过实际误差需求后，继续提高上限并不会继续制造更多工作量。

阶段上也能看到这个平台效应。经典版本的 emit 在深度 12、14、16、18、20 下约为 2.60、10.09、12.06、12.29、12.18 ms；DOD 的 emit 约为 0.68、2.17、2.84、2.84、2.86 ms。深度 14 以后，emit 的变化主要跟随活跃三角形数，而不是跟随设置值本身。分裂阶段也类似：经典版本从深度 16 开始稳定在 3.8 到 4.1 ms 左右，DOD 稳定在 1.9 到 2.0 ms 左右。

这一组实验的成熟结论是：最大深度是细分上限，不是实际负载的线性旋钮。低于需求层级时，它会强烈限制三角形规模和耗时；高于需求层级后，真正决定负载的是误差函数、相机路径和地形局部变化。报告中后续讨论 ROAM 性能时，不能只引用 `maxDepthSetting`，必须同时引用实际三角形数和 `maxDepthReached`。

### 4.4 距离权重实验：主动改变细分规模

实验 3 将距离权重设为 20、40、60、80。和最大深度不同，距离权重直接改变误差函数对近处和中距离区域的敏感程度，所以它更像是主动调节实际工作量。

![图4-10：实验3距离权重对平均三角形数的影响](../../benchmark-output/report-experiments/experiment-03-distance-scale/chart_exp03_distance_triangles_lines.svg)

![图4-11：实验3距离权重对平均 LOD 耗时的影响](../../benchmark-output/report-experiments/experiment-03-distance-scale/chart_exp03_distance_lod_lines.svg)

距离权重从 20 增加到 80 时，经典版本平均三角形数从 7336 增加到 31251，约为 4.26 倍；DOD 从 7293 增加到 31463，约为 4.31 倍；GPU 类版本从 7409 增加到 31352，约为 4.23 倍。三种算法的几何规模增长非常接近，说明这一组可以用于比较算法对负载增长的响应。

耗时增长并不完全相同。经典版本 LOD 从 3.88 ms 增加到 25.29 ms，约为 6.52 倍，明显高于三角形数增长倍数；DOD 从 2.05 ms 增加到 10.06 ms，约为 4.92 倍；GPU 类版本从 3.04 ms 增加到 13.65 ms，约为 4.50 倍。经典版本呈现更强的超线性增长，原因可以从阶段数据看出来：它的 emit 从 2.38 ms 增加到 12.34 ms，split 从 0.59 ms 增加到 4.18 ms，两个阶段都随规模显著放大。

DOD 的增长更接近三角形规模本身。它的 emit 从 0.58 ms 增加到 2.86 ms，约为 4.94 倍；split 从 0.51 ms 增加到 1.91 ms，约为 3.75 倍。这说明 DOD 对网格输出的并行化基本按输出规模增长，没有出现经典版本那样明显的额外放大。GPU 类版本的 GPU compute 从 0.28 ms 增加到 0.65 ms，只增加约 2.29 倍，但快照构建从 0.39 ms 增加到 1.72 ms，读回从 0.39 ms 增加到 1.98 ms，反而更贴近数据规模增长。

这组实验给出的结论比“DOD 快”更具体：当实际三角形数被距离权重推高时，经典版本最先暴露的是 CPU emit 和 split 的增长；DOD 把 emit 降低到比较接近线性输出成本；GPU 类版本的 shader compute 增长最慢，但快照和读回会随数据规模一起增长。因此，GPU 版本如果要在更大规模下超过 DOD，主要突破点不是继续压低 0.65 ms 左右的 compute，而是减少随规模增长的 CPU-GPU 数据边界成本。

### 4.5 高度图实验：输入分辨率不等于实际负载

实验 4 对比 `Hm_Terrain_Test_129.pgm` 和 `Hm_Terrain_Peking_513.png`。需要注意的是，`summary.csv` 中记录的 Peking 文件实际宽高为 547×547，而 Test 文件为 129×129。单看输入分辨率，Peking 文件更大；但 ROAM 的运行负载取决于误差函数在当前路径上实际触发了多少活跃三角形。

![图4-12：实验4不同高度图的平均三角形数](../../benchmark-output/report-experiments/experiment-04-heightmap/chart_exp04_heightmap_triangles.svg)

![图4-13：实验4不同高度图的平均 LOD 耗时](../../benchmark-output/report-experiments/experiment-04-heightmap/chart_exp04_heightmap_lod_percentiles.svg)

数据结果和输入分辨率直觉相反。Peking 文件下三种算法的平均三角形数约为 1.08 万，而 Test129 下约为 3.13 万，Peking 只有 Test129 的约 34.5%。对应地，经典版本 LOD 从 Test129 的 25.85 ms 降到 Peking 的 6.17 ms，DOD 从 9.75 ms 降到 3.35 ms，GPU 类版本从 13.22 ms 降到 4.27 ms。

这个结果说明，当前实验路径附近的实际误差分布比高度图原始分辨率更重要。更高分辨率提供的是潜在细节上限，但如果相机路径经过的区域没有触发足够深的细分，算法不会自动生成更多叶节点。反过来，低分辨率高度图也可能因为形状、阈值和相机路径组合，生成更多活跃三角形。

单位三角形成本也提供了额外信息。DOD 在两张图上的每万三角形 LOD 耗时几乎一致，Peking 为 3.09 ms，Test129 为 3.10 ms，说明 DOD 的成本主要跟输出规模走。经典版本在 Peking 上为 5.71 ms，在 Test129 上为 8.25 ms，说明高负载下它的单位成本会变差。GPU 类版本分别为 3.94 ms 和 4.21 ms，也比经典版本稳定，但仍高于 DOD。

因此，这一组不能得出“Peking 地形更简单”这样的泛化结论，只能得出和本实验强相关的结论：在当前路径和阈值下，实际活跃三角形数比输入图像分辨率更能解释耗时。后续如果扩展实验，应增加多条相机路径，并在表格中同时报告输入分辨率、实际三角形数和单位三角形耗时。

### 4.6 GPU 管线实验：慢点不在 shader compute

实验 5 使用默认参数专门拆分 GPU 类 ROAM 的管线耗时。该组数据中，GPU 类版本平均 LOD 为 13.43 ms，平均三角形数为 31301，和实验 1 的默认组规模一致，因此可以用来解释 GPU 类版本为什么快于经典版本但慢于 DOD。

![图4-14：实验5 GPU 类 ROAM 的 LOD 时间占比](../../benchmark-output/report-experiments/experiment-05-gpu-breakdown/chart_exp05_gpu_pipeline_share.svg)

![图4-15：实验5 GPU 类 ROAM 沿相机路径的耗时变化](../../benchmark-output/report-experiments/experiment-05-gpu-breakdown/chart_exp05_gpu_timing_over_time.svg)

![图4-16：实验5 GPU 类 ROAM 的 ROAM 阶段占比](../../benchmark-output/report-experiments/experiment-05-gpu-breakdown/chart_exp05_roam_stage_share.svg)

![图4-17：实验5 GPU 类 ROAM 阶段耗时变化](../../benchmark-output/report-experiments/experiment-05-gpu-breakdown/chart_exp05_roam_stage_timing_over_time.svg)

| GPU 类 ROAM 分项 | 平均耗时/ms | 占 LOD 比例 |
| --- | ---: | ---: |
| CPU update | 5.34 | 39.80% |
| CPU upload | 0.86 | 6.37% |
| GPU snapshot | 1.68 | 12.53% |
| GPU dispatch | 0.26 | 1.92% |
| GPU compute | 0.68 | 5.04% |
| GPU readback | 1.99 | 14.83% |
| Other LOD | 2.62 | 19.49% |

最关键的数字是 `gpuComputeMs=0.68 ms`，只占 LOD 总耗时约 5.04%。这说明当前 GPU 类 ROAM 不是因为计算着色器本身慢才落后于 DOD。真正占比较高的是 CPU update、GPU snapshot 和 GPU readback，三者合计约 9.02 ms，占 LOD 的 67.16%。如果再加上 CPU upload 和 dispatch，CPU-GPU 边界相关成本接近 4.79 ms，已经远高于 shader compute。

阶段图也支持同一个判断。GPU 类版本的 CPU emit 为 0，但 `splitMilliseconds` 仍为 2.09 ms，`mergeMilliseconds` 仍为 1.06 ms，`otherStageMs` 达到 10.27 ms。这里的 `otherStageMs` 不是一个独立算法阶段，而是 “LOD 总耗时减去 split、merge、emit、validate 后剩余的部分”，在 GPU 类版本中主要对应 CPU update、快照、上传、GPU 执行、读回和其他管线开销。也就是说，网格输出虽然已经转移到 GPU，但完整帧仍被混合管线的组织成本限制。

前面问题记录中提到的缓冲区容量复用、避免每帧重传高度图、延迟读取查询结果等修改，在这组数据里也能看到效果：`gpuAllocMs` 和 `gpuQueryWaitMs` 都接近 0。它们已经不是主要瓶颈。剩下的核心问题是拓扑状态仍由 CPU DOD 路径维护，GPU 每帧还要接收快照并把部分结果读回 CPU。只要这个数据往返还在，GPU 类版本就更像是“GPU 加速的混合 ROAM”，而不是完整 GPU 驱动的 ROAM。

### 4.7 本章结论

综合五组实验，可以得到几条和本项目直接相关的结论。

第一，在当前机器、当前路径和当前参数下，DOD CPU ROAM 是三种实现中最稳的总体方案。它在默认组中把平均 LOD 从经典版本的 24.65 ms 降到 10.43 ms，同时保持几乎相同的三角形规模。更重要的是，它降低的是具体阶段成本：emit 下降 74.20%，split 下降 48.10%，merge 下降 58.16%，而不是通过降低画面细节换速度。

第二，经典 CPU ROAM 的最大成本不是单纯“算法老”，而是在这组实现里 CPU update、网格输出和拓扑阶段都更重。默认组中经典 emit 为 12.17 ms，是最大单项；距离权重提高时，它的 LOD 增长 6.52 倍，高于三角形数的 4.26 倍。这说明经典版本在高活跃三角形规模下会出现单位成本变差。

第三，GPU 类 ROAM 的 shader compute 很快，但完整系统没有因此自动超过 DOD。实验 5 中 GPU compute 只有 0.68 ms，占 5.04%；而 CPU update、快照和读回合计占 67.16%。因此，当前 GPU 类版本的短板不在“GPU 算不动”，而在 CPU 拓扑仍存在、快照仍要构造、数据仍要跨 CPU-GPU 边界往返。

第四，真正解释负载的是实际活跃三角形数和阶段组成，而不是单个配置名。最大深度超过实际需求后会平台化；距离权重能更直接地推高活跃三角形数；高度图输入分辨率更高也不必然更慢。本项目后续继续做性能结论时，应始终同时报告 `avgTriangles`、`maxDepthReached`、每万三角形耗时和 split/merge/emit/GPU 分项。

如果把结论压缩成一句话：这组实验支持“ROAM 的批量扫描、误差评估和网格输出适合数据导向与并行化，但分裂、合并、无裂缝拓扑维护和 CPU-GPU 状态边界仍然决定上限”。这不是泛泛地说哪个算法快，而是本项目五组数据共同推出来的结果。

## 第五章 总结与展望

### 5.1 已完成的工作

本项目首先完成了基础渲染与交互框架。我基于 C++20、CMake、SDL2、OpenGL、GLM 和 Dear ImGui 搭建了一个完整的实时地形渲染程序，实现了窗口创建、OpenGL 上下文、相机控制、输入处理、着色器编译、地形绘制和界面调试面板。程序支持高度图地形加载、纹理采样、基础光照、线框显示和运行时参数调整。

在经典 CPU ROAM 方面，我实现了基于二叉三角树的传统 ROAM。这个版本支持分裂 / 合并、迟滞控制、基边邻居、菱形强制分裂和拓扑验证，并统计 T 形裂缝、非法邻居和非法拓扑。它是整个项目的正确性基准版本，也帮助我理解 ROAM 的拓扑约束为什么比普通网格简化更复杂。

在数据导向 CPU ROAM 方面，我把经典版本的基于指针的节点改成基于索引的节点池，并使用更适合连续扫描的数据组织方式。我对误差评估、活跃叶节点收集、候选标记、部分拓扑提交和网格输出做了多线程处理。基准测试结果显示，DOD 版本明显降低了 CPU 更新耗时，是当前整体性能最好的版本。

在 GPU 类 ROAM 方面，我实现了 GPU 能力检测，并使用 SSBO 存储节点缓冲区、活跃叶节点缓冲区、候选缓冲区和计数缓冲区。我实现了 GPU 活跃叶节点压缩、误差评估、候选标记、仅分裂实验、GPU 网格输出和间接绘制。GPU 版本仍然保留 DOD CPU 拓扑基准版本，但它证明了 ROAM 中一些高度并行阶段确实可以迁移到 GPU。

在基准测试与数据分析方面，我实现了固定相机路径运行时基准测试，可以自动输出 Markdown 和 CSV，并统计帧耗时、LOD 耗时、CPU 更新耗时、CPU 上传耗时、GPU 计算、快照构建、读回等待、三角形数、节点数和 CPU 利用率。通过多组参数测试，我对经典、DOD 和 GPU 三个版本做了横向比较，也找到了 GPU 版本当前最主要的瓶颈。

### 5.2 存在的不足

当前最大的不足是 GPU 版本还不是完整 GPU 拓扑。GPU 类 ROAM 仍然依赖 DOD CPU 拓扑，GPU 主要负责并行阶段和网格输出。完整分裂 / 合并 / 无裂缝拓扑更新还没有完全放到 GPU 上，这限制了 GPU 版本的最终性能。

第二个不足是 CPU-GPU 数据交界仍然是瓶颈。实验中 GPU 计算时间较低，但快照构建、上传和读回等待仍然占据明显时间。这说明把算法搬到 GPU 不只是写计算着色器，还必须考虑数据驻留、同步策略和读回延迟。如果每帧仍然由 CPU 构造完整快照，那么 GPU 很难真正成为主导。

第三个不足是基准测试场景还不够丰富。当前主要使用固定相机路径，高度图数量也有限，没有覆盖更多真实开放世界地形、极端视角和长时间飞行路径。虽然这些测试已经能说明当前项目的主要结论，但如果要进一步接近真实游戏环境，还需要更多地形数据集和相机路径。

第四个不足是调试可视化还可以增强。目前已有线框、LOD 状态、统计面板和调试着色，但还可以加入更清晰的深度热力图、强制分裂高亮和菱形对可视化。ROAM 的拓扑行为比较抽象，如果可视化更直观，报告展示和后续调试都会更容易。

### 5.3 未来可优化方向

未来最重要的方向是 GPU 持久化拓扑。理想情况下，GPU 应该长期持有节点池，而不是每帧从 CPU DOD 状态构造完整快照。这样可以减少 CPU-GPU 上传，也能让 GPU 版本不再只是“DOD 后处理加速器”。这需要 GPU 侧分配、空闲列表、分裂和合并的并发安全设计。

第二个方向是脏范围更新和持久映射缓冲区。即使短期内不做完整 GPU 拓扑，也可以只上传变化的节点范围，而不是整帧上传节点缓冲区。持久映射缓冲区也可能减少上传开销，让 CPU-GPU 数据交界更平滑。

第三个方向是更完整的 GPU 分裂 / 合并。目前 GPU 仅分裂只是实验层，后续可以尝试 GPU 端菱形分裂、合并和邻居修复。不过这部分难点很大，因为并发写拓扑时必须避免多个调用实例同时修改同一组邻居。

第四个方向是网格着色器或计算驱动管线。如果使用更现代的图形 API 或网格着色器，可以减少 CPU 绘制提交，并探索更完整的 GPU 驱动地形渲染。虽然本项目使用 OpenGL 4.3，但这个方向对于理解现代 GPU 地形渲染仍然有参考意义。

第五个方向是和其他地形 LOD 算法对比。后续可以加入 Geometry Clipmap、Chunked LOD、CDLOD、CBT 等方法，把 ROAM 放到更大的地形 LOD 框架中比较。这样可以更清楚地说明 ROAM 在现代硬件下的优点和缺点，而不是只在 ROAM 内部比较经典、DOD 和 GPU。

最后一个方向是更系统的实验数据集。未来可以增加更多高度图、更多相机路径和更长时间的基准测试，并统计 P50、P90、P99，而不仅是平均值和最大值。这样报告中的性能结论会更稳定，也更接近真实游戏运行时的性能分析方式。

## 参考文献

[1] Duchaineau, M. A., Wolinsky, M., Sigeti, D. E., Miller, M. C., Aldrich, C., Mineev-Weinstein, M. B. ROAMing Terrain: Real-time Optimally Adapting Meshes. IEEE Visualization, 1997. https://www.osti.gov/biblio/621480-roaming-地形-real-time-optimally-adapting-网格es

[2] Duchaineau, M. A. et al. ROAMing Terrain PDF. Lawrence Livermore National Laboratory. http://www.llnl.gov/graphics/ROAM/roam.pdf

[3] White, S. Terrain Visualization in Games. International Journal of Computer Games Technology, 2008. https://www.hindawi.com/journals/ijcgt/2008/753584/

[4] Khronos Group. Khronos Releases OpenGL 4.3 Specification with Major Enhancements. 2012. https://www.khronos.org/news/press/khronos-releases-opengl-4.3-specification-with-major-enhancements

[5] arXiv:2603.18398. Open-world game mission design analysis. https://arxiv.org/abs/2603.18398

[6] arXiv:2606.14919. Entity Component System / data-oriented concurrent game engine research. https://arxiv.org/abs/2606.14919

[7] Losasso, F., Hoppe, H. Geometry Clipmaps: Terrain Rendering Using Nested Regular Grids. ACM Transactions on Graphics, 2004.

[8] Luebke, D. et al. Level of Detail for 3D Graphics. Morgan Kaufmann, 2003.
