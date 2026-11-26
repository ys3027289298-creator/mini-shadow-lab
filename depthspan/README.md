# depthspan

光源近远平面跨度（span = far − near）与深度精度的取舍，回答"实心家具底部
为什么没有接触阴影"。`make test` 跑 12 项自检和三档场景扫描。

## 公式

- 受光判定（整数深度码空间，接收面与遮挡体同码即整片受光）：
  `lit := code(recv) − code(bias) <= code(map)`
- 最小可分辨世界深度：`dz_min = (far - near) / 2^bits = S / 2^bits`
- 厚度 h（扣除 bias）可见的跨度上限：`Smax = (h - bias) * 2^bits`

## 两条曲线定跨度

跨度 S 减小时 `dz_min` 线性下降（精度上升），但近平面抬升会裁掉视锥外的
悬挂物（`N_cull` 阶梯上升）。扫描全跨度，取强度仍 ≥ 0.60 的可行区间：
区间左缘 = 精度侧最紧跨度，右缘 = 裁剪最少的最宽跨度。

## 三档场景

| 场景 | 层高 | naive far | 最薄家具 | 结果 |
|---|---|---|---|---|
| small_room | 2.8m | 20m | 2mm | 全跨度通过，Smax=98.3m 不构成约束 |
| mid_hall | 8m | 60m | 3mm | 全跨度通过，Smax=163.8m 不构成约束 |
| large_atrium | 48m | 120m | 1mm | 精度上限点 32.6m 仅 0.583，失败；可行跨度 34~48m |

失败档按要求输出跨度、`dz_min`、被裁物体编号与未解析物体编号。

## 导出曲线

```
DS_CSV=scan.csv ./depthspan
python3 tools/plot_tradeoff.py scan.csv tradeoff.svg   # 仅用标准库
```
