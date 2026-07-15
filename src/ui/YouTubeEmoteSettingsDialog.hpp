#pragma once
#include <string>

#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>

class QDragEnterEvent;
class QDropEvent;

// YouTubeカスタムメンバーエモートのショートコード（例:「:草:」）↔画像ファイルの
// マッピングを管理するダイアログ。画像はドラッグ＆ドロップで自動格納先フォルダへ
// コピーされ、overlay.html へは WsServer が新設した簡易HTTP画像配信
// （http://127.0.0.1:<port>/emotes/<filename>）経由のURLとして同期される。
class YouTubeEmoteSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit YouTubeEmoteSettingsDialog(QWidget *parent = nullptr);

	// 画像の自動格納先フォルダ（%APPDATA%\obs-studio\plugins\obs-live-hub\youtube_emotes）
	static QString imagesDir();
	// マッピングCSVのパス（%APPDATA%\...\youtube_emotes.csv）
	static QString csvPath();
	// overlay.html 同期用JSON（type: "youtube_emotes_sync"）を生成する
	static std::string makeSyncJson();

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private slots:
	void accept() override;
	void onDeleteRow();
	void onAddFilesClicked();

private:
	void loadFromCsv();
	bool saveToCsv() const;
	void addTableRow(const QString &code, const QString &fileName);
	void importImageFiles(const QStringList &paths);
	static QString suggestCodeFromFileName(const QString &fileName);

	QTableWidget     *table_;
	QPushButton      *addFilesBtn_;
	QPushButton      *delBtn_;
	QDialogButtonBox *buttonBox_;
};
