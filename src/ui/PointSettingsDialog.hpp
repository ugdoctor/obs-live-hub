#pragma once
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class PointManager;

class PointSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit PointSettingsDialog(PointManager *manager, QWidget *parent = nullptr);

private slots:
	void onOpenFolder();
	void onReloadActions();
	void onRefreshUserTable();
	void onSearchChanged(const QString &text);
	void onAdjustPoints();
	void accept() override;

private:
	void loadSettings();
	void buildActionTable();
	void buildUserTable(const QString &filter = {});

	PointManager *manager_;

	// 設定タブ
	QCheckBox *enabledCheck_          = nullptr;
	QSpinBox  *commentAmtSpin_        = nullptr;
	QSpinBox  *watchIntervalSpin_     = nullptr;
	QSpinBox  *watchAmtSpin_          = nullptr;
	QSpinBox  *commentCooldownSpin_   = nullptr;
	QSpinBox  *useCooldownSpin_       = nullptr;

	// アクション一覧タブ
	QTableWidget *actionTable_ = nullptr;

	// ユーザーポイントタブ
	QLineEdit    *searchEdit_  = nullptr;
	QTableWidget *userTable_   = nullptr;
	QSpinBox     *adjustSpin_  = nullptr;
};
