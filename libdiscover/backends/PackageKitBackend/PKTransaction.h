/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef PKTRANSACTION_H
#define PKTRANSACTION_H

#include <Transaction/Transaction.h>
#include <PackageKit/Transaction>
#include <QPointer>
#include <QSet>

class PKTransaction : public Transaction
{
    Q_OBJECT
public:
    explicit PKTransaction(const QVector<AbstractResource*>& app, Transaction::Role role);
    PackageKit::Transaction* transaction();

    void cancel() override;
    void proceed() override;

public Q_SLOTS:
    void start();

private:
    void processProceedFunction();
    void statusChanged();

    void cleanup(PackageKit::Transaction::Exit, uint);
    void errorFound(PackageKit::Transaction::Error err, const QString& error);
    void mediaChange(PackageKit::Transaction::MediaType media, const QString& type, const QString& text);
    void requireRestart(PackageKit::Transaction::Restart restart, const QString& p);
    void progressChanged();
    void eulaRequired(const QString &eulaID, const QString &packageID, const QString &vendor, const QString &licenseAgreement);
    void cancellableChanged();
    void packageResolved(PackageKit::Transaction::Info info, const QString& packageId);
    void submitResolve();
    void repoSignatureRequired(const QString &packageID,
                               const QString &repoName,
                               const QString &keyUrl,
                               const QString &keyUserid,
                               const QString &keyId,
                               const QString &keyFingerprint,
                               const QString &keyTimestamp,
                               PackageKit::Transaction::SigType type);

    void trigger(PackageKit::Transaction::TransactionFlags flags);
    QPointer<PackageKit::Transaction> m_trans;
    const QVector<AbstractResource*> m_apps;
    QSet<QString> m_pkgnames;
    QVector<std::function<PackageKit::Transaction*()>> m_proceedFunctions;

    QMap<PackageKit::Transaction::Info, QStringList> m_newPackageStates;
    bool isFristDownload = true;
    int opreationTag = 0;
};

#endif // PKTRANSACTION_H
