#ifndef HOOK_H
#define HOOK_H

#include <QString>
#include <QList>
#include <QPair>

#include "dpi.h"

int applySettingsFromUser(const QString &devicePath, int colorMode, int pollingRate, bool angleSnap, int keyRespTime, int sleepTime, int deepSleepTime, bool rippleControl, const int dpiValues[6], int activeDpiStage);
int getBatteryInfo(const QString &devicePath);
QList<QPair<QString, QString>> getDevices(bool allDevices);
QString findAttackSharkDevice();

#endif // HOOK_H
