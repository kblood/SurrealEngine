# Verified directory-generation transaction primitive

> **Not the production browser pointer.** Atomic directory exchange prevents a
> missing or partially copied directory in one pathname lookup, but an HTML
> load makes several requests. HTML received before an exchange can request an
> old hashed JavaScript or WASM URL after the old directory has moved and get a
> 404. Use this primitive for local QA and bounded bootstrap/recovery only. The
> production procedure must publish immutable public version roots and switch a
> small stable redirect; do not point the website at a new release with this
> command alone. Use `Docs/WEB_VERSIONED_RELEASE.md` and
> `web/versioned_release.mjs` for the production website.
> After version-pointer initialization, retire this CLI for that target. If its
> journal already exists, recover it before initialization; never run the old
> directory swap against an initialized stable pointer.

`web/release_transaction.mjs` stages, promotes, recovers, and rolls back one
complete browser-release generation. It verifies the v2 release manifest,
exact file set, byte lengths, SHA-256 values, intended base path, and absence of
symbolic links before a generation can move.

The production server path is:

```text
site root: /var/www/html/webxr
live relative path: Ports/SurrealEngine
public URL: https://dionysus.dk/webxr/Ports/SurrealEngine/
```

## Guarantees and modes

- A lock rejects concurrent release transactions.
- A write-then-rename journal records the expected live and incoming hashes
  before any stable-path change.
- Staging copies to a unique partial directory, verifies it, then renames the
  complete generation to its hidden stage name. Interrupted copies never alter
  the live directory.
- On Linux, promotion and rollback default to `renameat2(RENAME_EXCHANGE)`.
  The live and incoming directory entries exchange names in one kernel
  operation. This is filesystem atomicity for each lookup, not a transaction
  spanning a browser's HTML, JavaScript, and WASM requests.
- The previous generation is retained under a hidden rollback name. A rollback
  atomically exchanges it with the live generation and preserves the displaced
  failed generation.
- Recovery inspects hashes and filesystem state rather than trusting the last
  journal-state write. It handles a crash on either side of every rename or
  exchange. A corrupt incoming generation is exchanged back out and retained
  under a hidden rejected name.
- Windows defaults to the journaled two-rename mode for local dry runs. That
  mode is crash recoverable but can briefly lack a stable directory; it is not
  the production website mode.

The site root and every managed path must be writable only by the deployment
identity; the verifier and cooperating lock are not designed to defeat a second
privileged process mutating files between verification and rename. The candidate
passed to `stage` must be outside the managed site root. Upload
it to a private temporary location such as `/var/tmp`, not under public
`Ports`.

## Stage

Use the manifest hash from the frozen qualification card:

```sh
node web/release_transaction.mjs stage \
  --site-root=/var/www/html/webxr \
  --candidate=/var/tmp/SurrealEngine-candidate \
  --manifest-sha256=<new-manifest-sha256> \
  --record=/var/tmp/surreal-stage.json
```

The result prints `stageName`. Repeating the same stage is idempotent only when
the complete staged manifest still matches.

## Promote

Promotion requires both the expected old live hash and staged candidate hash.
It also requires the explicit mutation acknowledgement:

```sh
node web/release_transaction.mjs promote \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --stage-name=<stageName> \
  --manifest-sha256=<new-manifest-sha256> \
  --live-manifest-sha256=<old-live-manifest-sha256> \
  --record=/var/tmp/surreal-promote.json
```

On Linux, do not override the default exchange mode. The result records
`swapMode: exchange`, `liveBefore`, `liveAfter`, and `displacedName`. Verify the
public manifest and run the hosted flat smoke before beginning physical Q1.

## Recover after process or host loss

If a journal exists, no new stage/promotion/rollback can start. Run recovery:

```sh
node web/release_transaction.mjs recover \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --record=/var/tmp/surreal-recover.json
```

If the process died and left its lock, recovery refuses to guess. Confirm the
PID in the scoped `.SurrealEngine-<scope>.release-lock` JSON file is dead, then explicitly
preserve and replace the stale lock:

```sh
node web/release_transaction.mjs recover \
  --execute=yes \
  --break-stale-lock=yes \
  --site-root=/var/www/html/webxr \
  --record=/var/tmp/surreal-recover.json
```

The abandoned lock file is retained as evidence. The scope is derived from the
full live-relative path, so separate deployment targets cannot share a lock or
journal accidentally.

If the interrupted operation was staging, there is no mutation journal because
staging never changes the live path. Supply the expected live manifest hash so
recovery can verify that invariant before it preserves and clears the dead lock:

```sh
node web/release_transaction.mjs recover \
  --execute=yes \
  --break-stale-lock=yes \
  --site-root=/var/www/html/webxr \
  --live-manifest-sha256=<expected-live-manifest-sha256> \
  --record=/var/tmp/surreal-recover-stage.json
```

## Roll back

Use the exact `displacedName` from the promotion record:

```sh
node web/release_transaction.mjs rollback \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --rollback-name=<displacedName> \
  --manifest-sha256=<old-live-manifest-sha256> \
  --live-manifest-sha256=<new-live-manifest-sha256> \
  --record=/var/tmp/surreal-rollback.json
```

Rollback is complete only after the public manifest matches the old hash and
the retained failed generation matches the candidate hash.

## Tests

```powershell
node web/test_release_transaction.mjs
wsl.exe -e sh -lc "cd /mnt/c/Devstuff/QuestGames/SurrealEngine/repos/worktrees/active/webgl2-production && node web/test_release_exchange_transaction.mjs"
```

The first suite covers the portable journaled fallback, file/path validation,
dead locks, interrupted state writes, tamper rejection, recovery, and rollback.
The second runs on a native Linux filesystem and proves the atomic exchange
path across promotion, rollback, interrupted journaling, and corrupt-live
reversal.
