#pragma once

#include <QDialog>
#include <QTableWidget>

// メンバーシッププラン価格設定ダイアログ。
// プラン名 → 価格・通貨 のマッピングを管理する。
// 設定は PluginConfig::membershipPlanPrices に保存され、
// サポーター履歴ビューアが表示・集計時に参照する（記録時に金額は確定しない設計）。
class MembershipPlanPriceDialog : public QDialog {
	Q_OBJECT
public:
	explicit MembershipPlanPriceDialog(QWidget *parent = nullptr);

private slots:
	void onAdd();
	void onDelete();
	void accept() override;

private:
	QTableWidget *table_ = nullptr;
};
