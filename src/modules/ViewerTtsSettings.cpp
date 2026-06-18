/*
obs-live-hub
Copyright (C) 2026 ugdoctor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "ViewerTtsSettings.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TAG = "ViewerTtsSettings";

// CSV列インデックス
enum CsvCol {
	C_USERID = 0, C_PLATFORM, C_ENGINE,
	C_STYLE_ID, C_BOUYOMI_VOICE,
	C_RATE, C_PITCH, C_VOLUME,
	C_AIVIS_SPEED, C_AIVIS_PITCH, C_AIVIS_INTONATION, C_AIVIS_VOLUME, C_AIVIS_EMOTION,
	C_BOUYOMI_VOLUME, C_BOUYOMI_SPEED, C_BOUYOMI_TONE,
	C_COUNT
};

static const char *kHeader =
	"userId,platform,engine,styleId,bouyomiVoice,"
	"rate,pitch,volume,"
	"aivisSpeed,aivisPitch,aivisIntonation,aivisVolume,aivisEmotion,"
	"bouyomiVolume,bouyomiSpeed,bouyomiTone";

// ──────────────────────────────────────────────────────────────
ViewerTtsSettings &ViewerTtsSettings::instance()
{
	static ViewerTtsSettings inst;
	return inst;
}

ViewerTtsSettings::ViewerTtsSettings()
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) > 0) {
		csvPath_ = QString::fromLocal8Bit(appdata) +
		           "\\obs-studio\\plugins\\obs-live-hub\\tts_settings.csv";
	}
#endif
	load();
}

// ──────────────────────────────────────────────────────────────
// static
QString ViewerTtsSettings::makeKey(const QString &userId, const QString &platform)
{
	return platform + ":" + userId;
}

std::optional<ViewerTtsEntry> ViewerTtsSettings::get(const QString &userId,
                                                      const QString &platform) const
{
	const std::string key = makeKey(userId, platform).toStdString();
	const auto        it  = entries_.find(key);
	if (it == entries_.end())
		return std::nullopt;
	return it->second;
}

void ViewerTtsSettings::set(const QString &userId, const QString &platform,
                             const ViewerTtsEntry &entry)
{
	entries_[makeKey(userId, platform).toStdString()] = entry;
	saveNow();
}

// ──────────────────────────────────────────────────────────────
void ViewerTtsSettings::load()
{
	if (csvPath_.isEmpty())
		return;

	QFile f(csvPath_);
	if (!f.open(QIODevice::ReadOnly)) {
		obs_log(LOG_INFO, "[%s] CSV not found, starting empty", TAG);
		obs_log(LOG_INFO, "[%s] 0 user(s) loaded from CSV", TAG);
		return;
	}

	QTextStream ts(&f);
	bool        firstLine = true;
	int         count     = 0;

	while (!ts.atEnd()) {
		const QString line = ts.readLine().trimmed();
		if (firstLine) {
			firstLine = false;
			continue; // ヘッダ行をスキップ
		}
		if (line.isEmpty())
			continue;

		const QStringList p = line.split(',');
		if (p.size() < C_COUNT)
			continue;

		const QString userId   = p[C_USERID].trimmed();
		const QString platform = p[C_PLATFORM].trimmed();
		if (userId.isEmpty() || platform.isEmpty())
			continue;

		ViewerTtsEntry e;
		e.engine        = p[C_ENGINE].trimmed().toStdString();
		e.styleId       = p[C_STYLE_ID].trimmed().toLongLong();
		e.bouyomiVoice  = p[C_BOUYOMI_VOICE].trimmed().toInt();
		e.rate          = p[C_RATE].trimmed().toFloat();
		e.pitch         = p[C_PITCH].trimmed().toFloat();
		e.volume        = p[C_VOLUME].trimmed().toFloat();
		e.aivisSpeed      = p[C_AIVIS_SPEED].trimmed().toFloat();
		e.aivisPitch      = p[C_AIVIS_PITCH].trimmed().toFloat();
		e.aivisIntonation = p[C_AIVIS_INTONATION].trimmed().toFloat();
		e.aivisVolume     = p[C_AIVIS_VOLUME].trimmed().toFloat();
		e.aivisEmotion    = p[C_AIVIS_EMOTION].trimmed().toFloat();
		e.bouyomiVolume = p[C_BOUYOMI_VOLUME].trimmed().toInt();
		e.bouyomiSpeed  = p[C_BOUYOMI_SPEED].trimmed().toInt();
		e.bouyomiTone   = p[C_BOUYOMI_TONE].trimmed().toInt();

		entries_[makeKey(userId, platform).toStdString()] = e;
		++count;
	}
	f.close();

	obs_log(LOG_INFO, "[%s] %d user(s) loaded from CSV", TAG, count);
}

void ViewerTtsSettings::saveNow() const
{
	if (csvPath_.isEmpty())
		return;

	QDir().mkpath(QFileInfo(csvPath_).absolutePath());

	QFile f(csvPath_);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		obs_log(LOG_WARNING, "[%s] failed to save CSV: %s", TAG,
		        csvPath_.toUtf8().constData());
		return;
	}

	QTextStream ts(&f);
	ts << kHeader << "\n";

	for (const auto &kv : entries_) {
		const ViewerTtsEntry &e = kv.second;
		// キーから userId / platform を逆引きせず、entry に保持しないため
		// キー文字列 "platform:userId" を分割して復元する
		const QString key      = QString::fromStdString(kv.first);
		const int     colonIdx = key.indexOf(':');
		if (colonIdx < 0)
			continue;
		const QString platform = key.left(colonIdx);
		const QString userId   = key.mid(colonIdx + 1);

		ts << userId                                    << ','
		   << platform                                  << ','
		   << QString::fromStdString(e.engine)          << ','
		   << QString::number(e.styleId)                << ','
		   << QString::number(e.bouyomiVoice)           << ','
		   << QString::number(static_cast<double>(e.rate),            'f', 6) << ','
		   << QString::number(static_cast<double>(e.pitch),           'f', 6) << ','
		   << QString::number(static_cast<double>(e.volume),          'f', 6) << ','
		   << QString::number(static_cast<double>(e.aivisSpeed),      'f', 6) << ','
		   << QString::number(static_cast<double>(e.aivisPitch),      'f', 6) << ','
		   << QString::number(static_cast<double>(e.aivisIntonation), 'f', 6) << ','
		   << QString::number(static_cast<double>(e.aivisVolume),     'f', 6) << ','
		   << QString::number(static_cast<double>(e.aivisEmotion),    'f', 6) << ','
		   << QString::number(e.bouyomiVolume)          << ','
		   << QString::number(e.bouyomiSpeed)           << ','
		   << QString::number(e.bouyomiTone)            << '\n';
	}

	obs_log(LOG_INFO, "[%s] saved %zu user(s) to CSV", TAG, entries_.size());
}
