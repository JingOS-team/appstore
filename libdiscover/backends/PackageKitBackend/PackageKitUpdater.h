/*
 *   SPDX-FileCopyrightText: 2013 Lukas Appelhans <l.appelhans@gmx.de>
 *                           2021 Zhang He Gang <zhanghegang@jingos.com>
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
#ifndef PACKAGEKITUPDATER_H
#define PACKAGEKITUPDATER_H

#include <resources/AbstractBackendUpdater.h>
#include "PackageKitBackend.h"
#include <PackageKit/Transaction>
#include "PKTransaction.h"

class SystemUpgrade;

int percentageWithStatus(PackageKit::Transaction::Status status, uint percentage,int opreationTag);

class PackageKitUpdater : public AbstractBackendUpdater
{
    Q_OBJECT
public:
    explicit PackageKitUpdater(PackageKitBackend * parent = nullptr);
    ~PackageKitUpdater() override;

    void prepare() override;

    bool hasUpdates() const override;
    qreal progress() const override;

    void setProgressing(bool progressing);

    void removeResources(const QList<AbstractResource*>& apps) override;
    void addResources(const QList<AbstractResource*>& apps) override;
    Transaction* updateResource(AbstractResource* app) override;
    QList<AbstractResource*> toUpdate() const override;
    bool isMarked(AbstractResource* res) const override;
    QDateTime lastUpdate() const override;
    bool isCancelable() const override;
    bool isProgressing() const override;
    void fetchChangelog() const override;
    double updateSize() const override;
    quint64 downloadSpeed() const override;

    void proceed() override;
    void setOfflineUpdates(bool use) override;

public Q_SLOTS:
    ///must be implemented if ever isCancelable is true
    void cancel() override;
    void start() override;

private Q_SLOTS:
    void errorFound(PackageKit::Transaction::Error err, const QString& error);
    void mediaChange(PackageKit::Transaction::MediaType media, const QString& type, const QString& text);
    void eulaRequired(const QString &eulaID, const QString &packageID, const QString &vendor, const QString &licenseAgreement);
    void finished(PackageKit::Transaction::Exit exit, uint);
    void cancellableChanged();
    void percentageChanged();
    void updateDetail(const QString& packageID, const QStringList& updates, const QStringList& obsoletes, const QStringList& vendorUrls,
                      const QStringList& bugzillaUrls, const QStringList& cveUrls, PackageKit::Transaction::Restart restart, const QString& updateText,
                      const QString& changelog, PackageKit::Transaction::UpdateState state, const QDateTime& issued, const QDateTime& updated);
    void packageResolved(PackageKit::Transaction::Info info, const QString& packageId);
    void repoSignatureRequired(const QString &packageID,
                               const QString &repoName,
                               const QString &keyUrl,
                               const QString &keyUserid,
                               const QString &keyId,
                               const QString &keyFingerprint,
                               const QString &keyTimestamp,
                               PackageKit::Transaction::SigType type);

private:
    void processProceedFunction();
    void itemProgress(const QString &itemID, PackageKit::Transaction::Status status, uint percentage);
    void fetchLastUpdateTime();
    void lastUpdateTimeReceived(QDBusPendingCallWatcher* w);
    void setupTransaction(PackageKit::Transaction::TransactionFlags flags);
    bool useOfflineUpdates() const;

    QSet<QString> involvedPackages(const QSet<AbstractResource*>& packages) const;
    QSet<AbstractResource*> packagesForPackageId(const QSet<QString>& packages) const;

    QPointer<PackageKit::Transaction> m_transaction;
    PackageKitBackend * const m_backend;
    QSet<AbstractResource*> m_toUpgrade;
    QSet<AbstractResource*> m_allUpgradeable;
    bool m_isCancelable;
    bool m_isProgressing;
    bool m_useOfflineUpdates = false;
    int m_percentage;
    QDateTime m_lastUpdate;
    QMap<PackageKit::Transaction::Info, QStringList> m_packagesModified;
    QVector<std::function<PackageKit::Transaction*()>> m_proceedFunctions;

    SystemUpgrade* m_upgrade = nullptr;
};


#endif
