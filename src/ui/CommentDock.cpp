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

#include "CommentDock.hpp"

CommentDock::CommentDock(QWidget *parent) : QDockWidget("Comment Viewer", parent)
{
	container_ = new QWidget(this);
	layout_ = new QVBoxLayout(container_);
	commentList_ = new QListWidget(container_);

	layout_->addWidget(commentList_);
	layout_->setContentsMargins(4, 4, 4, 4);
	container_->setLayout(layout_);
	setWidget(container_);
}

CommentDock::~CommentDock() {}

void CommentDock::onCommentReceived(const QString &author, const QString &message)
{
	commentList_->addItem(QString("[%1] %2").arg(author, message));
	commentList_->scrollToBottom();
}
