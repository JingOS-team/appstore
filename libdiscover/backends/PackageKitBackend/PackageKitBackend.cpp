/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *   SPDX-FileCopyrightText: 2013 Lukas Appelhans <l.appelhans@gmx.de>
 *                           2021 Zhang He Gang <zhanghegang@jingos.com>
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "PackageKitBackend.h"
#include "PackageKitSourcesBackend.h"
#include "PackageKitUpdater.h"
#include "AppPackageKitResource.h"
#include "PKTransaction.h"
#include "LocalFilePKResource.h"
#include "PKResolveTransaction.h"
#include <resources/AbstractResource.h>
#include <resources/StandardBackendUpdater.h>
#include <resources/SourcesModel.h>
#include <appstream/OdrsReviewsBackend.h>
#include <appstream/AppStreamIntegration.h>
#include <appstream/AppStreamUtils.h>
#include <QProcess>
#include <QStringList>
#include <QDebug>
#include <QStandardPaths>
#include <QFile>
#include <QAction>
#include <QMimeDatabase>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <PackageKit/Daemon>
#include <PackageKit/Offline>
#include <PackageKit/Details>
#include <KLocalizedString>
#include <KProtocolManager>

#include "utils.h"
#include "config-paths.h"
#include "libdiscover_backend_debug.h"
#include <network/HttpClient.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#define APPLIST_URL "applist"


DISCOVER_BACKEND_PLUGIN(PackageKitBackend)

template <typename T, typename W>
static void setWhenAvailable(const QDBusPendingReply<T>& pending, W func, QObject* parent)
{
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pending, parent);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
    parent, [func](QDBusPendingCallWatcher* watcher) {
        watcher->deleteLater();
        QDBusPendingReply<T> reply = *watcher;
        func(reply.value());
    });
}

QString PackageKitBackend::locateService(const QString &filename)
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("applications/")+filename);
}

PackageKitBackend::PackageKitBackend(QObject* parent)
    : AbstractResourcesBackend(parent)
    , m_appdata(new AppStream::Pool)
    , m_updater(new PackageKitUpdater(this))
    , m_refresher(nullptr)
    , m_isFetching(0)
    , m_reviews(AppStreamIntegration::global()->reviews())
{
    QTimer* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &PackageKitBackend::checkForUpdates);
    t->setInterval(60 * 60 * 1000);
    t->setSingleShot(false);
    t->start();

    m_delayedDetailsFetch.setSingleShot(true);
    m_delayedDetailsFetch.setInterval(100);
    connect(&m_delayedDetailsFetch, &QTimer::timeout, this, &PackageKitBackend::performDetailsFetch);

    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::restartScheduled, m_updater, &PackageKitUpdater::enableNeedsReboot);
    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::isRunningChanged, this, &PackageKitBackend::checkDaemonRunning);
    connect(m_reviews.data(), &OdrsReviewsBackend::ratingsReady, this, [this] {
        m_reviews->emitRatingFetched(this, kTransform<QList<AbstractResource*>>(m_packages.packages.values(), [] (AbstractResource* r) {
            return r;
        }));
    });

    auto proxyWatch = new QFileSystemWatcher(this);
    proxyWatch->addPath(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1String("/kioslaverc"));
    connect(proxyWatch, &QFileSystemWatcher::fileChanged, this, [this]() {
        KProtocolManager::reparseConfiguration();
        updateProxy();
    });

    SourcesModel::global()->addSourcesBackend(new PackageKitSourcesBackend(this));

    // reloadPackageList();

    acquireFetching(true);
    setWhenAvailable(PackageKit::Daemon::getTimeSinceAction(PackageKit::Transaction::RoleRefreshCache), [this](uint timeSince) {
        if (timeSince > 3600)
            checkForUpdates();
        else
            fetchUpdates();
        acquireFetching(false);
    }, this);

    PackageKit::Daemon::global()->setHints(QStringLiteral("locale=%1").arg(qEnvironmentVariable("LANG")));
    connect(PackageKit::Daemon::global(), &PackageKit::Daemon::networkStateChanged, this, [=]() {
        auto networkState = PackageKit::Daemon::networkState();
        emit networkStateChanged(networkState);
    });

    loadServerPackageList();
}

PackageKitBackend::~PackageKitBackend()
{
    m_threadPool.waitForDone(200);
    m_threadPool.clear();
}

void PackageKitBackend::updateProxy()
{
    if (PackageKit::Daemon::isRunning()) {
        static bool everHad = KProtocolManager::useProxy();
        if (!everHad && !KProtocolManager::useProxy())
            return;

        everHad = KProtocolManager::useProxy();
        PackageKit::Daemon::global()->setProxy(KProtocolManager::proxyFor(QStringLiteral("http")),
                                               KProtocolManager::proxyFor(QStringLiteral("https")),
                                               KProtocolManager::proxyFor(QStringLiteral("ftp")),
                                               KProtocolManager::proxyFor(QStringLiteral("socks")),
                                               {},
                                               {});
    }
}

bool PackageKitBackend::isFetching() const
{
    return m_isFetching;
}

void PackageKitBackend::acquireFetching(bool f)
{
    if (f)
        m_isFetching++;
    else {
        if(m_isFetching >= 1){
            m_isFetching--;
        }
    }

    if ((!f && m_isFetching==0) || (f && m_isFetching==1)) {
        emit fetchingChanged();
        if (m_isFetching==0)
            emit available();
    }
    Q_ASSERT(m_isFetching>=0);
}

