#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>

class TtsSpeechDialog : public QDialog {
	Q_OBJECT
public:
	explicit TtsSpeechDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;

private:
	void loadFromConfig();
	void saveToConfig();

	QCheckBox *enabledCheck_;

	QSlider *volumeSlider_;
	QLabel  *volumeLabel_;

	QSlider *rateSlider_;
	QLabel  *rateLabel_;

	QSlider *pitchSlider_;
	QLabel  *pitchLabel_;

	QCheckBox *readUsernameCheck_;

	QSpinBox *maxLengthSpin_;

	QCheckBox *twitchCheck_;
	QCheckBox *youtubeCheck_;

	QDialogButtonBox *buttonBox_;
};
