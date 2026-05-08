/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#include <base/timer_rpl.h>
#include <ui/toast/toast.h>
#include <mainwindow.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextEdit>
#include "settings/settings_enhanced.h"

#include "chat_helpers/inline_bot_rules.h"
#include "settings/settings_common.h"
#include <ui/vertical_list.h>
#include "ui/layers/generic_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "boxes/connection_box.h"
#include "boxes/enhanced_options_box.h"
#include "boxes/about_box.h"
#include "ui/boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "core/update_checker.h"
#include "core/enhanced_settings.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "layout/layout_item_base.h"
#include "facades.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "apiwrap.h"
#include "api/api_blocked_peers.h"

#include <optional>
#include <string_view>

namespace Settings {

namespace {

[[nodiscard]] Core::Settings &SettingsPrefs() {
	return Core::App().settings();
}

void SavePref(const std::string_view key, bool value) {
	SettingsPrefs().writePref<bool>(key, value);
	Core::App().saveSettingsDelayed();
}

[[nodiscard]] QStringList RegexLines(const QString &text) {
	auto result = QStringList();
	for (const auto &line : text.split('\n')) {
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			result.push_back(trimmed);
		}
	}
	return result;
}

[[nodiscard]] bool ValidRuleForm(
		const QString &username,
		const QStringList &rules) {
	if (!InlineBotRules::ValidateUsername(username) || rules.empty()) {
		return false;
	}
	for (const auto &rule : rules) {
		if (!InlineBotRules::ValidateRegex(rule)) {
			return false;
		}
	}
	return true;
}

void InlineBotRuleDetailsBox(
		not_null<Ui::GenericBox*> box,
		InlineBotRules::RuleItem item,
		Fn<void()> refresh) {
	box->setTitle(rpl::single(item.username));
	box->setWidth(st::boxWideWidth);

	const auto layout = box->verticalLayout();
	AddDividerText(
		layout,
		(item.source == InlineBotRules::Source::Remote)
			? tr::lng_settings_inline_bot_remote_rule_about()
			: tr::lng_settings_inline_bot_local_rule_about());

	const auto enabled = AddButtonWithIcon(
		layout,
		tr::lng_settings_inline_bot_rule_enabled(),
		st::settingsButtonNoIcon);
	enabled->toggleOn(rpl::single(item.enabled))->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != item.enabled;
	}) | rpl::on_next([=](bool toggled) {
		if (item.source == InlineBotRules::Source::Remote) {
			InlineBotRules::SetRemoteEnabled(item.username, toggled);
		} else {
			InlineBotRules::SetLocalEnabled(item.localIndex, toggled);
		}
		box->closeBox();
		if (refresh) {
			refresh();
		}
	}, enabled->lifetime());

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::lng_settings_inline_bot_rule_regexes(),
		TextWithTags{ item.rules.join('\n') }),
		st::boxRowPadding);
	field->rawTextEdit()->setReadOnly(true);

	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
		if (refresh) {
			refresh();
		}
	});
}