struct DelayedAppStreamLoad {
    QVector<AppStream::Component> components;
    QHash<QString, AppStream::Component> missingComponents;
    bool correct = true;
};

static DelayedAppStreamLoad loadAppStream(AppStream::Pool* appdata)
{
    DelayedAppStreamLoad ret;

    ret.correct = appdata->load();
    if (!ret.correct) {
        qWarning() << "Could not open the AppStream metadata pool" << appdata->lastError();
    }

    const auto components = appdata->components();
    ret.components.reserve(components.size());
    QSet<QStringList> componentList;
    foreach (const AppStream::Component& component, components) {
        componentList.insert(component.packageNames());
        if (component.kind() == AppStream::Component::KindFirmware)
            continue;

        const auto pkgNames = component.packageNames();
        if (pkgNames.isEmpty()) {
            const auto entries = component.launchable(AppStream::Launchable::KindDesktopId).entries();
            if (component.kind() == AppStream::Component::KindDesktopApp && !entries.isEmpty()) {
                const QString file = PackageKitBackend::locateService(entries.first());
                if (!file.isEmpty()) {
                    ret.missingComponents[file] = component;
                }
            }
        } else {
            ret.components << component;
        }
    }
    return ret;
}

void PackageKitBackend::loadServerPackageList()
{
    m_packageServerResourceManager = new PackageServerResourceManager();

    connect(m_packageServerResourceManager, &PackageServerResourceManager::loadStart, this, [this]{
        isLoaded = false;
    });

    connect(m_packageServerResourceManager, &PackageServerResourceManager::loadFinished, this, [this]{
        isLoaded = true;
        if (isRefresh) {
           searchPackagekitResources();
           isRefresh = false;
        }
    });

    connect(m_packageServerResourceManager, &PackageServerResourceManager::loadError, this, [this](QString errorStr){
        isLoaded = true;
        if (isRefresh) {
           searchPackagekitResources();
           isRefresh = false;
        }
    });
    m_packageServerResourceManager->loadCacheData();

}

void PackageKitBackend::searchPackagekitResources()
{
    PackageKit::Transaction * searchT = PackageKit::Daemon::searchNames(m_packageServerResourceManager->m_serverPackageNames);
    connect(searchT, &PackageKit::Transaction::package, this, &PackageKitBackend::addPackageForPackageKit);

    connect(searchT, &PackageKit::Transaction::finished, this, [this](PackageKit::Transaction::Exit status) {
        getPackagesFinished();
        checkForUpdates();
    }, Qt::QueuedConnection);
}

void PackageKitBackend::refreshCache()
{
    if(m_packageServerResourceManager){
        isRefresh = true;
        m_packageServerResourceManager->refreshData();
    }
}

void PackageKitBackend::reloadPackageList()
{
    acquireFetching(true);
    if (m_refresher) {
        disconnect(m_refresher.data(), &PackageKit::Transaction::finished, this, &PackageKitBackend::reloadPackageList);
    }

    m_appdata.reset(new AppStream::Pool);

    auto fw = new QFutureWatcher<DelayedAppStreamLoad>(this);
    connect(fw, &QFutureWatcher<DelayedAppStreamLoad>::finished, this, [this, fw]() {
        const auto data = fw->result();
        fw->deleteLater();

        if (!data.correct && m_packages.packages.isEmpty()) {
            QTimer::singleShot(0, this, [this]() {
                Q_EMIT passiveMessage(i18n("Please make sure that Appstream is properly set up on your system"));
            });
        }
        for (const auto &component: data.components) {
            const auto pkgNames = component.packageNames();
            addComponent(component, pkgNames);
        }

        if (data.components.isEmpty()) {
            qCDebug(LIBDISCOVER_BACKEND_LOG) << "empty appstream db";
            if (PackageKit::Daemon::backendName() == QLatin1String("aptcc") || PackageKit::Daemon::backendName().isEmpty()) {
                checkForUpdates();
            }
        }
        if (!m_appstreamInitialized) {
            m_appstreamInitialized = true;
            Q_EMIT loadedAppStream();
        }
        acquireFetching(false);
    });
    fw->setFuture(QtConcurrent::run(&m_threadPool, &loadAppStream, m_appdata.get()));
}

AppPackageKitResource* PackageKitBackend::addComponent(const AppStream::Component& component, const QStringList& pkgNames)
{
    Q_ASSERT(isFetching());
    Q_ASSERT(!pkgNames.isEmpty());
    auto& resPos = m_packages.packages[component.id()];
    AppPackageKitResource* res = qobject_cast<AppPackageKitResource*>(resPos);
    if (!res) {
        res = new AppPackageKitResource(component, pkgNames.at(0), this);
        resPos = res;
    } else {
        res->clearPackageIds();
    }
    foreach (const QString& pkg, pkgNames) {
        m_packages.packageToApp[pkg] += component.id();
    }

    foreach (const QString& pkg, component.extends()) {
        m_packages.extendedBy[pkg] += res;
    }
    return res;
}

void PackageKitBackend::resolvePackages(const QStringList &packageNames)
{
    if (!m_resolveTransaction) {
        m_resolveTransaction = new PKResolveTransaction(this);
        connect(m_resolveTransaction, &PKResolveTransaction::allFinished, this, &PackageKitBackend::getPackagesFinished);
        connect(m_resolveTransaction, &PKResolveTransaction::started, this, [this] {
            m_resolveTransaction = nullptr;
        });
    }

    m_resolveTransaction->addPackageNames(packageNames);
}

