/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *   SPDX-FileCopyrightText: 2013 Lukas Appelhans <l.appelhans@gmx.de>
 *                           2021 Zhang He Gang <zhanghegang@jingos.com>
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "PackageKitResource.h"
#include "PackageKitBackend.h"
#include "PackageKitMessages.h"
#include <KLocalizedString>
#include <PackageKit/Daemon>
#include <PackageKit/Transaction>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <utils.h>
#include "config-paths.h"

#if defined(WITH_MARKDOWN)
extern "C" {
#include <mkdio.h>
}
#endif

const QStringList PackageKitResource::m_objects({ QStringLiteral("qrc:/qml/DependenciesButton.qml") });

PackageKitResource::PackageKitResource(QString packageName, QString summary, PackageKitBackend* parent)
    : AbstractResource(parent)
    , m_summary(std::move(summary))
    , m_packageName(std::move(packageName))
{
    setObjectName(m_packageName);

    connect(this, &PackageKitResource::dependenciesFound, this, [this](const QJsonObject& obj) {
        setDependenciesCount(obj.size());
    });
    connect(this, &PackageKitResource::openAppStatusChanged, this, &PackageKitResource::onOpenAppSlots);
}

QString PackageKitResource::name() const
{
    if (m_name == "") {
        return m_packageName;
    }
    return m_name;
}

QString PackageKitResource::packageName() const
{
    return m_packageName;
}

QStringList PackageKitResource::allPackageNames() const
{
    return { m_packageName };
}

QString PackageKitResource::availablePackageId() const
{
    //First we check if it's upgradeable and use this version to display
    const QString pkgid = backend()->upgradeablePackageId(this);
    if (!pkgid.isEmpty())
        return pkgid;

    QMap<PackageKit::Transaction::Info, QStringList>::const_iterator it = m_packages.constFind(PackageKit::Transaction::InfoAvailable);
    if (it != m_packages.constEnd())
        return it->last();
    return installedPackageId();
}

QString PackageKitResource::installedPackageId() const
{
    const auto installed = m_packages[PackageKit::Transaction::InfoInstalled];
    return installed.isEmpty() ? QString() : installed.last();
}

void PackageKitResource::invokeApplication() const
{
    if (isOpenningApp) {
        return;
    }
    Q_EMIT this->openAppStatusChanged(true);
    auto trans = PackageKit::Daemon::getFiles({installedPackageId()});
    connect(trans, &PackageKit::Transaction::errorCode, backend(), &PackageKitBackend::transactionError);
    connect(trans, &PackageKit::Transaction::errorCode, this, &PackageKitResource::openAppError);
    connect(trans, &PackageKit::Transaction::files, this, [this](const QString &/*packageID*/, const QStringList &_filenames) {
        //This workarounds bug in zypper's backend (suse) https://github.com/hughsie/PackageKit/issues/351
        QStringList filenames = _filenames;
        if (filenames.count() == 1 && !QFile::exists(filenames.constFirst())) {
            filenames = filenames.constFirst().split(QLatin1Char(';'));
        }
        
        const auto locations = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        const auto desktopFiles = kFilter<QStringList>(filenames, [locations](const QString &exe) {
            for (const auto &location: locations) {
                if (exe.startsWith(location) && exe.contains(QLatin1String(".desktop"))) {
                    QFile exeFile(exe);
                    return exeFile.exists();
                }
            }
            return false;
        });
        qDebug()<<Q_FUNC_INFO<<" invokeapplicaiton desktopFiles:"<< desktopFiles;
        Q_EMIT this->openAppStatusChanged(false);
        if (!desktopFiles.isEmpty()) {
            QProcess::startDetached(QStringLiteral(CMAKE_INSTALL_FULL_LIBEXECDIR_KF5 "/discover/runservice"), { desktopFiles });
            return;
        }

        Q_EMIT backend()->passiveMessage(i18n("Cannot launch %1", name()));
    });
}

void PackageKitResource::onOpenAppSlots(const bool isOpen)
{
    isOpenningApp = isOpen;
}

void PackageKitResource::openAppError(PackageKit::Transaction::Error, const QString& message)
{
    Q_EMIT this->openAppStatusChanged(false);
}

QString PackageKitResource::comment()
{
    if (m_comment == "") {
        return m_summary;
    }
    return m_comment;
}

QString PackageKitResource::longDescription()
{
    fetchDetails();
    return m_details.description();
}

QUrl PackageKitResource::homepage()
{
    fetchDetails();
    return QUrl(m_details.url());
}

QVariant PackageKitResource::icon() const
{
    qDebug()<<Q_FUNC_INFO<<" currentIcon::"<< m_icon << " name:"<< name();
    if (!m_icon.isValid() || m_icon == "") {
        return "qrc:/img/ic_app_list_empty.png";
    }
    return m_icon;
}

