#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>

class DebugSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit DebugSettingsDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;

private:
	void loadFromConfig();
	void saveToConfig();

	QCheckBox *showConnectionCheck_;
	QCheckBox *showTtsCheck_;
	QCheckBox *showQuotaCheck_;
	QCheckBox *showVoteCheck_;
	QCheckBox *showLogCheck_;
	QCheckBox *showCommentDetailCheck_;
	QCheckBox *showEffectCheck_;
	QCheckBox *showPointCheck_;
	QDialogButtonBox *buttonBox_;
};
