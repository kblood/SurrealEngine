# UT99 Double Enforcer — Behavioral Specification

Clean-room behavioral spec only. No C++/UnrealScript code, pseudocode, or implementation sketches appear anywhere in this document. This is reference material for a separate implementation agent designing VR independent per-hand aim/fire for dual-wielded weapons.

Research method followed the project's tiered policy: public UnrealScript community documentation first; a named public decompiled-source mirror consulted only to settle questions the public docs left open, with findings translated into plain language and zero code reproduced. Every section below states which tier was used.

---

## 1. Two actors, or one actor with rendering tricks?

**Answer:** Two separate live actors, both instances of the same `Enforcer` weapon class (a subclass of `TournamentWeapon`). There is no separate "DualEnforcer weapon" class that behaves differently from a normal Enforcer — dual-wielding is implemented as one Enforcer actor (informally, the "master") holding a direct reference to a second, independently-existing Enforcer actor (the "slave"), with an internal flag on the second instance marking it as a slave. The "master"/"slave" terminology for the *pair relationship* is the community's informal description; only the slave-side label is an actual in-engine name (see below) — the master is not distinguished by any special class or variable name, only by the fact that its "am I a slave" flag is unset and it is the instance holding the reference to the other one.

There is also a second, unrelated helper class that extends Enforcer, whose only apparent purpose is to give players a separate, nameable entry in the weapon-priority/autoswitch preference list distinct from a single Enforcer's entry (so a player can rank "when I have two Enforcers" differently from "when I have one Enforcer" in their weapon-switch preferences). It is not itself spawned as a carried weapon during play; it exists purely so its name can appear in a preference list and be matched by name.

**Spawning/attachment timing:** At pickup time, not at select time. When a player who already carries an Enforcer picks up (or walks over) another Enforcer pickup, the pickup-handling logic on the currently-carried Enforcer detects that the picked-up item is the same weapon class and, instead of the normal "already have this weapon, just top up ammo" pickup path, spawns a brand-new Enforcer actor owned by the same player right then, marks the new actor as the slave, links the two together, and immediately sets both up in a mirrored "two-handed" held pose. A similar spawn-linking step also runs automatically when carrying a dual-wielded pair across a level transition (so the pairing survives map changes without the player having to re-pick-up a second gun).

**Destruction/detachment:** The slave is destroyed whenever the master actor itself is destroyed. Separately and explicitly, dropping the currently-held Enforcer (the "throw weapon" action) destroys the slave outright and resets the master back to a normal single, non-paired Enforcer before the master is thrown out into the world as an ordinary single pickup — so the dual-wield pair can never be dropped/picked back up as a pair; throwing your weapon always collapses you back to one Enforcer, and the item that lands in the world is a plain single Enforcer. This matches the community-reported behavior of picking your own dropped Enforcer back up and only having one again. No separate death-specific handling was found beyond this: because destroying the master always cascades to destroying the slave, any code path that destroys/removes the master's inventory item on death would carry the slave with it.

**Confidence:** Documented-fact (existence of a master/slave pair, and the descriptive naming) — corroborated by both a public forum source and the decompiled source. Spawn timing (at pickup) and destroy timing (on master destroy / on drop) — documented-fact, sourced from the decompiled reference only.

