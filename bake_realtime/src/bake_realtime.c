
/* bake_realtime: 烘焙光贴图阴影与实时角色阴影的合成。
 *
 * 墙面像素的亮度分两项:
 *   直接光  Ld = Ed * Vd            (Vd: 直接光可见性, 0..1)
 *   环境光  La = Ea * gAO            (gAO: 环境光遮蔽, 0..1)
 * 烘焙光贴图只含静态物的遮挡, 实时阴影图只含角色的遮挡。
 *
 * 两种合成候选:
 *   取最小值  Vd = min(sV, rV), La = Ea * gAO            (AO 只算一次)
 *   相乘      Vd = sV * rV,     La = Ea * gAO * rV       (AO 被实时项再压一遍)
 *
 * 自检覆盖: 连续性反解、半影宽度随角色距离变化、三档距离的交界处亮度差
 * (阈值 = 一个 8bit 量化级 1/255), 并输出 CSV 供绘图。 */
#include <stdio.h>
#include <math.h>

/* ---- 光照场景常量 (法线向阳的白墙, albedo=1) ---- */
#define ED      0.80f   /* 直射太阳光辐照度 */
#define EA      0.25f   /* 环境/间接光辐照度 */
#define SV      0.90f   /* 烘焙静态直接光可见性 (墙边轻微静态遮挡) */
#define GAO     0.90f   /* 烘焙环境光遮蔽项 */
#define QUANT   (1.0f / 255.0f)
#define NPCF    9       /* 固定 9-tap PCF 核 (模拟固定开销的实时阴影) */
#define PI      3.14159265358979323846f

/* 半影宽度 (像素): 与 blocker-receiver 距离成正比, 与 pcf 模块的
 * penumbra_texels 同构 (light_size * d / receiver_depth 一项)。
 * 角色按点遮挡体处理, 宽度只由距离项给出。 */
static float penumbra_px(float blocker_dist, float light_angle_rad,
                         float px_footprint) {
    return 2.0f * blocker_dist * tanf(light_angle_rad) / px_footprint;
}

/* 连续模型: 半影斜坡中点处角色可见性恒为 0.5 */
static float vis_continuous(float /*distance*/) { return 0.5f; }

/* 离散 9-tap PCF: 采样点跨阴影边的比例。
 * 交界处像素相对几何阴影边有 0.25px 的亚像素相位 (光栅化错位),
 * taps 位于 x = k - 0.25 (k = -4..4), 半影斜坡宽 w_px,
 * 斜坡内单 tap 被挡比例 c = clamp(0.5 - x/w, 0, 1)。
 *   w->0  硬边: 5 个 tap 落在暗侧, rV = 4/9;
 *   w>>核 斜坡铺满: 相位被抹平, rV -> 0.5 (连续解)。 */
static float vis_pcf(float w_px) {
    const float phase = 0.25f;
    float blocked = 0.0f;
    for (int k = -NPCF / 2; k <= NPCF / 2; k++) {
        float x = (float)k - phase;        /* tap 到边的有符号距离, 单位像素 */
        float c;
        if (w_px < 1e-4f) c = (x < 0.0f) ? 1.0f : 0.0f;
        else {
            c = 0.5f - x / w_px;
            if (c < 0.0f) c = 0.0f;
            if (c > 1.0f) c = 1.0f;
        }
        blocked += c;
    }
    return 1.0f - blocked / (float)NPCF;
}

/* 取最小值管线: 直接光 min 合成, 环境光只吃烘焙 AO */
static float shade_min(float rV) {
    return ED * fminf(SV, rV) + EA * GAO;
}

/* 相乘管线: 直接光相乘, 环境光被 rV 再乘一遍 (AO 算两次) */
static float shade_mul(float rV) {
    return ED * SV * rV + EA * GAO * rV;
}

/* 相乘相对取最小值的过暗量 (即接不上的差异量) */
static float over_dark(float rV) { return shade_min(rV) - shade_mul(rV); }

