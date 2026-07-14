#pragma once

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QTableWidget>

#include "modules/SupporterLedger.hpp"

// サポーター（課金）履歴ビューア。
// 配信中の場合は開く前に警告ダイアログを表示する（plugin-main.cpp 側で制御）。
// 左ペイン: ユーザー一覧（累計USD降順）、右ペイン: 選択ユーザーの課金履歴。
// 金額なしイベント（メンバーシップ系）は金額欄を「-」で表示する。
class SupporterHistoryDialog : public QDialog {
	Q_OBJECT
public:
	explicit SupporterHistoryDialog(QWidget *parent = nullptr);

	void refresh();

private slots:
	void onUserSelected(int row);
	void onInjectTestData();

private:
	void updateUserList();
	void updateEventTable(const QString &userKey);
	static QString formatUsd(double usd, bool hasAmount);
	// PluginConfig::membershipPlanPrices を planName → USD金額 マップに変換
	static QMap<QString, double> buildPlanUsdMap();

	QLabel *totalLabel_       = nullptr;
	QListWidget *userList_    = nullptr;
	QTableWidget *eventTable_ = nullptr;

	// 表示中のユーザーキー一覧（row → userKey）
	QList<QPair<QString, SupporterLedger::UserEntry>> cachedUsers_;
};