QJsonArray PackageKitResource::licenses()
{
    fetchDetails();

    if (!m_details.license().isEmpty()) {
        return { QJsonObject {{ QStringLiteral("name"), m_details.license() }} };
    }

    return { QJsonObject {{ QStringLiteral("name"), {} }} };
}

QList<PackageState> PackageKitResource::addonsInformation()
{
    return QList<PackageState>();
}

QString PackageKitResource::availableVersion() const
{
    return PackageKit::Daemon::packageVersion(availablePackageId());
}

QString PackageKitResource::installedVersion() const
{
    return PackageKit::Daemon::packageVersion(installedPackageId());
}

int PackageKitResource::size()
{
    fetchDetails();
    return m_details.size();
}

QString PackageKitResource::origin() const
{
    auto pkgid = availablePackageId();
    return PackageKit::Daemon::packageData(pkgid);
}

QString PackageKitResource::section()
{
    return QString();
}

AbstractResource::State PackageKitResource::state()
{
    if (backend()->isPackageNameUpgradeable(this))
        return Upgradeable;
    else if (m_packages.contains(PackageKit::Transaction::InfoInstalled))
        return Installed;
    else if (m_packages.contains(PackageKit::Transaction::InfoAvailable))
        return None;
    else
        return Broken;
}

void PackageKitResource::addPackageId(PackageKit::Transaction::Info info, const QString &packageId, bool arch)
{
    if (info == PackageKit::Transaction::InfoAvailable && m_packages.contains(PackageKit::Transaction::InfoInstalled)) {
        m_packages.remove(PackageKit::Transaction::InfoInstalled);
    }
    auto oldState = state();
    if (arch)
        m_packages[info].append(packageId);
    else
        m_packages[info].prepend(packageId);

    if (oldState != state())
        emit stateChanged();

    Q_EMIT versionsChanged();
}

QStringList PackageKitResource::categories()
{
    return { QStringLiteral("Unknown") };
}

AbstractResource::Type PackageKitResource::type() const
{
    return m_type;
}

void PackageKitResource::fetchDetails()
{
    const QString pkgid = availablePackageId();
    if (!m_details.isEmpty() || pkgid.isEmpty())
        return;
    m_details.insert(QStringLiteral("fetching"), true);//we add an entry so it's not re-fetched.

    backend()->fetchDetails(pkgid);
}

void PackageKitResource::failedFetchingDetails(PackageKit::Transaction::Error error, const QString& msg)
{
    qWarning() << "error fetching details" << error << msg;
}

void PackageKitResource::setDependenciesCount(int deps)
{
    if (deps != m_dependenciesCount) {
        m_dependenciesCount = deps;
        Q_EMIT sizeChanged();
    }
}

void PackageKitResource::setDetails(const PackageKit::Details & details)
{
    const bool ourDetails = details.packageId() == availablePackageId();
    if (!ourDetails)
        return;

    if (m_details != details) {
        const auto oldState = state();
        const auto oldSize= size();
        m_details = details;
        if (oldState != state())
            emit stateChanged();

        if (!backend()->isFetching())
            Q_EMIT backend()->resourcesChanged(this, {"size", "homepage", "license"});

        if (oldSize != size()) {
            Q_EMIT sizeChanged();
        }
    }
}

void PackageKitResource::fetchChangelog()
{
}

void PackageKitResource::fetchUpdateDetails()
{
    const auto pkgid = availablePackageId();
    if (pkgid.isEmpty()) {
        auto a = new OneTimeAction([this] { fetchUpdateDetails(); }, this);
        connect(this, &PackageKitResource::stateChanged, a, &OneTimeAction::trigger);
        return;
    }
    PackageKit::Transaction* t = PackageKit::Daemon::getUpdateDetail(availablePackageId());
    connect(t, &PackageKit::Transaction::updateDetail, this, &PackageKitResource::updateDetail);
    connect(t, &PackageKit::Transaction::errorCode, this, [this](PackageKit::Transaction::Error err, const QString & error) {
        qWarning() << "error fetching updates:" << err << error;
        emit changelogFetched(QString());
    });
}

static void addIfNotEmpty(const QString& title, const QString& content, QString& where)
{
    if (!content.isEmpty())
        where += QLatin1String("<p><b>") + title + QLatin1String("</b>&nbsp;") + QString(content).replace(QLatin1Char('\n'), QLatin1String("<br />")) + QLatin1String("</p>");
}

