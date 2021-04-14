/*
 *   SPDX-FileCopyrightText: 2013 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef DUMMYREVIEWSBACKEND_H
#define DUMMYREVIEWSBACKEND_H

#include <ReviewsBackend/AbstractReviewsBackend.h>
#include <ReviewsBackend/ReviewsModel.h>
#include <QMap>

class DummyBackend;
class DummyReviewsBackend : public AbstractReviewsBackend
{
    Q_OBJECT
public:
    explicit DummyReviewsBackend(DummyBackend* parent = nullptr);
    ~DummyReviewsBackend() override;

    QString userName() const override {
        return QStringLiteral("dummy");
    }
    void login() override {}
    void logout() override {}
    void registerAndLogin() override {}

    Rating* ratingForApplication(AbstractResource* app) const override;
    bool hasCredentials() const override {
        return false;
    }
    void deleteReview(Review*) override {}
    void fetchReviews(AbstractResource* app, int page = 1) override;
    bool isFetching() const override {
        return false;
    }
    void submitReview(AbstractResource*, const QString&, const QString&, const QString&) override;
    void flagReview(Review*, const QString&, const QString&) override {}
    void submitUsefulness(Review*, bool) override;

    void initialize();
    bool isResourceSupported(AbstractResource * res) const override;

Q_SIGNALS:
    void ratingsReady();

private:
    QHash<AbstractResource*, Rating*> m_ratings;
};

#endif // DUMMYREVIEWSBACKEND_H
