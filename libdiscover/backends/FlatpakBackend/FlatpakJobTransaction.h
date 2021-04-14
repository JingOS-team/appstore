/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *   SPDX-FileCopyrightText: 2017 Jan Grulich <jgrulich@redhat.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef FLATPAKJOBTRANSACTION_H
#define FLATPAKJOBTRANSACTION_H

#include <Transaction/Transaction.h>
#include <QPointer>

extern "C" {
#include <flatpak.h>
#include <gio/gio.h>
#include <glib.h>
}

class FlatpakResource;
class FlatpakTransactionThread;
class FlatpakJobTransaction : public Transaction
{
    Q_OBJECT
public:
    FlatpakJobTransaction(FlatpakResource *app, Role role, bool delayStart = false);

    ~FlatpakJobTransaction();

    void cancel() override;

public Q_SLOTS:
    void finishTransaction();
    void start();

private:
    void updateProgress();

    QPointer<FlatpakResource> m_app;
    QPointer<FlatpakTransactionThread> m_appJob;
};

#endif // FLATPAKJOBTRANSACTION_H
