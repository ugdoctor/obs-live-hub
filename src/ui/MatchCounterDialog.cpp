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

#include "MatchCounterDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

static QPushButton *makeColorButton(QWidget *parent)
{
	auto *btn = new QPushButton(parent);
	btn->setMinimumWidth(120);
	btn->setFixedHeight(28);
	return btn;
}

void MatchCounterDialog::applyButtonColor(QPushButton *btn, const QColor &c)
{
	const bool dark = c.lightnessF() < 0.5;
	btn->setStyleSheet(
		QString("background-color: %1; color: %2; border: 1px solid #666; border-radius: 3px;")
			.arg(c.name())
			.arg(dark ? "#ffffff" : "#000000"));
	btn->setText(c.name().toUpper());
}

MatchCounterDialog::MatchCounterDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("勝敗カウンター 外観設定");
	setMinimumWidth(400);

	auto *appearGroup = new QGroupBox("外観設定", this);
	auto *appearForm  = new QFormLayout(appearGroup);
	appearForm->setSpacing(8);

	widthSpin_ = new QSpinBox(this);
	widthSpin_->setRange(100, 1920);
	widthSpin_->setSuffix(" px");
	appearForm->addRow("幅:", widthSpin_);

	bgColorBtn_ = makeColorButton(this);
	appearForm->addRow("背景色:", bgColorBtn_);

	bgOpacitySlider_ = new QSlider(Qt::Horizontal, this);
	bgOpacitySlider_->setRange(0, 100);
	bgOpacityLabel_ = new QLabel(this);
	bgOpacityLabel_->setMinimumWidth(40);
	auto *opacityRow = new QWidget(this);
	auto *opacityH   = new QHBoxLayout(opacityRow);
	opacityH->setContentsMargins(0, 0, 0, 0);
	opacityH->addWidget(bgOpacitySlider_, 1);
	opacityH->addWidget(bgOpacityLabel_);
	appearForm->addRow("背景透明度:", opacityRow);

	textColorBtn_ = makeColorButton(this);
	appearForm->addRow("通常時（達成時）文字色:", textColorBtn_);

	warnColorBtn_ = makeColorButton(this);
	appearForm->addRow("未達成時（赤字）文字色:", warnColorBtn_);

	fontFamilyCombo_ = new QFontComboBox(this);
	appearForm->addRow("フォント:", fontFamilyCombo_);

	fontSizeSpin_ = new QSpinBox(this);
	fontSizeSpin_->setRange(8, 96);
	fontSizeSpin_->setSuffix(" px");
	appearForm->addRow("フォントサイズ:", fontSizeSpin_);

	auto *closeBtnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(10);
	mainLayout->addWidget(appearGroup);
	mainLayout->addWidget(closeBtnBox);

	connect(widthSpin_,    &QSpinBox::editingFinished, this,
	        &MatchCounterDialog::onAppearanceChanged);
	connect(fontSizeSpin_, &QSpinBox::editingFinished, this,
	        &MatchCounterDialog::onAppearanceChanged);
	connect(fontFamilyCombo_, &QFontComboBox::currentFontChanged, this,
	        [this](const QFont &) { onAppearanceChanged(); });
	connect(bgOpacitySlider_, &QSlider::valueChanged, this, [this](int v) {
		bgOpacityLabel_->setText(QString::number(v) + "%");
	});
	connect(bgOpacitySlider_, &QSlider::sliderReleased, this,
	        &MatchCounterDialog::onAppearanceChanged);
	connect(bgColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(bgColor_, this, "背景色を選択");
		if (c.isValid()) {
			bgColor_ = c;
			applyButtonColor(bgColorBtn_, c);
			onAppearanceChanged();
		}
	});
	connect(textColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(textColor_, this, "通常時文字色を選択");
		if (c.isValid()) {
			textColor_ = c;
			applyButtonColor(textColorBtn_, c);
			onAppearanceChanged();
		}
	});
	connect(warnColorBtn_, &QPushButton::clicked, this, [this]() {
		QColor c = QColorDialog::getColor(warnColor_, this, "未達成時文字色を選択");
		if (c.isValid()) {
			warnColor_ = c;
			applyButtonColor(warnColorBtn_, c);
			onAppearanceChanged();
		}
	});
	connect(closeBtnBox, &QDialogButtonBox::rejected, this, &QDialog::close);

	loadFromConfig();
}

void MatchCounterDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();

	widthSpin_->blockSignals(true);
	widthSpin_->setValue(cfg.matchWidth);
	widthSpin_->blockSignals(false);

	fontSizeSpin_->blockSignals(true);
	fontSizeSpin_->setValue(cfg.matchFontSize);
	fontSizeSpin_->blockSignals(false);

	fontFamilyCombo_->blockSignals(true);
	fontFamilyCombo_->setCurrentFont(QFont(QString::fromStdString(cfg.matchFontFamily)));
	fontFamilyCombo_->blockSignals(false);

	bgColor_   = QColor(QString::fromStdString(cfg.matchBgColor));
	textColor_ = QColor(QString::fromStdString(cfg.matchTextColor));
	warnColor_ = QColor(QString::fromStdString(cfg.matchWarnColor));
	if (!bgColor_.isValid())   bgColor_   = QColor("#1e1e1e");
	if (!textColor_.isValid()) textColor_ = QColor("#ffffff");
	if (!warnColor_.isValid()) warnColor_ = QColor("#ff4444");
	applyButtonColor(bgColorBtn_,   bgColor_);
	applyButtonColor(textColorBtn_, textColor_);
	applyButtonColor(warnColorBtn_, warnColor_);

	bgOpacitySlider_->blockSignals(true);
	bgOpacitySlider_->setValue(static_cast<int>(cfg.matchBgOpacity * 100.0f + 0.5f));
	bgOpacitySlider_->blockSignals(false);
	bgOpacityLabel_->setText(QString::number(bgOpacitySlider_->value()) + "%");
}

void MatchCounterDialog::refresh()
{
	loadFromConfig();
}

void MatchCounterDialog::onAppearanceChanged()
{
	auto &cfg = PluginConfig::instance();
	cfg.matchWidth      = widthSpin_->value();
	cfg.matchBgColor    = bgColor_.name().toStdString();
	cfg.matchBgOpacity  = bgOpacitySlider_->value() / 100.0f;
	cfg.matchTextColor  = textColor_.name().toStdString();
	cfg.matchWarnColor  = warnColor_.name().toStdString();
	cfg.matchFontFamily = fontFamilyCombo_->currentFont().family().toStdString();
	cfg.matchFontSize   = fontSizeSpin_->value();
	cfg.save();
	// 外観変更はカウント数を変えないため isReset=false でよい
	emit matchDataChanged(false);
}
