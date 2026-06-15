#pragma once
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

class OverlayStyleDialog : public QDialog {
	Q_OBJECT
public:
	explicit OverlayStyleDialog(QWidget *parent = nullptr);

private slots:
	void accept() override;
	void onPreview();

private:
	void loadFromConfig();
	void saveToConfig();
	static void applyButtonColor(QPushButton *btn, const QColor &c);

	QSpinBox *widthSpin_;
	QSpinBox *heightSpin_;

	QPushButton *bgColorBtn_;
	QSlider *bgOpacitySlider_;
	QLabel *bgOpacityLabel_;

	QPushButton *cardBgColorBtn_;
	QSlider *cardOpacitySlider_;
	QLabel *cardOpacityLabel_;

	QPushButton *cardBorderColorBtn_;
	QSlider *cardBorderOpacitySlider_;
	QLabel *cardBorderOpacityLabel_;

	QPushButton *usernameColorBtn_;
	QPushButton *textColorBtn_;

	QSpinBox *fontSizeSpin_;
	QSpinBox *iconSizeSpin_;
	QSpinBox *maxCommentsSpin_;

	// アンケートパネル設定
	QPushButton *voteBgColorBtn_;
	QSlider     *voteBgOpacitySlider_;
	QLabel      *voteBgOpacityLabel_;
	QPushButton *voteQuestionColorBtn_;
	QPushButton *voteHintColorBtn_;
	QPushButton *voteBarColorBtn_;
	QPushButton *voteBarBgColorBtn_;
	QPushButton *voteTotalColorBtn_;
	QPushButton *voteStatusColorBtn_;

	// アンケートパネル フォントサイズ
	QSpinBox *voteQuestionSizeSpin_;
	QSpinBox *voteHintSizeSpin_;
	QSpinBox *voteResultSizeSpin_;
	QSpinBox *voteTotalSizeSpin_;
	QSpinBox *voteStatusSizeSpin_;

	QPushButton *previewBtn_;
	QDialogButtonBox *buttonBox_;

	QColor bgColor_;
	QColor cardBgColor_;
	QColor cardBorderColor_;
	QColor usernameColor_;
	QColor textColor_;

	QColor voteBgColor_;
	QColor voteQuestionColor_;
	QColor voteHintColor_;
	QColor voteBarColor_;
	QColor voteBarBgColor_;
	QColor voteTotalColor_;
	QColor voteStatusColor_;
};
