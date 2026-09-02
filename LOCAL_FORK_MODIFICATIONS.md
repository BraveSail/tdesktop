# Local Fork Modifications

This file records BraveSail/tdesktop behavior that must survive merges from
TDesktop-x64/tdesktop. Treat behavior as authoritative; old implementation
locations may move when upstream refactors code.

## Current integration target

- Fork base: `origin/dev2` at `56269c71018765dd3b88a0c14afaa5c0430d260b`.
- Upstream target: TDesktop-x64/tdesktop `v1.2.8` at
  `a17cad8e9846ee2a5679b335581722f9fb203dc1`.
- Merge base: `a8351504232ea2705623f376131f4eb84e74df78`
  (TDesktop-x64 1.2.3, based on Telegram Desktop 6.8.2).
- The target is based on Telegram Desktop 7.1.3.
- The fork has 18 unique commits and 47 changed files since the merge base.
- Upstream has 2956 unique commits and 2004 changed files since the merge base.
- Thirty files overlap and a merge dry-run reports 16 content conflicts.

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
- A push to `dev2` currently creates a real release and sends it to Telegram.

Merge guidance:

- Adopt upstream dependency versions, VS/toolset setup, action versions, and
  cache schema, then reapply the local stability fixes.
- Do not restore VS2022 overrides after upstream moves to VS2026/toolset 14.44.
- Use a fresh cache namespace for the first build after toolchain changes.
- Validate with `workflow_dispatch` and `send_to_telegram=false` before pushing
  the final integration commit to `dev2`.

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
