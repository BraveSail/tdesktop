# Local Fork Modifications

This file records BraveSail/mizugram behavior that must survive merges from
TDesktop-x64/tdesktop. Treat behavior as authoritative; old implementation
locations may move when upstream refactors code.

## Current integration target

- **Upstream is now `telegramdesktop/tdesktop` directly** (remote `upstream`);
  the previous upstream `TDesktop-x64/tdesktop` (64Gram) is kept as remote
  `64gram` for reference only.
- Current merge: official `v7.2.9` (`fb2e332095`) merged into `dev` as
  `ac4d08eae2`.
- The fork was renamed to **Mizugram**: `AppName` is "Mizugram Desktop",
  `AppFile` is "Mizugram", the GitHub repo is `BraveSail/mizugram`, and CI
  publishes GitHub releases tagged `mizugram-v*`. It follows the upstream
  version string (`7.2.9`). `UpstreamVersion` in `core/version.h` tracks the
  upstream release the tree is based on.
- The application id is `io.github.bravesail.Mizugram`: `lib/xdg/` file names,
  `Resources/qrc/telegram/telegram.qrc`, `CMakeLists.txt`,
  `platform/linux/specific_linux.cpp` and `core/update_checker.cpp`. The three
  `QFile::remove(...)` calls in `specific_linux.cpp` still name
  `io.github.tdesktop_x64.TDesktop` on purpose: they delete the desktop and
  D-Bus files that pre-rename builds installed, so they must not follow the id.
- Branding must survive merges: user-visible text lives in
  `Resources/langs/lang.strings` and `Resources/langs/localization/en.json`
  (the embedded pack applied at runtime, so a stale key there overrides
  `lang.strings`), plus `Resources/winrc/*.rc`, `build/setup.iss`,
  `lib/xdg/*` and the repository URLs in `docs/`.
- Sources are BraveSail's own: the default update prefix is
  `https://bravesail.github.io/mizugram/` (nothing is published there yet, so
  the built-in update check fails quietly; the `update_url` enhanced setting
  overrides it) and language packs come from
  `https://raw.githubusercontent.com/BraveSail/Localization/master/%1.json`.
  The TL viewer still points at `tdesktop-x64.github.io/tlv/`.
- Update trust is ours, not upstream's: `Resources/update/root-public.pem`,
  `manifest.min.json` and `manifest.sig` hold the Mizugram Ed25519 root key and
  a manifest that authorizes `mz-stable-2026a` for the `stable` and `beta`
  channels. A merge must never bring upstream's files back — the client would
  then trust Telegram's key and reject our packages. Private keys live outside
  the repository; a package is signed with
  `Packer -path <dir> -version <N> -channel stable
  -keys-loc Telegram/Resources/update -local-key <stable-private.pem>
  -local-key-id mz-stable-2026a`.
- `Telegram/build/version` intentionally has no BOM, matching upstream.

### Submodule sources

Three submodules are BraveSail forks; everything else points upstream.

- `Telegram/lib_ui` → `BraveSail/lib_ui`. Its `master` is upstream's pointer
  plus one commit, `Fix Windows popup submenu ownership`, which adds
  `SubmenuParent()` in `ui/widgets/popup_menu.cpp` (Windows must not have a
  popup own another popup, or native mouse presses get redirected to the
  owner and the menu closes; Wayland needs the opposite).
  **Keep this rebased onto whatever lib_ui pointer the merged upstream
  release expects** — it must not drift behind, or the build fails on a
  missing lib_ui API. Upstream does not have this fix (as of 7.2.8).
