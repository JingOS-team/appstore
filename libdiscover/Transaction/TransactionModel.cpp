/*
 *   SPDX-FileCopyrightText: 2012 Jonathan Thomas <echidnaman@kubuntu.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "TransactionModel.h"

// Qt includes
#include <QDebug>
#include <QMetaProperty>
#include <KLocalizedString>

// Own includes
#include "resources/AbstractResource.h"
#include "libdiscover_debug.h"

Q_GLOBAL_STATIC(TransactionModel, globalTransactionModel)

TransactionModel *TransactionModel::global()
{
    return globalTransactionModel;
}

TransactionModel::TransactionModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &TransactionModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TransactionModel::countChanged);
    connect(this, &TransactionModel::countChanged, this, &TransactionModel::progressChanged);
}

QHash< int, QByteArray > TransactionModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TransactionRoleRole] = "transactionRole";
    roles[TransactionStatusRole] = "status";
    roles[CancellableRole] = "cancellable";
    roles[ProgressRole] = "progress";
    roles[StatusTextRole] = "statusText";
    roles[ResourceRole] = "resource";
    roles[TransactionRole] = "transaction";
    return roles;
}

int TransactionModel::rowCount(const QModelIndex &parent) const
{
    // Root element parents all children
    if (!parent.isValid())
        return m_transactions.size();

    // Child elements have no children themselves
    return 0;
}

QVariant TransactionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    Transaction *trans = m_transactions[index.row()];
    switch (role) {
    case TransactionRoleRole:
        return trans->role();
    case TransactionStatusRole:
        return trans->status();
    case CancellableRole:
        return trans->isCancellable();
    case ProgressRole:
        return trans->progress();
    case StatusTextRole:
        switch (trans->status()) {
        case Transaction::SetupStatus:
            return i18nc("@info:status", "Starting");
        case Transaction::QueuedStatus:
            return i18nc("@info:status", "Waiting");
        case Transaction::DownloadingStatus:
            return i18nc("@info:status", "Downloading");
        case Transaction::CommittingStatus:
            switch (trans->role()) {
            case Transaction::InstallRole:
                return i18nc("@info:status", "Installing");
            case Transaction::RemoveRole:
                return i18nc("@info:status", "Removing");
            case Transaction::ChangeAddonsRole:
                return i18nc("@info:status", "Changing Addons");
            }
            break;
        case Transaction::DoneStatus:
            return i18nc("@info:status", "Done");
        case Transaction::DoneWithErrorStatus:
            return i18nc("@info:status", "Failed");
        case Transaction::CancelledStatus:
            return i18nc("@info:status", "Cancelled");
        }
        break;
    case TransactionRole:
        return QVariant::fromValue<QObject*>(trans);
    case ResourceRole:
        return QVariant::fromValue<QObject*>(trans->resource());
    }

    return QVariant();
}

Transaction *TransactionModel::transactionFromResource(AbstractResource *resource) const
{
    Transaction *ret = nullptr;

    Q_FOREACH (Transaction *trans, m_transactions) {
        if (trans->resource() == resource) {
            ret = trans;
            break;
        }
    }

    return ret;
}

QModelIndex TransactionModel::indexOf(Transaction *trans) const
{
    int row = m_transactions.indexOf(trans);
    QModelIndex ret = index(row);
    Q_ASSERT(!trans || ret.isValid());
    return ret;
}

QModelIndex TransactionModel::indexOf(AbstractResource *res) const
{
    Transaction *trans = transactionFromResource(res);

    return indexOf(trans);
}

void TransactionModel::addTransaction(Transaction *trans)
{
    if (!trans)
        return;

    if (m_transactions.contains(trans))
        return;

    if (m_transactions.isEmpty())
        emit startingFirstTransaction();

    int before = m_transactions.size();
    beginInsertRows(QModelIndex(), before, before + 1);
    m_transactions.append(trans);
    endInsertRows();

    connect(trans, &Transaction::statusChanged, this, [this]() {
        transactionChanged(StatusTextRole);
    });
    connect(trans, &Transaction::cancellableChanged, this, [this]() {
        transactionChanged(CancellableRole);
    });
    connect(trans, &Transaction::progressChanged, this, [this]() {
        transactionChanged(ProgressRole);
        Q_EMIT progressChanged();
    });

    emit transactionAdded(trans);
}

void TransactionModel::removeTransaction(Transaction *trans, bool isDestroy)
{
    Q_ASSERT(trans);
    trans->deleteLater();
    int r = m_transactions.indexOf(trans);
    if (r<0) {
        qCWarning(LIBDISCOVER_LOG) << "transaction not part of the model" << trans;
        return;
    }

    disconnect(trans, nullptr, this, nullptr);

    beginRemoveRows(QModelIndex(), r, r);
    m_transactions.removeAt(r);
    endRemoveRows();

    emit transactionRemoved(trans);
    if (m_transactions.isEmpty() && !isDestroy)
        emit lastTransactionFinished();
}

void TransactionModel::transactionChanged(int role)
{
    Transaction *trans = qobject_cast<Transaction *>(sender());
    QModelIndex transIdx = indexOf(trans);
    emit dataChanged(transIdx, transIdx, {role});
}

int TransactionModel::progress() const
{
    int sum = 0;
    int count = 0;
    foreach (Transaction* t, m_transactions) {
        if (t->isActive() && t->isVisible()) {
            sum += t->progress();
            ++count;
        }
    }
    return count==0 ? 0 : sum / count;
}
