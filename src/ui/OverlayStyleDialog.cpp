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

#include "OverlayStyleDialog.hpp"
#include "OverlayUtils.hpp"
#include "core/PluginConfig.hpp"

#include <QColorDialog>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

// ─── ヘルパー ─────────────────────────────────────────────────────────────

static QPushButton *makeColorButton(QWidget *parent)
{
	auto *btn = new QPushButton(parent);
	btn->setMinimumWidth(130);
	btn->setFixedHeight(28);
	return btn;
}

void OverlayStyleDialog::applyButtonColor(QPushButton *btn, const QColor &c)
{
	const bool dark = c.lightnessF() < 0.5;
	btn->setStyleSheet(
		QString("background-color: %1; color: %2; border: 1px solid #666; border-radius: 3px;")
			.arg(c.name())
			.arg(dark ? "#ffffff" : "#000000"));
	btn->setText(c.name().toUpper());
}

// ─── コンストラクタ ───────────────────────────────────────────────────────

OverlayStyleDialog::OverlayStyleDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("オーバーレイ外観設定");
	setMinimumWidth(460);

	// ── スピンボックス
	widthSpin_ = new QSpinBox(this);
	widthSpin_->setRange(200, 1920);
	widthSpin_->setSuffix(" px");

	heightSpin_ = new QSpinBox(this);
	heightSpin_->setRange(0, 2160);
	heightSpin_->setSuffix(" px");
	heightSpin_->setSpecialValueText("0 (100vh)");

	fontSizeSpin_ = new QSpinBox(this);
	fontSizeSpin_->setRange(8, 48);
	fontSizeSpin_->setSuffix(" px");

	iconSizeSpin_ = new QSpinBox(this);
	iconSizeSpin_->setRange(24, 64);
	iconSizeSpin_->setSuffix(" px");

	maxCommentsSpin_ = new QSpinBox(this);
	maxCommentsSpin_->setRange(5, 50);
	maxCommentsSpin_->setSuffix(" 件");

	// ── カラーボタン
	bgColorBtn_         = makeColorButton(this);
	cardBgColorBtn_     = makeColorButton(this);
	cardBorderColorBtn_ = makeColorButton(this);
	usernameColorBtn_   = makeColorButton(this);
	textColorBtn_       = makeColorButton(this);

	// ── 透明度スライダー + ラベル
	bgOpacitySlider_         = new QSlider(Qt::Horizontal, this);
	cardOpacitySlider_       = new QSlider(Qt::Horizontal, this);
	cardBorderOpacitySlider_ = new QSlider(Qt::Horizontal, this);
	for (auto *s : {bgOpacitySlider_, cardOpacitySlider_, cardBorderOpacitySlider_})
		s->setRange(0, 100);

	bgOpacityLabel_         = new QLabel(this);
	cardOpacityLabel_       = new QLabel(this);
	cardBorderOpacityLabel_ = new QLabel(this);
	for (auto *l : {bgOpacityLabel_, cardOpacityLabel_, cardBorderOpacityLabel_})
		l->setMinimumWidth(40);

	// ── アンケートパネル設定ウィジェット
	voteBgColorBtn_      = makeColorButton(this);
	voteQuestionColorBtn_ = makeColorButton(this);
	voteHintColorBtn_    = makeColorButton(this);
	voteBarColorBtn_     = makeColorButton(this);
	voteBarBgColorBtn_   = makeColorButton(this);
	voteTotalColorBtn_   = makeColorButton(this);
	voteStatusColorBtn_  = makeColorButton(this);

	voteBgOpacitySlider_ = new QSlider(Qt::Horizontal, this);
	voteBgOpacitySlider_->setRange(0, 100);
	voteBgOpacityLabel_  = new QLabel(this);
	voteBgOpacityLabel_->setMinimumWidth(40);

	auto makeVoteSizeSpin = [this]() {
		auto *s = new QSpinBox(this);
		s->setRange(8, 48);
		s->setSuffix(" px");
		return s;
	};
	voteQuestionSizeSpin_ = makeVoteSizeSpin();
	voteHintSizeSpin_     = makeVoteSizeSpin();
	voteResultSizeSpin_   = makeVoteSizeSpin();
	voteTotalSizeSpin_    = makeVoteSizeSpin();
	voteStatusSizeSpin_   = makeVoteSizeSpin();

	// ── プレビュー + ボタンボックス
	previewBtn_ = new QPushButton("プレビュー", this);
	buttonBox_  = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	// ── 透明度行ヘルパー (スライダー + パーセントラベルを横並びに)
	auto makeOpacityRow = [this](QSlider *sl, QLabel *lb) -> QWidget * {
		auto *w = new QWidget(this);
		auto *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(sl, 1);
		h->addWidget(lb);
		return w;
	};

	// ── コメントカード設定グループ
	auto *cardGroup  = new QGroupBox("コメントカード設定", this);
	auto *cardForm   = new QFormLayout(cardGroup);
	cardForm->setSpacing(8);
	cardForm->addRow("幅:", widthSpin_);
	cardForm->addRow("高さ:", heightSpin_);
	cardForm->addRow("背景色:", bgColorBtn_);
	cardForm->addRow("背景透明度:", makeOpacityRow(bgOpacitySlider_, bgOpacityLabel_));
	cardForm->addRow("カード背景色:", cardBgColorBtn_);
	cardForm->addRow("カード透明度:", makeOpacityRow(cardOpacitySlider_, cardOpacityLabel_));
	cardForm->addRow("カード枠線色:", cardBorderColorBtn_);
	cardForm->addRow("カード枠線透明度:", makeOpacityRow(cardBorderOpacitySlider_, cardBorderOpacityLabel_));
	cardForm->addRow("ユーザー名色:", usernameColorBtn_);
	cardForm->addRow("コメント文字色:", textColorBtn_);
	cardForm->addRow("フォントサイズ:", fontSizeSpin_);
	cardForm->addRow("アイコンサイズ:", iconSizeSpin_);
	cardForm->addRow("最大表示件数:", maxCommentsSpin_);

	// ── アンケートパネル設定グループ
	auto *voteGroup = new QGroupBox("アンケートパネル設定", this);
	auto *voteForm  = new QFormLayout(voteGroup);
	voteForm->setSpacing(8);
	voteForm->addRow("背景色:", voteBgColorBtn_);
	voteForm->addRow("背景透明度:", makeOpacityRow(voteBgOpacitySlider_, voteBgOpacityLabel_));
	voteForm->addRow("質問文字色:", voteQuestionColorBtn_);
	voteForm->addRow("選択肢ヒント色:", voteHintColorBtn_);
	voteForm->addRow("バー色:", voteBarColorBtn_);
	voteForm->addRow("バー背景色:", voteBarBgColorBtn_);
	voteForm->addRow("投票数テキスト色:", voteTotalColorBtn_);
	voteForm->addRow("受付中テキスト色:", voteStatusColorBtn_);
	voteForm->addRow("質問文フォントサイズ:", voteQuestionSizeSpin_);
	voteForm->addRow("ヒント文フォントサイズ:", voteHintSizeSpin_);
	voteForm->addRow("結果テキストフォントサイズ:", voteResultSizeSpin_);
	voteForm->addRow("投票数フォントサイズ:", voteTotalSizeSpin_);
	voteForm->addRow("受付中フォントサイズ:", voteStatusSizeSpin_);

	// ── 全体レイアウト
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(10);
	mainLayout->addWidget(cardGroup);
	mainLayout->addWidget(voteGroup);
	mainLayout->addWidget(previewBtn_);
	mainLayout->addWidget(buttonBox_);

	// ── カラーボタン接続
	connect(bgColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(bgColor_, this, "背景色を選択");
		if (c.isValid()) {
			bgColor_ = c;
			applyButtonColor(bgColorBtn_, c);
		}
	});
	connect(cardBgColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(cardBgColor_, this, "カード背景色を選択");
		if (c.isValid()) {
			cardBgColor_ = c;
			applyButtonColor(cardBgColorBtn_, c);
		}
	});
	connect(cardBorderColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(cardBorderColor_, this, "カード枠線色を選択");
		if (c.isValid()) {
			cardBorderColor_ = c;
			applyButtonColor(cardBorderColorBtn_, c);
		}
	});
	connect(usernameColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(usernameColor_, this, "ユーザー名色を選択");
		if (c.isValid()) {
			usernameColor_ = c;
			applyButtonColor(usernameColorBtn_, c);
		}
	});
	connect(textColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(textColor_, this, "コメント文字色を選択");
		if (c.isValid()) {
			textColor_ = c;
			applyButtonColor(textColorBtn_, c);
		}
	});

	// ── アンケートパネル カラーボタン接続
	connect(voteBgColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteBgColor_, this, "アンケートパネル背景色を選択");
		if (c.isValid()) {
			voteBgColor_ = c;
			applyButtonColor(voteBgColorBtn_, c);
		}
	});
	connect(voteQuestionColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteQuestionColor_, this, "質問文字色を選択");
		if (c.isValid()) {
			voteQuestionColor_ = c;
			applyButtonColor(voteQuestionColorBtn_, c);
		}
	});
	connect(voteHintColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteHintColor_, this, "選択肢ヒント色を選択");
		if (c.isValid()) {
			voteHintColor_ = c;
			applyButtonColor(voteHintColorBtn_, c);
		}
	});
	connect(voteBarColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteBarColor_, this, "バー色を選択");
		if (c.isValid()) {
			voteBarColor_ = c;
			applyButtonColor(voteBarColorBtn_, c);
		}
	});
	connect(voteBarBgColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteBarBgColor_, this, "バー背景色を選択");
		if (c.isValid()) {
			voteBarBgColor_ = c;
			applyButtonColor(voteBarBgColorBtn_, c);
		}
	});
	connect(voteTotalColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteTotalColor_, this, "投票数テキスト色を選択");
		if (c.isValid()) {
			voteTotalColor_ = c;
			applyButtonColor(voteTotalColorBtn_, c);
		}
	});
	connect(voteStatusColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(voteStatusColor_, this, "受付中テキスト色を選択");
		if (c.isValid()) {
			voteStatusColor_ = c;
			applyButtonColor(voteStatusColorBtn_, c);
		}
	});

	// ── スライダー → ラベル更新
	auto connectOpacity = [](QSlider *sl, QLabel *lb) {
		QObject::connect(sl, &QSlider::valueChanged, lb, [lb](int v) {
			lb->setText(QString::number(v) + "%");
		});
	};
	connectOpacity(bgOpacitySlider_,         bgOpacityLabel_);
	connectOpacity(cardOpacitySlider_,       cardOpacityLabel_);
	connectOpacity(cardBorderOpacitySlider_, cardBorderOpacityLabel_);
	connectOpacity(voteBgOpacitySlider_,     voteBgOpacityLabel_);

	connect(previewBtn_, &QPushButton::clicked, this, &OverlayStyleDialog::onPreview);
	connect(buttonBox_, &QDialogButtonBox::accepted, this, &OverlayStyleDialog::accept);
	connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

