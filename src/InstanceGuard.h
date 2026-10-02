/*---------------------------------------------------------*\
| InstanceGuard.h                                           |
|                                                           |
|   Makes sure only ONE OpenRGB window drives the lights.   |
|                                                           |
|   OpenRGB 1.0 doesn't stop a second copy from starting:   |
|   with "Start at login" + "Minimize on close" a copy      |
|   sits in the tray and opening OpenRGB again starts       |
|   another. Each copy loads this plugin, and both sending  |
|   frames made the lights flicker between two looks.       |
|                                                           |
|   A small owner file next to the settings file holds the  |
|   owning window's token plus a heartbeat:                 |
|     - the newest window claims ownership when it opens    |
|     - every other window sees a foreign token and goes    |
|       to standby (preview only, no device output)         |
|     - if the owner closes or crashes, its heartbeat stops |
|       and a standby window takes over automatically       |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class InstanceGuard : public QObject
{
    Q_OBJECT

public:
    InstanceGuard(const QString& owner_file, QObject* parent = nullptr);
    ~InstanceGuard() override;

    void    Claim();                    /* take ownership now */
    void    Release();                  /* give it up (on unload) */
    bool    IsOwner() const { return owner; }

    static constexpr int HEARTBEAT_MS   = 500;
    static constexpr int STALE_MS       = 3000;

signals:
    void    OwnershipChanged(bool is_owner);

private:
    void    Tick();
    bool    WriteOwnerFile();
    void    SetOwner(bool is_owner);

    QString path;
    QString token;
    QTimer  timer;
    bool    owner       = false;
    bool    released    = false;
};
