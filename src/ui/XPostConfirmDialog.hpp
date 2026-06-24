#pragma once
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>

class XPostConfirmDialog : public QDialog {
	Q_OBJECT
public:
	explicit XPostConfirmDialog(QWidget *parent = nullptr);

	QString postText() const;

private:
	void loadDefaultTemplate();

	QPlainTextEdit   *textEdit_;
	QComboBox        *linkPlatformCombo_;
	QDialogButtonBox *buttonBox_;
};
