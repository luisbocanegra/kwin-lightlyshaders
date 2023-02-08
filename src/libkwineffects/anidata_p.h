/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2011 Thomas Lübking <thomas.luebking@web.de>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwinanimationeffect.h"

#include <QEasingCurve>

namespace KWin
{

/**
 * Wraps effects->setActiveFullScreenEffect for the duration of it's lifespan
 */
class FullScreenEffectLock
{
public:
    FullScreenEffectLock(Effect *effect);
    ~FullScreenEffectLock();

private:
    Q_DISABLE_COPY(FullScreenEffectLock)
};
typedef QSharedPointer<FullScreenEffectLock> FullScreenEffectLockPtr;

class KWINEFFECTS_EXPORT AniData
{
public:
    AniData();
    AniData(AnimationEffect::Attribute a, int meta, const FPx2 &to,
            int delay, const FPx2 &from, bool waitAtSource,
            FullScreenEffectLockPtr = FullScreenEffectLockPtr(),
            bool keepAlive = true, GLShader *shader = nullptr);

    bool isActive() const;

    inline bool isOneDimensional() const
    {
        return from[0] == from[1] && to[0] == to[1];
    }

    quint64 id{0};
    QString debugInfo() const;
    AnimationEffect::Attribute attribute;
    int customCurve;
    FPx2 from, to;
    TimeLine timeLine;
    uint meta;
    qint64 frozenTime;
    qint64 startTime;
    QSharedPointer<FullScreenEffectLock> fullScreenEffectLock;
    bool waitAtSource;
    bool keepAlive;
    EffectWindowDeletedRef deletedRef;
    EffectWindowVisibleRef visibleRef;
    AnimationEffect::TerminationFlags terminationFlags;
    GLShader *shader{nullptr};
};

} // namespace

QDebug operator<<(QDebug dbg, const KWin::AniData &a);
