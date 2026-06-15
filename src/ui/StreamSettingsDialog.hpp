#pragma once
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>

class StreamSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit StreamSettingsDialog(QWidget *parent = nullptr);

private:
	void accept() override;
	void loadFromConfig();
	void saveToConfig();
	void onSearchGame();
	void startTwitchUpdate();
	void startYouTubeUpdate();
	void onTwitchUpdateDone(bool ok, const QString &error);
	void onYouTubeUpdateDone(bool ok, const QString &error);
	void checkAllDone();

	// Twitch section
	QGroupBox   *twitchGroup_;
	QLineEdit   *twitchTitleEdit_;
	QLineEdit   *twitchGameEdit_;
	QPushButton *twitchGameBtn_;
	QLineEdit   *twitchTagsEdit_;
	QComboBox   *twitchLangCombo_;
	QString      selectedGameId_;

	// YouTube section
	QGroupBox  *youtubeGroup_;
	QLineEdit  *youtubeTitleEdit_;
	QTextEdit  *youtubeDescEdit_;
	QComboBox  *youtubeCatCombo_;
	QComboBox  *youtubePrivacyCombo_;

	QDialogButtonBox *buttonBox_;

	int         pendingCount_ = 0;
	QStringList errors_;
};
