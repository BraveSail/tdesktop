/*
This file is part of Telegram Desktop,
the official desktop application.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/qt/qt_compare.h"

#include <QtCore/QStringList>

#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace InlineBotRules {

enum class Source {
	Remote,
	Local,
};

struct RuleItem {
	Source source = Source::Local;
	int localIndex = -1;
	QString username;
	QStringList rules;
	bool enabled = true;
};

[[nodiscard]] std::vector<RuleItem> RemoteRules();
[[nodiscard]] std::vector<RuleItem> LocalRules();
[[nodiscard]] std::vector<RuleItem> AllRules();
[[nodiscard]] QString Match(const QString &text);
[[nodiscard]] bool ValidateUsername(const QString &username);
[[nodiscard]] bool ValidateRegex(const QString &regex);
[[nodiscard]] QString NormalizeUsername(QString username);
[[nodiscard]] QString RulesSummary(const QStringList &rules);

void SetRemoteEnabled(const QString &username, bool enabled);
void SetLocalEnabled(int index, bool enabled);
bool AddLocal(const QString &username, const QStringList &rules);
bool UpdateLocal(int index, const QString &username, const QStringList &rules);
void RemoveLocal(int index);
void RefreshRemote(
	not_null<Main::Session*> session,
	Fn<void()> done = nullptr,
	bool force = false);

} // namespace InlineBotRules
