/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/translate_provider.h"

#include "base/options.h"
#include "core/enhanced_settings.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/translate_mtproto_provider.h"
#include "lang/translate_url_provider.h"
#include "lang/lang_keys.h"
#include "platform/platform_translate_provider.h"
#include "settings.h"

namespace {

base::options::option<QString> OptionTranslateUrlTemplate({
	.id = "translate-url-template",
	.name = "Translate URL template",
	.description = "Template URL for custom translation provider."
		" Supports %q text, %f source language and %t target language.",
});

} // namespace

namespace Ui {

TranslateSource CurrentTranslateSource() {
	const auto value = GetEnhancedInt("translation_provider");
	switch (value) {
	case int(TranslateSource::Telegram):
		return TranslateSource::Telegram;
	case int(TranslateSource::LLM):
		return TranslateSource::LLM;
	default:
		return TranslateSource::Google;
	}
}

QString TranslateSourceLabel(TranslateSource source) {
	switch (source) {
	case TranslateSource::LLM:
		return tr::lng_translation_source_llm(tr::now);
	case TranslateSource::Telegram:
		return tr::lng_translation_source_telegram(tr::now);
	case TranslateSource::Google:
		return tr::lng_translation_source_google(tr::now);
	}
	Unexpected("Source in TranslateSourceLabel.");
}

void SetTranslateSource(TranslateSource source) {
	SetEnhancedValue("translation_provider", int(source));
	EnhancedSettings::Write();
}

void SwitchTranslateSource() {
	switch (CurrentTranslateSource()) {
	case TranslateSource::Google:
		SetTranslateSource(TranslateSource::Telegram);
		return;
	case TranslateSource::Telegram:
		SetTranslateSource(TranslateSource::LLM);
		return;
	case TranslateSource::LLM:
		SetTranslateSource(TranslateSource::Google);
		return;
	}
	Unexpected("Source in SwitchTranslateSource.");
}

std::unique_ptr<TranslateProvider> CreateTranslateProvider(
		not_null<Main::Session*> session) {
	const auto urlTemplate = OptionTranslateUrlTemplate.value();
	if (!urlTemplate.isEmpty()
		&& urlTemplate.contains(u"%q"_q)) {
		return CreateUrlTranslateProvider(urlTemplate);
	}
	if (Core::App().settings().usePlatformTranslation()
		&& Platform::IsTranslateProviderAvailable()) {
		return Platform::CreateTranslateProvider();
	}
	return CreateMTProtoTranslateProvider(session);
}

TranslateProviderRequest PrepareTranslateProviderRequest(
		not_null<TranslateProvider*> provider,
		not_null<PeerData*> peer,
		MsgId msgId,
		TextWithEntities text) {
	auto result = TranslateProviderRequest{
		.peerId = uint64(peer->id.value),
		.msgId = IsServerMsgId(msgId) ? msgId.bare : 0,
		.text = std::move(text),
	};
	if (provider->supportsMessageId()) {
		return result;
	}
	if (result.msgId) {
		if (const auto i = peer->owner().message(peer, MsgId(result.msgId))) {
			result.text = i->originalText();
		}
		result.msgId = 0;
	}
	return result;
}

} // namespace Ui