// ─── 設定の読み込み・保存 ─────────────────────────────────────────────────

void OverlayStyleDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();

	widthSpin_->setValue(cfg.overlayWidth);
	heightSpin_->setValue(cfg.overlayHeight);
	fontSizeSpin_->setValue(cfg.fontSize);
	iconSizeSpin_->setValue(cfg.iconSize);
	maxCommentsSpin_->setValue(cfg.maxComments);

	bgColor_         = QColor(QString::fromStdString(cfg.bgColor));
	cardBgColor_     = QColor(QString::fromStdString(cfg.cardBgColor));
	cardBorderColor_ = QColor(QString::fromStdString(cfg.cardBorderColor));
	usernameColor_   = QColor(QString::fromStdString(cfg.usernameColor));
	textColor_       = QColor(QString::fromStdString(cfg.textColor));

	if (!bgColor_.isValid())         bgColor_         = QColor("#121212");
	if (!cardBgColor_.isValid())     cardBgColor_     = QColor("#1e1e1e");
	if (!cardBorderColor_.isValid()) cardBorderColor_ = QColor("#ffffff");
	if (!usernameColor_.isValid())   usernameColor_   = QColor("#e0e0e0");
	if (!textColor_.isValid())       textColor_       = QColor("#ffffff");

	applyButtonColor(bgColorBtn_,         bgColor_);
	applyButtonColor(cardBgColorBtn_,     cardBgColor_);
	applyButtonColor(cardBorderColorBtn_, cardBorderColor_);
	applyButtonColor(usernameColorBtn_,   usernameColor_);
	applyButtonColor(textColorBtn_,       textColor_);

	bgOpacitySlider_->setValue(static_cast<int>(cfg.bgOpacity * 100.0f + 0.5f));
	cardOpacitySlider_->setValue(static_cast<int>(cfg.cardOpacity * 100.0f + 0.5f));
	cardBorderOpacitySlider_->setValue(
		static_cast<int>(cfg.cardBorderOpacity * 100.0f + 0.5f));

	// アンケートパネル設定
	voteBgColor_       = QColor(QString::fromStdString(cfg.voteBgColor));
	voteQuestionColor_ = QColor(QString::fromStdString(cfg.voteQuestionColor));
	voteHintColor_     = QColor(QString::fromStdString(cfg.voteHintColor));
	voteBarColor_      = QColor(QString::fromStdString(cfg.voteBarColor));
	voteBarBgColor_    = QColor(QString::fromStdString(cfg.voteBarBgColor));

	if (!voteBgColor_.isValid())       voteBgColor_       = QColor("#000000");
	if (!voteQuestionColor_.isValid()) voteQuestionColor_ = QColor("#ffffff");
	if (!voteHintColor_.isValid())     voteHintColor_     = QColor("#aaaaaa");
	if (!voteBarColor_.isValid())      voteBarColor_      = QColor("#4488ff");
	if (!voteBarBgColor_.isValid())    voteBarBgColor_    = QColor("#333333");

	voteTotalColor_  = QColor(QString::fromStdString(cfg.voteTotalColor));
	voteStatusColor_ = QColor(QString::fromStdString(cfg.voteStatusColor));
	if (!voteTotalColor_.isValid())  voteTotalColor_  = QColor("#888888");
	if (!voteStatusColor_.isValid()) voteStatusColor_ = QColor("#44ff44");

	applyButtonColor(voteBgColorBtn_,       voteBgColor_);
	applyButtonColor(voteQuestionColorBtn_, voteQuestionColor_);
	applyButtonColor(voteHintColorBtn_,     voteHintColor_);
	applyButtonColor(voteBarColorBtn_,      voteBarColor_);
	applyButtonColor(voteBarBgColorBtn_,    voteBarBgColor_);
	applyButtonColor(voteTotalColorBtn_,    voteTotalColor_);
	applyButtonColor(voteStatusColorBtn_,   voteStatusColor_);

	voteBgOpacitySlider_->setValue(static_cast<int>(cfg.voteBgOpacity * 100.0f + 0.5f));

	voteQuestionSizeSpin_->setValue(cfg.voteQuestionSize);
	voteHintSizeSpin_->setValue(cfg.voteHintSize);
	voteResultSizeSpin_->setValue(cfg.voteResultSize);
	voteTotalSizeSpin_->setValue(cfg.voteTotalSize);
	voteStatusSizeSpin_->setValue(cfg.voteStatusSize);
}

void OverlayStyleDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.overlayWidth      = widthSpin_->value();
	cfg.overlayHeight     = heightSpin_->value();
	cfg.bgColor           = bgColor_.name().toStdString();
	cfg.bgOpacity         = bgOpacitySlider_->value() / 100.0f;
	cfg.cardBgColor       = cardBgColor_.name().toStdString();
	cfg.cardOpacity       = cardOpacitySlider_->value() / 100.0f;
	cfg.cardBorderColor   = cardBorderColor_.name().toStdString();
	cfg.cardBorderOpacity = cardBorderOpacitySlider_->value() / 100.0f;
	cfg.usernameColor     = usernameColor_.name().toStdString();
	cfg.textColor         = textColor_.name().toStdString();
	cfg.fontSize          = fontSizeSpin_->value();
	cfg.iconSize          = iconSizeSpin_->value();
	cfg.maxComments       = maxCommentsSpin_->value();
	cfg.voteBgColor       = voteBgColor_.name().toStdString();
	cfg.voteBgOpacity     = voteBgOpacitySlider_->value() / 100.0f;
	cfg.voteQuestionColor = voteQuestionColor_.name().toStdString();
	cfg.voteHintColor     = voteHintColor_.name().toStdString();
	cfg.voteBarColor      = voteBarColor_.name().toStdString();
	cfg.voteBarBgColor    = voteBarBgColor_.name().toStdString();
	cfg.voteTotalColor    = voteTotalColor_.name().toStdString();
	cfg.voteStatusColor   = voteStatusColor_.name().toStdString();
	cfg.voteQuestionSize  = voteQuestionSizeSpin_->value();
	cfg.voteHintSize      = voteHintSizeSpin_->value();
	cfg.voteResultSize    = voteResultSizeSpin_->value();
	cfg.voteTotalSize     = voteTotalSizeSpin_->value();
	cfg.voteStatusSize    = voteStatusSizeSpin_->value();
	cfg.save();
}

void OverlayStyleDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}

void OverlayStyleDialog::onPreview()
{
	const QString path = findOverlayHtmlPath();
	if (path.isEmpty()) {
		QMessageBox::warning(this, "obs-live-hub",
				     "overlay.html が見つかりません。\n"
				     "先にビルドして DLL を配置してください。");
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
		QMessageBox::warning(this, "obs-live-hub", "HTML ファイルを開けませんでした。");
}
