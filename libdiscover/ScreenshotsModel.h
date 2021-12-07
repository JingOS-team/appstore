/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SCREENSHOTSMODEL_H
#define SCREENSHOTSMODEL_H

#include <QModelIndex>
#include <QUrl>
#include "discovercommon_export.h"
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class AbstractResource;

class DISCOVERCOMMON_EXPORT ScreenshotsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(AbstractResource* application READ resource WRITE setResource NOTIFY resourceChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { ThumbnailUrl=Qt::UserRole+1, ScreenshotUrl };

    explicit ScreenshotsModel(QObject* parent = nullptr);
    QHash<int, QByteArray> roleNames() const override;

    AbstractResource* resource() const;
    void setResource(AbstractResource* res);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    Q_SCRIPTABLE QUrl screenshotAt(int row) const;
    int count() const;

    Q_INVOKABLE void remove(const QUrl &url);
    QList<QUrl> cacheLoaclScreents(QList<QUrl> thumbnails);

private Q_SLOTS:
    void screenshotsFetched(const QList<QUrl>& thumbnails, const QList<QUrl>& screenshots);
    void onCacheEnd();

Q_SIGNALS:
    void countChanged();
    void resourceChanged(const AbstractResource* resource);
    void cacheEndChanged();

private:
    AbstractResource* m_resource;
    QList<QUrl> m_thumbnails;
    QList<QUrl> m_screenshots;
    int m_loadCacheNumber;
    int m_thumberNumber;

};

#endif // SCREENSHOTSMODEL_H