void PackageKitBackend::fetchUpdates()
{
    if (m_updater->isProgressing())
        return;
    m_getUpdatesTransaction = PackageKit::Daemon::getUpdates();
    connect(m_getUpdatesTransaction, &PackageKit::Transaction::finished, this, &PackageKitBackend::getUpdatesFinished);
    connect(m_getUpdatesTransaction, &PackageKit::Transaction::package, this, &PackageKitBackend::addPackageToUpdate);
    connect(m_getUpdatesTransaction, &PackageKit::Transaction::errorCode, this, &PackageKitBackend::transactionError);
    connect(m_getUpdatesTransaction, &PackageKit::Transaction::percentageChanged, this, &PackageKitBackend::fetchingUpdatesProgressChanged);
    m_updatesPackageId.clear();
    m_hasSecurityUpdates = false;

    m_updater->setProgressing(true);

    Q_EMIT fetchingUpdatesProgressChanged();
}

void PackageKitBackend::addPackageForPackageKit(PackageKit::Transaction::Info info, const QString& packageId, const QString& summary)
{
    m_packageKitId += packageId;
    addPackage(info, packageId, summary, true);
}

void PackageKitBackend::addPackageArch(PackageKit::Transaction::Info info, const QString& packageId, const QString& summary)
{
    addPackage(info, packageId, summary, true);
}

void PackageKitBackend::addPackageNotArch(PackageKit::Transaction::Info info, const QString& packageId, const QString& summary)
{
    addPackage(info, packageId, summary, false);
}

void PackageKitBackend::addPackage(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary, bool arch)
{
    QString packageArch = PackageKit::Daemon::packageArch(packageId);
    if (packageArch == QLatin1String("source")) {
        // We do not add source packages, they make little sense here. If source is needed,
        // we are going to have to consider that in some other way, some other time
        // If we do not ignore them here, e.g. openSuse entirely fails at installing applications
        return;
    }
    QString localArc = QSysInfo::currentCpuArchitecture();
    if (localArc == "x86") {
        localArc = "amd64";
    }
    if (packageArch != localArc && packageArch != "all") {
        return;
    }

    const QString packageName = PackageKit::Daemon::packageName(packageId);
    QSet<AbstractResource*> r = resourcesByPackageName(packageName);

    if (r.isEmpty()) {
        auto pk = new PackageKitResource(packageName, summary, this);
        r = { pk };
        m_packagesToAdd.insert(pk);
    }
    foreach (auto res, r)
        static_cast<PackageKitResource*>(res)->addPackageId(info, packageId, arch);
}

void PackageKitBackend::getPackagesFinished()
{
    includePackagesToAdd();
    if (m_packageKitId.size() > 0) {
        fetchDetails(m_packageKitId);
    }
    emit updatesCountChanged();
}

void PackageKitBackend::includePackagesToAdd()
{
    if (m_packagesToAdd.isEmpty() && m_packagesToDelete.isEmpty())
        return;

    acquireFetching(true);
    foreach (PackageKitResource* res, m_packagesToAdd) {
        m_packages.packages[res->packageName()] = res;
    }
    foreach (PackageKitResource* res, m_packagesToDelete) {
        const auto pkgs = m_packages.packageToApp.value(res->packageName(), {res->packageName()});
        foreach (const auto &pkg, pkgs) {
            auto res = m_packages.packages.take(pkg);
            if (res) {
                if (AppPackageKitResource* ares = qobject_cast<AppPackageKitResource*>(res)) {
                    const auto extends = res->extends();
                    for (const auto &ext: extends)
                        m_packages.extendedBy[ext].removeAll(ares);
                }

                emit resourceRemoved(res);
                res->deleteLater();
            }
        }
    }
    m_packagesToAdd.clear();
    m_packagesToDelete.clear();
    acquireFetching(false);
}

void PackageKitBackend::transactionError(PackageKit::Transaction::Error, const QString& message)
{
    qWarning() << "Transaction error: " << message << sender();
    Q_EMIT passiveMessage(message);
}

void PackageKitBackend::packageDetails(const PackageKit::Details& details)
{
    const QSet<AbstractResource*> resources = resourcesByPackageName(PackageKit::Daemon::packageName(details.packageId()));
    if (resources.isEmpty())
        qWarning() << "couldn't find package for" << details.packageId();

    foreach (AbstractResource* res, resources) {
        qobject_cast<PackageKitResource*>(res)->setDetails(details);
    }
    emit updatesCountChanged();
}

QSet<AbstractResource*> PackageKitBackend::resourcesByPackageName(const QString& name) const
{
    return resourcesByPackageNames<QSet<AbstractResource*>>({name});
}

template <typename T>
T PackageKitBackend::resourcesByPackageNames(const QStringList &pkgnames) const
{
    T ret;
    ret.reserve(pkgnames.size());
    for (const QString &name : pkgnames) {
        const QStringList names = m_packages.packageToApp.value(name, QStringList(name));
        foreach (const QString& name, names) {
            AbstractResource* res = m_packages.packages.value(name);
            if (res)
                ret += res;
        }
    }
    return ret;
}

void PackageKitBackend::checkForUpdates()
{
    if (PackageKit::Daemon::global()->offline()->updateTriggered()) {
        qCDebug(LIBDISCOVER_BACKEND_LOG) << "Won't be checking for updates again, the system needs a reboot to apply the fetched offline updates.";
        return;
    }

    if (!m_refresher) {
        acquireFetching(true);
        m_refresher = PackageKit::Daemon::refreshCache(false);

        connect(m_refresher.data(), &PackageKit::Transaction::errorCode, this, &PackageKitBackend::transactionError);
        connect(m_refresher.data(), &PackageKit::Transaction::finished, this, [this]() {
            m_refresher = nullptr;
            fetchUpdates();
            acquireFetching(false);
        });
    } else {
        qWarning() << "already resetting";
    }
}