- `lib_base` and `cmake` (`cmake_helpers`) → their upstream sources.
- `Telegram/lib_storage` → `BraveSail/lib_storage`, forked from
  `TDesktop-x64/lib_storage` and pinned at its tip `d167d22` ("Ignore applied
  error"): it drops a local variable that upstream `ccdc725` still assigns,
  which the Windows release build rejects under warnings-as-errors.
- `Telegram/ThirdParty/tgcalls` → `BraveSail/tgcalls`, forked from
  `TDesktop-x64/tgcalls`; see the tgcalls note below.

### Merge-resolution traps (learned the hard way, 2026-09-13)

Two classes of damage a merge does **not** surface as a conflict:

1. **Never resolve a file with `git checkout --ours` / `--theirs`.** Those replace
   the entire file with one side, silently discarding every change the other
   side made — including the parts git had already auto-merged cleanly. This
   cost a full CI cycle: `lang.strings` was resolved with `--ours`, which dropped
   19 upstream keys whose only visible trace was a compile error far away
   (`scene_item_text.cpp: error C2039: 'lng_photo_editor_text_style_opaque' is
   not a member of 'tr'`). Resolve conflict *hunks* by hand instead, and always
   re-check the result against upstream afterwards:
   `comm -23 <(upstream keys) <(our keys)`.

2. **Submodules carry their own hidden conflict surface.** When an upstream
   release is followed directly (rather than via 64Gram), the submodule sources
   change too, and anything 64Gram had patched *inside* a submodule is lost
   without any conflict marker in the main tree. lib_ui hit this twice: the
   Windows popup submenu ownership fix, and 64Gram's `WindowTitle`
   "always on top" styles in `ui/widgets/widgets.style` (plus `banButtonBg` in
   `ui/colors.palette`). Both live in `BraveSail/lib_ui` now, on top of the
   upstream pointer. Before bumping a submodule pointer, diff the old fork
   against upstream (`git diff <upstream> <fork> --stat`) and port what is ours.

   **Do not assume "upstream is always the better side" — check the lineage.**
   The right question per submodule is whether the 64Gram pointer *contains*
   the upstream pointer:

   ```bash
   git merge-base --is-ancestor <upstream-pointer> <64gram-pointer> \
     && echo superset || echo not-a-superset
   ```

   - **tgcalls** — 64Gram's fork is a **strict superset** of what tdesktop
     7.2.8 expects, and the call code needs its extras (`enableStereoMode`,
     `customBitrate`, `enableHDVideo` on `GroupInstanceDescriptor`,
     `setIsStereoModeEnabled`). **Keep the fork** (`BraveSail/tgcalls`, forked
     from `TDesktop-x64/tgcalls`), otherwise `calls_group_call.cpp` stops
     compiling.
   - **lib_base / cmake_helpers** — 64Gram's copies are **older** than
     upstream (`AutoUpdateVersion` 4 vs 6, the tlottie migration, the d3d
     validator). **Keep the upstream pointer.**
   - **lib_storage** — upstream plus 64Gram's unused-variable fix, in
     `BraveSail/lib_storage`; keep it until upstream fixes the warning.
   - **lib_ui** — upstream pointer plus our own patches, in `BraveSail/lib_ui`.

   **Setting a submodule pointer: check out first, then stage — and verify the
   remote.** `git update-index --cacheinfo 160000,<sha>,<path>` looks like it
   works, but a later `git add <path>` re-records whatever commit the submodule
   worktree has checked out, silently reverting the change. Correct order:

   ```bash
   git -C <path> checkout <sha>      # move the submodule worktree first
   git add <path>                    # now staging records <sha>
   git ls-tree HEAD <path>           # confirm locally
   git push && git fetch && git ls-tree origin/<branch> <path>   # confirm remotely
   ```

   Skipping the remote check cost a full CI cycle: the pointer looked right
   locally but the pushed commit still had the upstream one.

### Enhancement hotspots (where upstream merges conflict)

Files that upstream changes and this fork also touches, so they are the
first to check on every merge:

- `Telegram/Resources/langs/lang.strings` — the fork's settings/feature
  strings sit in one block; upstream adds new keys in the same area. Keep
  both sides.
- `Telegram/SourceFiles/history/view/history_view_message.cpp` — upstream
  refactors gesture/paint code here (e.g. `hasGesture` → `gestureShift`).
  Take upstream's structure, keep the `screenshot_mode` enhancement.
- `Telegram/SourceFiles/menu/menu_item_download_files.cpp` — upstream's
  `Collected()` skips a restricted item instead of dropping the whole menu
  entry. Keep upstream's structure, but keep the
  `allowsMediaDownloadControls()` check so Force Copy semantics survive.
- Version/brand files (`core/version.h`, `build/version`,
  `Resources/winrc/*.rc`, `Resources/uwp/AppX/AppxManifest.xml`,
  `Resources/update/*` — the update trust material is ours).
- Rounding out the ~59-file overlap: `history/view/*`, `calls/*`, `core/*`,
  `boxes/*`, `storage/*`, `settings/*`, `Telegram/CMakeLists.txt`.

## Local user-visible behavior

### Configurable translation sources

Required behavior:

- Translation sources are Google, Telegram API, and LLM.
- Source selection is shared by Enhanced Settings, message context menus, and
  the translate popup.
- LLM translation uses the OpenAI Responses API and configurable URL, keys,
  model, temperature, and system prompt.
- Normal messages can translate in the popup or inline in the message.
- Selected text and polls remain popup based.
- The loading state must render while an inline request is pending.
- Telegram rich-message rendering and translation introduced upstream must be
  retained. Do not replace it with the old flat-text implementation.

Primary files and markers:

- `Telegram/SourceFiles/lang/translate_provider.*`:
  `TranslateSource`, `CurrentTranslateSource`, `CreateTranslateProvider`.
- `Telegram/SourceFiles/lang/translate_mtproto_provider.*`:
  Responses API request and LLM result parsing.
- `Telegram/SourceFiles/boxes/translate_box.*`:
  `TranslateMessageInline`, source switching, and rich-translate coexistence.
- `Telegram/SourceFiles/core/enhanced_settings.cpp`:
  `translation_provider`, `translate_in_message`, and `llm_` settings.
- `Telegram/SourceFiles/history/history_item*`:
  manual translation state and upstream `richPage` state.
- `Telegram/SourceFiles/history/view/history_view_message.*`:
  inline loading and rich-page rendering.

Merge guidance:

- Use upstream rich-message architecture as the base.
- Preserve both `HistoryMessageTranslation::richPage` and the fork's `manual`
  state.
- Starting a manual translation must clear stale text and rich-page results.
- Do not present Google/LLM as active rich-message sources unless that path
  actually routes through those providers.

### Force Copy

Required behavior:

- When enabled, ordinary no-forward restrictions do not block text selection,
  copying, or normal media copy/save/download controls.
- TTL, single-view, story, paid extended-media, and other ephemeral hard
  restrictions must not be bypassed accidentally.

Primary markers:

- `Core::kEnhancedForceCopyKey`
- `ForceCopyEnabled()`
- `HistoryItem::allowsMediaDownloadControls()`
- `OverlayWidget::contentNeedsScreenshotProtection()`

Merge guidance:

- Do not wrap all of `forbidsSaving()` in one Force Copy override. New upstream
  versions include TTL and paid-media checks in that helper.
- Separate ordinary no-forward policy from hard ephemeral/privacy policy.
- Audit chat lists, shared media, document/GIF corner downloads, overview,
  media viewer buttons, show-in-folder, and screenshot protection.

### Show RPC errors (ported from Nagram)

Required behavior:

- When the enhanced toggle (`enhanced-show-rpc-errors`) is enabled, every RPC
  request failure surfaces as a toast reading `type: description` (type only
  when description is empty). Off by default; the toggle lives in Enhanced
  Settings, Network section.
- Display is marshalled to the main thread (`crl::on_main`) and happens even
  for requests that registered their own fail handlers.

Primary markers:

- `Core::kEnhancedShowRpcErrorsKey`
- `rpcErrorOccured` in `Telegram/SourceFiles/mtproto/mtp_instance.cpp`
- `AddPrefToggle` call in `Settings::Enhanced::SetupEnhancedNetwork`
- Ported from Nagram (`nextalone/nagram`): `ErrorDatabase.showErrorToast`
  behind `NaConfig.getShowRPCError()`.

### Disable input status

Required behavior:

- `enhanced-disable-chat-action` suppresses ordinary typing status.
- It does not suppress upload, recording, or other send actions unless product
  behavior is explicitly changed later.

Primary markers:

- `Core::kEnhancedDisableChatActionKey`
- `Api::SendProgressManager::send()`
- `SendProgressType::Typing`

### Automatic inline-bot rules

Required behavior:

- Local and remote URL matching rules can invoke inline bots.
- Rules support enable/disable, editing, refresh, persistence, cancellation,
  placeholder handling, and optional direct send.
- Cancelling a rule-triggered inline query preserves the original URL.

Primary files and markers:

- `Telegram/SourceFiles/chat_helpers/inline_bot_rules.*`
- `InlineBotRules::Match`
- `InlineBotRules::RefreshRemote`
- `_inlineBotFromRule`
- `kEnhancedAutoInlineBotDirectSendKey`

Merge guidance:

- Retain upstream visual tab ordering in `history_widget.cpp` together with
  remote rule refresh.
- Use upstream `_inlineBotUsername`; rule-triggered placeholder offset remains
  zero.
- Prefer `QByteArray` for persisted JSON with UTF-8 conversion at the boundary
  if upstream settings storage uses `QByteArray` specializations.

### Windows CI and automatic release

Required behavior:

- `windows-debug.yml` and `windows-release.yml` remain available.
- Release builds retain `/FS`, embedded debug information, bounded parallelism,
  pagefile expansion, resource diagnostics, split caches, and Telegram upload.
- Cached prepare patch scripts are normalized before use and before cache save.
- A push to `dev` currently creates a real release and sends it to Telegram.
- Release artifacts are named
  `Mizugram-{platform}-{arch}-{qt}-{Release|Debug}-{version}`, the version
  being `AppVersionStr` from `Telegram/build/version` (the same string the
  About box shows): `Mizugram-Windows-x64-Qt6-Release-7.2.9.7z` for the release
  archive, `Mizugram-Windows-x64-Qt6-Debug-7.2.9` for the debug upload.

Merge guidance:

- Adopt upstream dependency versions, VS/toolset setup, action versions, and
  cache schema, then reapply the local stability fixes.
- Do not restore VS2022 overrides after upstream moves to VS2026/toolset 14.44.
- Use a fresh cache namespace for the first build after toolchain changes.
- Validate with `workflow_dispatch` and `send_to_telegram=false` before pushing
  the final integration commit to `dev`.

## Predicted conflict files for v1.2.8

- `.github/workflows/linux.yml`
- `.github/workflows/mac.yml`
- `.github/workflows/mac_packaged.yml`
- `.github/workflows/snap.yml`
- `.github/workflows/win.yml`
- `Telegram/build/prepare/prepare.py`
- `Telegram/SourceFiles/boxes/translate_box.cpp`
- `Telegram/SourceFiles/core/core_settings.cpp`
- `Telegram/SourceFiles/history/history_inner_widget.cpp`
- `Telegram/SourceFiles/history/history_item.cpp`
- `Telegram/SourceFiles/history/history_item.h`
- `Telegram/SourceFiles/history/history_widget.cpp`
- `Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp`
- `Telegram/SourceFiles/history/view/history_view_context_menu.cpp`
- `Telegram/SourceFiles/history/view/history_view_message.cpp`
- `Telegram/SourceFiles/history/view/history_view_message.h`

## Verification after every upstream merge

- Confirm no unmerged index entries or conflict boundaries remain.
- Run `git diff --check` and the repository's relevant generators/checks.
- Build a Linux Debug target in the documented Docker environment when
  available.
- Validate Windows Debug and Release with both cold and warm caches.
- Exercise all three translation source controls and inline/popup translation.
- Exercise Force Copy separately for ordinary no-forward, TTL/single-view,
  stories, and paid media.
- Exercise inline-bot rule matching, cancellation, direct send, refresh, and
  persistence across restart.
- Confirm Disable input status suppresses typing but not unrelated actions.
