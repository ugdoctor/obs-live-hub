#pragma once
#include <QComboBox>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>

class XPostDock : public QDockWidget {
	Q_OBJECT
public:
	explicit XPostDock(QWidget *parent = nullptr);

	void refresh(); // テンプレート一覧を再読み込み

signals:
	void postRequested(const QString &text);

private slots:
	void onTemplateChanged(int index);
	void onPostClicked();
	void onAutoPostModeChanged(); // ラジオボタン変更時

private:
	void populateTemplates();
	void updateAutoPostMode(); // 現在のラジオ選択を PluginConfig に保存

	QComboBox      *templateCombo_;
	QPlainTextEdit *textEdit_;
	QComboBox      *linkPlatformCombo_;
	QPushButton    *postButton_;
	// 配信開始時の自動表示モード（3択）
	QRadioButton   *radioOff_;    // 0: 自動投稿しない
	QRadioButton   *radioApi_;    // 1: API投稿確認ダイアログ
	QRadioButton   *radioManual_; // 2: 手動投稿ダイアログ
};