QList<AppStream::Component> PackageKitBackend::componentsById(const QString& id) const
{
    Q_ASSERT(m_appstreamInitialized);
    return m_appdata->componentsById(id);
}

static const auto needsResolveFilter = [] (AbstractResource* res) {
    return res->state() == AbstractResource::Broken && res->type() == AbstractResource::Application;
};
static const auto installedFilter = [] (AbstractResource* res) {
    return res->state() >= AbstractResource::Installed && res->type() == AbstractResource::Application;
};

class PKResultsStream : public ResultsStream
{
public:
    PKResultsStream(PackageKitBackend* backend, const QString &name)
        : ResultsStream(name)
        , backend(backend)
    {}

    PKResultsStream(PackageKitBackend* backend, const QString &name, const QVector<AbstractResource*> &resources)
        : ResultsStream(name)
        , backend(backend)
    {
        QTimer::singleShot(0, this, [resources, this] () {
            if (!resources.isEmpty())
                setResources(resources);
            finish();
        });
    }

    void setResources(const QVector<AbstractResource*> &res)
    {
        const auto toResolve = kFilter<QVector<AbstractResource*>>(res, needsResolveFilter);
        if (!toResolve.isEmpty())
            backend->resolvePackages(kTransform<QStringList>(toResolve, [] (AbstractResource* res) {
            return res->packageName();
        }));
        Q_EMIT resourcesFound(res);
    }
private:
    PackageKitBackend* const backend;
};

ResultsStream* PackageKitBackend::search(const AbstractResourcesBackend::Filters& filter)
{
    if (!filter.resourceUrl.isEmpty()) {
        return findResourceByPackageName(filter.resourceUrl);
    } else if (!filter.extends.isEmpty()) {
        auto stream = new PKResultsStream(this, QStringLiteral("PackageKitStream-extends"));
        auto f = [this, filter, stream] {
            const auto resources = kTransform<QVector<AbstractResource*>>(m_packages.extendedBy.value(filter.extends), [](AppPackageKitResource* a) {
                return a;
            });
            if (!resources.isEmpty()) {
                stream->setResources(resources);
            }
        };
        runWhenInitialized(f, stream);
        return stream;
    } else if (filter.state == AbstractResource::Upgradeable) {
        return new ResultsStream(QStringLiteral("PackageKitStream-upgradeable"), kTransform<QVector<AbstractResource*>>(upgradeablePackages())); //No need for it to be a PKResultsStream
    } else if (filter.state == AbstractResource::Installed) {
        auto stream = new PKResultsStream(this, QStringLiteral("PackageKitStream-installed"));

        auto f = [this, stream] {
            m_packages.installsApplications.clear();
            const auto toResolve = kFilter<QVector<AbstractResource*>>(m_packages.packages, needsResolveFilter);
            if (!toResolve.isEmpty()) {
                resolvePackages(kTransform<QStringList>(toResolve, [] (AbstractResource* res) {
                    return res->packageName();
                }));
                connect(m_resolveTransaction, &PKResolveTransaction::allFinished, this, [this,stream, toResolve] {
                    const auto resolved = kFilter<QVector<AbstractResource*>>(toResolve, installedFilter);
                    const auto filterServerData = kFilter<QVector<AbstractResource*>>(resolved,
                                                                                      [this] (AbstractResource* res) {
//                                                                                     if(res->appstreamId() != ""){
//                                                                                     return false;
//                }
                                                                                     if(m_packages.installsApplications.contains(res->packageName())){
                                                                                     return false;
                }

                                                                                     m_packages.installsApplications.insert(res->packageName(),res);
                                                                                     return m_packageServerResourceManager->existPackageName(res->packageName());
                });
                    if (!filterServerData.isEmpty())
                        Q_EMIT stream->resourcesFound(filterServerData);
                    stream->finish();
                });
            }
            const auto resolved = kFilter<QVector<AbstractResource*>>(m_packages.packages, installedFilter);

            const auto filterServerData = kFilter<QVector<AbstractResource*>>(resolved,
                                                                              [this] (AbstractResource* res) {

                                                                             //                                                                             if(res->appstreamId() != ""){
                                                                             //                                                                             return false;
                                                                             //        }
                                                                             if(m_packages.installsApplications.contains(res->packageName())){
                                                                             return false;}

                                                                             m_packages.installsApplications.insert(res->packageName(),res);
                                                                             return m_packageServerResourceManager->existPackageName(res->packageName());
        });

            if (!filterServerData.isEmpty()) {
                QTimer::singleShot(0, this, [filterServerData, toResolve, stream] () {
                    if (!filterServerData.isEmpty())
                        Q_EMIT stream->resourcesFound(filterServerData);

                    if (toResolve.isEmpty())
                        stream->finish();
                });
            }else {
                if (toResolve.isEmpty()){
                    stream->finish();
                }
            }
        };
        if(!isLoaded){
            runWhenLoadedCache(f,stream);
        } else {
            runWhenInitialized(f, stream);
        }
        return stream;
    } else if (filter.search.isEmpty()) {
        auto stream = new PKResultsStream(this, QStringLiteral("PackageKitStream-all"));
        auto f = [this, filter, stream] {
            getAppList(filter.category->typeName(),filter.search,stream);
            disconnect(ec);
            disconnect(sc);
        };

        if(!isLoaded){
            runWhenLoadedCache(f,stream);
        } else {
            runWhenInitialized(f, stream);
        }
        return stream;
    } else {
        auto stream = new PKResultsStream(this, QStringLiteral("PackageKitStream-search"));
        const auto f = [this, stream, filter] () {
            getAppList("",filter.search,stream);
            disconnect(ec);
            disconnect(sc);

            const QList<AppStream::Component> components = m_appdata->search(filter.search);
            const QStringList ids = kTransform<QStringList>(components, [](const AppStream::Component& comp) {
                return comp.id();
            });
            if (!ids.isEmpty()) {
                const auto resources = kFilter<QVector<AbstractResource*>>(resourcesByPackageNames<QVector<AbstractResource*>>(ids), [](AbstractResource* res) {
                    return !qobject_cast<PackageKitResource*>(res)->extendsItself();
                });
            }

            PackageKit::Transaction * tArch = PackageKit::Daemon::resolve(filter.search, PackageKit::Transaction::FilterArch);
            connect(tArch, &PackageKit::Transaction::package, this, &PackageKitBackend::addPackageArch);
            connect(tArch, &PackageKit::Transaction::package, stream, [stream](PackageKit::Transaction::Info /*info*/, const QString &packageId) {
                stream->setProperty("packageId", packageId);
            });
            connect(tArch, &PackageKit::Transaction::finished, stream, [stream, ids, this](PackageKit::Transaction::Exit status) {
                getPackagesFinished();
                if (status == PackageKit::Transaction::Exit::ExitSuccess) {
                    const auto packageId = stream->property("packageId");
                    if (!packageId.isNull()) {
                        const auto res = resourcesByPackageNames<QVector<AbstractResource*>>({PackageKit::Daemon::packageName(packageId.toString())});
                    }
                }
            }, Qt::QueuedConnection);
        };
        if(!isLoaded){
            runWhenLoadedCache(f,stream);
        } else {
            runWhenInitialized(f, stream);
        }
        return stream;
    }
}

