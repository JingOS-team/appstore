/*
 *   SPDX-FileCopyrightText: 2017 Aleix Pol Gonzalez <aleixpol@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef APPSTREAMINTEGRATION_H
#define APPSTREAMINTEGRATION_H

#include "discovercommon_export.h"
#include <QObject>
#include <KOSRelease>
#include "OdrsReviewsBackend.h"

class DISCOVERCOMMON_EXPORT AppStreamIntegration : public QObject
{
    Q_OBJECT
public:
    static AppStreamIntegration* global();

    QSharedPointer<OdrsReviewsBackend> reviews();
    KOSRelease* osRelease() {
        return &m_osrelease;
    }

private:
    QWeakPointer<OdrsReviewsBackend> m_reviews;
    KOSRelease m_osrelease;

    AppStreamIntegration() {}
};

#endif // APPSTREAMINTEGRATION_H
