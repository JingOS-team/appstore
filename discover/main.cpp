/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: LGPL-2.0-or-later
 */

// #define QT_QML_DEBUG

#include <QApplication>
#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KQuickAddons/QtQuickSettings>
#include <KLocalizedString>
#include <QCommandLineParser>
#include <QWindow>
#include "DiscoverObject.h"
#include <DiscoverBackendsFactory.h>
#include "DiscoverVersion.h"
#include <QTextStream>
#include <QStandardPaths>
#include <kstartupinfo.h>
#include <QDateTime>
#include<japplicationqt.h>

#include <QX11Info>

typedef QHash<QString, DiscoverObject::CompactMode> StringCompactMode;
Q_GLOBAL_STATIC_WITH_ARGS(StringCompactMode, s_decodeCompactMode, (StringCompactMode {
    { QLatin1String("auto"), DiscoverObject::Auto },
    { QLatin1String("compact"), DiscoverObject::Compact },
    { QLatin1String("full"), DiscoverObject::Full }
}))

QCommandLineParser* createParser()
{
    QCommandLineParser* parser = new QCommandLineParser;
    parser->addOption(QCommandLineOption(QStringLiteral("application"), i18n("Directly open the specified application by its appstream:// URI."), QStringLiteral("name")));
    parser->addOption(QCommandLineOption(QStringLiteral("mime"), i18n("Open with a search for programs that can deal with the given mimetype."), QStringLiteral("name")));
    parser->addOption(QCommandLineOption(QStringLiteral("category"), i18n("Display a list of entries with a category."), QStringLiteral("name")));
    parser->addOption(QCommandLineOption(QStringLiteral("mode"), i18n("Open Discover in a said mode. Modes correspond to the toolbar buttons."), QStringLiteral("name")));
    parser->addOption(QCommandLineOption(QStringLiteral("listmodes"), i18n("List all the available modes.")));
    parser->addOption(QCommandLineOption(QStringLiteral("compact"), i18n("Compact Mode (auto/compact/full)."), QStringLiteral("mode"), QStringLiteral("auto")));
    parser->addOption(QCommandLineOption(QStringLiteral("local-filename"), i18n("Local package file to install"), QStringLiteral("package")));
    parser->addOption(QCommandLineOption(QStringLiteral("listbackends"), i18n("List all the available backends.")));
    parser->addOption(QCommandLineOption(QStringLiteral("search"), i18n("Search string."), QStringLiteral("text")));
    parser->addOption(QCommandLineOption(QStringLiteral("feedback"), i18n("Lists the available options for user feedback")));
    parser->addOption(QCommandLineOption(QStringLiteral("test"), QStringLiteral("Test file"), QStringLiteral("file.qml")));
    parser->addPositionalArgument(QStringLiteral("urls"), i18n("Supports appstream: url scheme"));
    DiscoverBackendsFactory::setupCommandLine(parser);
    KAboutData::applicationData().setupCommandLine(parser);
    return parser;
}

void processArgs(QCommandLineParser* parser, DiscoverObject* mainWindow)
{
    if (parser->isSet(QStringLiteral("application")))
        mainWindow->openApplication(QUrl(parser->value(QStringLiteral("application"))));
    else if (parser->isSet(QStringLiteral("mime")))
        mainWindow->openMimeType(parser->value(QStringLiteral("mime")));
    else if (parser->isSet(QStringLiteral("category")))
        mainWindow->openCategory(parser->value(QStringLiteral("category")));
    else if (parser->isSet(QStringLiteral("mode")))
        mainWindow->openMode(parser->value(QStringLiteral("mode")));
    else
        mainWindow->openMode(QStringLiteral("Browsing"));

    if (parser->isSet(QStringLiteral("search")))
        Q_EMIT mainWindow->openSearch(parser->value(QStringLiteral("search")));

    if (parser->isSet(QStringLiteral("local-filename")))
        mainWindow->openLocalPackage(QUrl::fromUserInput(parser->value(QStringLiteral("local-filename")), {}, QUrl::AssumeLocalFile));

    foreach (const QString &arg, parser->positionalArguments()) {
        const QUrl url = QUrl::fromUserInput(arg, {}, QUrl::AssumeLocalFile);
        if (url.isLocalFile())
            mainWindow->openLocalPackage(url);
        else if (url.scheme() == QLatin1String("apt"))
            Q_EMIT mainWindow->openSearch(url.host());
        else
            mainWindow->openApplication(url);
    }
}

