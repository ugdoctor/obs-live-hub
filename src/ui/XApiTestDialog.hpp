#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>

// X API テスト用ダイアログ。
// 「認証確認」で GET /2/users/me を呼び、「テスト投稿」で POST /2/tweets を呼ぶ。
// バックグラウンドスレッドで通信し、QPointer で安全にコールバックする。
class XApiTestDialog : public QDialog {
	Q_OBJECT
public:
	explicit XApiTestDialog(QWidget *parent = nullptr);

private slots:
	void onVerifyClicked();
	void onTestPostClicked();

private:
	void setVerifyStatus(const QString &text, bool ok);
	void setPostStatus(const QString &text, bool ok);
	void setBusy(bool busy);

	QPushButton *verifyBtn_;
	QPushButton *testPostBtn_;
	QLabel      *verifyStatus_;
	QLabel      *postStatus_;
	QPushButton *closeBtn_;
};
