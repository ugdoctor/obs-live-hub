/*
obs-live-hub
Copyright (C) 2026 ugdoctor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

// X Web Intent を使った手動投稿ダイアログ。
// このファイルは X API（XClient）を一切使用しない。

#include "XManualPostDialog.hpp"
#include "core/PluginConfig.hpp"
#include "platforms/YouTubePlatform.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QClipboard>
#include <QThread>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

// linkPlatform 文字列 → プレースホルダーテキストに変換
static QString platformPlaceholder(const std::string &platform)
{
	if (platform == "twitch")  return "例: https://www.twitch.tv/your_channel";
	if (platform == "youtube") return "例: https://www.youtube.com/live/xxxxxxxxxxx";
	if (platform == "other")   return "URLを入力してください";
	return "（省略可）URLを入力してください";
}

XManualPostDialog::XManualPostDialog(QWidget *parent, YouTubePlatform *youtube)
	: QDialog(parent)
{
	setWindowTitle("X 手動投稿（Web Intent）");
	setMinimumWidth(520);

	const auto &cfg = PluginConfig::instance();

	// ── Twitch URL を構築 ──
	if (!cfg.twitchChannel.empty())
		twitchUrl_ = "https://twitch.tv/" + QString::fromStdString(cfg.twitchChannel);

	// ── テンプレート選択 ──
	templateCombo_ = new QComboBox(this);
	for (const auto &tpl : cfg.xTemplates)
		templateCombo_->addItem(QString::fromStdString(tpl.name));
	if (cfg.xTemplates.empty())
		templateCombo_->addItem("（テンプレートなし）");

	// ── 本文テキスト ──
	textEdit_ = new QTextEdit(this);
	textEdit_->setPlaceholderText("投稿本文を入力してください");
	textEdit_->setMinimumHeight(100);

	// ── リンク URL（手動入力） ──
	linkUrlEdit_ = new QLineEdit(this);

	// ── Twitch / YouTube チェックボックス ──
	auto *linkCheckGroup  = new QGroupBox("配信リンクを追加（設定から自動取得）", this);
	auto *linkCheckLayout = new QVBoxLayout(linkCheckGroup);

	// Twitch
	twitchLinkCheck_ = new QCheckBox(
		twitchUrl_.isEmpty() ? "Twitchリンク（チャンネル名が未設定）" :
		                       "Twitchリンク: " + twitchUrl_,
		linkCheckGroup);
	twitchLinkCheck_->setEnabled(!twitchUrl_.isEmpty());
	if (twitchUrl_.isEmpty())
		twitchLinkCheck_->setToolTip(
			"Twitch チャンネル名が設定されていません。\n"
			"「接続設定」でチャンネル名を設定してください。");

	// YouTube — youtube ポインタの有無と currentBroadcastId() で3通りに分岐
	youtubeLinkCheck_ = new QCheckBox("YouTubeリンク", linkCheckGroup);

	if (youtube) {
		const QString resolvedId = youtube->currentBroadcastId();
		if (!resolvedId.isEmpty()) {
			// すでに動画IDが確定済み
			youtubeUrl_ = "https://www.youtube.com/watch?v=" + resolvedId;
			youtubeLinkCheck_->setText("YouTubeリンク: " + youtubeUrl_);
			youtubeLinkCheck_->setEnabled(true);
		} else {
			// まだ取得中 — チェックボックスは有効にして先にチェックできるようにする
			youtubeLinkCheck_->setText("YouTubeリンク: 取得中...");
			youtubeLinkCheck_->setEnabled(true);
			youtubeLinkCheck_->setToolTip(
				"YouTube配信の動画IDを取得中です。\n"
				"チェックを入れておくと、取得完了後にそのままURLが追加されます。\n"
				"「ブラウザで投稿」は取得完了まで少し待ってから押してください。");

			// Qt の接続はダイアログ破棄時に自動切断される（this が receiver）
			// チェック状態はユーザーの設定を維持し、ON/OFF を変更しない
			QObject::connect(youtube, &YouTubePlatform::broadcastResolved,
			                 this, [this](const QString &videoId) {
				const bool isGuiThread =
					QThread::currentThread() == QApplication::instance()->thread();
				obs_log(LOG_INFO,
				        "[XManualPost] broadcastResolved received: videoId=%s  isGuiThread=%s",
				        videoId.toUtf8().constData(),
				        isGuiThread ? "true" : "false");

				const QString newUrl = "https://www.youtube.com/watch?v=" + videoId;
				obs_log(LOG_INFO, "[XManualPost] updating YouTube link label to: %s  "
				        "checkbox checked=%s",
				        newUrl.toUtf8().constData(),
				        youtubeLinkCheck_->isChecked() ? "true" : "false");

				youtubeUrl_ = newUrl;
				youtubeLinkCheck_->setText("YouTubeリンク: " + youtubeUrl_);
				youtubeLinkCheck_->setToolTip("");

				obs_log(LOG_INFO, "[XManualPost] YouTube link label updated (checkbox state unchanged: %s)",
				        youtubeLinkCheck_->isChecked() ? "checked" : "unchecked");
			});
		}
	} else {
		// YouTube 未接続 / 未設定（フォールバック: config の固定IDを確認）
		const std::string &ytId = cfg.youtubeBroadcastId;
		if (!ytId.empty() && ytId != "me") {
			youtubeUrl_ = "https://www.youtube.com/watch?v=" + QString::fromStdString(ytId);
			youtubeLinkCheck_->setText("YouTubeリンク: " + youtubeUrl_);
			youtubeLinkCheck_->setEnabled(true);
		} else {
			youtubeLinkCheck_->setText("YouTubeリンク（動画IDが確定していません）");
			youtubeLinkCheck_->setEnabled(false);
			youtubeLinkCheck_->setToolTip(
				"YouTube の配信URLが取得できないため利用できません。\n\n"
				"理由: 設定の「YouTube ブロードキャストID」が「me」または空の場合、\n"
				"実際の動画IDは配信開始後に YouTube API で自動取得されますが、\n"
				"YouTube が接続されていないため参照できません。\n\n"
				"回避策: 「接続設定」の「YouTube ブロードキャストID」欄に\n"
				"YouTube Studio で確認できる動画IDを直接入力してください。");
		}
	}

	linkCheckLayout->addWidget(twitchLinkCheck_);
	linkCheckLayout->addWidget(youtubeLinkCheck_);

	// ── 画像 ──
	auto *imageRow = new QHBoxLayout;
	imagePathEdit_  = new QLineEdit(this);
	imagePathEdit_->setPlaceholderText("（省略可）画像ファイルのパス");
	imagePathEdit_->setReadOnly(true);
	selectImageBtn_ = new QPushButton("参照...", this);
	selectImageBtn_->setMaximumWidth(80);
	imageRow->addWidget(imagePathEdit_);
	imageRow->addWidget(selectImageBtn_);

	// ── クリップボード案内ラベル ──
	clipboardStatusLabel_ = new QLabel(this);
	clipboardStatusLabel_->setWordWrap(true);
	clipboardStatusLabel_->hide();

	// ── フォームレイアウト ──
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	form->addRow("テンプレート:",   templateCombo_);
	form->addRow("本文:",           textEdit_);
	form->addRow("追加URL（任意）:", linkUrlEdit_);
	form->addRow("",                linkCheckGroup);
	form->addRow("画像:",           imageRow);
	form->addRow("",                clipboardStatusLabel_);

	// ── ボタン ──
	postBtn_  = new QPushButton("ブラウザで投稿", this);
	postBtn_->setDefault(true);
	closeBtn_ = new QPushButton("閉じる", this);

	auto *btnRow = new QHBoxLayout;
	btnRow->addStretch();
	btnRow->addWidget(postBtn_);
	btnRow->addWidget(closeBtn_);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addLayout(btnRow);

	// ── シグナル接続 ──
	QObject::connect(templateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 this, &XManualPostDialog::onTemplateChanged);
	QObject::connect(selectImageBtn_, &QPushButton::clicked,
	                 this, &XManualPostDialog::onSelectImageClicked);
	QObject::connect(postBtn_,  &QPushButton::clicked, this, &XManualPostDialog::onPostClicked);
	QObject::connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

	// 初期テンプレートを反映
	if (!cfg.xTemplates.empty())
		onTemplateChanged(0);
}

void XManualPostDialog::onTemplateChanged(int index)
{
	const auto &templates = PluginConfig::instance().xTemplates;
	if (index < 0 || index >= static_cast<int>(templates.size()))
		return;

	const auto &tpl = templates[static_cast<size_t>(index)];
	textEdit_->setPlainText(QString::fromStdString(tpl.text));
	linkUrlEdit_->setPlaceholderText(platformPlaceholder(tpl.linkPlatform));

	if (!tpl.imagePath.empty())
		imagePathEdit_->setText(QString::fromStdString(tpl.imagePath));

	// テンプレートに保存されたリンクチェック状態を反映
	// （broadcastResolved ではチェック状態を変更しないため、初期値はここで設定する）
	twitchLinkCheck_->setChecked(tpl.includeTwitchLink);
	youtubeLinkCheck_->setChecked(tpl.includeYoutubeLink);

	clipboardStatusLabel_->hide();
}

void XManualPostDialog::onSelectImageClicked()
{
	const QString path = QFileDialog::getOpenFileName(
		this, "画像ファイルを選択", QString(),
		"画像ファイル (*.png *.jpg *.jpeg *.gif *.webp);;すべてのファイル (*)");
	if (!path.isEmpty())
		imagePathEdit_->setText(path);
}

void XManualPostDialog::onPostClicked()
{
	// 1. 投稿テキストを組み立てる
	QString text = textEdit_->toPlainText().trimmed();

	// YouTube チェック済みだが URL がまだ取得中の場合はブロック
	if (youtubeLinkCheck_->isChecked() && youtubeUrl_.isEmpty()) {
		clipboardStatusLabel_->setText(
			"YouTubeのURLを取得中です。少し待ってから再度お試しください。");
		clipboardStatusLabel_->setStyleSheet("color: #ffaa00;");
		clipboardStatusLabel_->show();
		return;
	}

	// URLを収集（手動入力 → Twitch → YouTube の順）
	QStringList urls;
	const QString manualUrl = linkUrlEdit_->text().trimmed();
	if (!manualUrl.isEmpty())
		urls << manualUrl;
	if (twitchLinkCheck_->isEnabled() && twitchLinkCheck_->isChecked())
		urls << twitchUrl_;
	if (youtubeLinkCheck_->isChecked() && !youtubeUrl_.isEmpty())
		urls << youtubeUrl_;

	if (!urls.isEmpty())
		text += "\n\n" + urls.join("\n");

	// 2. 画像をクリップボードへコピー
	const QString imagePath = imagePathEdit_->text().trimmed();
	if (!imagePath.isEmpty()) {
		QPixmap pix(imagePath);
		if (pix.isNull()) {
			clipboardStatusLabel_->setText(
				"画像の読み込みに失敗しました。ファイルを確認してください。");
			clipboardStatusLabel_->setStyleSheet("color: #ff5555;");
			clipboardStatusLabel_->show();
		} else {
			QApplication::clipboard()->setPixmap(pix);
			clipboardStatusLabel_->setText(
				"画像をクリップボードにコピーしました。"
				"投稿画面が開いたら Ctrl+V で貼り付けてください。");
			clipboardStatusLabel_->setStyleSheet("color: #44bb44;");
			clipboardStatusLabel_->show();
		}
	}

	// 3. Web Intent URL を組み立てる（UTF-8 パーセントエンコード）
	const QByteArray encoded =
		QUrl::toPercentEncoding(text, QByteArray(), QByteArray(""));
	const QUrl intentUrl =
		QUrl("https://x.com/intent/post?text=" + QString::fromUtf8(encoded));

	obs_log(LOG_INFO, "[XManualPost] opening browser: text=\"%s\"",
	        text.toUtf8().constData());

	// 4. デフォルトブラウザで開く
	QDesktopServices::openUrl(intentUrl);
}
