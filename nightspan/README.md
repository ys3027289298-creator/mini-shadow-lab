# nightspan

夜间阴影色带的深度量化分析：有效位深 `B_eff = BITS - log2(S/u)`，
由「色带不可见」反解允许跨度 `S_max = w*L_a*u*2^BITS/(L_dir*NdotL)`，
三档光照（白天/黄昏/夜晚）各跑一次，超阈值的档给出修正跨度。

`make test` 跑自检。
