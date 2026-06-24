#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>

class XAccountSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit XAccountSettingsDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;

private:
	void loadFromConfig();
	void saveToConfig();

	QLineEdit        *apiKeyEdit_;
	QLineEdit        *apiSecretEdit_;
	QLineEdit        *accessTokenEdit_;
	QLineEdit        *accessTokenSecretEdit_;
	QDialogButtonBox *buttonBox_;
};