void PackageKitBackend::runWhenInitialized(const std::function<void ()>& f, QObject* stream)
{
    if (!m_appstreamInitialized) {
        connect(this, &PackageKitBackend::loadedAppStream, stream, f);
    } else {
        QTimer::singleShot(0, this, f);
    }
}

void PackageKitBackend::runWhenLoadedCache(const std::function<void ()>& f, QObject* stream)
{
    disconnect(ec);
    disconnect(sc);
    auto onErrored = [this,f,stream]{
        runWhenInitialized(f, stream);
    };
    auto onFinished =  [this,f,stream]{
        runWhenInitialized(f, stream);
    };
    sc = connect(m_packageServerResourceManager, &PackageServerResourceManager::loadFinished, this,onFinished);
    ec = connect(m_packageServerResourceManager, &PackageServerResourceManager::loadError, this, onErrored);
}

PKResultsStream * PackageKitBackend::findResourceByPackageName(const QUrl& url)
{
    if (url.isLocalFile()) {
        QMimeDatabase db;
        const auto mime = db.mimeTypeForUrl(url);
        if (    mime.inherits(QStringLiteral("application/vnd.debian.binary-package"))
                || mime.inherits(QStringLiteral("application/x-rpm"))
                || mime.inherits(QStringLiteral("application/x-tar"))
                || mime.inherits(QStringLiteral("application/x-zstd-compressed-tar"))
                || mime.inherits(QStringLiteral("application/x-xz-compressed-tar"))
           ) {
            return new PKResultsStream(this, QStringLiteral("PackageKitStream-localpkg"), { new LocalFilePKResource(url, this)});
        }
    } else if (url.scheme() == QLatin1String("appstream")) {
        static const QMap<QString, QString> deprecatedAppstreamIds = {
            { QStringLiteral("org.kde.krita.desktop"), QStringLiteral("krita.desktop") },
            { QStringLiteral("org.kde.digikam.desktop"), QStringLiteral("digikam.desktop") },
            { QStringLiteral("org.kde.ktorrent.desktop"), QStringLiteral("ktorrent.desktop") },
            { QStringLiteral("org.kde.gcompris.desktop"), QStringLiteral("gcompris.desktop") },
            { QStringLiteral("org.kde.kmymoney.desktop"), QStringLiteral("kmymoney.desktop") },
            { QStringLiteral("org.kde.kolourpaint.desktop"), QStringLiteral("kolourpaint.desktop") },
            { QStringLiteral("org.blender.blender.desktop"), QStringLiteral("blender.desktop") },
        };

        const auto appstreamIds = AppStreamUtils::appstreamIds(url);
        if (appstreamIds.isEmpty())
            Q_EMIT passiveMessage(i18n("Malformed appstream url '%1'", url.toDisplayString()));
        else {
            auto stream = new PKResultsStream(this, QStringLiteral("PackageKitStream-appstream-url"));
            const auto f = [this, appstreamIds, stream] () {
                AbstractResource* pkg = nullptr;

                QStringList allAppStreamIds = appstreamIds;
                {
                    auto it = deprecatedAppstreamIds.constFind(appstreamIds.first());
                    if (it != deprecatedAppstreamIds.constEnd()) {
                        allAppStreamIds << *it;
                    }
                }

                for (auto it = m_packages.packages.constBegin(), itEnd = m_packages.packages.constEnd(); it != itEnd; ++it) {
                    const bool matches = kContains(allAppStreamIds, [&it] (const QString& id) {
                        static const QLatin1String desktopPostfix(".desktop");
                        return it.key().compare(id, Qt::CaseInsensitive) == 0 ||
                               //doing (id == id.key()+".desktop") without allocating
                               (id.size() == (desktopPostfix.size() + it.key().size()) && id.endsWith(desktopPostfix) && id.startsWith(it.key(), Qt::CaseInsensitive));
                    });
                    if (matches) {
                        pkg = it.value();
                        break;
                    }
                }
                if (pkg)
                    stream->setResources({pkg});
                stream->finish();
                //             if (!pkg)
                //                 qCDebug(LIBDISCOVER_BACKEND_LOG) << "could not find" << host << deprecatedHost;
            };
            runWhenInitialized(f, stream);
            return stream;
        }
    }
    return new PKResultsStream(this, QStringLiteral("PackageKitStream-unknown-url"), {});
}

