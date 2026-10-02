/*---------------------------------------------------------*\
| InstanceGuard.cpp                                         |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "InstanceGuard.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace
{
    struct OwnerInfo
    {
        bool    valid   = false;
        QString token;
        qint64  beat    = 0;
    };

    OwnerInfo ReadOwner(const QString& path)
    {
        OwnerInfo info;
        QFile f(path);

        if(f.open(QIODevice::ReadOnly))
        {
            QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            info.token = o.value("token").toString();
            info.beat  = (qint64)o.value("heartbeat").toDouble();
            info.valid = !info.token.isEmpty();
        }
        return info;
    }
}

InstanceGuard::InstanceGuard(const QString& owner_file, QObject* parent)
    : QObject(parent), path(owner_file),
      token(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    timer.setInterval(HEARTBEAT_MS);
    connect(&timer, &QTimer::timeout, this, &InstanceGuard::Tick);
    timer.start();
}

InstanceGuard::~InstanceGuard()
{
    Release();
}

void InstanceGuard::Claim()
{
    released = false;
    if(!timer.isActive())
    {
        timer.start();
    }

    /*-----------------------------------------------------*\
    | Fail open: if the file can't be written we still      |
    | drive the lights rather than sit in standby forever   |
    \*-----------------------------------------------------*/
    WriteOwnerFile();
    SetOwner(true);
}

void InstanceGuard::Release()
{
    if(released)
    {
        return;
    }
    released = true;
    timer.stop();

    /*-----------------------------------------------------*\
    | Only remove the file if it is still ours, so a        |
    | standby window can take over straight away            |
    \*-----------------------------------------------------*/
    if(ReadOwner(path).token == token)
    {
        QFile::remove(path);
    }
    owner = false;
}

void InstanceGuard::Tick()
{
    if(released)
    {
        return;
    }

    OwnerInfo info  = ReadOwner(path);
    qint64    now   = QDateTime::currentMSecsSinceEpoch();

    if(info.valid && info.token == token)
    {
        WriteOwnerFile();                       /* heartbeat */
        SetOwner(true);
    }
    else if(!info.valid || (now - info.beat) > STALE_MS || info.beat > now + STALE_MS)
    {
        /*-------------------------------------------------*\
        | Nobody owns the lights (owner closed or crashed)  |
        \*-------------------------------------------------*/
        Claim();
    }
    else
    {
        SetOwner(false);
    }
}

bool InstanceGuard::WriteOwnerFile()
{
    QJsonObject o;
    o["token"]      = token;
    o["pid"]        = (double)QCoreApplication::applicationPid();
    o["heartbeat"]  = (double)QDateTime::currentMSecsSinceEpoch();

    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly))
    {
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}

void InstanceGuard::SetOwner(bool is_owner)
{
    if(owner != is_owner)
    {
        owner = is_owner;
        emit OwnershipChanged(owner);
    }
}
