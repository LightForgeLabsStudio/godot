# Lightforge Godot Fork

This repository is a narrowly maintained fork of
[`godotengine/godot`](https://github.com/godotengine/godot). It exists to deliver
machine-readable local script-profiler output for Lightborn while the generic
capability is proposed upstream.

## Supported line

- Upstream base: tag `4.6.3-stable`
- Long-lived base branch: `lightforge/4.6.3`
- Upstream remote: `https://github.com/godotengine/godot.git`
- Lightborn tracking epic:
  [`LightbornExileDivinityEngine#1451`](https://github.com/LightForgeLabsStudio/LightbornExileDivinityEngine/issues/1451)

The `master` branch mirrors the ordinary GitHub fork and is not the Lightborn
delivery line. Profiler work branches from `lightforge/4.6.3`. Godot 4.7 work is
outside this patch line and remains tracked separately in the game repository.

## Patch policy

The Lightforge patch stack may contain only:

- generic structured output for Godot's local script profiler;
- tests and documentation for that output;
- reproducible build, packaging, and provenance automation needed to distribute
  the patched editor.

Game telemetry, run identifiers, playtest semantics, and other Lightborn domain
concepts must not enter engine source. Correlation values supplied by callers are
opaque strings.

Unrelated engine fixes, permissive compatibility shims, and speculative profiler
features require their own upstream work and do not belong on this line.

## Remotes and updates

A local checkout uses:

```text
origin   https://github.com/LightForgeLabsStudio/godot.git
upstream https://github.com/godotengine/godot.git
```

Verify the base at any time with:

```text
git rev-parse 4.6.3-stable
git merge-base --is-ancestor 4.6.3-stable lightforge/4.6.3
```

The expected tag commit is
`35e80b3a8822a9df9be390814b62f44c0a9c69e8`. Updates are rebased or
cherry-picked deliberately; merge commits from upstream are not added to the patch
stack.

## Baseline build

`.github/workflows/lightforge_windows_baseline.yml` is the fork's authoritative
Windows build. It uses the upstream 4.6.3 SCons build system and dependencies to
produce one x86-64 editor build containing:

- the GUI editor executable;
- the console wrapper executable used by Lightborn tooling.

The workflow runs Godot's built-in tests and packages:

- both executables;
- `LICENSE.txt`, `COPYRIGHT.txt`, and `AUTHORS.md`;
- `provenance.json` with engine source, upstream base, build flags, workflow, and run
  identifiers;
- `SHA256SUMS.txt` covering the binaries and notices.

The companion `lightforge_engine_acceptance.yml` workflow in the private Lightborn
repository downloads this public artifact, verifies its checksums and provenance,
and starts the unchanged game project. Keeping that check in Lightborn avoids
granting the public fork credentials to the private game repository.

The fork repository variable `DISABLE_GODOT_CI=true` suppresses Godot's inherited
all-platform matrix. It must remain set; the Lightforge workflow is the required
gate for this patch line.

Pull requests use the `lightforge-baseline` build identifier. A manual dispatch
requires an explicit candidate identifier and defaults to
`lightforge-profiler.1`; release artifacts must be produced from a merged
`lightforge/4.6.3` commit with that candidate identifier.

## Distribution

Development workflow artifacts are not releases and do not change Lightborn's
engine pin. A candidate release is created only by the dedicated release issue after
the structured profiler patch and benchmarks pass. Distributed artifacts retain all
Godot license and copyright notices.

## Exit condition

Archive this fork after an official stable Godot release supplies the accepted
machine-readable profiler Interface and Lightborn has returned to a checksummed
official build. The upstream proposal and migration are tracked by
[`LightbornExileDivinityEngine#1458`](https://github.com/LightForgeLabsStudio/LightbornExileDivinityEngine/issues/1458).
