/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SNAPTRANSACTION_H
#define SNAPTRANSACTION_H

#include <resources/AbstractResource.h>
#include <Transaction/Transaction.h>
#include <QPointer>

class SnapResource;
class QSnapdRequest;
class QSnapdClient;

class SnapTransaction : public Transaction
{
    Q_OBJECT
public:
    SnapTransaction(QSnapdClient* client, SnapResource* app, Role role, AbstractResource::State newState);

    void cancel() override;
    void proceed() override;

private Q_SLOTS:
    void finishTransaction();

private:
    void setRequest(QSnapdRequest* req);
    void progressed();

    QSnapdClient * const m_client;
    SnapResource * const m_app;
    QScopedPointer<QSnapdRequest> m_request;
    const AbstractResource::State m_newState;
};

#endif // SNAPTRANSACTION_H
