#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QSpinBox>

class ConversationOverlayDialog : public QDialog {
	Q_OBJECT
public:
	explicit ConversationOverlayDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;

private:
	void loadFromConfig();
	void saveToConfig();

	QSpinBox         *maxBubblesSpin_;
	QSpinBox         *hOffsetSpin_;
	QRadioButton     *alternateRadio_;
	QRadioButton     *fixedRadio_;
	QDialogButtonBox *buttonBox_;
};
