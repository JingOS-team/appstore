/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *                           2021 Zhang He Gang <zhanghegang@jingos.com>
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef PACKAGEKITBACKEND_H
#define PACKAGEKITBACKEND_H

#include "PackageKitResource.h"
#include <resources/AbstractResourcesBackend.h>
#include <QVariantList>
#include <QStringList>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <QSharedPointer>
#include <QThreadPool>
#include <PackageKit/Transaction>
#include <AppStreamQt/pool.h>
#include <QString>
#include <Category/Category.h>
#include <packageserverresourcemanager.h>
#include <QHash>

class AppPackageKitResource;
class PackageKitUpdater;
class OdrsReviewsBackend;
class PKResultsStream;
class PKResolveTransaction;

class DISCOVERCOMMON_EXPORT PackageKitBackend : public AbstractResourcesBackend
{
    Q_OBJECT
public:
    explicit PackageKitBackend(QObject* parent = nullptr);
    ~PackageKitBackend() override;

    AbstractBackendUpdater* backendUpdater() const override;
    AbstractReviewsBackend* reviewsBackend() const override;
    QSet<AbstractResource*> resourcesByPackageName(const QString& name) const;

    ResultsStream* search(const AbstractResourcesBackend::Filters & search) override;
    PKResultsStream* findResourceByPackageName(const QUrl& search);
    int updatesCount() const override;
    bool hasSecurityUpdates() const override;

    Transaction* installApplication(AbstractResource* app) override;
    Transaction* installApplication(AbstractResource* app, const AddonList& addons) override;
    Transaction* removeApplication(AbstractResource* app) override;
    bool isValid() const override {
        return true;
    }
    QSet<AbstractResource*> upgradeablePackages() const;
    bool isFetching() const override;

    bool isPackageNameUpgradeable(const PackageKitResource* res) const;
    QString upgradeablePackageId(const PackageKitResource* res) const;
    QVector<AppPackageKitResource*> extendedBy(const QString& id) const;

    void resolvePackages(const QStringList &packageNames);
    void fetchDetails(const QString& pkgid) {
        fetchDetails(QSet<QString> {pkgid});
    }
    void fetchDetails(const QSet<QString>& pkgid);

    void checkForUpdates() override;
    void refreshCache() override;
    QString displayName() const override;

    bool hasApplications() const override {
        return true;
    }
    static QString locateService(const QString &filename);

    QList<AppStream::Component> componentsById(const QString &id) const;
    void fetchUpdates();
    int fetchingUpdatesProgress() const override;

    void addPackageArch(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary);
    void addPackageNotArch(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary);
    void addPackageForPackageKit(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary);


public Q_SLOTS:
    void reloadPackageList();
    void loadServerPackageList();
    void transactionError(PackageKit::Transaction::Error, const QString& message);

private Q_SLOTS:
    void getPackagesFinished();
    void addPackage(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary, bool arch);
    void packageDetails(const PackageKit::Details& details);
    void addPackageToUpdate(PackageKit::Transaction::Info, const QString& pkgid, const QString& summary);
    void getUpdatesFinished(PackageKit::Transaction::Exit,uint);

Q_SIGNALS:
    void loadedAppStream();
    void available();
public:
   bool isNetworking = false;
private:
    friend class PackageKitResource;
    template <typename T>
    T resourcesByPackageNames(const QStringList& names) const;

    void runWhenInitialized(const std::function<void()> &f, QObject* stream);
    void runWhenLoadedCache(const std::function<void()> &f, QObject* stream);

    void checkDaemonRunning();
    void acquireFetching(bool f);
    void includePackagesToAdd();
    void performDetailsFetch();
    ResultsStream *getAppList(QString category,QString keyword,PKResultsStream * stream);
    void loadLocalPackageData(QString category,QString keyword,PKResultsStream *stream);
    void searchPackagekitResources();
    void showResource();
    AppPackageKitResource* addComponent(const AppStream::Component& component, const QStringList& pkgNames);
    void updateProxy();

    QScopedPointer<AppStream::Pool> m_appdata;
    PackageKitUpdater* m_updater;
    QPointer<PackageKit::Transaction> m_refresher;
    int m_isFetching;
    QSet<QString> m_updatesPackageId;
    QSet<QString> m_packageKitId;
    bool m_hasSecurityUpdates = false;
    QSet<PackageKitResource*> m_packagesToAdd;
    QSet<PackageKitResource*> m_packagesToDelete;
    bool m_appstreamInitialized = true;//false;

    struct {
        QHash<QString, AbstractResource*> packages;
        QHash<QString, QStringList> packageToApp;
        QHash<QString, QVector<AppPackageKitResource*>> extendedBy;
        QHash<QString, AbstractResource*> installsApplications;
    } m_packages;

    QTimer m_delayedDetailsFetch;
    QSet<QString> m_packageNamesToFetchDetails;
    QSharedPointer<OdrsReviewsBackend> m_reviews;
    QPointer<PackageKit::Transaction> m_getUpdatesTransaction;
    QThreadPool m_threadPool;
    QPointer<PKResolveTransaction> m_resolveTransaction;
    PackageServerResourceManager* m_packageServerResourceManager;
    bool isLoaded = false;
    QMetaObject::Connection ec;
    QMetaObject::Connection sc;
    bool isRefresh = false;
    int loadPackageTime = 0;

};

#endif // PACKAGEKITBACKEND_H
