/*
 *   Copyright (C) 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library/Lesser General Public License
 *   version 2, or (at your option) any later version, as published by the
 *   Free Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library/Lesser General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "MuonDiscoverMainWindow.h"

// Qt includes
#include <QDebug>
#include <QtDeclarative/QDeclarativeEngine>
#include <QtDeclarative/QDeclarativeContext>
#include <QtDeclarative/QDeclarativeView>
#include <QDesktopServices>
#include <QTimer>
#include <QGraphicsObject>
#include <qdeclarative.h>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QDeclarativeNetworkAccessManagerFactory>

// #if !defined(QT_NO_OPENGL)
//     #include <QGLWidget>
// #endif

// KDE includes
#include <KActionCollection>
#include <KAction>
#include <kdeclarative.h>
#include <Plasma/Theme>
#include <KStandardDirs>

// QApt includes
#include <LibQApt/Backend>

// Libmuon includes
#include <Application.h>
#include <ApplicationBackend.h>
#include <Category/CategoryModel.h>
#include <Category/Category.h>
#include <Transaction/TransactionListener.h>
#include <Transaction/Transaction.h>
#include <ReviewsBackend/AbstractReviewsBackend.h>
#include <ReviewsBackend/Rating.h>
#include <ApplicationModel/LaunchListModel.h>
#include <ApplicationModel/ApplicationModel.h>
#include <resources/ResourcesModel.h>
#include <resources/ResourcesUpdatesModel.h>

// Own includes
#include "ApplicationProxyModelHelper.h"
#include "BackendsSingleton.h"
#include "ReviewsModel.h"
#include "OriginsBackend.h"
#include "ApplicationAddonsModel.h"
#include "ScreenshotsModel.h"
#include "KNSBackend/KNSBackend.h"
#include <libmuon/MuonDataSources.h>

QML_DECLARE_TYPE(ResourcesModel)
QML_DECLARE_TYPE(AbstractResourcesBackend)


class CachedNetworkAccessManager : public QNetworkAccessManager {
    public:
        explicit CachedNetworkAccessManager(QObject* parent = 0) : QNetworkAccessManager(parent) {}
    
        virtual QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoingData = 0) {
            QNetworkRequest req(request);
            req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
            return QNetworkAccessManager::createRequest(op, request, outgoingData);
        }
};

class CachedNAMFactory : public QDeclarativeNetworkAccessManagerFactory
{
    virtual QNetworkAccessManager* create(QObject* parent) {
        CachedNetworkAccessManager* ret = new CachedNetworkAccessManager(parent);
        QString cacheDir = KStandardDirs::locateLocal("cache", "screenshotsCache", true);
        QNetworkDiskCache* cache = new QNetworkDiskCache(ret);
        cache->setCacheDirectory(cacheDir);
        ret->setCache(cache);
        return ret;
    }
};

MuonDiscoverMainWindow::MuonDiscoverMainWindow()
    : MuonMainWindow()
{
    m_view = new QDeclarativeView(this);
    m_view->setBackgroundRole(QPalette::Background);
    
    Plasma::Theme::defaultTheme()->setUseGlobalSettings(false); //don't change every plasma theme!
    Plasma::Theme::defaultTheme()->setThemeName("appdashboard");
    
    KDeclarative kdeclarative;
    kdeclarative.setDeclarativeEngine(m_view->engine());
    kdeclarative.initialize();
    //binds things like kconfig and icons
    kdeclarative.setupBindings();
    
    qmlRegisterType<CategoryModel>("org.kde.muon", 1, 0, "CategoryModel");
    qmlRegisterType<ApplicationProxyModelHelper>("org.kde.muon", 1, 0, "ApplicationProxyModel");
    qmlRegisterType<TransactionListener>("org.kde.muon", 1, 0, "TransactionListener");
    qmlRegisterType<ReviewsModel>("org.kde.muon", 1, 0, "ReviewsModel");
    qmlRegisterType<LaunchListModel>("org.kde.muon", 1, 0, "LaunchListModel");
    qmlRegisterType<OriginsBackend>("org.kde.muon", 1, 0, "OriginsBackend");
    qmlRegisterType<ApplicationAddonsModel>("org.kde.muon", 1, 0, "ApplicationAddonsModel");
    qmlRegisterType<ScreenshotsModel>("org.kde.muon", 1, 0, "ScreenshotsModel");
    qmlRegisterType<ResourcesUpdatesModel>("org.kde.muon", 1, 0, "ResourcesUpdatesModel");
    qmlRegisterType<AbstractReviewsBackend>();
    qmlRegisterType<Rating>();
    qmlRegisterType<Application>();
    qmlRegisterType<AbstractResource>();
    qmlRegisterType<AbstractResourcesBackend>();
    qmlRegisterType<Category>();
    qmlRegisterType<ResourcesModel>();
    qmlRegisterType<Source>();
    qmlRegisterType<Entry>();
    qmlRegisterType<Transaction>();
    
    connect(this, SIGNAL(backendReady(QApt::Backend*)), SLOT(setBackend(QApt::Backend*)));
    
    //Here we set up a cache for the screenshots
//     m_view->engine()->setNetworkAccessManagerFactory(new CachedNAMFactory);
    ResourcesModel* resourcesModel = BackendsSingleton::self()->appsModel();
    m_view->engine()->rootContext()->setContextProperty("resourcesModel", qVariantFromValue<ResourcesModel*>(resourcesModel));
    m_view->engine()->rootContext()->setContextProperty("app", this);
    m_view->setResizeMode(QDeclarativeView::SizeRootObjectToView);
// #if !defined(QT_NO_OPENGL)
//     m_view->setViewport(new QGLWidget);
// #endif
    QTimer::singleShot(10, this, SLOT(initObject()));
    setupActions();
    m_view->setSource(QUrl("qrc:/qml/Main.qml"));
    if(!m_view->errors().isEmpty()) {
        QString errors;

        for (const QDeclarativeError &error : m_view->errors()) {
            errors.append(error.toString() + QLatin1String("\n\n"));
        }

        QVariantMap args;
        args["ErrorText"] = errors;
        args["FromWorker"] = false;
        errorOccurred(QApt::InitError, args);
        qDebug() << "errors: " << m_view->errors();
    }
    Q_ASSERT(m_view->errors().isEmpty());

    KConfigGroup window(componentData().config(), "Window");
    restoreGeometry(window.readEntry<QByteArray>("geometry", QByteArray()));
    restoreState(window.readEntry<QByteArray>("windowState", QByteArray()));
    
    setCentralWidget(m_view);
    resourcesModel->addResourcesBackend(new KNSBackend("comic.knsrc", "face-smile-big", this));
}

MuonDiscoverMainWindow::~MuonDiscoverMainWindow()
{
    KConfigGroup window(componentData().config(), "Window");
    window.writeEntry("geometry", saveGeometry());
    window.writeEntry("windowState", saveState());
    window.sync();
}

void MuonDiscoverMainWindow::setBackend(QApt::Backend* b)
{
    if (!m_view->rootObject())
        return;

    BackendsSingleton::self()->initialize(b, this);
    ApplicationBackend* applicationBackend = new ApplicationBackend(this);
    applicationBackend->setBackend(m_backend);
    BackendsSingleton::self()->appsModel()->addResourcesBackend(applicationBackend);
}

QAction* MuonDiscoverMainWindow::getAction(const QString& name)
{
    return actionCollection()->action(name);
}

void MuonDiscoverMainWindow::openMimeType(const QString& mime)
{
    emit listMimeInternal(mime);
}

void MuonDiscoverMainWindow::openCategory(const QString& category)
{
    Category* c = new Category(category, this);
    emit listCategoryInternal(c);
}

void MuonDiscoverMainWindow::openApplication(const QString& app)
{
    m_view->rootObject()->setProperty("defaultStartup", false);
    m_appToBeOpened = app;
    triggerOpenApplication();
    if(!m_appToBeOpened.isEmpty())
        connect(BackendsSingleton::self()->appsModel(), SIGNAL(rowsInserted(QModelIndex, int, int)), SLOT(triggerOpenApplication()));
}

void MuonDiscoverMainWindow::triggerOpenApplication()
{
    AbstractResource* app = BackendsSingleton::self()->appsModel()->applicationByPackageName(m_appToBeOpened);
    if(app) {
        emit openApplicationInternal(app);
        m_appToBeOpened.clear();
        disconnect(BackendsSingleton::self()->appsModel(), SIGNAL(rowsInserted(QModelIndex, int, int)), this, SLOT(triggerOpenApplication()));
    }
}

QSize MuonDiscoverMainWindow::sizeHint() const
{
    return QSize(800, 900);
}

QUrl MuonDiscoverMainWindow::featuredSource() const
{
    return MuonDataSources::featuredSource();
}
