#include "VoteManagerDialog.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>

static const QString CHOICE_KEYS = QStringLiteral("ABCDEF");

VoteManagerDialog::VoteManagerDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub アンケート管理");
	setWindowFlags(windowFlags() | Qt::Window);
	setMinimumWidth(500);

	// ---- 作成エリア ----
	auto *createGroup = new QGroupBox("アンケート作成", this);

	questionEdit_ = new QLineEdit(this);
	questionEdit_->setPlaceholderText("質問文を入力してください（例: 好きな麺は？）");

	choiceModeBtn_ = new QRadioButton("選択肢形式", this);
	freeModeBtn_   = new QRadioButton("自由記述形式", this);
	choiceModeBtn_->setChecked(true);

	auto *modeRow = new QHBoxLayout;
	modeRow->addWidget(choiceModeBtn_);
	modeRow->addWidget(freeModeBtn_);
	modeRow->addStretch();

	// 選択肢リスト
	choicesWidget_ = new QWidget(this);
	choicesLayout_ = new QVBoxLayout(choicesWidget_);
	choicesLayout_->setContentsMargins(0, 0, 0, 0);
	choicesLayout_->setSpacing(4);

	addChoiceBtn_ = new QPushButton("＋ 選択肢を追加", this);
	addChoiceBtn_->setMaximumWidth(140);

	// デフォルト 2択（A / B）
	onAddChoice();
	onAddChoice();

	auto *createLayout = new QVBoxLayout(createGroup);
	createLayout->addWidget(new QLabel("質問:", this));
	createLayout->addWidget(questionEdit_);
	createLayout->addWidget(new QLabel("形式:", this));
	createLayout->addLayout(modeRow);
	createLayout->addWidget(choicesWidget_);
	createLayout->addWidget(addChoiceBtn_);

	// ---- アクションボタン ----
	startBtn_ = new QPushButton("▶ アンケート開始", this);
	endBtn_   = new QPushButton("■ 終了", this);
	clearBtn_ = new QPushButton("クリア", this);
	endBtn_->setEnabled(false);

	startBtn_->setStyleSheet(
		"QPushButton{background:#2d7d46;color:#fff;font-weight:bold;padding:4px 14px;}"
		"QPushButton:disabled{background:#888;}");
	endBtn_->setStyleSheet(
		"QPushButton{background:#7d2d2d;color:#fff;font-weight:bold;padding:4px 14px;}"
		"QPushButton:disabled{background:#888;}");

	auto *actionRow = new QHBoxLayout;
	actionRow->addWidget(startBtn_);
	actionRow->addWidget(endBtn_);
	actionRow->addWidget(clearBtn_);
	actionRow->addStretch();

	// ---- 結果エリア ----
	auto *resultGroup = new QGroupBox("集計結果", this);

	resultQuestionLabel_ = new QLabel("（アンケート未開始）", this);
	resultQuestionLabel_->setWordWrap(true);
	resultQuestionLabel_->setStyleSheet("font-weight:bold;");

	resultStatusLabel_ = new QLabel(this);
	resultStatusLabel_->setTextFormat(Qt::RichText);

	resultTotalLabel_ = new QLabel(this);
	resultTotalLabel_->setStyleSheet("color:gray;");

	// 選択肢結果ページ (page 0)
	choiceResultWidget_ = new QWidget(this);
	choiceResultLayout_ = new QVBoxLayout(choiceResultWidget_);
	choiceResultLayout_->setContentsMargins(0, 0, 0, 0);
	choiceResultLayout_->setSpacing(6);

	// 自由記述結果ページ (page 1)
	freeResultTable_ = new QTableWidget(0, 3, this);
	freeResultTable_->setHorizontalHeaderLabels({"回答", "票数", "%"});
	freeResultTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	freeResultTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	freeResultTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	freeResultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	freeResultTable_->setSelectionMode(QAbstractItemView::NoSelection);
	freeResultTable_->setMinimumHeight(100);

	resultStack_ = new QStackedWidget(this);
	resultStack_->addWidget(choiceResultWidget_); // page 0
	resultStack_->addWidget(freeResultTable_);    // page 1

	auto *resultLayout = new QVBoxLayout(resultGroup);
	resultLayout->addWidget(resultQuestionLabel_);
	resultLayout->addWidget(resultStatusLabel_);
	resultLayout->addWidget(resultTotalLabel_);
	resultLayout->addWidget(resultStack_, 1);

	// ---- 閉じるボタン ----
	auto *closeBtn = new QPushButton("閉じる", this);
	auto *closeRow = new QHBoxLayout;
	closeRow->addStretch();
	closeRow->addWidget(closeBtn);

	// ---- 全体レイアウト ----
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(createGroup);
	mainLayout->addLayout(actionRow);
	mainLayout->addWidget(resultGroup, 1);
	mainLayout->addLayout(closeRow);

	// ---- シグナル接続 ----
	connect(choiceModeBtn_, &QRadioButton::toggled, [this](bool) { onModeChanged(); });
	connect(freeModeBtn_,   &QRadioButton::toggled, [this](bool) { onModeChanged(); });
	connect(addChoiceBtn_, &QPushButton::clicked, [this]() { onAddChoice(); });
	connect(startBtn_, &QPushButton::clicked, [this]() { onStartVote(); });
	connect(endBtn_, &QPushButton::clicked, [this]() {
		emit voteEndRequested();
		endBtn_->setEnabled(false);
		startBtn_->setEnabled(true);
	});
	connect(clearBtn_, &QPushButton::clicked, [this]() { emit voteClearRequested(); });
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::hide);
}

