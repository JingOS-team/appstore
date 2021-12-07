/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *                           2021 Zhang He Gang <zhanghegang@jingos.com>
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef APPPACKAGEKITRESOURCE_H
#define APPPACKAGEKITRESOURCE_H

#include "PackageKitResource.h"
#include "PackageKitBackend.h"

class AppPackageKitResource : public PackageKitResource
{
    Q_OBJECT
public:
    explicit AppPackageKitResource(const AppStream::Component& data, const QString &packageName, PackageKitBackend* parent);

    QString appstreamId() const override;
    AbstractResource::Type type() const override;
//    QString name() const override;
//    QVariant icon() const override;
    QStringList mimetypes() const override;
    QStringList categories() override;
    QString longDescription() override;
    QUrl homepage() override;
    QUrl helpURL() override;
    QUrl bugURL() override;
    QUrl donationURL() override;
    QString comment() override;
    QJsonArray licenses() override;
    QStringList allPackageNames() const override;
    QList<PackageState> addonsInformation() override;
    QStringList extends() const override;
    void invokeApplication() const override;
    bool canExecute() const override;
    QDate releaseDate() const override;
    QString changelog() const override;
    QString author() const override;
    void fetchChangelog() override;
    QSet<QString> alternativeAppstreamIds() const override;

private:
    const AppStream::Component m_appdata;
    mutable QString m_name;
};

#endif // APPPACKAGEKITRESOURCE_H
