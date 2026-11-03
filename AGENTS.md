# Agent Guidelines

These guidelines apply to every change in this repository. They are written in
project-neutral language so they can be copied to other repositories.

## Working principles

- Make the smallest change that fully satisfies the request.
- Match the existing structure, naming, formatting and style of nearby code.
- Treat unfamiliar files or uncommitted changes as work in progress. Inspect
  them before editing, and never delete or overwrite them casually.
- Do not commit, tag, or publish a release unless explicitly asked.
- Prefer dedicated editing/search tools over ad-hoc shell commands.
- Use clear file paths when describing changes.

## Before editing

- Read the relevant implementation, not just the obvious file.
- Search for all call sites and references before removing or renaming an
  identifier, option, setting, or documented behavior.
- Check whether the change affects persisted settings, command-line options,
  packaging, help text, or release metadata.
- Keep backward compatibility for existing user configuration where practical.

## Build and verification

- Use the repository's own build script or documented build command.
- Do not install system packages or launch the GUI application for testing.
  The user performs manual desktop testing.
- Fix every new compiler warning or error introduced by the change. Do not
  silence warnings to make the build pass.
- After packaging, verify that the packaged executable matches the staged build
  by comparing their hashes.
- Verify the package version and metadata, not just the compiled binary.
- Run `git diff --check` before committing.
- After removing a feature, grep for leftover identifiers, strings, menu
  entries, documentation references and translation-like text.

## Versioning

- When asked to release, bump the version in every authoritative place, for
  example the build script, build definition, package metadata, application
  metadata, user documentation and any version string shown by the program.
- Keep generated package filenames consistent with the new version.
- Do not leave mixed versions between code, documentation and package names.
- If asked to use a version that already exists, move the tag and replace the
  release assets so they match the final commit.

## Commits and tags

- Stage only the intended files. Do not stage credentials, scratch files,
  temporary task files, or unrelated user changes.
- Write a short imperative commit summary followed by concise bullets when the
  change has several parts.
- Use annotated tags for releases.
- Before pushing, fetch the remote. If it has advanced, rebase or merge
  cleanly; never overwrite unrelated remote work.
- Never force-push a branch. Force-update a release tag only when explicitly
  asked to keep the same version.

## Release workflow

Release only on explicit request. A complete release normally includes:

1. Update user-facing release notes.
2. Bump the version everywhere it is authoritative.
3. Build from a clean state and confirm there are no new warnings.
4. Verify the packaged executable hash and package version.
5. Commit the changes and push the branch.
6. Create or move the release tag and push it.
7. Create or update the release entry with the prepared notes.
8. Replace old release assets with the new packages.
9. Verify the uploaded asset checksums against the local files.
10. Remove temporary credentials and report the final hashes.

## Documentation and in-app help

- A user-visible behavior change requires matching updates to every user
  document and to the in-app hints/help.
- Keep wording simple, direct and consistent across the interface and docs.
- State common controls once instead of repeating them for every item.
- Emphasize critical or non-obvious gestures.
- For dense help content, prefer a wide layout with columns over a very tall
  single column.
- Remove outdated statements rather than leaving contradictory text.
- Use a single documented spelling and terminology for actions and settings.

## Settings and compatibility

- Persist user choices with a stable configuration key.
- When removing an old key, keep accepting it during parsing so existing
  configuration files continue to load, even if the value is ignored.
- Reset behavior must be intentional: do not re-enable notices or options the
  user explicitly turned off unless requested.
- Avoid changing defaults in a way that silently overrides saved settings.
- Keep the settings UI order logical and put related options next to each
  other.

## UI and input behavior

- Restore pointer or keyboard grabs after closing modal dialogs.
- Do not draw or act on motion while a menu or long-press gesture is pending.
- Ensure temporary tool overrides are not immediately overwritten by the normal
  tool-selection logic.
- Consider hover, modifier keys, Caps Lock/Num Lock, and release events outside
  the window.
- Distinguish short press, long press, and click semantics explicitly.
- Cancel pending timers when the gesture ends or is superseded.

## Safety and hygiene

- Never commit tokens, passwords, private keys, or personal credentials.
- Remove temporary credential files immediately after use.
- Do not run destructive commands or rewrite history without explicit
  permission.
- Keep temporary build and release work under the approved temporary directory.
- Before reporting completion, summarize what changed, what remains
  uncommitted or unreleased, and any manual testing still required.
