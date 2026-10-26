---
synopsis: "C API: `nix_store_get_system_features`"
prs: []
---

A new function `nix_store_get_system_features()` in `nix_api_store.h` can return a store's system features. A store can check `requiredSystemFeatures` against that list. The returned list can include extra features from enabled experimental features. One example is `recursive-nix`. `nix config show system-features` can list only the `system-features` setting. The command can omit those implied features.