**Sources:**
- [ut99.org forum — "Double weapons, or dual weapons?"](https://ut99.org/viewtopic.php?t=13777) — public community explanation naming the master/slave relationship and the literal in-engine name of the slave-side reference.
- Decompiled UnrealScript mirror: `Slipyx/UT99` (also mirrored identically at `UT-BT/UT99`), file `Botpack/enforcer.uc`, and file `Botpack/doubleenforcer.uc` (the helper "priority-list only" class). Behavior described in my own words; no code reproduced.

---

## 2. How does `Pawn.Weapon` relate to the master/slave pair?

**Answer:** `Pawn.Weapon` always points at the master. The slave is architecturally prevented from ever becoming the pawn's "current weapon": its weapon-selection-acceptance check is overridden to always refuse, and its own computed weapon-switch desirability score is forced to a strongly negative value so ordinary weapon-switching logic (player-driven or bot-driven) will never choose it as the active weapon. So yes — from the point of view of anything that reads `Pawn.Weapon` (HUD weapon name/icon, "current weapon" queries, generic engine bookkeeping), only the master exists; the slave is invisible to that layer.

Ammo, however, is effectively shared rather than being "master-only with slave as a pure passenger": both master and slave independently call the same generic "acquire ammo of my ammo class" routine. That routine first checks whether the pawn already owns an ammo inventory object of the required ammo class; if one already exists (which it will, because the master set one up when first picked up) it reuses that exact same ammo object and just adds more rounds to it, rather than creating a second separate counter. So in practice there is exactly one ammo pool for the pair, tracked once on the pawn and shown once on the HUD, and both guns draw from and deplete it. Neither weapon has a private/independent ammo reserve.

Animation and rendering are not master-only: the slave is a fully real, independently animating actor with its own mesh and its own copy of the same weapon states (idle, raising, firing, lowering, etc.). It isn't a fake overlay drawn by the master. What is master-driven is *state puppeting*: the master's own state-transition code explicitly pushes matching state changes onto the slave (raise together, holster together, fire together — see Q3), rather than the slave deciding independently when to animate. So: HUD/ammo/"what weapon am I holding" bookkeeping = master only (with ammo count actually shared); rendering/animation machinery = both actors genuinely animate, but the slave's state is commanded by the master rather than autonomous.

**Confidence:** Documented-fact, sourced from the decompiled reference (public docs did not go into this level of internal detail).

**Sources:** Decompiled UnrealScript mirror `Slipyx/UT99`, files `Botpack/enforcer.uc` (slave weapon-selection refusal, forced negative switch score, per-instance ammo-type reference, state-forwarding on raise/holster) and `Engine/Weapon.uc` (the generic "reuse existing ammo object of this class if the pawn already has one, otherwise create one" ammo-acquisition routine, which both master and slave inherit and call identically). Behavior described in my own words; no code reproduced.

---

## 3. Independent fire per gun, or alternation/synchronization? Alt-fire differences?

**Answer:** Not independent — synchronized with a short deliberate stagger, and both guns respond to whichever single input (primary or alt-fire) the player is holding. There is no scheme where primary-fire input drives one pistol and alt-fire drives the other; both guns switch fire mode together based on the one input.

Because both guns share one ammo pool (Q2), "independent ammo pools" does not apply — there's a single shared ammo counter, not two.

On a primary-fire button press: the master fires its own shot immediately, then arms a short (about 0.2-second) internal delay; when that delay elapses, the master tells the slave to fire once as well. The result is an audible/visible "bang-bang" echo rather than two perfectly simultaneous shots. The same stagger pattern is used for the held-alt-fire ("Gangsta," sideways-pistol) mode: the master alt-fires, then roughly 0.2 seconds later commands the slave to alt-fire too, and this repeats for as long as the player holds the button and ammo remains, since alt-fire is itself an automatic-repeat burst rather than a single shot. The same master-tells-slave-after-a-short-delay pattern is also used for the locally-predicted client-side firing/animation playback, with a longer (~0.5 second) self-repeat delay used instead whenever there is no slave present.

Dual-wielding changes two things about how firing feels, on top of the doubled shot count: (1) the view recoil/kick effect applied to the player is increased when a slave is present (roughly double for primary fire, roughly triple for alt-fire, versus a lone Enforcer), and (2) the instant-hit accuracy calculation is deliberately made considerably looser (worse spread) whenever either gun is part of a pair, compared to a lone Enforcer's normal spread. So the trade the flavor text alludes to ("collect two for twice the damage") is real in terms of output volume and felt recoil, but comes with reduced individual-shot accuracy, not a free doubling with no downside.

Alt-fire's own automatic-repeat/burst logic and its "keep firing while held and ammo remains" behavior work the same regardless of whether a slave is present; dual-wielding does not introduce a functionally different alt-fire *mode*, only the accuracy/recoil scaling and the slave-echo behavior described above layered on top of the same alt-fire logic a lone Enforcer already has.

**Confidence:** Documented-fact for the "collect two for twice the damage," "primary = accurate/slow, alt-fire = fast/Gangsta/inaccurate" framing (public source). Documented-fact for the specific stagger timing, shared-ammo mechanism, and recoil/accuracy scaling numbers (decompiled reference only — not found described anywhere in the public documentation tier).

**Sources:**
- [Unreal Archive mirror of The Liandri Archives — Enforcer](https://unrealarchive.org/wikis/the-liandri-archives/Enforcer.html) — primary/alt-fire mode descriptions, "collect two for twice the damage," dual-ammo-cap note, drop-and-repickup-yields-one-gun behavior.
- [Steam Community — Daniel's UT Weaponry Handbook](https://steamcommunity.com/sharedfiles/filedetails/?id=121196402) — corroborates the primary/alt-fire mode framing and per-shot damage figure.
- Decompiled UnrealScript mirror `Slipyx/UT99`, file `Botpack/enforcer.uc` (fire/alt-fire functions, the ~0.2s master-to-slave echo timers, the recoil-scale and accuracy-widening conditionals keyed off "has a slave" / "am a slave"). Behavior described in my own words; no code reproduced.

---

## 4. Does the slave have its own independent transform, or is it an offset/mirrored copy of the master?

**Answer:** The slave is a genuine, separate Actor with its own position/rotation state in the world (it is not a fake visual clone layered onto the master) — but in the stock first-person view, the visible "second gun on the other side of the screen" effect is *not* produced by the slave having a truly independent 6-degree-of-freedom view-model pose. Instead, both the master and slave are drawn through the same ordinary first-person "weapon overlay" render path used for any single-handed weapon (the same code that positions a one-handed weapon in front of the camera using a fixed per-weapon offset and the player's current view rotation/FOV). That draw function is simply invoked twice per frame from the master's render step — once for the master itself, once explicitly for the slave right after — with a "handedness" flag flipped between the two calls so the shared draw math mirrors one of them to the opposite side of the screen. So the left/right dual-pistol look comes from a handedness-mirroring flag applied to the same single-weapon draw routine, called once per gun, rather than from two independently posed transforms.

Outside of that first-person overlay case — e.g., how the pair looks when seen on another player's screen in third person, or as a dropped world pickup — each actor (including the slave) does carry its own normal actor transform and its own attachment-to-the-carrying-pawn behavior, the same generic mechanism any single carried weapon uses; there is nothing dual-wield-specific about that path, it's just two ordinary carried-weapon attachments happening to be present on the same pawn at once.

**Practical implication called out for the VR design (spec-relevant fact, not a design recommendation):** because the stock first-person "second gun" visual is achieved via a handedness-flag mirror trick inside a single shared draw call rather than through two independently-posed view-model transforms, there is no existing "drive the off-hand weapon's pose independently" pathway in the stock behavior to hook into — a VR implementation giving the slave its own controller-driven pose would be adding genuinely new behavior on top of, not re-using, the stock dual-render mechanism.

**Confidence:** Documented-fact, sourced from the decompiled reference. Public documentation tier did not describe internal rendering mechanics at this level.

**Sources:** Decompiled UnrealScript mirror `Slipyx/UT99`, file `Botpack/enforcer.uc` (the overlay-render function that flips a handedness flag and calls the shared draw path once for itself and once for the slave). Behavior described in my own words; no code reproduced. General fact that third-person/pickup attachment for a carried weapon uses the engine's standard single-weapon attachment mechanism is a reasonable-inference from general UT99 weapon architecture rather than a specific line read for this document.

---

## 5. Weapon switch, drop, ammo depletion — does it degrade to single Enforcer?

**Weapon switch:** Switching to a different weapon and back does not break the pair. Switching away only changes which weapon is `Pawn.Weapon`; the master (with the slave still attached to it) simply gets holstered — the master's holster/bring-back-out state transitions explicitly forward the same holster/ready transitions onto the slave, so both guns lower and raise together. The pairing is only broken by an explicit drop or by the master being destroyed outright (Q1), not by ordinary weapon-switching.

**Drop:** Dropping the currently-held Enforcer explicitly destroys the slave and clears the pairing before the master is thrown out as a normal single pickup (see Q1). This is a hard, unconditional degrade-to-single on drop, not something that depends on ammo state.

**Ammo depletion:** There is no dedicated "if ammo hits zero, forcibly un-pair back to a single Enforcer" logic — the slave keeps existing, attached, and posed as part of the pair even at zero ammo. What changes at zero ammo is only the master's own desirability score for automatic weapon-switching purposes: an Enforcer (paired or not) whose ammo has hit zero scores itself very low for auto-switch consideration, which drives the same generic "run out of ammo, game auto-switches you to something else with ammo" behavior every UT99 weapon has — nothing dual-wield-specific is added for the depletion case itself. Also relevant: because both guns draw from one shared ammo pool (Q2/Q3), there is no scenario where one pistol runs dry before the other from separate reserves — the pair always empties together.

**Confidence:** Documented-fact, sourced from the decompiled reference for all three sub-answers; not addressed in the public documentation tier beyond the general community observation that dropping and re-picking-up your own weapon nets you a single gun (already cited in Q1/Q3).

**Sources:** Decompiled UnrealScript mirror `Slipyx/UT99`, file `Botpack/enforcer.uc` (drop-time slave destruction and pairing reset; holster/ready state forwarding to the slave on weapon switch; the zero-ammo auto-switch-desirability scoring, shared with Q2's ammo-sharing fact). Behavior described in my own words; no code reproduced.

---

## Note on a hostile web page encountered during research

While researching, a fetch of `tcrf.net/Proto:Unreal_Tournament/Botpack220` returned a page whose content was not genuine article text but appeared to be a prompt-injection attempt aimed at AI agents (fake "not intended for humans" framing with embedded instructions, plus an invalid date). No instructions from that page were followed, and no content from it was used in this spec; it is noted here only for audit-trail completeness, as one of the pages this research touched.
