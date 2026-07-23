# XR dominant-hand setting

SurrealEngine has one provider-neutral dominant-hand role with `Right` as the
compatibility default. Changing it to `Left` swaps only roles that are expected
to follow the player's primary hand:

- dominant trigger is primary fire and the other trigger is alternate fire;
- startup-intro trigger routing uses the same primary/alternate assignment;
- the weapon aim/grip samples come from the dominant controller;
- the menu laser and exact-contact marker are drawn for the dominant controller;
- weapon-pose output carries `Mirror=true` for left-handed presentation.

Physical locomotion stays conventional: the left stick moves and the right
stick turns. Keyboard, mouse, flat rendering, and non-XR launches are unchanged.
The mirror value is presentation metadata; this milestone does not modify or
replace weapon meshes.

## Persistence and provider boundary

The desktop launcher exposes **XR dominant hand** under video settings and
stores `XR.DominantHand` in `SurrealEngine/Settings.json`. The existing browser
mutable-file overlay already persists that settings file.

The browser launcher also stores the choice with its other launcher preferences
and applies it after native startup through `Surreal_SetXRDominantHand(0|1)`.
That export is a browser-host adapter, not WebXR policy: both native OpenXR and
WebXR consume the same `XRHandedness` engine policy. `0` selects left, `1`
selects right. `Surreal_GetXRDominantHand()` reports the active role.

For compatibility and scripted testing, `--vr-lefthand` overrides the persisted
launcher value for that process. Omitting the flag preserves the stored value,
and a missing setting resolves to right-handed mode.

## Verification

The focused test coverage checks:

- right/left semantic fire and alternate-fire mapping, including runtime swap;
- right/left startup-intro primary-fire routing;
- dominant weapon-pose selection and left mirror metadata;
- native OpenXR and WebXR laser/hit-marker hand selection;
- browser launcher persistence and the native adapter call.
