#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>

class AivisParamLimitDialog : public QDialog {
	Q_OBJECT
public:
	explicit AivisParamLimitDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;

private:
	void loadFromConfig();
	void saveToConfig();

	QDoubleSpinBox *speedMin_,       *speedMax_;
	QDoubleSpinBox *pitchMin_,       *pitchMax_;
	QDoubleSpinBox *intonationMin_,  *intonationMax_;
	QDoubleSpinBox *volumeScaleMin_, *volumeScaleMax_;
	QDoubleSpinBox *emotionMin_,     *emotionMax_;

	QDialogButtonBox *buttonBox_;
};