void VoteManagerDialog::onModeChanged()
{
	const bool isChoice = choiceModeBtn_->isChecked();
	choicesWidget_->setVisible(isChoice);
	addChoiceBtn_->setVisible(isChoice);
}

void VoteManagerDialog::onAddChoice()
{
	const int idx = choiceEdits_.size();
	if (idx >= 6)
		return;

	const QChar key = CHOICE_KEYS.at(idx);

	auto *rowWidget = new QWidget(choicesWidget_);
	auto *rl        = new QHBoxLayout(rowWidget);
	rl->setContentsMargins(0, 0, 0, 0);
	rl->setSpacing(4);

	auto *keyLabel = new QLabel(QString(key) + ":", rowWidget);
	keyLabel->setFixedWidth(22);
	keyLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	auto *edit = new QLineEdit(rowWidget);
	edit->setPlaceholderText(QString("選択肢 %1 のテキスト").arg(key));

	auto *delBtn = new QPushButton("削除", rowWidget);
	delBtn->setFixedWidth(48);
	connect(delBtn, &QPushButton::clicked, [this, rowWidget]() {
		const int i = choiceRows_.indexOf(rowWidget);
		if (i >= 0)
			onRemoveChoice(i);
	});

	rl->addWidget(keyLabel);
	rl->addWidget(edit, 1);
	rl->addWidget(delBtn);

	choicesLayout_->addWidget(rowWidget);
	choiceRows_.append(rowWidget);
	choiceEdits_.append(edit);

	addChoiceBtn_->setEnabled(choiceEdits_.size() < 6);
}

void VoteManagerDialog::onRemoveChoice(int index)
{
	if (choiceRows_.size() <= 1)
		return; // 最低 1 択は残す

	auto *rowWidget = choiceRows_.takeAt(index);
	choiceEdits_.removeAt(index);
	choicesLayout_->removeWidget(rowWidget);
	rowWidget->deleteLater();

	updateChoiceKeys();
	addChoiceBtn_->setEnabled(choiceEdits_.size() < 6);
}

void VoteManagerDialog::updateChoiceKeys()
{
	for (int i = 0; i < choiceRows_.size(); ++i) {
		auto *rl = qobject_cast<QHBoxLayout *>(choiceRows_.at(i)->layout());
		if (!rl)
			continue;
		if (auto *lbl = qobject_cast<QLabel *>(rl->itemAt(0)->widget()))
			lbl->setText(QString(CHOICE_KEYS.at(i)) + ":");
		if (auto *edit = choiceEdits_.at(i))
			edit->setPlaceholderText(QString("選択肢 %1 のテキスト").arg(CHOICE_KEYS.at(i)));
	}
}

