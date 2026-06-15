#pragma once
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>

struct VoteResultItem {
	QString key;
	QString label;
	int count = 0;
	double percent = 0.0;
};

class VoteManagerDialog : public QDialog {
	Q_OBJECT
public:
	explicit VoteManagerDialog(QWidget *parent = nullptr);
	void updateResults(const QString &question, const QString &mode, int total,
			   const QList<VoteResultItem> &results, bool active);

signals:
	void voteStartRequested(const QString &question, const QString &mode,
				const QList<QPair<QString, QString>> &choices);
	void voteEndRequested();
	void voteClearRequested();

private:
	void onModeChanged();
	void onAddChoice();
	void onRemoveChoice(int index);
	void onStartVote();
	void updateChoiceKeys();

	// 作成エリア
	QLineEdit    *questionEdit_;
	QRadioButton *choiceModeBtn_;
	QRadioButton *freeModeBtn_;
	QWidget      *choicesWidget_;
	QVBoxLayout  *choicesLayout_;
	QPushButton  *addChoiceBtn_;
	QList<QWidget *>   choiceRows_;
	QList<QLineEdit *> choiceEdits_;

	// アクションボタン
	QPushButton *startBtn_;
	QPushButton *endBtn_;
	QPushButton *clearBtn_;

	// 結果エリア
	QLabel         *resultQuestionLabel_;
	QLabel         *resultStatusLabel_;
	QLabel         *resultTotalLabel_;
	QStackedWidget *resultStack_;
	QWidget        *choiceResultWidget_;
	QVBoxLayout    *choiceResultLayout_;
	QTableWidget   *freeResultTable_;
};