void EditInlineBotRuleBox(
		not_null<Ui::GenericBox*> box,
		std::optional<InlineBotRules::RuleItem> item,
		Fn<void()> refresh) {
	const auto editing = item.has_value();
	box->setTitle(editing
		? tr::lng_settings_inline_bot_rule_edit()
		: tr::lng_settings_inline_bot_rule_add());
	box->setWidth(st::boxWideWidth);

	const auto username = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_settings_inline_bot_rule_username(),
		item ? item->username : QString()),
		st::boxRowPadding);

	const auto rules = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::lng_settings_inline_bot_rule_regexes(),
		TextWithTags{ item ? item->rules.join('\n') : QString() }),
		st::boxRowPadding);
	Ui::AddDividerText(
		box->verticalLayout(),
		tr::lng_settings_inline_bot_rule_regexes_about());

	if (editing) {
		const auto enabled = AddButtonWithIcon(
			box->verticalLayout(),
			tr::lng_settings_inline_bot_rule_enabled(),
			st::settingsButtonNoIcon);
		enabled->toggleOn(rpl::single(item->enabled))->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return toggled != item->enabled;
		}) | rpl::on_next([=](bool toggled) {
			InlineBotRules::SetLocalEnabled(item->localIndex, toggled);
		}, enabled->lifetime());
	}

	box->setFocusCallback([=] {
		username->setFocusFast();
	});

	box->addButton(tr::lng_settings_save(), [=] {
		const auto bot = username->getLastText();
		const auto lines = RegexLines(rules->getLastText());
		if (!ValidRuleForm(bot, lines)) {
			username->showError();
			rules->showError();
			return;
		}
		const auto ok = editing
			? InlineBotRules::UpdateLocal(item->localIndex, bot, lines)
			: InlineBotRules::AddLocal(bot, lines);
		if (ok) {
			box->closeBox();
			if (refresh) {
				refresh();
			}
		}
	});
	if (editing) {
		box->addButton(tr::lng_settings_inline_bot_rule_delete(), [=] {
			InlineBotRules::RemoveLocal(item->localIndex);
			box->closeBox();
			if (refresh) {
				refresh();
			}
		});
	}
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
		if (refresh) {
			refresh();
		}
	});
}

void ShowInlineBotRulesBox();

void InlineBotRulesBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_settings_inline_bot_rules());
	box->setWidth(st::boxWideWidth);

	const auto layout = box->verticalLayout();
	const auto remote = InlineBotRules::RemoteRules();
	const auto local = InlineBotRules::LocalRules();
	const auto refresh = [weak = base::make_weak(box)] {
		if (const auto strong = weak.get()) {
			strong->closeBox();
		}
		ShowInlineBotRulesBox();
	};

	if (!remote.empty()) {
		AddSubsectionTitle(layout, tr::lng_settings_inline_bot_remote_rules());
		for (const auto &rule : remote) {
			const auto button = AddButtonWithLabel(
				layout,
				rpl::single(rule.username),
				rpl::single((rule.enabled
					? tr::lng_settings_inline_bot_rule_on(tr::now)
					: tr::lng_settings_inline_bot_rule_off(tr::now))
					+ u" - "_q
					+ InlineBotRules::RulesSummary(rule.rules)),
				st::settingsButtonNoIcon);
			button->addClickHandler([=] {
				Ui::show(Box(InlineBotRuleDetailsBox, rule, refresh));
			});
		}
		Ui::AddSkip(layout);
	}

	AddSubsectionTitle(layout, tr::lng_settings_inline_bot_local_rules());
	const auto add = AddButtonWithIcon(
		layout,
		tr::lng_settings_inline_bot_rule_add_short(),
		st::settingsButtonNoIcon);
	add->addClickHandler([=] {
		Ui::show(Box(
			EditInlineBotRuleBox,
			std::optional<InlineBotRules::RuleItem>(),
			refresh));
	});
	for (const auto &rule : local) {
		const auto button = AddButtonWithLabel(
			layout,
			rpl::single(rule.username),
			rpl::single((rule.enabled
				? tr::lng_settings_inline_bot_rule_on(tr::now)
				: tr::lng_settings_inline_bot_rule_off(tr::now))
				+ u" - "_q
				+ InlineBotRules::RulesSummary(rule.rules)),
			st::settingsButtonNoIcon);
		button->addClickHandler([=] {
			Ui::show(Box(EditInlineBotRuleBox, std::optional(rule), refresh));
		});
	}

	box->addButton(tr::lng_settings_inline_bot_rules_refresh(), [=] {
		if (const auto window = App::wnd()) {
			if (const auto controller = window->sessionController()) {
				const auto weak = base::make_weak(box);
				InlineBotRules::RefreshRemote(
					&controller->session(),
					[=] {
						if (const auto strong = weak.get()) {
							strong->closeBox();
						}
						Ui::show(Box(InlineBotRulesBox));
					},
					true);
				return;
			}
		}
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void ShowInlineBotRulesBox() {
	const auto show = [] {
		Ui::show(Box(InlineBotRulesBox));
	};
	if (const auto window = App::wnd()) {
		if (const auto controller = window->sessionController()) {
			InlineBotRules::RefreshRemote(&controller->session(), show);
			return;
		}
	}
	show();
}

void AddPrefToggle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const std::string_view key,
		bool fallback) {
	const auto button = AddButtonWithIcon(
		container,
		std::move(text),
		st::settingsButtonNoIcon);
	button->toggleOn(
		rpl::single(SettingsPrefs().readPref<bool>(key, fallback))
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != SettingsPrefs().readPref<bool>(key, fallback);
	}) | rpl::on_next([=](bool toggled) {
		SavePref(key, toggled);
	}, button->lifetime());
}

} // namespace

	void Enhanced::SetupEnhancedNetwork(not_null<Ui::VerticalLayout *> container) {
		const auto wrap = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
						container,
						object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();

		AddDividerText(inner, tr::lng_settings_restart_hint());
		AddSkip(inner);
		AddSubsectionTitle(inner, tr::lng_settings_network());

		auto uploadBoostBtn = AddButtonWithLabel(
				inner,
				tr::lng_settings_net_upload_speed_boost(),
				rpl::single(NetBoostBox::BoostLabel(GetEnhancedInt("net_speed_boost"))),
				st::settingsButtonNoIcon
		);
		uploadBoostBtn->setColorOverride(QColor(255, 0, 0));
		uploadBoostBtn->addClickHandler([=] {
			Ui::show(Box<NetBoostBox>());
		});

		AddSkip(container);
	}

	void Enhanced::writeBlocklistFile() {
		QFile file(cWorkingDir() + qsl("tdata/blocklist.json"));
		if (file.open(QIODevice::WriteOnly)) {
			auto toArray = [&] {
				QJsonArray array;
				for (auto id : blockList) {
					array.append(id);
				}
				return array;
			};
			auto doc = QJsonDocument(toArray());
			file.write(doc.toJson(QJsonDocument::Compact));
			file.close();
			Ui::Toast::Show("Restart in 3 seconds!");
			QTimer::singleShot(3 * 1000, []{ Core::Restart(); });
		} else {
			Ui::Toast::Show("Failed to save blocklist.");
		}
	}

	void Enhanced::reqBlocked(int offset) {
		if (_requestId) {
			return;
		}
		_requestId = App::wnd()->sessionController()->session().api().request(MTPcontacts_GetBlocked(
				MTP_flags(0),
				MTP_int(offset),
				MTP_int(100)
		)).done([=](const MTPcontacts_Blocked &result) {
			_requestId = 0;
			result.match([&](const MTPDcontacts_blockedSlice& data) { // Incomplete list of blocked users response.
				blockCount = data.vcount().v;
				for (const auto& user : data.vusers().v) {
					blockList.append(int64(UserId(user.c_user().vid().v).bare));
				}
				if (blockCount > blockList.length()) {
					reqBlocked(offset+100);
				} else {
					writeBlocklistFile();
				}
			}, [&](const MTPDcontacts_blocked& data) { // 	Full list of blocked users response.
				for (const auto& user : data.vusers().v) {
					blockList.append(int64(UserId(user.c_user().vid().v).bare));
				}
				writeBlocklistFile();
			});
		}).fail([=] {
			_requestId = 0;
		}).send();
	}

	void Enhanced::SetupEnhancedMessages(not_null<Ui::VerticalLayout *> container) {
		AddDivider(container);
		AddSkip(container);
		AddSubsectionTitle(container, tr::lng_settings_messages());

		const auto wrap = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
						container,
						object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();

		AddPrefToggle(
			inner,
			tr::lng_settings_disable_input_status(),
			Core::kEnhancedDisableChatActionKey,
			false);
		AddPrefToggle(
			inner,
			tr::lng_settings_auto_inline_bot(),
			Core::kEnhancedAutoInlineBotKey,
			true);
		AddPrefToggle(
			inner,
			tr::lng_settings_auto_inline_bot_direct_send(),
			Core::kEnhancedAutoInlineBotDirectSendKey,
			false);
		AddButtonWithIcon(
			inner,
			tr::lng_settings_inline_bot_rules(),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			ShowInlineBotRulesBox();
		});
		AddPrefToggle(
			inner,
			tr::lng_settings_force_copy(),
			Core::kEnhancedForceCopyKey,
			false);
		AddSkip(inner);

		auto MsgIdBtn = AddButtonWithIcon(
				inner,
				tr::lng_settings_show_message_id(),
				st::settingsButtonNoIcon
		);
		MsgIdBtn->setColorOverride(QColor(255, 0, 0));
		MsgIdBtn->toggleOn(
				rpl::single(GetEnhancedBool("show_messages_id"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_messages_id"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_messages_id", toggled);
			EnhancedSettings::Write();
			Core::Restart();
		}, container->lifetime());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_show_repeater_option(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("show_repeater_option"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_repeater_option"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_repeater_option", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		if (GetEnhancedBool("show_repeater_option")) {
			AddButtonWithIcon(
					inner,
					tr::lng_settings_repeater_reply_to_orig_msg(),
					st::settingsButtonNoIcon
			)->toggleOn(
					rpl::single(GetEnhancedBool("repeater_reply_to_orig_msg"))
			)->toggledChanges(
			) | rpl::filter([=](bool toggled) {
				return (toggled != GetEnhancedBool("repeater_reply_to_orig_msg"));
			}) | rpl::on_next([=](bool toggled) {
				SetEnhancedValue("repeater_reply_to_orig_msg", toggled);
				EnhancedSettings::Write();
			}, container->lifetime());
		}

		auto value = rpl::single(
				AlwaysDeleteBox::DeleteLabel(GetEnhancedInt("always_delete_for"))
		) | rpl::then(
				_AlwaysDeleteChanged.events()
		) | rpl::map([] {
			return AlwaysDeleteBox::DeleteLabel(GetEnhancedInt("always_delete_for"));
		});

		auto btn = AddButtonWithLabel(
				container,
				tr::lng_settings_always_delete_for(),
				std::move(value),
				st::settingsButtonNoIcon
		);
		btn->events(
		) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto event = e->type();
			if (event == QEvent::UpdateLater) _AlwaysDeleteChanged.fire({});
		}, container->lifetime());
		btn->addClickHandler([=] {
			Ui::show(Box<AlwaysDeleteBox>());
		});

		AddButtonWithIcon(
				inner,
				tr::lng_settings_disable_cloud_draft_sync(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("disable_cloud_draft_sync"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("disable_cloud_draft_sync"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("disable_cloud_draft_sync", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddSkip(container);

		AddButtonWithIcon(
				inner,
				tr::lng_settings_hide_classic_forward(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("hide_classic_fwd"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("hide_classic_fwd"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("hide_classic_fwd", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_disable_link_warning(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("disable_link_warning"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("disable_link_warning"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("disable_link_warning", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_disable_premium_animation(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("disable_premium_animation"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("disable_premium_animation"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("disable_premium_animation", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_disable_global_search(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("disable_global_search"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("disable_global_search"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("disable_global_search", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_show_group_sender_avatar(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("show_group_sender_avatar"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_group_sender_avatar"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_group_sender_avatar", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		QString langPackBaseId = Lang::GetInstance().baseId();
		if (langPackBaseId == "zh-hant-raw" || langPackBaseId == "zh-hans-raw") {
			AddButtonWithIcon(
					inner,
					tr::lng_settings_translate_to_tc(),
					st::settingsButtonNoIcon
			)->toggleOn(
					rpl::single(GetEnhancedBool("translate_to_tc"))
			)->toggledChanges(
			) | rpl::filter([=](bool toggled) {
				return (toggled != GetEnhancedBool("translate_to_tc"));
			}) | rpl::on_next([=](bool toggled) {
				SetEnhancedValue("translate_to_tc", toggled);
				EnhancedSettings::Write();
			}, container->lifetime());
		}

		auto secondsBtn = AddButtonWithIcon(
			inner,
			tr::lng_settings_show_seconds(),
			st::settingsButtonNoIcon
		);
		secondsBtn->setColorOverride(QColor(255, 0, 0));
		secondsBtn->toggleOn(
			rpl::single(GetEnhancedBool("show_seconds"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_seconds"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_seconds", toggled);
			EnhancedSettings::Write();
			QTimer::singleShot(1 * 1000, []{ Core::Restart(); });
		}, container->lifetime());

		auto jsonBtn = AddButtonWithIcon(
			inner,
			tr::lng_settings_show_view_as_json(),
			st::settingsButtonNoIcon
		);
		jsonBtn->toggleOn(
			rpl::single(GetEnhancedBool("show_json"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_json"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_json", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		auto hideBtn = AddButtonWithIcon(
			inner,
			tr::lng_settings_hide_messages(),
			st::settingsButtonNoIcon
		);
		hideBtn->setColorOverride(QColor(255, 0, 0));
		hideBtn->toggleOn(
				rpl::single(GetEnhancedBool("blocked_user_spoiler_mode"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("blocked_user_spoiler_mode"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("blocked_user_spoiler_mode", toggled);
			EnhancedSettings::Write();
			if (toggled) {
				Ui::Toast::Show("Please wait a moment, fetching blocklist...");

				App::wnd()->sessionController()->session().api().blockedPeers().slice() | rpl::take(
					1
				) | rpl::on_next([&](const Api::BlockedPeers::Slice &result) {
					if (blockList.length() == result.total) {
						return;
					}
					blockList = QList<int64>();
					reqBlocked(0);
				}, container->lifetime());
			}
		}, container->lifetime());

		AddDividerText(inner, tr::lng_settings_hide_messages_desc());
	}

	void Enhanced::SetupEnhancedButton(not_null<Ui::VerticalLayout *> container) {
		AddDivider(container);
		AddSkip(container);
		AddSubsectionTitle(container, tr::lng_settings_button());

		const auto wrap = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
						container,
						object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();

		auto EmojiBtn = AddButtonWithIcon(
				inner,
				tr::lng_settings_show_emoji_button_as_text(),
				st::settingsButtonNoIcon
		);
		EmojiBtn->setColorOverride(QColor(255, 0, 0));
		EmojiBtn->toggleOn(
				rpl::single(GetEnhancedBool("show_emoji_button_as_text"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_emoji_button_as_text"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_emoji_button_as_text", toggled);
			EnhancedSettings::Write();
			Core::Restart();
		}, container->lifetime());

		AddDividerText(inner, tr::lng_show_emoji_button_as_text_desc());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_show_scheduled_button(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("show_scheduled_button"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("show_scheduled_button"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("show_scheduled_button", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddSkip(container);
	}

	void Enhanced::SetupEnhancedVoiceChat(not_null<Ui::VerticalLayout *> container) {
		AddDivider(container);
		AddSkip(container);
		AddSubsectionTitle(container, tr::lng_settings_voice_chat());

		const auto wrap = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
						container,
						object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();

		AddButtonWithIcon(
				inner,
				tr::lng_settings_radio_controller(),
				st::settingsButtonNoIcon
		)->addClickHandler([=] {
			Ui::show(Box<RadioController>());
		});

		AddDividerText(inner, tr::lng_radio_controller_desc());

		AddButtonWithIcon(
				inner,
				tr::lng_settings_auto_unmute(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("auto_unmute"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("auto_unmute"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("auto_unmute", toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddDividerText(inner, tr::lng_auto_unmute_desc());

		auto value = rpl::single(
				BitrateController::BitrateLabel(GetEnhancedInt("bitrate"))
		) | rpl::then(
				_BitrateChanged.events()
		) | rpl::map([=] {
			return BitrateController::BitrateLabel(GetEnhancedInt("bitrate"));
		});

		auto btn = AddButtonWithLabel(
				container,
				tr::lng_bitrate_controller(),
				std::move(value),
				st::settingsButtonNoIcon
		);
		btn->events(
		) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto event = e->type();
			if (event == QEvent::UpdateLater) _BitrateChanged.fire({});
		}, container->lifetime());
		btn->addClickHandler([=] {
			Ui::show(Box<BitrateController>());
		});

		AddButtonWithIcon(
				inner,
				tr::lng_settings_enable_hd_video(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("hd_video"))
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != GetEnhancedBool("hd_video"));
		}) | rpl::on_next([=](bool toggled) {
			SetEnhancedValue("hd_video", toggled);
			Ui::Toast::Show(tr::lng_hd_video_hint(tr::now));
			EnhancedSettings::Write();
		}, container->lifetime());

		AddSkip(container);
	}

	void Enhanced::SetupEnhancedOthers(not_null<Window::SessionController*> controller, not_null<Ui::VerticalLayout *> container) {
		AddDivider(container);
		AddSkip(container);
		AddSubsectionTitle(container, tr::lng_settings_other());

		auto hideBtn = AddButtonWithIcon(
			container,
			tr::lng_settings_hide_all_chats(),
			st::settingsButtonNoIcon
		);
		hideBtn->setColorOverride(QColor(255, 0, 0));
		hideBtn->toggleOn(
				rpl::single(GetEnhancedBool("hide_all_chats"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != GetEnhancedBool("hide_all_chats"));
		}) | rpl::on_next([=](bool enabled) {
			SetEnhancedValue("hide_all_chats", enabled);
			EnhancedSettings::Write();
			Core::Restart();
		}, container->lifetime());

		AddButtonWithIcon(
				container,
				tr::lng_settings_replace_edit_button(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("replace_edit_button"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != GetEnhancedBool("replace_edit_button"));
		}) | rpl::on_next([=](bool enabled) {
			SetEnhancedValue("replace_edit_button", enabled);
			EnhancedSettings::Write();
			controller->reloadFiltersMenu();
		}, container->lifetime());

		AddButtonWithIcon(
				container,
				tr::lng_settings_skip_message(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("skip_to_next"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != GetEnhancedBool("skip_to_next"));
		}) | rpl::on_next([=](bool enabled) {
			SetEnhancedValue("skip_to_next", enabled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddDividerText(container, tr::lng_settings_skip_message_desc());

		AddButtonWithIcon(
				container,
				tr::lng_settings_hide_counter(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("hide_counter"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != GetEnhancedBool("hide_counter"));
		}) | rpl::on_next([=](bool enabled) {
			SetEnhancedValue("hide_counter", enabled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddButtonWithIcon(
				container,
				tr::lng_settings_hide_stories(),
				st::settingsButtonNoIcon
		)->toggleOn(
				rpl::single(GetEnhancedBool("hide_stories"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != GetEnhancedBool("hide_stories"));
		}) | rpl::on_next([=](bool enabled) {
			SetEnhancedValue("hide_stories", enabled);
			EnhancedSettings::Write();
		}, container->lifetime());

		AddSkip(container);
	}

	rpl::producer<QString> Enhanced::title() {
		return tr::lng_settings_enhanced();
	}

	Enhanced::Enhanced(
			QWidget *parent,
			not_null<Window::SessionController *> controller)
			: Section(parent, controller) {
		setupContent(controller);
	}

	void Enhanced::setupContent(not_null<Window::SessionController *> controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		SetupEnhancedNetwork(content);
		SetupEnhancedMessages(content);
		SetupEnhancedButton(content);
		SetupEnhancedVoiceChat(content);
		SetupEnhancedOthers(controller, content);

		Ui::ResizeFitChild(this, content);
	}
} // namespace Settings

