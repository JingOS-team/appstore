/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *   SPDX-FileCopyrightText: 2018 Abhijeet Sharma <sharma.abhijeet2096@gmail.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef FWUPDTRANSACTION_H
#define FWUPDTRANSACTION_H

#include <Transaction/Transaction.h>
#include "FwupdBackend.h"
#include "FwupdResource.h"


class FwupdResource;
class FwupdTransaction : public Transaction
{
    Q_OBJECT
public:
    FwupdTransaction(FwupdResource* app, FwupdBackend* backend);
    ~FwupdTransaction();
    void cancel() override;
    void proceed() override;

private Q_SLOTS:
    void updateProgress();
    void finishTransaction();
    void fwupdInstall(const QString &file);

private:
    void install();

    FwupdResource* const m_app;
    FwupdBackend* const m_backend;
};

#endif // FWUPDTRANSACTION_H
