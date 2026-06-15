#pragma once
#include <QDockWidget>
#include <QListWidget>
#include <QVBoxLayout>

class CommentDock : public QDockWidget {
	Q_OBJECT
public:
	explicit CommentDock(QWidget *parent = nullptr);
	~CommentDock() override;

public slots:
	// EventBus のコメントイベントを受けてリストに追加する。
	// Qt::QueuedConnection 経由で呼ぶこと（非UIスレッドからは直接呼ばない）。
	void onCommentReceived(const QString &author, const QString &message);

private:
	QWidget *container_;
	QListWidget *commentList_;
	QVBoxLayout *layout_;
};