int main(int argc, char** argv)
{
     qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    // needs to be set before we create the QGuiApplication
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager, true);

    QApplication app(argc, argv);
    JApplicationQt japp;
    japp.enableBackgroud(true);
    QCoreApplication::setApplicationName("App Store");
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("plasmadiscover")));
    app.setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    app.setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    KCrash::initialize();
    KQuickAddons::QtQuickSettings::init();
    KLocalizedString::setApplicationDomain("appstore");
    KAboutData about(QStringLiteral("discover"), i18n("Discover"), version, i18n("An application explorer"),
                     KAboutLicense::GPL, i18n("© 2010-2020 Plasma Development Team"));
    about.addAuthor(i18n("Aleix Pol Gonzalez"), QString(), QStringLiteral("aleixpol@kde.org"));
    about.addAuthor(i18n("Nate Graham"), i18n("Quality Assurance, Design and Usability"), QStringLiteral("nate@kde.org"));
    about.addAuthor(i18n("Dan Leinir Turthra Jensen"), i18n("KNewStuff"), QStringLiteral("admin@leinir.dk"));
    about.setProductName("discover/discover");
    about.setProgramLogo(app.windowIcon());

    about.setTranslator(
        i18ndc(nullptr, "NAME OF TRANSLATORS", "Your names"),
        i18ndc(nullptr, "EMAIL OF TRANSLATORS", "Your emails"));

    KAboutData::setApplicationData(about);

    DiscoverObject *mainWindow = nullptr;
    {
        QScopedPointer<QCommandLineParser> parser(createParser());
        parser->process(app);
        about.processCommandLine(parser.data());
        DiscoverBackendsFactory::processCommandLine(parser.data(), parser->isSet(QStringLiteral("test")));

        if (parser->isSet(QStringLiteral("listbackends"))) {
            QTextStream(stdout) << i18n("Available backends:\n");
            DiscoverBackendsFactory f;
            foreach (const QString& name, f.allBackendNames(false, true))
                QTextStream(stdout) << " * " << name << '\n';
            return 0;
        }

        if (parser->isSet(QStringLiteral("test"))) {
            QStandardPaths::setTestModeEnabled(true);
        }

        KDBusService* service = new KDBusService(KDBusService::Unique, &app);

        {
            auto options = parser->optionNames();
            options.removeAll(QStringLiteral("backends"));
            options.removeAll(QStringLiteral("test"));
            QVariantMap initialProperties;
            if (!options.isEmpty() || !parser->positionalArguments().isEmpty())
                initialProperties = {{QStringLiteral("currentTopLevel"), QStringLiteral("qrc:/qml/LoadingPage.qml")}};
            mainWindow = new DiscoverObject(s_decodeCompactMode->value(parser->value(QStringLiteral("compact")), DiscoverObject::Full), initialProperties);
        }
        QObject::connect(&app, &QCoreApplication::aboutToQuit, mainWindow, &DiscoverObject::deleteLater);
        QObject::connect(service, &KDBusService::activateRequested, mainWindow, [mainWindow](const QStringList &arguments, const QString &/*workingDirectory*/) {


            if (!mainWindow->rootObject())
                QCoreApplication::instance()->quit();


            auto window = qobject_cast<QWindow*>(mainWindow->rootObject());
            if (window && QX11Info::isPlatformX11()) {
                KStartupInfo::setNewStartupId(window, QX11Info::nextStartupId());
            }
            window->raise();

            if (arguments.isEmpty())
                return;
            QScopedPointer<QCommandLineParser> parser(createParser());
            parser->parse(arguments);
            processArgs(parser.data(), mainWindow);
        });

        processArgs(parser.data(), mainWindow);

        if (parser->isSet(QStringLiteral("listmodes"))) {
            QTextStream(stdout) << i18n("Available modes:\n");
            foreach (const QString& mode, mainWindow->modes())
                QTextStream(stdout) << " * " << mode << '\n';
            delete mainWindow;
            return 0;
        }

        if (parser->isSet(QStringLiteral("feedback"))) {
            QTextStream(stdout) << mainWindow->describeSources() << '\n';
            delete mainWindow;
            return 0;
        }

        if (parser->isSet(QStringLiteral("test"))) {
            const QUrl testFile = QUrl::fromUserInput(parser->value(QStringLiteral("test")), {}, QUrl::AssumeLocalFile);
            Q_ASSERT(!testFile.isEmpty() && testFile.isLocalFile());

            mainWindow->loadTest(testFile);
        }
    }
    qint64 endTime = QDateTime::currentMSecsSinceEpoch();
    return app.exec();
}
