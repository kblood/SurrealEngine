# WebXR deployment dry run: candidate 71650dd7

## Decision

Do not deploy the current package yet. Its payload and compliance records are
internally consistent, but its recorded intended base path has the wrong case
and names the stable directory rather than an immutable candidate directory.

The first public destination should be the revisioned experimental candidate:

- public URL:
  `https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-71650dd7/`
- server directory:
  `/var/www/html/webxr/Ports/SurrealEngine-Candidate-71650dd7/`

This upload must not create, replace, or modify
`/var/www/html/webxr/Ports/SurrealEngine/`. Promotion to that stable-looking
path is a separate operation after headset and human/legal gates pass.

This document is a dry run only. No website files were copied, no server was
started, and no remote state was inspected or changed while preparing it.

## Candidate identity

The inspected package is
`release-candidates/SurrealEngine-WebXR-71650dd7` and contains 25 files in
total: the 24 records in `release-manifest.json` plus the manifest itself.

| Item | Expected value |
| --- | --- |
| Source commit | `71650dd7e80411532cc2baaca6623ad9404b25a9` |
| Source tree | `1275b4f61f3ba7ecfe6f568ded6ddcc2c38bd4c5` |
| Package size | `36,946,251` bytes |
| `engine/SurrealEngine.js` | `528,788` bytes; SHA-256 `22dd5ec417ea5debdb0d9d9a8bd5b0bd5d6d2f759e81c22ae5b1ddba4ce128dd` |
| `engine/SurrealEngine.wasm` | `10,996,625` bytes; SHA-256 `1ca34875dbfbd822904195ea90bceb2727a91552c210d563c3a5ede7c04acb77` |
| Corresponding source | `25,093,312` bytes; SHA-256 `25c47c542c26943adb7b01e928f25f2987959fd4c2ddc728c6761a79f27af101` |
| `release-manifest.json` | SHA-256 `0c5bd9a59e086fc6d69babfc7763ca17acd155013714c5f0e4d4dc9cecaf8fb1` |

The completed package audit found no size/hash mismatches, unmanifested
payloads, or packaged game data. The package smoke and the mechanical
LGPL/source/relinking checks passed. These checks do not substitute for the
remaining headset and human/legal review gates.

## Blocking path correction

The repository default in `web/release-package.json`, prior candidate URLs,
the site documentation, and the Apache alias all use lowercase `/webxr/`.
However, this package's `HOSTING.txt` and `release-manifest.json` record:

```text
/WebXR/Ports/SurrealEngine/
```

Linux/Apache paths are case-sensitive, so `/WebXR/` and `/webxr/` cannot be
treated as equivalent. The recorded path also conflicts with the recommended
revisioned candidate destination. Relative runtime URLs make the payload
technically relocatable, but silently ignoring release metadata would make the
published artifact's provenance false.

Before upload, regenerate the package from the same clean commit and source
materials with:

```text
--intended-base-path /webxr/Ports/SurrealEngine-Candidate-71650dd7/
```

Then rerun the complete package smoke, file-set/hash audit, prohibited-game-data
scan, and corresponding-source/relinking audit. Record the new package and
manifest hashes in the deployment record. Do not hand-edit the generated
manifest or `HOSTING.txt`.

This is the only destination mismatch found locally. The final server's actual
document root and PHP/directory-index configuration still need a read-only
operator confirmation; see **Missing destination information** below.

## Parent `Ports` directory constraints

The local website staging layout intentionally contains only:

```text
Ports/
  .htaccess
  progress.html
```

It deliberately has no index document so the existing server file browser is
visible at `https://dionysus.dk/webxr/Ports/`. No PHP files are present anywhere
in the inspected local workspace, so the server-side file-browser
implementation is not represented locally.

The deployment must therefore obey these boundaries:

- Do not upload `index.html`, `index.htm`, or a new `index.php` to the parent
  `/webxr/Ports/` directory.
- Do not replace the parent `Ports/.htaccess`; it carries the revalidation rule
  for the directory listing and `progress.html`.
- Do not delete or modify `Ports/QuakeQuest`, `Ports/progress.html`, or any
  sibling candidate.
- Upload the candidate's own `index.html`, `.htaccess`, and `_headers` only
  inside `SurrealEngine-Candidate-71650dd7/`. An application index is required
  inside that subfolder and does not block the parent listing.
- Preserve dotfiles during transfer. Apache must allow at least the documented
  `FileInfo` overrides for the app's `.htaccess`.
- Confirm before deployment that the existing PHP/directory-index mechanism
  still exposes the parent listing and that adding a child directory is all it
  needs to discover the candidate.

