/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "DummyBackend.h"
#include "DummyResource.h"
#include "DummyReviewsBackend.h"
#include "DummyTransaction.h"
#include "DummySourcesBackend.h"
#include <resources/StandardBackendUpdater.h>
#include <resources/SourcesModel.h>
#include <Transaction/Transaction.h>

#include <KAboutData>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QAction>

DISCOVER_BACKEND_PLUGIN(DummyBackend)

DummyBackend::DummyBackend(QObject* parent)
    : AbstractResourcesBackend(parent)
    , m_updater(new StandardBackendUpdater(this))
    , m_reviews(new DummyReviewsBackend(this))
    , m_fetching(true)
    , m_startElements(120)
{
    QTimer::singleShot(500, this, &DummyBackend::toggleFetching);
    connect(m_reviews, &DummyReviewsBackend::ratingsReady, this, &AbstractResourcesBackend::emitRatingsReady);
    connect(m_updater, &StandardBackendUpdater::updatesCountChanged, this, &DummyBackend::updatesCountChanged);

    populate(QStringLiteral("Dummy"));
    if (!m_fetching)
        m_reviews->initialize();

    SourcesModel::global()->addSourcesBackend(new DummySourcesBackend(this));
}

void DummyBackend::populate(const QString& n)
{
    const int start = m_resources.count();
    for (int i=start; i<start+m_startElements; i++) {
        const QString name = n+QLatin1Char(' ')+QString::number(i);
        DummyResource* res = new DummyResource(name, AbstractResource::Application, this);
        res->setSize(100+(m_startElements-i));
        res->setState(AbstractResource::State(1+(i%3)));
        m_resources.insert(name.toLower(), res);
        connect(res, &DummyResource::stateChanged, this, &DummyBackend::updatesCountChanged);
    }

    for (int i=start; i<start+m_startElements; i++) {
        const QString name = QLatin1String("addon")+QString::number(i);
        DummyResource* res = new DummyResource(name, AbstractResource::Addon, this);
        res->setState(AbstractResource::State(1+(i%3)));
        res->setSize(300+(m_startElements-i));
        m_resources.insert(name, res);
        connect(res, &DummyResource::stateChanged, this, &DummyBackend::updatesCountChanged);
    }

    for (int i=start; i<start+m_startElements; i++) {
        const QString name = QLatin1String("techie")+QString::number(i);
        DummyResource* res = new DummyResource(name, AbstractResource::Technical, this);
        res->setState(AbstractResource::State(1+(i%3)));
        res->setSize(300+(m_startElements-i));
        m_resources.insert(name, res);
        connect(res, &DummyResource::stateChanged, this, &DummyBackend::updatesCountChanged);
    }
}

void DummyBackend::toggleFetching()
{
    m_fetching = !m_fetching;
//     qDebug() << "fetching..." << m_fetching;
    emit fetchingChanged();
    if (!m_fetching)
        m_reviews->initialize();
}

int DummyBackend::updatesCount() const
{
    return m_updater->updatesCount();
}

ResultsStream* DummyBackend::search(const AbstractResourcesBackend::Filters& filter)
{
    QVector<AbstractResource*> ret;
    if (!filter.resourceUrl.isEmpty())
        return findResourceByPackageName(filter.resourceUrl);
    else foreach (AbstractResource* r, m_resources) {
            if (r->type() == AbstractResource::Technical && filter.state != AbstractResource::Upgradeable) {
                continue;
            }

            if (r->state() < filter.state)
                continue;

            if (r->name().contains(filter.search, Qt::CaseInsensitive) || r->comment().contains(filter.search, Qt::CaseInsensitive))
                ret += r;
        }
    return new ResultsStream(QStringLiteral("DummyStream"), ret);
}

ResultsStream * DummyBackend::findResourceByPackageName(const QUrl& search)
{
    if (search.isLocalFile()) {
        DummyResource* res = new DummyResource(search.fileName(), AbstractResource::Technical, this);
        res->setSize(666);
        res->setState(AbstractResource::None);
        m_resources.insert(res->packageName(), res);
        connect(res, &DummyResource::stateChanged, this, &DummyBackend::updatesCountChanged);
        return new ResultsStream(QStringLiteral("DummyStream-local"), { res });
    }

    auto res = search.scheme() == QLatin1String("dummy") ? m_resources.value(search.host().replace(QLatin1Char('.'), QLatin1Char(' '))) : nullptr;
    if (!res) {
        return new ResultsStream(QStringLiteral("DummyStream"), {});
    } else
        return new ResultsStream(QStringLiteral("DummyStream"), { res });
}

AbstractBackendUpdater* DummyBackend::backendUpdater() const
{
    return m_updater;
}

AbstractReviewsBackend* DummyBackend::reviewsBackend() const
{
    return m_reviews;
}

Transaction* DummyBackend::installApplication(AbstractResource* app, const AddonList& addons)
{
    return new DummyTransaction(qobject_cast<DummyResource*>(app), addons, Transaction::InstallRole);
}

Transaction* DummyBackend::installApplication(AbstractResource* app)
{
    return new DummyTransaction(qobject_cast<DummyResource*>(app), Transaction::InstallRole);
}

Transaction* DummyBackend::removeApplication(AbstractResource* app)
{
    return new DummyTransaction(qobject_cast<DummyResource*>(app), Transaction::RemoveRole);
}

void DummyBackend::checkForUpdates()
{
    if (m_fetching)
        return;
    toggleFetching();
    populate(QStringLiteral("Moar"));
    QTimer::singleShot(500, this, &DummyBackend::toggleFetching);
}

QString DummyBackend::displayName() const
{
    return QStringLiteral("Dummy");
}

bool DummyBackend::hasApplications() const
{
    return true;
}

#include "DummyBackend.moc"
