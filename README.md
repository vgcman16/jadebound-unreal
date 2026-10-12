# Jadebound — Unreal source

Jadebound is an original PC fantasy MMORPG in development. The intended game combines responsive isometric combat and distinct equipment-focused classes with mythical companions, mounts, gathering, crafting and authored quests.

This repository contains the current Unreal C++ progression plugin and its tests. It is a **source snapshot**, with the playable maps, Blueprint characters, animations and art excluded pending asset provenance review. It is not a downloadable game or a finished MMORPG. The older Godot prototype remains at [vgcman16/jadebound](https://github.com/vgcman16/jadebound).

## Current implementation

- Twelve-slot inventory, seven item definitions, stack limits, priced buying/selling, weapon equip/unequip and loot bookkeeping.
- One tracked quest with guarded rewards, revision-checked transactions and automatic journal persistence.
- Original weapon presentation hooks and a preserved standalone merchant/combat prototype locally.
- An opt-in loopback server foundation for two to four profiles, owner-specific progression models, validated requests and retained save writers.
- Same-server-session resume tickets that retain a character across disconnects. Native transport/gameplay reconnect qualification remains pending.
- Server-owned 100–0 health, telegraphed courtyard strikes, replicated life presentation hooks, and a ten-second manual revive in the opt-in development combat slice. Rendered client gameplay remains pending.
- Native healing balm pairs server health with one owned-item consumption. Held-save retries preserve subsequent damage or revive state; health across server restart remains unfinished.

Armor swapping, complete class skill sets, gathering/crafting, companions, mounts, dungeons, the main quest ending and production online infrastructure remain unfinished. The art direction is still under review.

## Build the source

Use Unreal Engine 5.8 and a compatible Windows C++ toolchain. Open `JadeboundCombatLab.uproject` to build the plugin, or copy `Plugins/JadeboundProgression` into an existing UE 5.8 project. No engine source, binary content or paid asset pack is bundled.

The supplied project is a source host. Gameplay requires the separately reviewed maps and assets referenced by the plugin. Native fixture tests also require those assets and isolated test-user directories; do not run persistence fixtures against a real character profile. The local development foundation stays disabled by default and is unavailable in Shipping.

The independent portable C++17 tests need no Unreal installation:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

See [validation results](docs/VALIDATION.md), [native player life](Plugins/JadeboundProgression/Docs/NativePlayerLife.md), [healing consumables](Plugins/JadeboundProgression/Docs/NativeConsumables.md), [local development resume](Plugins/JadeboundProgression/Docs/LocalDevelopmentResume.md), [roadmap](docs/ROADMAP.md), and [publication scope](docs/PUBLICATION.md). A public repository does not by itself grant an open-source license; no additional redistribution license has been selected.
