/*
 *   SPDX-FileCopyrightText: 2012 Jonathan Thomas <echidnaman@kubuntu.org
 *                           2021 Zhang He Gang <zhanghegang@jingos.com>
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "Transaction.h"

#include <resources/AbstractResource.h>
#include "TransactionModel.h"
#include <KFormat>
#include <KLocalizedString>
#include "libdiscover_debug.h"

Transaction::Transaction(QObject *parent, AbstractResource *resource,
                         Role role, const AddonList& addons)
    : QObject(parent)
    , m_resource(resource)
    , m_role(role)
    , m_status(CommittingStatus)
    , m_addons(addons)
    , m_isCancellable(true)
    , m_progress(0)
{
}

Transaction::~Transaction()
{
    if (status()<DoneStatus || TransactionModel::global()->contains(this)) {
        qCWarning(LIBDISCOVER_LOG) << "destroying Transaction before it's over" << this;
        TransactionModel::global()->removeTransaction(this,true);
    }
}

AbstractResource *Transaction::resource() const
{
    return m_resource;
}

Transaction::Role Transaction::role() const
{
    return m_role;
}

Transaction::Status Transaction::status() const
{
    return m_status;
}

AddonList Transaction::addons() const
{
    return m_addons;
}

bool Transaction::isCancellable() const
{
    return m_isCancellable;
}

int Transaction::progress() const
{
    return m_progress;
}

void Transaction::setStatus(Status status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged(m_status);

        if (m_status == DoneStatus) {
            emit transactionResult(m_resource);
        }

        if (m_status == DoneStatus || m_status == CancelledStatus || m_status == DoneWithErrorStatus) {
            setCancellable(false);

            TransactionModel::global()->removeTransaction(this);
        }
    }
}

void Transaction::setCancellable(bool isCancellable)
{
    if (m_isCancellable != isCancellable) {
        m_isCancellable = isCancellable;
        emit cancellableChanged(m_isCancellable);
    }
}

void Transaction::setProgress(int progress)
{
    if (m_progress != progress) {
        Q_ASSERT(qBound(0, progress, 100) == progress);
        m_progress = qBound(0, progress, 100);
        emit progressChanged(m_progress);
    }
}

bool Transaction::isActive() const
{
    return m_status == DownloadingStatus || m_status == CommittingStatus;
}

QString Transaction::name() const
{
    return m_resource->name();
}

QVariant Transaction::icon() const
{
    return m_resource->icon();
}

bool Transaction::isVisible() const
{
    return m_visible;
}

void Transaction::setVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        Q_EMIT visibleChanged(visible);
    }
}

void Transaction::setDownloadSpeed(quint64 downloadSpeed)
{
    if (downloadSpeed != m_downloadSpeed) {
        m_downloadSpeed = downloadSpeed;
        Q_EMIT downloadSpeedChanged(downloadSpeed);
    }
}

QString Transaction::downloadSpeedString() const
{
    return i18nc("@label Download rate", "%1/s", KFormat().formatByteSize(downloadSpeed()));
}

void Transaction::setRemainingTime(uint remainingTime)
{
    if (remainingTime != m_remainingTime) {
        m_remainingTime = remainingTime;
        Q_EMIT remainingTimeChanged(remainingTime);
    }
}

QString Transaction::remainingTimeString() const
{
    return KFormat().formatSpelloutDuration(m_remainingTime * 1000);
}
