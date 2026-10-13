# Release roadmap

The long-term goal is a large original MMORPG. The first release aims to establish a complete, enjoyable smaller game before expanding content and online capacity. These are proposed targets, not claims about shipped content.

| First-release target | Scope |
|---|---|
| Classes | Three distinct weapon identities, at least four meaningful active skills each |
| World | One detailed town, three connected wilderness regions, two dungeons |
| Combat | Twelve meaningfully different enemies and three bosses |
| Quests | Fifteen authored quests, including a main chain with an ending |
| Progression | Inventory, visible equipment, loot qualities, merchants, gathering, crafting, upgrades and persistent progression |
| Companions and mounts | Three original companions and two mounts |
| Multiplayer | Tested server authority with independent two-to-four-player clients |
| Delivery | Tested Windows package, onboarding, settings, credits and play/hosting instructions |

Immediate work is the merchant progression loop: inventory, visible weapons, transactions, loot, tracked one-time rewards and automatic persistence. Qualify full inventories, invalid requests, repeated rewards, death, a ten-second ghost/revive flow, ordinary restarts and reconnects through actual gameplay. Passing a compile or a targeted fixture does not complete this milestone.

The opt-in native combat source now owns player health, incoming raider strikes, death presentation and manual delayed revive. Healing Balm uses exact owned-item intents and preserves later damage or revive through held-save recovery; current native regressions and exported portable suites passed with the limits in the validation document. Life and resume identities remain retained server-session state; health/death and account ownership across a server restart remain unfinished. A fresh local merchant/combat/pickup/quest sequence and its ordinary restarts have now passed, including one payout and two unchanged repeats. Next are the clipped pack-layout repair, full-pack and invalid-purchase checks, natural death/revive, then independent two/four-client reconnect, damage isolation, peer ghost, input restoration and save-failure checks in a separate development test map. World-ordered damage issuance qualifies registered enemy replacement and replay refusal within the singleton development scope. Multiple-enemy admission and independent clients remain unfinished. Original character art continues through evaluated-surface studies and rendered concept comparison.

Continue replacing placeholders with detailed original characters, separate equipment pieces, rigged animations, effects, sound, architecture and creatures. Iterate actual rendered views against the original concept before accepting the character. Official reference art is research material and is not redistributed here.

Then extend class skills, enemy behaviors, progression and purposeful regions; add mining, another gathering activity, recipes/upgrades, conversations, companions, mounts and dungeons. A fresh character must reach the main quest ending. Measure performance on the development PC, optimize, and test the packaged game outside the editor.

Large-scale online play separately requires account identity, durable character ownership, party/social features, server capacity, recovery, migrations/backups, load tests, moderation, abuse prevention, hosting and operations. A local development server is not MMO-scale readiness. Public hosting and paid services remain separate decisions.
