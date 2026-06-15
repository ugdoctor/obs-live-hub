#pragma once
#include <QString>

class QProcess;

namespace AivisEngine {
void       start(const QString &exePath);
void       stop();
bool       isRunning();
QProcess  *process();
}
