#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>

class BouyomiParamLimitDialog : public QDialog {
	Q_OBJECT
public:
	explicit BouyomiParamLimitDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;

private:
	void loadFromConfig();
	void saveToConfig();

	QSpinBox *volumeMin_, *volumeMax_;
	QSpinBox *speedMin_,  *speedMax_;
	QSpinBox *toneMin_,   *toneMax_;

	QDialogButtonBox *buttonBox_;
};