bool PackageKitBackend::hasSecurityUpdates() const
{
    return m_hasSecurityUpdates;
}

int PackageKitBackend::updatesCount() const
{
    if (PackageKit::Daemon::global()->offline()->updateTriggered())
        return 0;

    int ret = 0;
    QSet<QString> packages;
    const auto toUpgrade = upgradeablePackages();
    for (auto res: toUpgrade) {
        const auto packageName = res->packageName();
        if (packages.contains(packageName)) {
            continue;
        }
        packages.insert(packageName);
        ret += 1;
    }
    return ret;
}

Transaction* PackageKitBackend::installApplication(AbstractResource* app, const AddonList& addons)
{
    Transaction* t = nullptr;
    if (!addons.addonsToInstall().isEmpty()) {
        QVector<AbstractResource*> appsToInstall = resourcesByPackageNames<QVector<AbstractResource*>>(addons.addonsToInstall());
        if (!app->isInstalled())
            appsToInstall << app;
        t = new PKTransaction(appsToInstall, Transaction::ChangeAddonsRole);
    } else if (!app->isInstalled())
        t = installApplication(app);

    if (!addons.addonsToRemove().isEmpty()) {
        const auto appsToRemove = resourcesByPackageNames<QVector<AbstractResource*>>(addons.addonsToRemove());
        t = new PKTransaction(appsToRemove, Transaction::RemoveRole);
    }

    return t;
}

Transaction* PackageKitBackend::installApplication(AbstractResource* app)
{
    return new PKTransaction({app}, Transaction::InstallRole);
}

Transaction* PackageKitBackend::removeApplication(AbstractResource* app)
{
    Q_ASSERT(!isFetching());
    if (!qobject_cast<PackageKitResource*>(app)) {
        Q_EMIT passiveMessage(i18n("Cannot remove '%1'", app->name()));
        return nullptr;
    }
    return new PKTransaction({app}, Transaction::RemoveRole);
}

QSet<AbstractResource*> PackageKitBackend::upgradeablePackages() const
{
    if (isFetching() || !m_packagesToAdd.isEmpty()) {
        return {};
    }

    QSet<AbstractResource*> ret;
    ret.reserve(m_updatesPackageId.size());
    Q_FOREACH (const QString& pkgid, m_updatesPackageId) {
        const QString pkgname = PackageKit::Daemon::packageName(pkgid);
        const auto pkgs = resourcesByPackageName(pkgname);
        if (pkgs.isEmpty()) {
            qWarning() << "couldn't find resource for" << pkgid;
        }
        ret.unite(pkgs);
    }
    QList<AbstractResource*> listRet =  ret.values();

    return kFilter<QSet<AbstractResource*>>(ret, [this] (AbstractResource* res) {
                                           bool isExistPkgName = m_packageServerResourceManager->existPackageName(res->packageName());
        return !static_cast<PackageKitResource*>(res)->extendsItself() && res->type() == AbstractResource::Application && isExistPkgName;
    });
}

void PackageKitBackend::addPackageToUpdate(PackageKit::Transaction::Info info, const QString& packageId, const QString& summary)
{
    if (info == PackageKit::Transaction::InfoBlocked) {
        return;
    }

    if (info == PackageKit::Transaction::InfoRemoving || info == PackageKit::Transaction::InfoObsoleting) {
        // Don't try updating packages which need to be removed
        return;
    }

    if (info == PackageKit::Transaction::InfoSecurity)
        m_hasSecurityUpdates = true;
    if(packageId.contains("qterm")){
        qDebug()<<Q_FUNC_INFO<<" qterm:::" << packageId;
    }
    m_updatesPackageId += packageId;
    addPackage(info, packageId, summary, true);
}

void PackageKitBackend::getUpdatesFinished(PackageKit::Transaction::Exit, uint)
{
    if (!m_updatesPackageId.isEmpty()) {
        resolvePackages(kTransform<QStringList>(m_updatesPackageId, [](const QString &pkgid) {
            return PackageKit::Daemon::packageName(pkgid);
        }));
        fetchDetails(m_updatesPackageId);
    }

    m_updater->setProgressing(false);

    includePackagesToAdd();
    if (isFetching()) {
        auto a = new OneTimeAction([this] {
            emit updatesCountChanged();
        }, this);
        connect(this, &PackageKitBackend::available, a, &OneTimeAction::trigger);
    } else
        emit updatesCountChanged();
}

bool PackageKitBackend::isPackageNameUpgradeable(const PackageKitResource* res) const
{
    return !upgradeablePackageId(res).isEmpty();
}

