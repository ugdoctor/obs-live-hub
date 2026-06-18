#pragma once
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

class EffectManager;

class EffectSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit EffectSettingsDialog(EffectManager *mgr, QWidget *parent = nullptr);

private slots:
	void accept() override;
	void onOpenFolder();
	void onReload();

private:
	void loadFromConfig();
	void saveToConfig();
	void refreshList();

	EffectManager *mgr_;
	QComboBox        *positionCombo_;
	QComboBox        *sizeCombo_;
	QSpinBox         *maxConcurrentSpin_;
	QSpinBox         *maxQueueSpin_;
	QListWidget      *effectList_;
	QPushButton      *openFolderBtn_;
	QPushButton      *reloadBtn_;
	QDialogButtonBox *buttonBox_;
};