QString PackageKitResource::joinPackages(const QStringList& pkgids, const QString &_sep, const QString &shadowPackage)
{
    QStringList ret;
    foreach (const QString& pkgid, pkgids) {
        const auto pkgname = PackageKit::Daemon::packageName(pkgid);
        if (pkgname == shadowPackage)
            ret += PackageKit::Daemon::packageVersion(pkgid);
        else
            ret += i18nc("package-name (version)", "%1 (%2)", pkgname, PackageKit::Daemon::packageVersion(pkgid));
    }
    const QString sep = _sep.isEmpty() ? i18nc("comma separating package names", ", ") : _sep;
    return ret.join(sep);
}

static QStringList urlToLinks(const QStringList& urls)
{
    QStringList ret;
    foreach (const QString& in, urls)
        ret += QStringLiteral("<a href='%1'>%1</a>").arg(in);
    return ret;
}

void PackageKitResource::updateDetail(const QString& packageID, const QStringList& /*updates*/, const QStringList& obsoletes, const QStringList& vendorUrls,
                                      const QStringList& /*bugzillaUrls*/, const QStringList& /*cveUrls*/, PackageKit::Transaction::Restart restart, const QString &_updateText,
                                      const QString& /*changelog*/, PackageKit::Transaction::UpdateState state, const QDateTime& /*issued*/, const QDateTime& /*updated*/)
{
#if defined(WITH_MARKDOWN)
    const QByteArray xx = _updateText.toUtf8();
    MMIOT *markdownHandle = mkd_string(xx.constData(), _updateText.size(), 0);

    QString updateText;
    if ( !mkd_compile( markdownHandle, MKD_FENCEDCODE | MKD_GITHUBTAGS | MKD_AUTOLINK ) ) {
        updateText = _updateText;
    } else {
        char *htmlDocument;
        const int size = mkd_document( markdownHandle, &htmlDocument );

        updateText = QString::fromUtf8( htmlDocument, size );
    }
    mkd_cleanup( markdownHandle );

#else
    const auto& updateText = _updateText;
#endif

    const auto name = PackageKit::Daemon::packageName(packageID);

    QString info;
    addIfNotEmpty(i18n("Obsoletes:"), joinPackages(obsoletes, {}, name), info);
    addIfNotEmpty(i18n("Release Notes:"), updateText, info);
    addIfNotEmpty(i18n("Update State:"), PackageKitMessages::updateStateMessage(state), info);
    addIfNotEmpty(i18n("Restart:"), PackageKitMessages::restartMessage(restart), info);

    if (!vendorUrls.isEmpty())
        addIfNotEmpty(i18n("Vendor:"), urlToLinks(vendorUrls).join(QLatin1String(", ")), info);

    emit changelogFetched(changelog() + info);
}

PackageKitBackend* PackageKitResource::backend() const
{
    return qobject_cast<PackageKitBackend*>(parent());
}

QString PackageKitResource::sizeDescription()
{
    if (m_dependenciesCount < 0) {
        fetchDetails();
        fetchDependencies();
    }

    if (m_dependenciesCount <= 0)
        return AbstractResource::sizeDescription();
    else
        return i18np("%2 (plus %1 dependency)", "%2 (plus %1 dependencies)", m_dependenciesCount, AbstractResource::sizeDescription());
}

QString PackageKitResource::sourceIcon() const
{
    return QStringLiteral("package-x-generic");
}

void PackageKitResource::fetchDependencies()
{
    const auto id = isInstalled() ? installedPackageId() : availablePackageId();
    if (id.isEmpty())
        return;
    m_dependenciesCount = 0;

    QSharedPointer<QJsonObject> packageDependencies(new QJsonObject);

    auto trans = PackageKit::Daemon::dependsOn(id);
    connect(trans, &PackageKit::Transaction::errorCode, this, [this](PackageKit::Transaction::Error, const QString& message) {
        qWarning() << "Transaction error: " << message << sender();
    });
    connect(trans, &PackageKit::Transaction::package, this, [packageDependencies](PackageKit::Transaction::Info /*info*/, const QString &packageID, const QString &summary) {
        (*packageDependencies)[PackageKit::Daemon::packageName(packageID)] = summary ;
    });
    connect(trans, &PackageKit::Transaction::finished, this, [this, packageDependencies](PackageKit::Transaction::Exit /*status*/) {
        Q_EMIT dependenciesFound(*packageDependencies);
    });
}

bool PackageKitResource::extendsItself() const
{
    const auto extendsResources = backend()->resourcesByPackageNames<QVector<AbstractResource*>>(extends());
    if (extendsResources.isEmpty())
        return false;

    const auto ourPackageNames = allPackageNames();
    for (auto r : extendsResources) {
        PackageKitResource* pkres = qobject_cast<PackageKitResource*>(r);
        if (pkres->allPackageNames() != ourPackageNames)
            return false;
    }
    return true;
}
