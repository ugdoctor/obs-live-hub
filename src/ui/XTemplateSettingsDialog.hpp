#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include "core/PluginConfig.hpp"

class XTemplateSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit XTemplateSettingsDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;
	void onSelectionChanged();
	void onAddClicked();
	void onDeleteClicked();
	void onMoveUpClicked();
	void onMoveDownClicked();
	void onEditorChanged();

private:
	void loadFromConfig();
	void saveToConfig();
	void populateList();
	void applyEditorToCurrentTemplate();

	bool ignoreEditorChanges_ = false;

	QListWidget      *list_;
	QLineEdit        *nameEdit_;
	QPlainTextEdit   *textEdit_;
	QComboBox        *linkPlatformCombo_;
	QCheckBox        *includeTwitchLinkCheck_;
	QCheckBox        *includeYoutubeLinkCheck_;
	QPushButton      *addBtn_;
	QPushButton      *deleteBtn_;
	QPushButton      *moveUpBtn_;
	QPushButton      *moveDownBtn_;
	QDialogButtonBox *buttonBox_;

	std::vector<XTemplate> templates_;
};
