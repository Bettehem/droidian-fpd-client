// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2023 Droidian Project
//
// Authors:
// Bardia Moshiri <fakeshell@bardia.tech>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <batman/wlrdisplay.h>
#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>
#include <QProcess>
#include <QThread>
#include <QFile>
#include "fpdinterface.h"

extern "C" int wlrdisplay_status() {
    int result = wlrdisplay(0, NULL);
    return result != 0;
}

extern "C" int delay(double seconds) {
    return usleep(seconds * 1000000);
}

void fpdunlocker(const QString& sessionId, int &exitStatus) {
    FPDInterface fpdInterface;
    QEventLoop loop;
    exitStatus = 0;

    QObject::connect(&fpdInterface, &FPDInterface::identified, [&](const QString &finger) {
        qDebug() << "Identified finger: " << finger;

        if (wlrdisplay_status() == 0) {
            QProcess *process = new QProcess();
            QString vibraGood = "fbcli -E bell-terminal";
            QString unlock = "loginctl unlock-session " + sessionId;
            QString command = vibraGood + " && " + unlock;
            process->start("bash", QStringList() << "-c" << command);

            QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [&](int exitCode, QProcess::ExitStatus) {
                    exitStatus = exitCode;
                });
        } else {
            exitStatus = 0;
        }

        loop.quit();
    });

    QObject::connect(&fpdInterface, &FPDInterface::errorInfo, [&](const QString &info) {
        qDebug() << "Error info:" << info;

        if (info.contains("FINGER_NOT_RECOGNIZED") && wlrdisplay_status() == 0) {
            QProcess *process = new QProcess();
            QString vibraBad = "for i in {0..2}; do fbcli -E button-pressed; done";
            process->start("bash", QStringList() << "-c" << vibraBad);
            exitStatus = 1;
        } else if (info.contains("ERROR_CANCELED") && wlrdisplay_status() != 0) {
            exitStatus = 1;
        } else {
            exitStatus = 0;
        }

        loop.quit();
    });

    qDebug() << "Waiting for finger identification...";

    fpdInterface.identify();

    loop.exec();
}

extern "C" void fpdrunner(const char *sessionId) {
    int oldStat = -1;

    while (1) {
        int dispStat = wlrdisplay_status() == 0 ? 1 : 0;
        int exitStatus = 0;

        if (oldStat != dispStat) {
            oldStat = dispStat;

            if (dispStat == 1) {
                int unlocked = 0;
                while (unlocked == 0) {
                    fpdunlocker(QString(sessionId), exitStatus);
                    if (exitStatus == 0) {
                        unlocked = 1;
                    } else {
                        delay(0.5);
                    }
                }
            }
        } else {
            delay(0.2);
        }
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    system("/usr/bin/binder-wait android.hardware.biometrics.fingerprint@2.1::IBiometricsFingerprint/default");

    char sessionId[64] = {0};

    while (1) {
        FILE *fp = popen("loginctl list-sessions | awk '/tty7/{print $1}'", "r");
        if (fp) {
            fgets(sessionId, sizeof(sessionId), fp);
            pclose(fp);
        }

        if (strlen(sessionId) > 0) {
            break;
        }

        delay(0.3);
    }

    QThread *mainLoopThread = new QThread();
    QObject::connect(mainLoopThread, &QThread::started, [=](){ fpdrunner(sessionId); });

    mainLoopThread->start();

    return app.exec();
}
