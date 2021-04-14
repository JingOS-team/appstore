/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <ReviewsBackend/AbstractReviewsBackend.h>
#include <ReviewsBackend/Review.h>
#include <resources/ResourcesModel.h>
#include <resources/AbstractResourcesBackend.h>
#include <resources/AbstractResource.h>
#include "libdiscover_debug.h"

ReviewsModel::ReviewsModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_app(nullptr)
    , m_backend(nullptr)
    , m_lastPage(0)
    , m_canFetchMore(true)
{}

ReviewsModel::~ReviewsModel() = default;

QHash< int, QByteArray > ReviewsModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
    roles.insert(ShouldShow, "shouldShow");
    roles.insert(Reviewer, "reviewer");
    roles.insert(CreationDate, "date");
    roles.insert(UsefulnessTotal, "usefulnessTotal");
    roles.insert(UsefulnessFavorable, "usefulnessFavorable");
    roles.insert(UsefulChoice, "usefulChoice");
    roles.insert(Rating, "rating");
    roles.insert(Summary, "summary");
    roles.insert(Depth, "depth");
    roles.insert(PackageVersion, "packageVersion");
    return roles;
}

QVariant ReviewsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();
    switch (role) {
    case Qt::DisplayRole:
        return m_reviews.at(index.row())->reviewText();
    case ShouldShow:
        return m_reviews.at(index.row())->shouldShow();
    case Reviewer:
        return m_reviews.at(index.row())->reviewer();
    case CreationDate:
        return m_reviews.at(index.row())->creationDate();
    case UsefulnessTotal:
        return m_reviews.at(index.row())->usefulnessTotal();
    case UsefulnessFavorable:
        return m_reviews.at(index.row())->usefulnessFavorable();
    case UsefulChoice:
        return m_reviews.at(index.row())->usefulChoice();
    case Rating:
        return m_reviews.at(index.row())->rating();
    case Summary:
        return m_reviews.at(index.row())->summary();
    case PackageVersion:
        return m_reviews.at(index.row())->packageVersion();
    case Depth:
        return m_reviews.at(index.row())->getMetadata(QStringLiteral("NumberOfParents")).toInt();
    }
    return QVariant();
}

int ReviewsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_reviews.count();
}

AbstractResource* ReviewsModel::resource() const
{
    return m_app;
}

AbstractReviewsBackend* ReviewsModel::backend() const
{
    return m_backend;
}

void ReviewsModel::setResource(AbstractResource* app)
{
    if (m_app!=app) {
        beginResetModel();
        m_reviews.clear();
        m_lastPage = 0;

        if (m_backend) {
            disconnect(m_backend, &AbstractReviewsBackend::reviewsReady, this, &ReviewsModel::addReviews);
        }
        m_app = app;
        m_backend = app ? app->backend()->reviewsBackend() : nullptr;
        if (m_backend) {
            connect(m_backend, &AbstractReviewsBackend::reviewsReady, this, &ReviewsModel::addReviews);

            QMetaObject::invokeMethod(this, "restartFetching", Qt::QueuedConnection);
        }
        endResetModel();
        emit rowsChanged();
        emit resourceChanged();
    }
}

void ReviewsModel::restartFetching()
{
    if (!m_app || !m_backend)
        return;

    m_canFetchMore=true;
    m_lastPage = 0;
    fetchMore();
    emit rowsChanged();
}

void ReviewsModel::fetchMore(const QModelIndex& parent)
{
    if (!m_backend || !m_app || parent.isValid() || m_backend->isFetching() || !m_canFetchMore)
        return;

    m_lastPage++;
    m_backend->fetchReviews(m_app, m_lastPage);
//     qCDebug(LIBDISCOVER_LOG) << "fetching reviews... " << m_lastPage;
}

void ReviewsModel::addReviews(AbstractResource* app, const QVector<ReviewPtr>& reviews, bool canFetchMore)
{
    if (app!=m_app)
        return;

    m_canFetchMore = canFetchMore;
//     qCDebug(LIBDISCOVER_LOG) << "reviews arrived..." << m_lastPage << reviews.size();

    if (!reviews.isEmpty()) {
        beginInsertRows(QModelIndex(), rowCount(), rowCount()+reviews.size()-1);
        m_reviews += reviews;
        endInsertRows();
        emit rowsChanged();
    }
}

bool ReviewsModel::canFetchMore(const QModelIndex& /*parent*/) const
{
    return m_canFetchMore;
}

void ReviewsModel::markUseful(int row, bool useful)
{
    Review* r = m_reviews[row].data();
    r->setUsefulChoice(useful ? Yes : No);
//     qCDebug(LIBDISCOVER_LOG) << "submitting usefulness" << r->applicationName() << r->id() << useful;
    m_backend->submitUsefulness(r, useful);
    const QModelIndex ind = index(row, 0, QModelIndex());
    emit dataChanged(ind, ind, {UsefulnessTotal, UsefulnessFavorable, UsefulChoice});
}

void ReviewsModel::deleteReview(int row)
{
    Review* r = m_reviews[row].data();
    m_backend->deleteReview(r);
}

void ReviewsModel::flagReview(int row, const QString& reason, const QString& text)
{
    Review* r = m_reviews[row].data();
    m_backend->flagReview(r, reason, text);
}
