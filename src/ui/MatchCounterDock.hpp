#pragma once
#include <QDockWidget>

class QButtonGroup;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTimer;

// 勝敗カウンター（MatchCounter）の日常操作用OBS常設ドック。
// 勝敗+1/-1・手入力・目標モード・メモ入力・全リセットを提供する。
// 外観設定（色・フォント・透明度等）は MatchCounterDialog が別途担当する。
class MatchCounterDock : public QDockWidget {
	Q_OBJECT
public:
	explicit MatchCounterDock(QWidget *parent = nullptr);

	// 外部（メニュー等）から設定変更があった場合にUIを再読み込みする
	void refresh();

signals:
	// isReset=true の場合、overlay側はリール演出をスキップして即座に数値を書き換える
	void matchDataChanged(bool isReset);

private slots:
	void onWinPlus();
	void onWinMinus();
	void onLossPlus();
	void onLossMinus();
	void onWinsEditingFinished();
	void onLossesEditingFinished();
	void onTargetModeChanged();
	void onTargetValueChanged();
	void onMemoTextEdited();
	void onMemoCommit();
	void onResetClicked();

private:
	void pushHistory(const char *result);
	void popHistoryIfLast(const char *result);
	void loadFromConfig();
	void updateTargetInputEnablement();
	void saveAndBroadcast(bool isReset);

	QWidget *container_ = nullptr;

	QSpinBox *winsSpin_   = nullptr;
	QSpinBox *lossesSpin_ = nullptr;

	QRadioButton   *targetNoneRadio_   = nullptr;
	QRadioButton   *targetWinsRadio_   = nullptr;
	QRadioButton   *targetRateRadio_   = nullptr;
	QButtonGroup   *targetModeGroup_   = nullptr;
	QSpinBox       *targetWinsSpin_    = nullptr;
	QDoubleSpinBox *targetWinRateSpin_ = nullptr;

	QLineEdit *memoEdit_     = nullptr;
	QTimer    *memoDebounce_ = nullptr;

	QPushButton *resetBtn_ = nullptr;
};
