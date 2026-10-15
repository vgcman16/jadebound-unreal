# Jadebound - Unreal source

Jadebound is an original PC fantasy MMORPG in development. The intended game combines responsive isometric combat and distinct equipment-focused classes with mythical companions, mounts, gathering, crafting and authored quests.

This repository is a **source host**, containing the progression plugin, configuration and tests. Playable maps, Blueprint characters, animations and art remain excluded pending asset provenance review. It is not a downloadable game or a finished MMORPG. The older Godot prototype remains at [vgcman16/jadebound](https://github.com/vgcman16/jadebound).

## Current source

- Twelve inventory slots, stack limits, merchant prices, weapon equip/unequip and owned loot. Seven established item definitions are joined by a provisional torso SKU whose ordinary purchase remains gated until its asset is qualified.
- One tracked quest, protected one-time rewards, revision-checked transactions and automatic V3 progression journals. Frozen V1/V2 decoding and explicit migration retain the old banks; canonical newer-format entries refuse unsafe fallback.
- Visible weapon presentation and optional original-character movement/animation integration code. Complete visible armor and accepted original characters remain unfinished; the required assets are not included.
- Project input configuration removes inherited F1-F5/F9 debug commands that conflict with gameplay function keys. The existing published tooltip layout remains unchanged.
- An opt-in loopback authority foundation with owner-specific progression, server-owned health, telegraphed strikes, delayed manual revive and native healing rules. Independent-client multiplayer, durable account identity and production online operations remain unqualified.

The private Windows Development package has passed a fresh quest/combat/loot sequence, natural death and delayed manual revive, merchant transactions, visible weapon changes and a separate cold reopening. Earlier editor-standalone testing also covered earned full inventories and retained-drop recovery. Those results apply to the private tested build. This public export separately passed five portable suites, an independent UE 5.8 editor-module compile and four memory-only V3 codec tests. These source checks do not run the excluded game. See [validation](docs/VALIDATION.md).

## Build the source

Use Unreal Engine 5.8 with a compatible Windows C++ toolchain. Open `JadeboundCombatLab.uproject` to build the plugin, or copy `Plugins/JadeboundProgression` into an existing UE 5.8 project. No engine source, binary content or paid asset pack is bundled. The host alone cannot reproduce the tested game without the separately reviewed maps and assets.

Portable C++17 tests require no Unreal installation. Use a fresh build directory:

```sh
cmake -S . -B build/public-v3
cmake --build build/public-v3 --config Release
ctest --test-dir build/public-v3 -C Release --output-on-failure
```

Native fixtures require their explicit opt-in flags, excluded gameplay assets where applicable, and fresh isolated test-user directories. Never run them against a real character. The local development foundation stays disabled by default and is unavailable in Shipping. Public capture writes only beneath the project's Saved directory; that adjusted path still needs independent runtime qualification.

See [native player life](Plugins/JadeboundProgression/Docs/NativePlayerLife.md), [healing](Plugins/JadeboundProgression/Docs/NativeConsumables.md), [damage issuance](Plugins/JadeboundProgression/Docs/NativeStrikeIssuance.md), [development resume](Plugins/JadeboundProgression/Docs/LocalDevelopmentResume.md), [roadmap](docs/ROADMAP.md) and [publication scope](docs/PUBLICATION.md). No additional open-source or redistribution license has been selected.
