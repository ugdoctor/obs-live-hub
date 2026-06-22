#pragma once
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVector>

struct AivisStyleInfo {
	QString name;
	int64_t id = 0;
};

struct AivisSpeakerInfo {
	QString name;
	QString uuid;
	QVector<AivisStyleInfo> styles;
};

class TtsSpeechDialog : public QDialog {
	Q_OBJECT
public:
	explicit TtsSpeechDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;
	void onDefaultEngineChanged(int engineIdx);
	void onEngineEnabledToggled(int engineIdx, bool enabled);
	void onRefreshSpeakersClicked();
	void onSpeakerChanged(int index);
	void onSpeakersLoaded(QVector<AivisSpeakerInfo> speakers);
	void updateEngineStatus();
	void refreshEngineStatuses();
	void onRecheckClicked();

private:
	void loadFromConfig();
	void saveToConfig();

	// ── 共通 TTS 設定 ──
	QCheckBox *enabledCheck_;
	QSlider   *volumeSlider_;
	QLabel    *volumeLabel_;
	QSlider   *rateSlider_;
	QLabel    *rateLabel_;
	QSlider   *pitchSlider_;
	QLabel    *pitchLabel_;
	QCheckBox *readUsernameCheck_;
	QSpinBox  *maxLengthSpin_;
	QCheckBox *twitchCheck_;
	QCheckBox *youtubeCheck_;
	QCheckBox *checkEngineConnectionCheck_;

	// ── TTSエンジン選択 (有効化 + デフォルト + 接続状態) ──
	static constexpr int kEngineCount = 7;
	QGroupBox    *engineListGroup_;
	QCheckBox    *engineEnabledCheck_[kEngineCount];
	QRadioButton *engineDefaultRadio_[kEngineCount];
	QLabel       *engineStatusLabel_[kEngineCount];
	QButtonGroup *defaultGroup_;
	QPushButton  *recheckBtn_;

	// ── AivisSpeech 設定グループ ──
	QGroupBox   *aivisGroup_;

	// エンジン制御
	QLineEdit   *enginePathEdit_;
	QPushButton *browseEngineBtn_;
	QLabel      *aivisEngineStatusLabel_;
	QPushButton *startEngineBtn_;
	QPushButton *stopEngineBtn_;
	QCheckBox   *autoStartCheck_;

	// 接続・音声
	QLineEdit   *aivisUrlEdit_;
	QComboBox   *speakerCombo_;
	QComboBox   *styleCombo_;
	QPushButton *refreshBtn_;

	QVector<AivisSpeakerInfo> speakers_;

	// ── 棒読みちゃん設定グループ ──
	QGroupBox   *bouyomiGroup_;
	QLineEdit   *bouyomiHostEdit_;
	QSpinBox    *bouyomiPortSpin_;
	QComboBox   *bouyomiVoiceCombo_;
	QSpinBox    *bouyomiVoiceSpin_;
	QLineEdit   *bouyomiExePathEdit_;
	QPushButton *browseBouyomiExeBtn_;
	QCheckBox   *bouyomiAutoStartCheck_;

	// ── VOICEROID（AssistantSeika）設定グループ ──
	QGroupBox   *voiceroidGroup_;
	QLineEdit   *voiceroidHostEdit_;
	QSpinBox    *voiceroidPortSpin_;
	QSpinBox    *voiceroidCidSpin_;
	QLineEdit   *voiceroidUsernameEdit_;
	QLineEdit   *voiceroidPasswordEdit_;

	QDialogButtonBox *buttonBox_;
};