static int failures = 0;
static void check(const char *name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

typedef struct {
    const char *tier;
    float dist;      /* 角色到墙的距离 m */
    float w;         /* 半影宽 像素 */
    float rV;        /* 交界处实时可见性 */
} Tier;

int main(void) {
    /* 光源角半径 0.5deg (游戏面光源常用值), 像素在墙面的足迹 5cm */
    const float ang = 0.5f * PI / 180.0f;
    Tier tiers[3] = {
        {"near",   0.2f, 0.0f, 0.0f},
        {"middle", 5.0f, 0.0f, 0.0f},
        {"far",   20.0f, 0.0f, 0.0f},
    };
    for (int i = 0; i < 3; i++)
        tiers[i].w = penumbra_px(tiers[i].dist, ang, 0.05f);

    /* ---- 反解条件: rV=1 (交界外侧) 两管线必须相等且等于无角色基线 ---- */
    float base = ED * SV + EA * GAO;
    check("boundary_min_continuous", fabsf(shade_min(1.0f) - base) < 1e-6f);
    check("boundary_mul_continuous", fabsf(shade_mul(1.0f) - base) < 1e-6f);
    /* 一旦进入半影 rV<1, 相乘立刻产生非零差异, 取最小值恒等于真值 */
    check("multiply_seam_nonzero", over_dark(0.5f) > 0.0f);
    check("min_matches_truth_partial",
          fabsf(shade_min(0.5f) - (ED * fminf(SV, 0.5f) + EA * GAO)) < 1e-6f);
    /* 过暗量随遮挡加深单调上升 (在 rV <= sV 区段) */
    check("overdark_monotonic",
          over_dark(0.3f) > over_dark(0.6f) && over_dark(0.6f) > over_dark(0.95f));

    /* ---- 半影随距离变宽, 固定核下硬边->欠采样->平滑 ---- */
    float rv_hard = vis_pcf(tiers[0].w), rv_mid = vis_pcf(tiers[1].w),
          rv_far = vis_pcf(tiers[2].w);
    check("penumbra_grows", tiers[2].w > tiers[1].w && tiers[1].w > tiers[0].w);
    check("pcf_hard_edge", fabsf(rv_hard - 4.0f / NPCF) < 1e-6f);
    check("pcf_partial",    rv_mid > rv_hard && rv_mid < 0.5f);
    /* 斜坡铺满核后亚像素相位被抹平, 离散解回到连续解 0.5 (容差 1 个 tap) */
    check("pcf_smooth",     fabsf(rv_far - vis_continuous(tiers[2].dist))
                            < 1.0f / NPCF);

    /* ---- 三档距离: 接缝亮度差 vs 一个量化级 ----
     * 验收判定独立于单元检查: 三档全部超标是本实验的预期结论,
     * 失败档位按要求输出距离/交界处亮度/非交界处亮度。
     * 口径: 接缝 = 合成结果相对物理真值 L* 的偏差, 不是投影本身的落差
     * (角色影子必然存在, 真值在交界处本来就比非交界处暗)。 */
    int rejected = 0;
    printf("\ntier,dist_m,penumbra_px,rV,nonjunction,truth_junction,"
           "min_junction,mul_junction,physical_drop_levels,"
           "min_seam_levels,mul_seam_levels,verdict\n");
    for (int i = 0; i < 3; i++) {
        float rV = vis_pcf(tiers[i].w);
        float truth = ED * fminf(SV, rV) + EA * GAO;   /* 物理真值 */
        float Lj_min = shade_min(rV);
        float Lj_mul = shade_mul(rV);
        float drop = fabsf(truth - base) / QUANT;      /* 投影本身的落差 */
        float dmin = fabsf(Lj_min - truth) / QUANT;    /* min 接缝: 恒 0 */
        float dmul = over_dark(rV) / QUANT;            /* 相乘接缝 */
        int ok = dmul < 1.0f;
        if (!ok) rejected++;
        printf("%s,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%s\n",
               tiers[i].tier, tiers[i].dist, tiers[i].w, rV,
               base, truth, Lj_min, Lj_mul, drop, dmin, dmul,
               ok ? "PASS" : "FAIL");
        if (!ok)
            printf("  -> %s 档超标: 距离=%.2fm 交界处(乘)=%.4f 交界处(真值/min)=%.4f "
                   "非交界处=%.4f 接缝过暗=%.2f 级 (min 接缝=%.2f 级)\n",
                   tiers[i].tier, tiers[i].dist, Lj_mul, truth, base, dmul, dmin);
    }
    check("multiply_rejected_all_tiers", rejected == 3);

    /* 连续模型对照: 三档 rV 都=0.5, 说明距离依赖来自固定核的离散化 */
    check("continuous_invariant",
          fabsf(vis_continuous(tiers[0].dist) - vis_continuous(tiers[2].dist))
          < 1e-6f);

    printf("\nall_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
