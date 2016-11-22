/***************************************************************************
 *   Copyright © 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef PACKAGEKITBACKEND_H
#define PACKAGEKITBACKEND_H

#include "PackageKitResource.h"
#include <resources/AbstractResourcesBackend.h>
#include <QVariantList>
#include <QStringList>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <PackageKit/Transaction>

#ifdef NEWAPPSTREAM
#include <AppStreamQt/pool.h>
#include <AppStreamQt/provided.h>
namespace AppStream {
    typedef Provided Provides;
    typedef Pool Database;
}
namespace Appstream = AppStream;
#else
#include <AppstreamQt/database.h>
#endif

class AppstreamReviews;
class AppPackageKitResource;
class PackageKitUpdater;
class PKTransaction;

class DISCOVERCOMMON_EXPORT PackageKitBackend : public AbstractResourcesBackend
{
    Q_OBJECT
    public:
        explicit PackageKitBackend(QObject* parent = nullptr);
        ~PackageKitBackend() override;
        
        AbstractBackendUpdater* backendUpdater() const override;
        AbstractReviewsBackend* reviewsBackend() const override;
        
        QVector< AbstractResource* > allResources() const override;
        AbstractResource* resourceByPackageName(const QString& name) const override;
        QList<AbstractResource*> searchPackageName(const QString& searchText) override;
        int updatesCount() const override;
        
        void installApplication(AbstractResource* app) override;
        void installApplication(AbstractResource* app, const AddonList& addons) override;
        void removeApplication(AbstractResource* app) override;
        bool isValid() const override { return true; }
        QSet<AbstractResource*> upgradeablePackages() const;
        bool isFetching() const override;
        QList<QAction*> messageActions() const override;

        bool isPackageNameUpgradeable(const PackageKitResource* res) const;
        QString upgradeablePackageId(const PackageKitResource* res) const;
        QVector<AppPackageKitResource*> extendedBy(const QString& id) const;
        void fetchUpdates();

        void clearPackages(const QStringList &packageNames);
        void resolvePackages(const QStringList &packageNames);
        void fetchDetails(const QString& pkgid);

    public Q_SLOTS:
        void transactionCanceled(Transaction* t);
        void removeTransaction(Transaction* t);
        void reloadPackageList();
        void refreshDatabase();

    private Q_SLOTS:
        void getPackagesFinished();
        void addPackage(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary, bool arch);
        void addPackageArch(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary);
        void addPackageNotArch(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary);
        void packageDetails(const PackageKit::Details& details);
        void transactionError(PackageKit::Transaction::Error, const QString& message);
        void addPackageToUpdate(PackageKit::Transaction::Info, const QString& pkgid, const QString& summary);
        void getUpdatesFinished(PackageKit::Transaction::Exit,uint);
        void getUpdatesDetailsFinished(PackageKit::Transaction::Exit,uint);

    private:
        QSet<AbstractResource*> resourcesByPackageName(const QString& name) const;
        template <typename T>
        T resourcesByPackageNames(const QStringList& names) const;

        void addTransaction(PKTransaction* trans);
        void checkDaemonRunning();
        void acquireFetching(bool f);
        void includePackagesToAdd();
        void performDetailsFetch();
        QAction* createActionForService(const QString &filename);

        Appstream::Database m_appdata;
        QList<Transaction*> m_transactions;
        PackageKitUpdater* m_updater;
        QPointer<PackageKit::Transaction> m_refresher;
        int m_isFetching;
        QSet<QString> m_updatesPackageId;
        QSet<PackageKitResource*> m_packagesToAdd;
        QSet<PackageKitResource*> m_packagesToDelete;
        QList<QAction*> m_messageActions;

        struct Packages {
            QHash<QString, AbstractResource*> packages;
            QHash<QString, QStringList> packageToApp;
            QHash<QString, QVector<AppPackageKitResource*>> extendedBy;
            void clear() { *this = {}; }
        };

        QTimer m_delayedDetailsFetch;
        QSet<QString> m_packageNamesToFetchDetails;
        Packages m_packages;
        AppstreamReviews* const m_reviews;
};

#endif // PACKAGEKITBACKEND_H
