#pragma once
#include <QColor>
#include <QDialog>

class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

// 勝敗カウンター（MatchCounter）の外観設定専用ダイアログ。
// 勝敗数・目標・メモの操作はMatchCounterDock（常設ドック）に移管済み。
// 各操作は即座にPluginConfigへ保存し、matchDataChanged()シグナルで
// plugin-main.cppにWebSocketブロードキャストを促す。
class MatchCounterDialog : public QDialog {
	Q_OBJECT
public:
	explicit MatchCounterDialog(QWidget *parent = nullptr);

	// 外部から設定変更があった場合にUIを再読み込みする
	void refresh();

signals:
	void matchDataChanged(bool isReset);

private slots:
	void onAppearanceChanged();

private:
	static void applyButtonColor(QPushButton *btn, const QColor &c);
	void loadFromConfig();

	QSpinBox       *widthSpin_       = nullptr;
	QPushButton    *bgColorBtn_      = nullptr;
	QSlider        *bgOpacitySlider_ = nullptr;
	QLabel         *bgOpacityLabel_  = nullptr;
	QPushButton    *textColorBtn_    = nullptr;
	QPushButton    *warnColorBtn_    = nullptr;
	QFontComboBox  *fontFamilyCombo_ = nullptr;
	QSpinBox       *fontSizeSpin_    = nullptr;

	QColor bgColor_;
	QColor textColor_;
	QColor warnColor_;
};
