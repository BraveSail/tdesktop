# Nagram Translation Source Port Plan

## Goal

Port Nagram's translation source feature into 64Gram Desktop, with LLM translation as a first-class source.

The feature is not complete until all three user-facing entry points stay in sync:

- Enhanced Settings translation source selector
- Message context menu translation source switch
- Translate popup source switch

There is a separate display-mode requirement:

- Message inline translation vs translate popup mode switch

## Nagram Behavior To Match

Nagram has two related translation controls:

- `translationProvider`: selects the provider used by the custom translation path.
- `useTelegramTranslateInChat`: forces in-chat message translation through Telegram's native translate alert/API path.

Provider IDs observed in Nagram:

- `1`: Google
- `8`: Telegram API
- `10`: LLM

The desktop port should keep these IDs for settings compatibility where practical.

## Current Desktop State

Already started:

- Added `translation_provider` under Enhanced settings.
- Added source labels for Google and Telegram API.
- Restored Telegram API translation path through `messages.translateText`.
- Kept Google App translation path.
- Added a source selector in Enhanced Settings.
- Added a translation source submenu in message context menu.
- Added a source button in the translate popup.
- Added LLM source using OpenAI Responses API.

Important gap:

- None known in this plan. Manual UI/runtime verification is still needed.

## Required Work

### 1. LLM Provider

Add LLM as a real translation source:

- Extend source selection to include `LLM`.
- Route `translation_provider == 10` to an LLM request path.
- Return normal `TranslateProviderResult` values so translate popup and chat auto-translation share the same path.

Minimum usable format:

- OpenAI Responses API endpoint.
- Request URL: `{base_url}/responses`.
- Auth: `Authorization: Bearer {api_key}`.
- Request body: `model`, `instructions`, `input`, `temperature`.
- Response parse: `output[].content[].text` or `output[].content[].output_text`.

Suggested default settings:

- `llm_api_url`: `https://api.openai.com/v1`
- `llm_api_keys`: empty
- `llm_model`: `gpt-4.1-mini`
- `llm_temperature`: `0.7`
- `llm_system_prompt`: empty means use built-in Nagram-style prompt

### 2. LLM Settings UI

Add a settings box under Enhanced Settings:

- API URL
- API keys, comma-separated
- Model
- Temperature
- System prompt

The Translation source selector should show:

- Google
- Telegram API
- LLM

When LLM is selected but API keys are empty, translation should fail cleanly with the existing translate error state.

### 3. Message Context Menu Switch

The message context menu must include a translation source switch wherever the translate action appears:

- Selected text translate
- Message translate
- Poll translate

Expected behavior:

- Menu shows current source with a check mark.
- Selecting another source updates `translation_provider`.
- The next translate action uses the selected source.
- Do not add separate source state for each menu entry.

### 4. Message Inline / Popup Mode Switch

Add a switch equivalent to Nagram's `useTelegramTranslateInChat`, but named for desktop semantics.

Expected behavior:

- One Enhanced setting controls whether message translate opens the popup or applies translation in the message view.
- Popup mode keeps using `Ui::TranslateBox`.
- Inline mode should use Telegram Desktop's existing message translation surface where possible.
- This is independent from translation source; LLM must work in both display modes if the inline surface uses `TranslateProvider`.
- Poll translation may stay popup-only if inline mode is not supported.

Implemented desktop setting:

- `translate_in_message`

Implemented behavior:

- Normal message translate opens the popup when disabled.
- Normal message translate writes translation into the message view when enabled.
- Selected text and poll translation stay popup-based.

### 5. Translate Popup Switch

The translate popup must include a source switch button.

Expected behavior:

- Button text shows the active source.
- Clicking it switches source in order: Google -> Telegram API -> LLM -> Google.
- The popup immediately retries the translation with the current target language.
- Button and provider logic must read/write the same `translation_provider` setting as Enhanced Settings.

### 6. Provider Semantics

Google:

- Uses existing Google App translator.
- Respects `translate_to_tc` as `zh-Hant`.
- Must handle batch requests one result per input.

Telegram API:

- Uses `messages.translateText`.
- Supports message IDs when available.
- Respects `translate_to_tc` as `zh-TW`.

LLM:

- Uses text content, not message IDs.
- For message-ID requests, resolve to original text before sending to LLM or report unsupported through the generic fallback.
- Preserve line breaks and formatting where practical.
- Prompt must instruct the model to output only translated text.
- Must use Responses API, not Chat Completions.

## Validation Checklist

- `td_lang_lang` target runs after adding language strings.
- Single-file compile at least:
  - `lang/translate_provider.cpp`
  - `lang/translate_mtproto_provider.cpp`
  - LLM provider file
  - `boxes/translate_box.cpp`
  - `boxes/translate_box_content.cpp`
  - `boxes/enhanced_options_box.cpp`
  - `settings/settings_enhanced.cpp`
  - `history/view/history_view_context_menu.cpp`
- `git diff --check` passes.
- Manual UI check:
  - Enhanced selector changes source.
  - Message context submenu reflects source.
  - Translate popup button reflects source.
  - Switching source in popup immediately re-translates.

## Do Not Forget

The user's main request is LLM translation. Google and Telegram API alone are not enough.

The user's explicit UI requirement is not only a settings page. The source switch must exist in both:

- message context menu
- translate popup
