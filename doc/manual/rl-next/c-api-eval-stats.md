---
synopsis: "C API: `nix_eval_state_get_stats` and `nix_eval_state_get_stats_json`"
---

Two new functions in `nix_api_expr.h` read the evaluation statistics of an `EvalState`.
`nix_eval_state_get_stats_json()` returns the statistics Nix prints with `NIX_SHOW_STATS=1`, as JSON.
`nix_eval_state_get_stats()` fills a struct with the cumulative counters only and is cheap enough to call around every evaluation.
