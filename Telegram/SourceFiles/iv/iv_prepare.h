/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Iv {

struct Options;
struct Prepared;

struct Source {
	MTPPage page;
	std::optional<MTPPhoto> webpagePhoto;
	std::optional<MTPDocument> webpageDocument;
};

[[nodiscard]] Prepared Prepare(
	const Source &source,
	const Options &options);

} // namespace Iv