QString PackageKitBackend::upgradeablePackageId(const PackageKitResource* res) const
{
    QString name = res->packageName();
    foreach (const QString& pkgid, m_updatesPackageId) {
        if (PackageKit::Daemon::packageName(pkgid) == name)
            return pkgid;
    }
    return QString();
}

void PackageKitBackend::fetchDetails(const QSet<QString>& pkgid)
{
    if (!m_delayedDetailsFetch.isActive()) {
        m_delayedDetailsFetch.start();
    }

    m_packageNamesToFetchDetails += pkgid;
}

void PackageKitBackend::performDetailsFetch()
{
    Q_ASSERT(!m_packageNamesToFetchDetails.isEmpty());
    const auto ids = m_packageNamesToFetchDetails.values();

    PackageKit::Transaction* transaction = PackageKit::Daemon::getDetails(ids);
    connect(transaction, &PackageKit::Transaction::details, this, &PackageKitBackend::packageDetails);
    connect(transaction, &PackageKit::Transaction::errorCode, this, &PackageKitBackend::transactionError);
    m_packageNamesToFetchDetails.clear();
}

void PackageKitBackend::checkDaemonRunning()
{
    if (!PackageKit::Daemon::isRunning()) {
        qWarning() << "PackageKit stopped running!";
    } else
        updateProxy();
}

AbstractBackendUpdater* PackageKitBackend::backendUpdater() const
{
    return m_updater;
}

QVector<AppPackageKitResource*> PackageKitBackend::extendedBy(const QString& id) const
{
    return m_packages.extendedBy[id];
}

AbstractReviewsBackend* PackageKitBackend::reviewsBackend() const
{
    return m_reviews.data();
}

QString PackageKitBackend::displayName() const
{
    return AppStreamIntegration::global()->osRelease()->prettyName();
}

int PackageKitBackend::fetchingUpdatesProgress() const
{
    if (!m_getUpdatesTransaction)
        return 0;

    if (m_getUpdatesTransaction->status() == PackageKit::Transaction::StatusWait || m_getUpdatesTransaction->status() == PackageKit::Transaction::StatusUnknown) {
        return m_getUpdatesTransaction->property("lastPercentage").toInt();
    }
    int percentage = percentageWithStatus(m_getUpdatesTransaction->status(), m_getUpdatesTransaction->percentage(),2);
    m_getUpdatesTransaction->setProperty("lastPercentage", percentage);
    return percentage;
}

void PackageKitBackend::showResource()
{
}

void PackageKitBackend::loadLocalPackageData(QString category,QString keyword,PKResultsStream *stream)
{
     QList<ServerData> categoriesData;
    if (keyword != "") {
        categoriesData =  m_packageServerResourceManager->resourceByKeyword(keyword);
    }else {
        categoriesData =  m_packageServerResourceManager->resourceByCategory(category);
    }
    QStringList notFindResources;
    QVector<AbstractResource*> localdisplayRes;
    foreach(ServerData itemData , categoriesData){
        QString itemPackageName = itemData.appName;
        QSet<AbstractResource*> originResource = resourcesByPackageName(itemPackageName);
        if(originResource.isEmpty()){
            notFindResources.append(itemPackageName);
            continue;
        }
        QList<AbstractResource*> listResources = originResource.values();
        foreach(AbstractResource* listItem , listResources){
            listItem->setAppId(itemData.appId);
            listItem->setBanner(itemData.banner);
            listItem->setIcon(itemData.icon);
            listItem->setName(itemData.name);
            listItem->setAppName(itemData.appName);
            listItem->setCategoryDisplay(itemData.categoryDisplay);
            listItem->setComment(itemData.comment);
            localdisplayRes.append(listItem);
        }
    }
    if (notFindResources.size() <= 0) {
        stream->setResources(localdisplayRes);
        stream->finish();
        return;
    }
    stream->setResources(localdisplayRes);
    m_packageKitId.clear();

    PackageKit::Transaction * searchT = PackageKit::Daemon::searchNames(notFindResources);
    connect(searchT, &PackageKit::Transaction::package, this, &PackageKitBackend::addPackageForPackageKit);

    connect(searchT,&PackageKit::Transaction::errorCode,this,[this,stream] {
        stream->finish();
    });
    connect(searchT, &PackageKit::Transaction::finished, this, [this,stream,notFindResources](PackageKit::Transaction::Exit status) {
        getPackagesFinished();
        if (status == PackageKit::Transaction::Exit::ExitSuccess) {
            QVector<AbstractResource*> displayRes;

            foreach(QString pkgname,notFindResources){
                ServerData pkgVaule = m_packageServerResourceManager->resourceByName(pkgname);
                QSet<AbstractResource*> res = resourcesByPackageName(pkgname);
                if (res.count() > 0) {
                    AbstractResource* getResource = res.values().first();
                    getResource->setAppId(pkgVaule.appId);
                    getResource->setBanner(pkgVaule.banner);
                    getResource->setIcon(pkgVaule.icon);
                    getResource->setName(pkgVaule.name);
                    getResource->setAppName(pkgVaule.appName);
                    getResource->setCategoryDisplay(pkgVaule.categoryDisplay);
                    getResource->setComment(pkgVaule.comment);
                    displayRes.append(getResource);
                }
            }
            stream->setResources(displayRes);
            stream->finish();
        }
    }, Qt::QueuedConnection);
}

