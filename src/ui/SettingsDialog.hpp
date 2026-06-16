#pragma once
#include <cstdint>
#include <string>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(QWidget *parent = nullptr);

signals:
	void twitchTokenUpdated(); // OAuth でトークン取得成功時に emit

private slots:
	void accept() override;
	void onOpenOverlay();
	void onGoogleAuthClicked();
	void onPollIntervalChanged(int seconds);
	// OAuth コールバックサーバーから code を受け取る（メインスレッドで呼ぶこと）
	void onOAuthCodeReceived(const QString &code);
	void onTwitchAuthClicked();
	// Twitch コールバックサーバーからトークンを受け取る（メインスレッドで呼ぶこと）
	void onTwitchTokenReceived(const QString &token);

private:
	void loadFromConfig();
	void saveToConfig();
	void updateAuthStatus();
	void updateTwitchAuthStatus();
	// WinHTTP トークン交換の結果を受け取る（メインスレッドで呼ぶ）
	void onTokenExchangeResult(bool ok, const std::string &accessToken,
				   const std::string &refreshToken, int64_t expiresIn,
				   const std::string &error);

	// YouTube
	QLineEdit *youtubeApiKeyEdit_;
	QLineEdit *youtubeBroadcastIdEdit_;
	QLineEdit *youtubeClientIdEdit_;
	QLineEdit *youtubeClientSecretEdit_;
	QLabel *googleAuthStatusLabel_;
	QPushButton *googleAuthBtn_;
	// コメント取得間隔設定
	QSpinBox *youtubePollIntervalSpinBox_;
	QLabel *youtubePollQuotaLabel_;
	// クォータ設定
	QLabel *youtubeQuotaInfoLabel_;
	QCheckBox *youtubeIgnoreQuotaCheck_;

	// Twitch
	QLineEdit *twitchOAuthEdit_;
	QLineEdit *twitchUsernameEdit_;
	QLineEdit *twitchChannelEdit_;
	QLineEdit *twitchClientIdEdit_;
	QLabel *twitchAuthStatusLabel_;
	QPushButton *twitchAuthBtn_;

	// WebSocket
	QSpinBox *wsPortSpinBox_;
	QPushButton *openOverlayBtn_;
	QDialogButtonBox *buttonBox_;
	QFormLayout *layout_;

	// 認証フロー中に一時保存する Client ID / Secret（Google）
	QString pendingClientId_;
	QString pendingClientSecret_;
	// 認証フロー中に一時保存する Twitch Client ID
	QString pendingTwitchClientId_;
};
