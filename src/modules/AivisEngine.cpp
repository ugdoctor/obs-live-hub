/*
obs-live-hub
Copyright (C) 2026 ugdoctor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "AivisEngine.hpp"

#include <QProcess>

static QProcess *s_process = nullptr;

void AivisEngine::start(const QString &exePath)
{
	if (s_process && s_process->state() != QProcess::NotRunning)
		return;

	delete s_process;
	s_process = new QProcess();
	s_process->start(exePath, {"--allow_origin", "*"});
}

void AivisEngine::stop()
{
	if (!s_process)
		return;

	s_process->terminate();
	if (!s_process->waitForFinished(3000))
		s_process->kill();

	delete s_process;
	s_process = nullptr;
}

bool AivisEngine::isRunning()
{
	return s_process && s_process->state() != QProcess::NotRunning;
}

QProcess *AivisEngine::process()
{
	return s_process;
}