The known Apache layout maps the parent to `/var/www/html/webxr/Ports/` and
allows `FileInfo Indexes` overrides. That is sufficient to plan the path, but
not evidence of the current production `DirectoryIndex`, PHP handler, owner,
group, or deployment account.

## Headers and MIME requirements

The candidate's app-level `.htaccess` must cause all app responses to include:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
X-Content-Type-Options: nosniff
Cache-Control: no-cache, must-revalidate
```

`engine/SurrealEngine.wasm` must be served as `application/wasm`. `_headers` is
portable-host documentation; on this Apache host, `.htaccess` or equivalent
virtual-host configuration is authoritative. HTTPS is required for WebXR.

The cross-origin headers must be checked on at least `index.html`, JavaScript,
WASM, JSON, and the corresponding-source response, not merely on the first
HTML request. The browser must report `crossOriginIsolated === true` before the
threaded WASM build is considered loadable.

## Immutable candidate and cache policy

The candidate directory is immutable operationally: once published, none of
its files may be edited or overwritten. A corrected build gets a new revisioned
directory, even if the correction appears small. Never merge a new package into
this directory.

For candidate 71650dd7, retain the generated conservative response policy
`Cache-Control: no-cache, must-revalidate` for every app file. This is deliberate
and matches prior release-package guidance: it avoids stale unversioned module
files and keeps the verified `.htaccess`/manifest pair intact. ETags or
`Last-Modified` may make revalidation inexpensive. Here, "immutable candidate"
means the URL's bytes never change; it does not require the HTTP
`Cache-Control: immutable` directive.

Long-lived `max-age=31536000, immutable` caching could be safe inside a
revisioned directory, but changing to it is out of scope for this deployment.
It would require a reviewed packaging-policy change, a regenerated manifest,
and proof that no shared proxy or alias can serve changed bytes at the same
URL. The parent listing and `progress.html` must continue to revalidate.

## Source offer and redistribution boundary

The visible footer in `index.html` links to the relative corresponding-source
archive. At the planned origin it resolves to:

```text
https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-71650dd7/source/SurrealEngine-71650dd7-corresponding-source.tar.gz
```

The response must be downloadable and match SHA-256
`25c47c542c26943adb7b01e928f25f2987959fd4c2ddc728c6761a79f27af101`.
`SOURCE-OFFER.txt`, `source-compliance.json`, the LGPL text, the SurrealVideo
notice, and relinking instructions must remain reachable at the same relative
paths. The final public URL, hosting duration, notices, terms, and
redistribution model still require responsible human/legal review.

No commercial game, demo, save, log, owner path, or imported OPFS data may be
uploaded. Users provide their own game folders locally through the browser.

## Atomic upload sequence

The following is the exact intended operator order. It is not authorization to
run these steps from this document.

1. Produce the path-correct package in a clean local directory and rerun every
   package, hash, no-data, and source-compliance gate.
2. Read-only verify that the final directory does not already exist and confirm
   the production document root, filesystem owner/group, available space, PHP
   listing behavior, and `AllowOverride`/headers modules.
3. Create a same-filesystem, non-final sibling staging directory such as
   `/var/www/html/webxr/Ports/.SurrealEngine-Candidate-71650dd7.upload-<nonce>`.
   It must not be linked from the parent listing during transfer.
4. Upload the complete package in one batch, including `.htaccess`. Do not copy
   individual files into any live SurrealEngine directory.
5. On the server, verify the exact 25-file set and every manifest size/hash,
   including the manifest itself and corresponding-source archive. Reject extra
   files and confirm no game-data extension slipped into staging.
6. Apply the already-confirmed production owner/group and least-permissive
   readable directory/file modes. Do not infer them from this local checkout.
7. Atomically rename the verified staging directory to
   `/var/www/html/webxr/Ports/SurrealEngine-Candidate-71650dd7/`. The final name
   must be absent; never overwrite or merge it.
8. Run all non-headset public-origin gates below. Keep the path labeled
   experimental and do not advertise it as stable while hardware/legal gates
   remain open.
9. Run the physical headset gates. Stable-path promotion is a new, separately
   approved deployment only after all required evidence passes.

## Rollback

Because the target is a new immutable sibling, rollback does not require
restoring an older app directory:

1. If verification fails before rename, remove or quarantine only the exact
   hidden staging directory after resolving its absolute path. No live files
   have changed.
2. If a public-origin check fails after rename, atomically rename only
   `SurrealEngine-Candidate-71650dd7` to a non-public/quarantined sibling such as
   `.SurrealEngine-Candidate-71650dd7.failed-<timestamp>`.
3. Confirm the parent Ports listing, QuakeQuest, `progress.html`, and all prior
   candidates are unchanged.
4. Diagnose and publish a corrected build under a new candidate name. Do not
   repair candidate 71650dd7 in place or reuse its public URL for different
   bytes.

Any later stable-path promotion should have its own atomic swap and rollback
plan, retaining the previous stable directory until the promoted version passes
public-origin checks. That operation is intentionally excluded here.

## Post-upload non-headset gates

All of these must pass at the exact public candidate URL before headset testing:

- HTTPS returns the lowercase candidate URL without a case-changing redirect,
  and the parent Ports browser remains visible with QuakeQuest,
  `progress.html`, prior candidates, and the new candidate intact.
- `index.html` and every manifest-listed file return 200. The public file set,
  sizes, and hashes match the newly generated path-correct package exactly.
- `index.html`, JavaScript, WASM, JSON, and source responses carry the required
  COOP/COEP/CORP/nosniff/cache headers; WASM uses `application/wasm`.
- `crossOriginIsolated` is true and threaded WASM initializes without console,
  page, WebGPU, or uncaught-promise errors.
- The page visibly says that no game data is included. With no imported data,
  launch stays behind the expected data-selection gate.
- The source-offer link is visible, all license/relinking records open, and the
  downloaded source archive matches the recorded hash.
- The exact public base URL passes the release-shell smoke: launcher/render
  initialization, synthetic no-owner-data launch, input, responsive layout,
  fullscreen, storage capability, and zero page errors.
- A flat-browser manual pass checks game-folder selection UI, pointer lock,
  Escape recovery, keyboard/mouse fallback, audio resume after visibility
  change, and clean return to the launcher. Owner data may be used by its owner
  for qualification, but must never become a deployment artifact or log.

## Post-upload headset gates

The path remains an experimental candidate until a physical Quest 3 is awake
and actively connected through Virtual Desktop/VDXR before page load and the
`immersive-vr` request. A desktop browser report with no active headset is not
headset evidence.

Record the candidate commit, public URL, headset/Quest OS, Virtual Desktop and
VDXR versions, browser version, GPU/driver, refresh rate, presentation mode,
and a privacy-safe `surrealengine-webxr-headset-report-v2` before entry, during
presentation, and after exit/re-entry. The report must contain no game data,
paths, or logs.

Run and pass:

- Automatic presentation and forced WebGL compatibility bridge, with nonzero
  entry, frame, bridge, and input counters appropriate to the selected mode.
- Correct left/right stereo, FOV, scale, depth, horizon, head translation and
  rotation, near/far clipping, and no rotational distortion, mirrored view,
  stale-pose swimming, short black cutoff, or one-eye failure.
- Both tracked controllers with correct handedness, pose, scale, orientation,
  convergence, and latency. The dominant-hand beam and exact hit/contact marker
  must agree with the menu item that actually activates.
- Trigger Fire/AltFire, both sticks for locomotion/turning, menu/back routing,
  intro skip/handoff, menu topmost ordering and non-mirrored UI, plus keyboard
  and mouse fallback without phantom clicks or stuck controls.
- Weapon aim and projectiles for representative hitscan, Eightball/rocket, Flak
  primary/alternate, Impact Hammer, and toss/adjust-aim paths without head-aim
  regressions.
- Audio on initial start, immersive entry, browser visibility loss/return,
  immersive exit, and re-entry, followed by at least a short five-minute smoke
  and the release endurance interval defined by the qualification checklist.
- Clean session exit, re-entry, controller sleep/wake, focus loss/recovery, and
  return to flat mode without a restart.

Direct `XRGPUBinding` is tested only when the candidate actually negotiates and
reports that path. It must not be inferred from the availability of WebGPU.
Failure of any projection, input, UI, audio, lifecycle, privacy, or legal gate
blocks stable promotion.

## Missing destination information

The following production facts are not present in the local repositories and
must be confirmed read-only by the website operator before deployment:

- whether `/var/www/html/webxr/Ports/` is still the live document-root mapping;
- the existing PHP/file-browser file and its location, because no `.php` file
  exists in local staging;
- the active Apache `DirectoryIndex`, `Options Indexes`, PHP handler, and
  virtual-host rules for the parent listing;
- whether `mod_headers`, `mod_mime`, and `.htaccess` overrides are enabled in
  production exactly as the checked-in example expects;
- the required filesystem owner, group, modes, quota/free space, deployment
  account, and same-filesystem staging location;
- whether a CDN or reverse proxy adds, strips, caches, or rewrites headers and
  whether it treats URL path case distinctly;
- the approved human/legal reviewer and required source-offer hosting period;
- whether the public link should remain unlisted during hardware qualification
  or may appear immediately in the parent file browser.

Until these values and the path-correct repack are confirmed, the deployment is
blocked by design.