void VoteManagerDialog::onStartVote()
{
	const QString question = questionEdit_->text().trimmed();
	if (question.isEmpty()) {
		QMessageBox::warning(this, "入力エラー", "質問文を入力してください。");
		return;
	}

	const QString mode = choiceModeBtn_->isChecked() ? "choice" : "free";
	QList<QPair<QString, QString>> choices;

	if (mode == "choice") {
		for (int i = 0; i < choiceEdits_.size(); ++i) {
			const QString label = choiceEdits_.at(i)->text().trimmed();
			if (label.isEmpty()) {
				QMessageBox::warning(
					this, "入力エラー",
					QString("選択肢 %1 のテキストを入力してください。")
						.arg(CHOICE_KEYS.at(i)));
				return;
			}
			choices.append({QString(CHOICE_KEYS.at(i)), label});
		}
	}

	emit voteStartRequested(question, mode, choices);
	startBtn_->setEnabled(false);
	endBtn_->setEnabled(true);
}

void VoteManagerDialog::updateResults(const QString &question, const QString &mode, int total,
				      const QList<VoteResultItem> &results, bool active)
{
	startBtn_->setEnabled(!active);
	endBtn_->setEnabled(active);

	if (question.isEmpty()) {
		resultQuestionLabel_->setText("（アンケート未開始）");
		resultStatusLabel_->clear();
		resultTotalLabel_->clear();
		return;
	}

	resultQuestionLabel_->setText("質問: " + question);
	resultStatusLabel_->setText(
		active ? "<span style='color:#2d7;font-weight:bold;'>● 受付中</span>"
		       : "<span style='color:#d44;font-weight:bold;'>■ 受付終了</span>");
	resultTotalLabel_->setText(QString("投票総数: %1票").arg(total));

	if (mode == "choice") {
		resultStack_->setCurrentIndex(0);

		// 既存バー行をクリア
		while (QLayoutItem *item = choiceResultLayout_->takeAt(0)) {
			if (auto *w = item->widget())
				w->deleteLater();
			delete item;
		}

		for (const auto &r : results) {
			auto *rowW = new QWidget(choiceResultWidget_);
			auto *rvl  = new QVBoxLayout(rowW);
			rvl->setContentsMargins(0, 2, 0, 2);
			rvl->setSpacing(2);

			auto *infoRow = new QHBoxLayout;
			auto *nameL   = new QLabel(r.key + ": " + r.label, rowW);
			auto *cntL    = new QLabel(
				QString("%1票 %2%").arg(r.count).arg(r.percent, 0, 'f', 1), rowW);
			cntL->setStyleSheet("color:gray;");
			infoRow->addWidget(nameL, 1);
			infoRow->addWidget(cntL);

			auto *bar = new QProgressBar(rowW);
			bar->setRange(0, 1000);
			bar->setValue(qRound(r.percent * 10.0));
			bar->setTextVisible(false);
			bar->setFixedHeight(8);
			bar->setStyleSheet(
				"QProgressBar{background:rgba(128,128,128,0.25);border-radius:3px;border:none;}"
				"QProgressBar::chunk{background:#5af;border-radius:3px;}");

			rvl->addLayout(infoRow);
			rvl->addWidget(bar);
			choiceResultLayout_->addWidget(rowW);
		}

	} else {
		resultStack_->setCurrentIndex(1);
		freeResultTable_->setRowCount(0);
		for (const auto &r : results) {
			const int row = freeResultTable_->rowCount();
			freeResultTable_->insertRow(row);
			freeResultTable_->setItem(row, 0, new QTableWidgetItem(r.label));
			freeResultTable_->setItem(row, 1,
						   new QTableWidgetItem(QString::number(r.count)));
			freeResultTable_->setItem(
				row, 2,
				new QTableWidgetItem(
					QString("%1%").arg(r.percent, 0, 'f', 1)));
		}
	}
}
