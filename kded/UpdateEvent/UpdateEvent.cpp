/***************************************************************************
 *   Copyright © 2009 Jonathan Thomas <echidnaman@kubuntu.org>             *
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

#include "UpdateEvent.h"

// Qt includes
#include <QtCore/QProcess>
#include <QtCore/QStringBuilder>

// KDE includes
#include <KDebug>
#include <KToolInvocation>

UpdateEvent::UpdateEvent(QObject* parent, const QString &name)
        : Event(parent, name)
        , m_checkerProcess(0)
{
}

UpdateEvent::~UpdateEvent()
{
}

void UpdateEvent::show(int updates, int securityUpdates)
{
    if (!updates && !securityUpdates) {
        return;
    }

    QString updatesText;
    QString securityText;
    QString text;
    QString icon;

    if (securityUpdates) {
        securityText = i18ncp("Notification text", "%1 security update is available",
                                                   "%1 security updates are available",
                                                   securityUpdates);
    }

    if (updates) {
        updatesText = i18ncp("Notification text", "%1 software update is available",
                                                  "%1 software updates are available",
                                                  updates);
    }

    if (securityUpdates && updates) {
        icon = "security-medium";
        text = securityText % QLatin1Char('\n') % updatesText;
    } else if (securityUpdates && !updates) {
        icon = "security-medium";
        text = securityText;
    } else {
        icon = "system-software-update";
        text = updatesText;
    }

    QStringList actions;
    actions << i18nc("Start the update", "Update");
    actions << i18nc("Button to dismiss this notification once", "Ignore for now");
    actions << i18nc("Button to make this notification never show up again",
                     "Never show again");

    if (!m_active) {
        Event::show(icon, text, actions);
    } else {
        Event::update(icon, text);
    }
}

void UpdateEvent::run()
{
    KToolInvocation::kdeinitExec("/usr/bin/muon-updater");

    Event::run();
}

void UpdateEvent::getUpdateInfo()
{
    m_checkerProcess = new QProcess(this);
    connect(m_checkerProcess, SIGNAL(finished(int)), this, SLOT(parseUpdateInfo()));
    m_checkerProcess->start("/usr/lib/update-notifier/apt-check");
}

void UpdateEvent::parseUpdateInfo()
{
    // Weirdly enough, apt-check gives output on stderr
    QByteArray line = m_checkerProcess->readAllStandardError();

    // Format updates;security
    int eqpos = line.indexOf(';');

    if (eqpos > 0) {
        QByteArray updatesString = line.left(eqpos);
        QByteArray securityString = line.right(line.size() - eqpos -2);

        int numSecurityUpdates = securityString.toInt();
        int numUpdates = updatesString.toInt() - numSecurityUpdates;

        show(numUpdates, numSecurityUpdates);
    }

    // ';' not found, apt-check broke :("
}

#include "UpdateEvent.moc"
