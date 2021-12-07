/*
 *   SPDX-FileCopyrightText: 2014 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "DiscoverNotifier.h"
#include "BackendNotifierFactory.h"
#include "UnattendedUpdates.h"
#include <QDebug>
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusMessage>
#include <QNetworkConfigurationManager>
#include <KLocalizedString>
#include <KNotificationJobUiDelegate>
#include <KPluginFactory>

#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>

#include "updatessettings.h"
#include "../libdiscover/utils.h"

DiscoverNotifier::DiscoverNotifier(QObject * parent)
    : QObject(parent)
{
    m_backends = BackendNotifierFactory().allBackends();
    foreach (BackendNotifierModule* module, m_backends) {
        connect(module, &BackendNotifierModule::foundUpdates, this, &DiscoverNotifier::updateStatusNotifier);
        connect(module, &BackendNotifierModule::needsRebootChanged, this, [this]() {
            if (!m_needsReboot) {
                m_needsReboot = true;
                showRebootNotification();
                Q_EMIT stateChanged();
                Q_EMIT needsRebootChanged(true);
            }
        });

        connect(module, &BackendNotifierModule::foundUpgradeAction, this, &DiscoverNotifier::foundUpgradeAction);
    }
    
    // connect(&m_timer, &QTimer::timeout, this, &DiscoverNotifier::showUpdatesNotification);
    m_timer.setSingleShot(true);
    m_timer.setInterval(1000);
    updateStatusNotifier();

    //Only fetch updates after the system is comfortably booted
    QTimer::singleShot(20000, this, &DiscoverNotifier::recheckSystemUpdateNeeded);

    m_settings = new UpdatesSettings(this);
    m_settingsWatcher = KConfigWatcher::create(m_settings->sharedConfig());
    refreshUnattended();
    connect(m_settingsWatcher.data(), &KConfigWatcher::configChanged, this, &DiscoverNotifier::refreshUnattended);
}

DiscoverNotifier::~DiscoverNotifier() = default;

void DiscoverNotifier::showDiscover()
{
    auto *job = new KIO::ApplicationLauncherJob(KService::serviceByDesktopName(QStringLiteral("org.kde.discover")));
    job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
    job->start();

    if (m_updatesAvailableNotification) {
        m_updatesAvailableNotification->close();
    }
}

void DiscoverNotifier::showDiscoverUpdates()
{
    auto *job = new KIO::CommandLauncherJob(QStringLiteral("plasma-discover"), {
        QStringLiteral("--mode"),
        QStringLiteral("update")
    });
    job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
    job->setDesktopName(QStringLiteral("org.kde.discover"));
    job->start();

    if (m_updatesAvailableNotification) {
        m_updatesAvailableNotification->close();
    }
}

void DiscoverNotifier::showUpdatesNotification()
{
    if (m_updatesAvailableNotification) {
        m_updatesAvailableNotification->close();
    }
    if (state() != NormalUpdates && state() != SecurityUpdates) {
        //it's not very helpful to notify that everything is in order
        return;
    }

    m_updatesAvailableNotification = KNotification::event(QStringLiteral("Update"), message(), {}, iconName(), nullptr, KNotification::CloseOnTimeout, QStringLiteral("discoverabstractnotifier"));
    const QString name = i18n("View Updates");
    m_updatesAvailableNotification->setDefaultAction(name);
    m_updatesAvailableNotification->setActions({name});
    connect(m_updatesAvailableNotification, QOverload<unsigned int>::of(&KNotification::activated), this, &DiscoverNotifier::showDiscoverUpdates);
}

void DiscoverNotifier::updateStatusNotifier()
{
    const bool hasSecurityUpdates = kContains(m_backends, [](BackendNotifierModule* module) {
        return module->hasSecurityUpdates();
    });
    const bool hasUpdates = hasSecurityUpdates || kContains(m_backends, [](BackendNotifierModule* module) {
        return module->hasUpdates();
    });

    if (m_hasUpdates == hasUpdates && m_hasSecurityUpdates == hasSecurityUpdates )
        return;

    m_hasSecurityUpdates = hasSecurityUpdates;
    m_hasUpdates = hasUpdates;

    if (state() != NoUpdates) {
        m_timer.start();
    }

    emit stateChanged();
}

// we only want to do unattended updates when on an ethernet or wlan network
static bool isConnectionAdequate(const QNetworkConfiguration &network)
{
    return (network.bearerType() == QNetworkConfiguration::BearerEthernet || network.bearerType() == QNetworkConfiguration::BearerWLAN);
}

void DiscoverNotifier::refreshUnattended()
{
    m_settings->read();
    const auto enabled = m_settings->useUnattendedUpdates() && m_manager->isOnline() && isConnectionAdequate(m_manager->defaultConfiguration());
    if (bool(m_unattended) == enabled)
        return;

    if (enabled) {
        m_unattended = new UnattendedUpdates(this);
    } else {
        delete m_unattended;
        m_unattended = nullptr;
    }
}


DiscoverNotifier::State DiscoverNotifier::state() const
{
    if (m_needsReboot)
        return RebootRequired;
    else if (m_isBusy)
        return Busy;
    else if (m_manager && !m_manager->isOnline())
        return Offline;
    else if (m_hasSecurityUpdates)
        return SecurityUpdates;
    else if (m_hasUpdates)
        return NormalUpdates;
    else
        return NoUpdates;
}

QString DiscoverNotifier::iconName() const
{
    switch (state()) {
    case SecurityUpdates:
        return QStringLiteral("update-high");
    case NormalUpdates:
        return QStringLiteral("update-low");
    case NoUpdates:
        return QStringLiteral("update-none");
    case RebootRequired:
        return QStringLiteral("system-reboot");
    case Offline:
        return QStringLiteral("offline");
    case Busy:
        return QStringLiteral("state-download");
    }
    return QString();
}

QString DiscoverNotifier::message() const
{
    switch (state()) {
    case SecurityUpdates:
        return i18n("Security updates available");
    case NormalUpdates:
        return i18n("Updates available");
    case NoUpdates:
        return i18n("System up to date");
    case RebootRequired:
        return i18n("Computer needs to restart");
    case Offline:
        return i18n("Offline");
    case Busy:
        return i18n("Applying unattended updates...");
    }
    return QString();
}

void DiscoverNotifier::recheckSystemUpdateNeeded()
{
    if (!m_manager) {
        m_manager = new QNetworkConfigurationManager(this);
        connect(m_manager, &QNetworkConfigurationManager::onlineStateChanged, this, &DiscoverNotifier::stateChanged);
        if (!m_manager->isOnline()) {
            emit stateChanged();
        }
    }

    foreach (BackendNotifierModule* module, m_backends)
        module->recheckSystemUpdateNeeded();

    refreshUnattended();
}

QStringList DiscoverNotifier::loadedModules() const
{
    QStringList ret;
    for (BackendNotifierModule* module : m_backends)
        ret += QString::fromLatin1(module->metaObject()->className());
    return ret;
}

void DiscoverNotifier::showRebootNotification()
{
    KNotification *notification = new KNotification(QStringLiteral("notification"), KNotification::Persistent | KNotification::DefaultEvent);
    notification->setIconName(QStringLiteral("system-software-update"));
    notification->setActions(QStringList{i18nc("@action:button", "Restart")});
    notification->setTitle(i18n("Restart is required"));
    notification->setText(i18n("The system needs to be restarted for the updates to take effect."));

    connect(notification, &KNotification::action1Activated, this, &DiscoverNotifier::reboot);

    notification->sendEvent();
}

void DiscoverNotifier::reboot()
{
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.LogoutPrompt"),
                                       QStringLiteral("/LogoutPrompt"),
                                       QStringLiteral("org.kde.LogoutPrompt"),
                                       QStringLiteral("promptReboot"))
    );
}

void DiscoverNotifier::foundUpgradeAction(UpgradeAction* action)
{
    KNotification *notification = new KNotification(QStringLiteral("distupgrade-notification"), KNotification::Persistent | KNotification::DefaultEvent);
    notification->setIconName(QStringLiteral("system-software-update"));
    notification->setActions(QStringList{i18nc("@action:button", "Upgrade")});
    notification->setTitle(i18n("Upgrade available"));
    notification->setText(i18n("New version: %1", action->description()));

    connect(notification, &KNotification::action1Activated, this, [action] () {
        action->trigger();
    });

    notification->sendEvent();
}

void DiscoverNotifier::setBusy(bool isBusy)
{
    if (isBusy == m_isBusy)
        return;

    m_isBusy = isBusy;
    Q_EMIT busyChanged(isBusy);
    Q_EMIT stateChanged();
}
