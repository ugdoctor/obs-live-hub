#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
	void onEngineChanged(int index);
	void onRefreshSpeakersClicked();
	void onSpeakerChanged(int index);
	void onSpeakersLoaded(QVector<AivisSpeakerInfo> speakers);
	void updateEngineStatus();

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

	// ── TTSエンジン選択 ──
	QComboBox *engineCombo_;

	// ── AivisSpeech 設定グループ ──
	QGroupBox   *aivisGroup_;

	// エンジン制御
	QLineEdit   *enginePathEdit_;
	QPushButton *browseEngineBtn_;
	QLabel      *engineStatusLabel_;
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
	QGroupBox *bouyomiGroup_;
	QLineEdit *bouyomiHostEdit_;
	QSpinBox  *bouyomiPortSpin_;
	QComboBox *bouyomiVoiceCombo_;
	QSpinBox  *bouyomiVoiceSpin_;

	QDialogButtonBox *buttonBox_;
};
