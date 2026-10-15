# Validation status - 15 October 2026

## Current public export

| Check | Status |
|---|---|
| Source curation | Exact current V3 source/config candidate independently reviewed; privacy and asset exclusions preserved |
| Portable C++17 suites | Five suites passed on this exported source after fresh configure and serial Release build |
| Independent Unreal build | All 52 public module cpp files compiled and linked against the public descriptor with serial no-PCH/no-unity settings; native metadata generation separately completed without recompiling |
| Native memory-only V3 codec tests | Four exact Torso codec tests passed with a new public DLL, isolated UserDir and exact report/normal-exit checks |
| Public capture privacy path | Retained under the project Saved directory; independently exercised public-path gameplay remains pending |
| Binary assets and executable delivery | Excluded; this checkout cannot run the private tested game by itself |

The five portable suites are DevelopmentResumeRegistryTests, DevelopmentResumeOptionsTests, PlayerLifeRulesTests, NativeBalmRulesTests and NativeStrikeSequenceTests. NativeBalmRulesTests uses the updated core, but its equality helper does not compare EquippedTorso; its pass does not qualify torso or V3 persistence. The existing CMake file remains unchanged.

The public native run selected only TorsoV3Codec, TorsoLegacyCodecIsolation, TorsoV3PendingDrops and TorsoV3MemoryArchive under Jadebound.Persistence. These synthetic memory tests had no gameplay fixture flag or real profile. All four report rows succeeded without errors or warnings; the queue emptied and the original process closed normally. They qualify selected codec behavior, not physical journal failure recovery, gameplay or visible armor.

The first public native compile completed successfully; its wrapper then refused qualification because a module-only UBT build omitted the target metadata action. A separate installed UBT WriteMetadata phase consumed the unmodified generated target metadata and wrote the public module mapping and target receipt without recompilation. The original wrapper failure remains retained. The metadata phase preserved the actual fresh DLL, public source and engine version metadata.

The public Unreal tests have been updated together with the V3 API. Source155 actor-bank fixtures require `-JadeboundV3ActorBankTests` and a fresh absolute UserDir ending in `Jadebound-S155-A001/UserDir-v001` (or another valid nonzero three-digit attempt). Source148 persistence fixtures similarly require `-JadeboundV3PersistenceTests` and `Jadebound-S148-A001/UserDir-v001`. Keep their naming, isolation and refusal guards. The documented suffixes are patterns, not invitations to reuse a completed attempt or an actual player profile. Other fixture flags and content requirements remain specific to each test.

## Privately tested source and Windows package

The current private project compiled as a Windows Development Game, cooked the two selected maps and packaged the matching executable. A separate qualification corrected an overbroad historical-map expectation without replaying the successful cook. Thirty-seven selected native regressions had passed; this was targeted validation, not the complete game or public export.

Two ordinary packaged processes ran outside the editor with a fresh, unseeded character and a separate cold reopening. Actual input accepted the quest, defeated a raider, collected earned loot, observed natural health reaching zero, waited through the ghost countdown and manually revived to full health. Turning in the quest awarded five coins and one shard once. Repeat interaction and a post-reopen repeat left the earned journals unchanged.

The same packaged character bought a spear, visibly swapped weapons, unequipped and re-equipped the glaive, bought and sold ore, and saw unaffordable controls disabled. F1 executed Rooted Guard without changing to wireframe. The reopened character retained the completed quest, carried items, currency and glaive. Both original game processes closed normally with protected inputs unchanged. Disabled shelf controls do not prove a forced invalid server request was executed.

Separate ordinary editor-standalone evidence covered an earned full bag, affordable capacity refusal, disabled unequip with explanation, owned-loot refusal without consuming the drop, later recovery of that same drop, a matching-stack purchase while full, insufficient-funds refusal, death/respawn and cold reopening. These cases were not all repeated in the packaged pass. Historical healing checks remain separate; no new packaged consumable-use proof is claimed.

A 6,000-frame stationary merchant capture measured 149.71 average FPS, 8.04 ms p95 and 23.43 ms maximum. Metadata records 1280x800 system resolution; effective internal resolution remains unknown. This does not qualify the proposed 1600x1000 courtyard target or combat, loading, crowded scenes or multiplayer. One identity-verified process sample measured 1.39 GiB working set and 1.80 GiB lifetime peak; virtual address space is not physical RAM usage.

## Remaining limits

The earlier public validation history remains recoverable through Git history. Its detailed passes and preserved failures are not silently promoted into new export results. Actual private profiles, logs, hashes of player saves, reports, footage and binaries are not published here.

Original character and interface art remain unaccepted. Complete visible armor, original animations, gathering/crafting, companions, mounts, dungeons and the main quest ending are unfinished. The local authority foundation has not established independent-client multiplayer, production authentication, server-restart ownership/recovery, load capacity or MMO readiness. The successful public engine build does not supply the excluded maps or qualify gameplay without them.
