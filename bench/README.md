# bench — 两套阴影实现的等价性 / 性能对比实验台

独立于 `../shadowmap ../cascade ../pcf`，不修改原模块。完整结论见 [REPORT.md](REPORT.md)。

## 两套实现

唯一差异点在 `src/bias.h` 的斜率因子 $s=\tan(\arccos x)=\sqrt{1-x^2}/x$：

- `ref_d`：double 中间精度 + libm `tan(acos())`（慢，无角度钳制）
- `nat_f`：float 代数式 `sqrtf(1-x²)/max(x,1e-4)`（快，沿用 shadowmap.c 原式，带钳制约束）
- `fast_k`：位魔法 rsqrt + k 次牛顿迭代，作为隔离"中间精度"的旋钮，不是候选

## 一键复现

```bash
make
./run_all.sh          # 约 45s，生成 artifacts/csv 与 artifacts/figs
```

## 命令

| 命令 | 作用 |
|---|---|
| `./bench compare <elev> <r> <mapmode> <A> <B> [tag]` | 逐像素等价性 + 四阶段计时，输出 PGM |
| `./bench funnel ...` | L1 bias→L2 原始 tap→L3 加偏 tap→L4 PCF→L5 像素 五层差异探针 |
| `./bench sweep-elev <r> <mode> <strat> [hi lo step]` | 光高角扫描 |
| `./bench sweep-k <elev> <r> <mode>` | 牛顿步数 k 的性能/精度/画面可控性曲线 |
| `./margin ...` | 15.36M tap 的裕量带分布 + 误差增益 G 因果实验 |
| `./diag ...` | 最差像素定位、逐 tap 翻转还原 |
| `./unit_trigger` | 画面失效充要条件的单元级证明 |

策略参数：`ref` / `nat` / `fast:<k>`；`mapmode`：0=f32，1=f16，2=u16。

## 目录

```
src/common.h       场景、光空间、AABB 求交、量化配置
src/bias.h         三套斜率因子（唯一差异点）
src/pipeline.*     S1 建图 / S2 bias / S3 PCF / S4 着色 四阶段
src/bench.c        compare / funnel / sweep-*
src/margin.c       裕量带 + 误差放大实验
src/diag.c         最差像素诊断
src/unit_trigger.c 失效触发条件
plot/plot.py       报告图表（dataviz 调色板，已通过校验）
artifacts/csv/     全部原始测量
artifacts/figs/    6 张报告图
```
