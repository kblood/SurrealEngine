# Immutable browser release and stable pointer

`web/versioned_release.mjs` is the production deployment path for the browser
build. It keeps the canonical entry URL stable while every loaded page, script,
and WASM binary stays on one immutable generation.

```text
canonical entry: /webxr/Ports/SurrealEngine/
generation:      /webxr/Ports/SurrealEngine/releases/<manifest-sha256>/
generation ID:   SHA-256 of the complete release-manifest.json bytes
```

The stable `.htaccess` redirects only an empty path or explicit `index.html`
with a non-cacheable HTTP 302. Once redirected, all relative requests stay
under the selected generation. Published generation directories never move or
change. Promotion and rollback atomically replace that one `.htaccess` file.

The first initialization preserves the old unversioned package files at the
stable root as a compatibility namespace. A tab that received the old HTML
before initialization can still fetch its old root-relative manifest, assets,
WASM, and corresponding source afterward. Fresh navigation is redirected to
the immutable copy. Do not remove or modify those compatibility files.

## Host preflight

The production host must use Apache 2.4 with `AllowOverride FileInfo`, the
rewrite, headers, and MIME modules enabled, and either `Options FollowSymLinks`
or `Options SymLinksIfOwnerMatch` permitted for this directory. Apache rejects
per-directory rewrite rules without one of those options. Confirm on an actual
Apache non-production path—not only the Node model server—that:

- the canonical root returns `302` with `Cache-Control: no-store`;
- `Location` is the complete same-origin generation path ending in `/`;
- the generation HTML and manifest revalidate;
- hashed CSS, JavaScript, and WASM return one-year `immutable` caching;
- `.wasm` is `application/wasm`, compressed variants negotiate correctly, and
  COOP/COEP/CORP headers remain present;
- directory listing is disabled for `releases/`.

The site root, lock, journal, stable package, and generation tree must be
writable only by the deployment identity. The verifier and lock coordinate the
release tool; they do not defend against another privileged writer changing a
verified file between operations.

Before initialization, confirm that no scoped legacy
`.release-transaction.json` exists. The version-pointer tool refuses to run
while an old directory-generation journal is unfinished. Recover that journal
with `web/release_transaction.mjs`, then retire the old CLI permanently for this
target; recovering a directory swap after pointer initialization could replace
the stable compatibility container.

## One-time initialization

Initialize against the exact manifest currently served at the stable root:

```sh
node web/versioned_release.mjs initialize \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --live-manifest-sha256=<current-manifest-sha256> \
  --record=/var/tmp/surreal-version-initialize.json
```

Initialization verifies the existing package, copies its manifest-listed
payload to `releases/<current hash>/`, verifies that copy, fsyncs it, and then
commits the redirect. It is idempotent for the same exact hash. A process loss
before the pointer journal can be retried; a process loss after the journal is
handled by `recover`.

## Publish without changing stable

Upload the candidate outside the managed site root, then publish it:

```sh
node web/versioned_release.mjs publish \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --candidate=/var/tmp/SurrealEngine-candidate \
  --manifest-sha256=<candidate-manifest-sha256> \
  --record=/var/tmp/surreal-version-publish.json
```

Publication copies to a unique partial path, verifies the exact v2 manifest,
file set, byte counts, hashes, base path, and absence of symlinks, makes the
tree durable, and renames it to its public full-manifest-hash path. Repeating a
publish is accepted only if the existing generation verifies exactly.

Run the complete hosted flat smoke and required physical qualification against
this generation URL before promotion. Publishing alone does not change what
the canonical entry serves.

After promotion or rollback, pin the externally expected pointer identity in
the browser smoke; a smoke that merely trusts whichever manifest the page
returns can accidentally pass an old stable build:

```sh
python web/smoke_test_release_package.py \
  --base-url=https://dionysus.dk/webxr/Ports/SurrealEngine \
  --expected-manifest-sha256=<expected-current-hash>
```

## Promote

```sh
node web/versioned_release.mjs promote \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --current-manifest-sha256=<expected-current-hash> \
  --target-manifest-sha256=<qualified-candidate-hash> \
  --record=/var/tmp/surreal-version-promote.json
```

The command verifies both retained generations and the expected current
pointer, writes a durable journal, and atomically replaces `.htaccess`. The
internal completion record is durable before the journal is removed. A stale
operator expectation or missing/tampered target stops before the pointer
commit.

## Roll back

Rollback changes only the pointer; it never deletes either generation:

```sh
node web/versioned_release.mjs rollback \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --current-manifest-sha256=<failed-current-hash> \
  --target-manifest-sha256=<retained-good-hash> \
  --record=/var/tmp/surreal-version-rollback.json
```

Afterward, verify the canonical 302, target manifest, flat launch, immutable
asset headers, and retained failed generation. Existing tabs on the failed
generation remain internally consistent; rollback affects only fresh stable
navigation.

## Recover

If a pointer journal remains after process or host loss:

```sh
node web/versioned_release.mjs recover \
  --execute=yes \
  --site-root=/var/www/html/webxr \
  --record=/var/tmp/surreal-version-recover.json
```

Recovery compares the complete old/new pointer-file hashes. An old pointer
aborts the uncommitted operation; a valid new pointer finalizes it; a corrupt
target or ambiguous pointer file restores the verified old pointer. The
journal remains if recovery cannot establish a safe state.

If a dead process left the complete exclusive lock file, first confirm its PID
is dead. Preserve and clear it explicitly. A no-journal recovery also requires
the expected current manifest so a wrong retry cannot consume the lock:

```sh
node web/versioned_release.mjs recover \
  --execute=yes \
  --break-stale-lock=yes \
  --site-root=/var/www/html/webxr \
  --current-manifest-sha256=<expected-current-hash> \
  --record=/var/tmp/surreal-version-unlock.json
```

## Retention and tests

Published versions are runtime dependencies, not disposable build output.
Retain every promoted generation and the bootstrap compatibility payload. At a
minimum, no generation can be collected before its one-year immutable cache
lifetime plus a conservative session margin, and rollback targets need longer
retention. Any future garbage collector requires a separate audited ledger.

```powershell
node web/test_versioned_release.mjs
node web/test_release_transaction.mjs
wsl.exe -e sh -lc "cd /mnt/c/Devstuff/QuestGames/SurrealEngine/repos/worktrees/active/webgl2-production && node web/test_release_exchange_transaction.mjs"
```

The Node versioned test models bootstrap compatibility across the commit boundary,
immutable A/B roots, atomic pointer replacement under concurrent HTTP reads,
process-loss recovery, ambiguous pointer restoration, rollback, idempotent
publication, CLI acknowledgement, and no-journal stale-lock recovery. The two
directory-generation suites remain lower-level durability and bootstrap QA;
they are not the production serving model.