ResultsStream *PackageKitBackend::getAppList(QString category,QString keyword,PKResultsStream *stream)
{
    QString url;
    url = QLatin1String(BASE_URL) + QLatin1String(APPLIST_URL);
    QString requestParam = QLatin1String("category");
   
    if (category == QLatin1String("feature_applications")) {
        requestParam = "label";
        category = "recommend";
        if(isNetworking){
            return stream;
        }
        isNetworking = true;
        HttpClient::global() -> get(url)
    .header(QString::fromUtf8("content-type"), QString::fromUtf8("application/json"))
    .queryParam(requestParam, category)
    .onResponse([this,stream](QByteArray result) {
        isNetworking = false;
        auto json = QJsonDocument::fromJson(result).object();
        if (json.empty()) {
            stream->finish();
            return;
        }
        auto httpCode = json.value(QString::fromUtf8("code")).toInt();
        if (httpCode != 200) {
            stream->finish();
            return;
        }
        auto appList = json.value(QString::fromUtf8("apps")).toArray();
        if (appList.size() < 1) {
            stream->finish();
            return;
        }
        QStringList notResources;
        QHash<QString,ServerData> cacheRequest;
        QVector<AbstractResource*> displayRes;
        for (int i = 0; i < appList.size(); i++) {
            auto appObj = appList.at(i).toObject();
            auto appId = appObj.value(QString::fromUtf8("appId")).toString();
            auto appName = appObj.value(QString::fromUtf8("appName")).toString();
            auto icon = appObj.value(QString::fromUtf8("icon")).toString();
            auto banner = appObj.value(QString::fromUtf8("banner")).toString();
            auto categories = appObj.value(QString::fromUtf8("categories")).toArray();
            auto display = appObj.value(QString::fromUtf8("display")).toArray();
            auto resource = m_packages.packages.value(appName);
            QString name = "";
            QString comment = "";
            for (int j = 0; j < display.size(); j++) {
                auto displayObj = display.at(j).toObject();
                QString lang = "";
                if (QLocale::system().bcp47Name().startsWith("zh")) {
                    lang = "cn";
                } else {
                    lang = "en";
                }
                if (lang == displayObj.value(QString::fromUtf8("lang")).toString()) {
                    name = displayObj.value(QString::fromUtf8("name")).toString();
                    comment = displayObj.value(QString::fromUtf8("summary")).toString();
                }
            }
            QString categoryDisplay = "";
            for (int j = 0; j < categories.size(); j++) {
                categoryDisplay += categories.at(j).toString();
                if (j != categories.size() - 1) {
                    categoryDisplay += ",";
                }
            }
            if (resource) {
                resource->setAppId(appId);
                resource->setBanner(banner);
                resource->setIcon(icon);
                resource->setName(name);
                resource->setAppName(appName);
                resource->setCategoryDisplay(categoryDisplay);
                resource->setComment(comment);
                displayRes.append(resource);
            } else {
                ServerData currentData;
                currentData.appId = appId;
                currentData.banner = banner;
                currentData.categoryDisplay = categoryDisplay;
                currentData.comment = comment;
                currentData.icon = icon;
                currentData.name = name;
                currentData.appName = appName;
                notResources.append(appName);
                cacheRequest.insert(appName,currentData);
            }
        }
        if (notResources.size() <= 0) {
            stream->setResources(displayRes);
            stream->finish();
            return;
        }
        stream->setResources(displayRes);
        m_packageKitId.clear();
        //,PackageKit::Transaction::FilterApplication
        loadPackageTime =  QDateTime::currentMSecsSinceEpoch();
        PackageKit::Transaction * searchT = PackageKit::Daemon::resolve(notResources);
        connect(searchT, &PackageKit::Transaction::package, this, &PackageKitBackend::addPackageForPackageKit);

        connect(searchT,&PackageKit::Transaction::errorCode,this,[this,stream] (PackageKit::Transaction::Error error, const QString &details) {
            qWarning()<<Q_FUNC_INFO << " stream Transaction error:" << details;
            stream->finish();
        });
        connect(searchT, &PackageKit::Transaction::finished, this, [this,stream,cacheRequest,notResources](PackageKit::Transaction::Exit status) {
            getPackagesFinished();
            if (status == PackageKit::Transaction::Exit::ExitSuccess) {
                QVector<AbstractResource*> displayRes;

                for (auto it = cacheRequest.constBegin(), itEnd = cacheRequest.constEnd(); it != itEnd; ++it) {
                    QString pkgKey = it.key();
                    ServerData pkgVaule = it.value();
                    QSet<AbstractResource*> res = resourcesByPackageName(pkgKey);
                    if (res.count() > 0) {
                        AbstractResource* getResource = res.values().first();
                        getResource->setAppId(pkgVaule.appId);
                        getResource->setBanner(pkgVaule.banner);
                        getResource->setIcon(pkgVaule.icon);
                        getResource->setName(pkgVaule.name);
                        getResource->setAppName(pkgVaule.appName);
                        getResource->setCategoryDisplay(pkgVaule.categoryDisplay);
                        getResource->setComment(pkgVaule.comment);
                        displayRes.append(getResource);
                    }
                }
                qWarning()<<Q_FUNC_INFO << " stream Transaction finished";
                stream->setResources(displayRes);
                stream->finish();
                int loadNetDataAndCacheEndtime = QDateTime::currentMSecsSinceEpoch();
            }
        }, Qt::QueuedConnection);

    })
    .onError([this,stream](QString errorStr) {
        qWarning()<<Q_FUNC_INFO << " stream applist busy onError:" << errorStr;
        isNetworking = false;
        stream->finish();
        return;
    })
    .timeout(10 * 1000)
    .exec();

    }
   else {
       loadLocalPackageData(category,keyword,stream);
   }
    return stream;
}

#include "PackageKitBackend.moc"
