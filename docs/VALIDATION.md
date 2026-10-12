# Validation — 12 October 2026

| Check | Result |
|---|---|
| Development candidate, UE 5.8 Windows Editor module | Serial compilation, UHT and module link passed |
| Existing native progression/receipt regressions | 31 passed in an isolated fresh test-user directory |
| Native player life fixture | Passed in a separate fresh directory after setup corrections; zero test errors/warnings |
| Exported portable C++17 tests | CMake/MSVC Release build and CTest: 3/3 passed, unchanged code input hashes |
| Public-source privacy/provenance preflight | Source-only scope; no private-path or credential-literal regex hits detected |

The registry suite covers reversed two/four-profile reconnect order, malformed/unknown/duplicate tickets, retained identities, cancellations, exhausted profiles and allocation-failure atomicity. The option suite includes duplicate/case-variant options, zero/invalid tickets, NULs, input bounds and refusal without character fallback. The native Windows receipt test covers creation, exact retry, conflicting data, oversize input and competing writers with preserved winning bytes.

The portable life suite adds seven groups for bounded fixed damage, exact ten-second revive timing, stale generations/strikes, replay, invalid states/times, overflow, long-session clock precision and independent state. All three exported executables passed with warnings treated as errors.

The native life fixture opened real isolated progression journals and used actual Unreal collision queries. It verified windup/dodge/obstruction, target cancellation after unpossession, five hits from 100 to zero, owner health values and public component state, dead movement gating, retained death on a replacement possessed body, early/stale/occupied-home/repeated revive refusal, movement-state restoration and unchanged progression. World time advanced through small frames with measured elapsed-time assertions. Blocked progression readiness was simulated with a flag; an actual failed pending journal write was not injected. Pawn replacement was trusted ownership assembly, not a client reconnect.

The new life fixture initially failed on an invalid LocalPlayer owner, then a noncanonical generated character ID, then long ticks clamped by Unreal. Those setup defects were corrected without relaxing production ownership/timing checks, and the final fixture passed in a new directory. The 31 regressions passed on the first run; this is **31 plus one across separate runs**, not one 32/32 run. Two earlier native combat fixtures require already-consumed exact historical directories and were deliberately not replayed; their compilation is not new execution proof.

These are code/fixture results. Independent-client transport, actual ticket delivery and reconnect gameplay, peer ghost rendering, keyboard movement after revive, ordinary packaged restarts, performance, art acceptance and first-release completion remain pending. Health/death and account ownership across server restart are unfinished. The public source host does not bundle the content required by gameplay fixtures. The public capture-root privacy adjustment has not been exercised in gameplay. No MMO-scale qualification is claimed.
