#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QComboBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class YouTubePlatform; // forward declaration（動的ID取得のシグナル接続用）

// X Web Intent を使った手動投稿ダイアログ。
// X API 認証情報は一切使用しない。ブラウザで x.com/intent/post を開くだけ。
// youtube を渡すと broadcastResolved シグナルで動的に YouTube URL を取得できる。
class XManualPostDialog : public QDialog {
	Q_OBJECT
public:
	explicit XManualPostDialog(QWidget *parent = nullptr,
	                           YouTubePlatform *youtube = nullptr);

private slots:
	void onTemplateChanged(int index);
	void onSelectImageClicked();
	void onPostClicked();

private:
	QComboBox   *templateCombo_;
	QTextEdit   *textEdit_;
	QLineEdit   *linkUrlEdit_;
	QCheckBox   *twitchLinkCheck_;   // Twitchチャンネルリンク（設定から自動構築）
	QCheckBox   *youtubeLinkCheck_;  // YouTubeリンク（動的取得または設定値）
	QLineEdit   *imagePathEdit_;
	QPushButton *selectImageBtn_;
	QLabel      *clipboardStatusLabel_;
	QPushButton *postBtn_;
	QPushButton *closeBtn_;

	QString twitchUrl_;  // 構築済み Twitch URL（空なら無効）
	QString youtubeUrl_; // 構築済み YouTube URL（空なら未取得または無効）
};
