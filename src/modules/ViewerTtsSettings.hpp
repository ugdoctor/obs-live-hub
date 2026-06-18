#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <cstdint>

#include <QString>

struct ViewerTtsEntry {
	std::string engine;

	int64_t styleId      = 0;
	int     bouyomiVoice = 0;

	float rate    = 1.0f;
	float pitch   = 1.0f;
	float volume  = 1.0f;

	float aivisSpeed      = 1.0f;
	float aivisPitch      = 0.0f;
	float aivisIntonation = 1.0f;
	float aivisVolume     = 1.0f;
	float aivisEmotion    = 1.0f;

	int bouyomiVolume = -1;
	int bouyomiSpeed  = -1;
	int bouyomiTone   = -1;
};

class ViewerTtsSettings {
public:
	static ViewerTtsSettings &instance();

	std::optional<ViewerTtsEntry> get(const QString &userId,
	                                  const QString &platform) const;
	void set(const QString &userId, const QString &platform,
	         const ViewerTtsEntry &entry);

private:
	ViewerTtsSettings();

	static QString makeKey(const QString &userId, const QString &platform);
	void           load();
	void           saveNow() const;

	QString csvPath_;
	std::unordered_map<std::string, ViewerTtsEntry> entries_;
};
