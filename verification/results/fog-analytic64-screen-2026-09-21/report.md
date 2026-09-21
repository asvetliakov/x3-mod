# Fixed analytic 64-total-step transport screen

Result: **failed-analytic64-transport-screen**. hard stop before cache/shader/step search. Failed gates: A/sky/near/candidate, A/sky/shell/candidate, A/sky/full/candidate, B/sky/near/candidate, B/sky/shell/candidate, B/sky/full/candidate, B/geometry/shell/candidate, B/geometry/full/candidate, temporal_movement. Temporal movement residual max is 0.00486731529 against .003 across 20 ray-pair cases.

The candidate uses exactly64 global density stations per valid ray and shares each station across near/middle/shell overlap accounting. Dense h64/h128 references use the same global-grid convention. Cost ranges from 364 median to 452 max analytic density reads per sky ray versus old48; this is not GPU timing.

No images, cache allocation, bake, shader, GPU, Wine, game, production edit, step-count search, tuning, install or commit occurred. Host runtime 2.86s.
