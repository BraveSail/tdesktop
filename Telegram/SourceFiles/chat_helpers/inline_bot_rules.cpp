/*
This file is part of Telegram Desktop,
the official desktop application.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/inline_bot_rules.h"

#include "apiwrap.h"
#include "base/flat_map.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace {

constexpr auto kRemoteMetadataName = "nagram_remote_metadata";
constexpr auto kRemoteTag = "#inlinebot";
constexpr auto kRemoteTtl = 15 * 60 * 1000LL;
constexpr auto kPatternCacheCapacity = 200;

[[nodiscard]] Core::Settings &AppSettings() {
	return Core::App().settings();
}

[[nodiscard]] QJsonArray RulesToJson(
		const std::vector<InlineBotRules::RuleItem> &rules) {
	auto result = QJsonArray();
	for (const auto &rule : rules) {
		auto object = QJsonObject();
		object.insert("username", rule.username);
		auto patterns = QJsonArray();
		for (const auto &pattern : rule.rules) {
			patterns.append(pattern);
		}
		object.insert("rules", patterns);
		if (rule.source == InlineBotRules::Source::Local) {
			object.insert("enabled", rule.enabled);
		}
		result.append(object);
	}
	return result;
}

[[nodiscard]] QString SerializeRules(
		const std::vector<InlineBotRules::RuleItem> &rules) {
	return QString::fromUtf8(QJsonDocument(RulesToJson(rules)).toJson(
		QJsonDocument::Compact));
}

[[nodiscard]] QJsonArray ReadRulesArray(const QString &raw) {
	const auto document = QJsonDocument::fromJson(raw.toUtf8());
	if (document.isArray()) {
		return document.array();
	} else if (document.isObject()) {
		return document.object().value("data").toArray();
	}
	return {};
}

[[nodiscard]] QStringList DisabledRemoteUsernames() {
	return AppSettings().readPref<QString>(
		Core::kEnhancedAutoInlineBotDisabledRemoteKey
	).split('\n', Qt::SkipEmptyParts);
}

[[nodiscard]] bool RemoteEnabled(const QString &username) {
	for (const auto &entry : DisabledRemoteUsernames()) {
		if (entry.trimmed().compare(username, Qt::CaseInsensitive) == 0) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] std::vector<InlineBotRules::RuleItem> ParseRules(
		const QString &raw,
		InlineBotRules::Source source) {
	auto result = std::vector<InlineBotRules::RuleItem>();
	auto index = 0;
	for (const auto value : ReadRulesArray(raw)) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		const auto username = InlineBotRules::NormalizeUsername(
			object.value("username").toString());
		const auto jsonRules = object.value("rules").toArray();
		auto patterns = QStringList();
		for (const auto pattern : jsonRules) {
			const auto text = pattern.toString().trimmed();
			if (!text.isEmpty()) {
				patterns.push_back(text);
			}
		}
		if (username.isEmpty() || patterns.empty()) {
			continue;
		}
		result.push_back({
			.source = source,
			.localIndex = (source == InlineBotRules::Source::Local)
				? index
				: -1,
			.username = username,
			.rules = patterns,
			.enabled = (source == InlineBotRules::Source::Remote)
				? RemoteEnabled(username)
				: object.value("enabled").toBool(true),
		});
		++index;
	}
	return result;
}

struct PatternCache {
	base::flat_map<QString, QRegularExpression> patterns;
	QStringList order;
};

[[nodiscard]] PatternCache &Cache() {
	static auto result = PatternCache();
	return result;
}

[[nodiscard]] const QRegularExpression *PatternFor(const QString &pattern) {
	auto &cache = Cache();
	const auto i = cache.patterns.find(pattern);
	if (i != end(cache.patterns)) {
		cache.order.removeAll(pattern);
		cache.order.push_back(pattern);
		return &i->second;
	}
	auto expression = QRegularExpression(pattern);
	if (!expression.isValid()) {
		return nullptr;
	}
	if (cache.order.size() >= kPatternCacheCapacity) {
		const auto oldest = cache.order.takeFirst();
		cache.patterns.remove(oldest);
	}
	cache.order.push_back(pattern);
	const auto [inserted, ok] = cache.patterns.emplace(pattern, expression);
	return ok ? &inserted->second : nullptr;
}

void SaveLocal(std::vector<InlineBotRules::RuleItem> rules) {
	AppSettings().writePref<QString>(
		Core::kEnhancedAutoInlineBotLocalRulesKey,
		SerializeRules(rules));
	Core::App().saveSettingsDelayed();
}

[[nodiscard]] bool RemoteNeedsUpdate() {
	const auto updated = AppSettings().readPref<QString>(
		Core::kEnhancedAutoInlineBotRemoteUpdatedKey).toLongLong();
	return updated <= 0
		|| (QDateTime::currentMSecsSinceEpoch() - updated) >= kRemoteTtl;
}

struct RemoteParseResult {
	bool found = false;
	std::vector<InlineBotRules::RuleItem> rules;
};

[[nodiscard]] RemoteParseResult ParseRemoteMessages(
		const MTPmessages_Messages &result) {
	auto parsed = RemoteParseResult();
	const auto tag = QString::fromLatin1(kRemoteTag);
	const auto collect = [&](const auto &data) {
		for (const auto &message : data.vmessages().v) {
			if (message.type() != mtpc_message) {
				continue;
			}
			const auto text = qs(message.c_message().vmessage());
			if (!text.startsWith(tag)) {
				continue;
			}
			const auto json = text.mid(tag.size()).trimmed();
			auto error = QJsonParseError();
			const auto document = QJsonDocument::fromJson(
				json.toUtf8(),
				&error);
			if (error.error != QJsonParseError::NoError
				|| (!document.isObject() && !document.isArray())) {
				continue;
			}
			parsed.found = true;
			const auto rules = ParseRules(json, InlineBotRules::Source::Remote);
			parsed.rules.insert(end(parsed.rules), begin(rules), end(rules));
		}
	};
	result.match([&](const MTPDmessages_messages &data) {
		collect(data);
	}, [&](const MTPDmessages_messagesSlice &data) {
		collect(data);
	}, [&](const MTPDmessages_channelMessages &data) {
		collect(data);
	}, [&](const MTPDmessages_messagesNotModified &data) {
	});
	return parsed;
}

void Finish(Fn<void()> done) {
	if (done) {
		done();
	}
}

void SearchRemote(
		not_null<Main::Session*> session,
		const MTPInputPeer &peer,
		Fn<void()> done) {
	using Flag = MTPmessages_Search::Flag;
	const auto tag = QString::fromLatin1(kRemoteTag);
	session->api().request(MTPmessages_Search(
		MTP_flags(Flag()),
		peer,
		MTP_string(tag),
		MTP_inputPeerEmpty(),
		MTP_inputPeerEmpty(),
		MTPVector<MTPReaction>(),
		MTP_int(0),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(10),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto parsed = ParseRemoteMessages(result);
		if (parsed.found) {
			AppSettings().writePref<QString>(
				Core::kEnhancedAutoInlineBotRemoteRulesKey,
				SerializeRules(parsed.rules));
			AppSettings().writePref<QString>(
				Core::kEnhancedAutoInlineBotRemoteUpdatedKey,
				QString::number(QDateTime::currentMSecsSinceEpoch()));
			Core::App().saveSettingsDelayed();
		} else {
			AppSettings().clearPref(Core::kEnhancedAutoInlineBotRemoteRulesKey);
			AppSettings().clearPref(Core::kEnhancedAutoInlineBotRemoteUpdatedKey);
		}
		Finish(done);
	}).fail([=] {
		Finish(done);
	}).send();
}

} // namespace

namespace InlineBotRules {

std::vector<RuleItem> RemoteRules() {
	return ParseRules(
		AppSettings().readPref<QString>(
			Core::kEnhancedAutoInlineBotRemoteRulesKey),
		Source::Remote);
}

std::vector<RuleItem> LocalRules() {
	return ParseRules(
		AppSettings().readPref<QString>(
			Core::kEnhancedAutoInlineBotLocalRulesKey),
		Source::Local);
}

std::vector<RuleItem> AllRules() {
	auto result = RemoteRules();
	const auto local = LocalRules();
	result.insert(end(result), begin(local), end(local));
	return result;
}

QString Match(const QString &text) {
	if (!AppSettings().readPref<bool>(Core::kEnhancedAutoInlineBotKey, true)) {
		return QString();
	}
	if (text.isEmpty()) {
		return QString();
	}
	for (const auto &rule : AllRules()) {
		if (!rule.enabled) {
			continue;
		}
		for (const auto &pattern : rule.rules) {
			const auto expression = PatternFor(pattern);
			if (expression && expression->match(text).hasMatch()) {
				return rule.username;
			}
		}
	}
	return QString();
}

bool ValidateUsername(const QString &username) {
	const auto normalized = NormalizeUsername(username);
	if (normalized.size() < 3) {
		return false;
	}
	for (const auto ch : normalized) {
		if (!ch.isLetterOrNumber() && ch != '_') {
			return false;
		}
	}
	return true;
}

bool ValidateRegex(const QString &regex) {
	return !regex.trimmed().isEmpty()
		&& QRegularExpression(regex).isValid();
}

QString NormalizeUsername(QString username) {
	username = username.trimmed();
	if (username.startsWith('@')) {
		username = username.mid(1);
	}
	return username;
}

QString RulesSummary(const QStringList &rules) {
	auto result = rules.join(u" | "_q);
	if (result.size() > 80) {
		result = result.left(77) + u"..."_q;
	}
	return result;
}

void SetRemoteEnabled(const QString &username, bool enabled) {
	const auto normalized = NormalizeUsername(username);
	auto disabled = QStringList();
	for (const auto &entry : DisabledRemoteUsernames()) {
		if (entry.trimmed().compare(normalized, Qt::CaseInsensitive) != 0) {
			disabled.push_back(entry);
		}
	}
	if (!enabled) {
		disabled.push_back(normalized);
	}
	AppSettings().writePref<QString>(
		Core::kEnhancedAutoInlineBotDisabledRemoteKey,
		disabled.join('\n'));
	Core::App().saveSettingsDelayed();
}

void SetLocalEnabled(int index, bool enabled) {
	auto rules = LocalRules();
	if (index < 0 || index >= int(rules.size())) {
		return;
	}
	rules[index].enabled = enabled;
	SaveLocal(std::move(rules));
}

bool AddLocal(const QString &username, const QStringList &rules) {
	const auto normalized = NormalizeUsername(username);
	if (!ValidateUsername(normalized) || rules.empty()) {
		return false;
	}
	for (const auto &rule : rules) {
		if (!ValidateRegex(rule)) {
			return false;
		}
	}
	auto local = LocalRules();
	local.insert(begin(local), RuleItem{
		.source = Source::Local,
		.localIndex = 0,
		.username = normalized,
		.rules = rules,
		.enabled = true,
	});
	SaveLocal(std::move(local));
	return true;
}

bool UpdateLocal(int index, const QString &username, const QStringList &rules) {
	const auto normalized = NormalizeUsername(username);
	auto local = LocalRules();
	if (index < 0
		|| index >= int(local.size())
		|| !ValidateUsername(normalized)
		|| rules.empty()) {
		return false;
	}
	for (const auto &rule : rules) {
		if (!ValidateRegex(rule)) {
			return false;
		}
	}
	local[index].username = normalized;
	local[index].rules = rules;
	SaveLocal(std::move(local));
	return true;
}

void RemoveLocal(int index) {
	auto local = LocalRules();
	if (index < 0 || index >= int(local.size())) {
		return;
	}
	local.erase(begin(local) + index);
	SaveLocal(std::move(local));
}

void RefreshRemote(
		not_null<Main::Session*> session,
		Fn<void()> done,
		bool force) {
	static auto loading = false;
	if (!force && !RemoteNeedsUpdate()) {
		Finish(done);
		return;
	}
	if (loading) {
		Finish(done);
		return;
	}
	loading = true;
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(QString::fromLatin1(kRemoteMetadataName)),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peer = session->data().peerLoaded(peerFromMTP(data.vpeer()));
		if (!peer) {
			loading = false;
			Finish(done);
			return;
		}
		SearchRemote(session, peer->input(), [=] {
			loading = false;
			Finish(done);
		});
	}).fail([=] {
		loading = false;
		Finish(done);
	}).send();
}

} // namespace InlineBotRules
