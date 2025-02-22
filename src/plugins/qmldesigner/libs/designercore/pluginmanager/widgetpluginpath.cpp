// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "widgetpluginpath.h"
#include <designercoretr.h>
#include <iwidgetplugin.h>

#include <utils/filepath.h>

#include <QLibrary>
#include <QPluginLoader>
#include <QFileInfo>
#include <QCoreApplication>
#include <QObject>
#include <QDebug>

enum { debug = 0 };

namespace QmlDesigner {

namespace Internal {

// Initialize and create instance of a plugin from scratch,
// that is, make sure the library is loaded and has an instance
// of the IPlugin type. Once something fails, mark it as failed
// ignore it from then on.
static IWidgetPlugin *instance(WidgetPluginData &p)
{
    if (debug)
        qDebug() << "Loading QmlDesigner plugin" << p.path << "...";

    // Go stale once something fails
    if (p.failed)
        return nullptr;

    // Pull up the plugin, retrieve IPlugin instance.
    if (!p.instanceGuard) {
        p.instance = nullptr;
        QPluginLoader loader(p.path);

        if (debug)
            qDebug() << "guard" << p.path;

        if (!(loader.isLoaded() || loader.load())) {
            p.failed = true;
            p.errorMessage = DesignerCore::Tr::tr("Failed to create instance of file "
                                                  "\"%1\": %2")
                                 .arg(p.path)
                                 .arg(loader.errorString());
            qWarning() << p.errorMessage;
            return nullptr;
        }
        QObject *object = loader.instance();
        if (!object) {
            p.failed = true;
            p.errorMessage = DesignerCore::Tr::tr("Failed to create instance of file \"%1\".").arg(p.path);
            qWarning() << p.errorMessage;
            return nullptr;
        }
        IWidgetPlugin *iplugin = qobject_cast<IWidgetPlugin *>(object);
        if (!iplugin) {
            p.failed = true;
            p.errorMessage = QCoreApplication::translate("WidgetPluginManager",
                                                         "File \"%1\" is not a Qt Quick Designer plugin."
                                                         ).arg(p.path);
            qWarning() << p.errorMessage;
            delete object;
            return nullptr;
        }
        p.instanceGuard = object;
        p.instance = iplugin;
    }
    // Ensure it is initialized
    /*if (!p.instance->isInitialized()) {
        if (!p.instance->initialize(&p.errorMessage)) {
            p.failed = true;
            delete p.instance;
            p.instance = 0;
            return 0;
        }
    }*/

    if (debug)
        qDebug() << "QmlDesigner plugin" << p.path << "successfully loaded!";
    return p.instance;
}

WidgetPluginData::WidgetPluginData(const QString &p) :
    path(p),
    failed(false),
    instance(nullptr)
{
}


WidgetPluginPath::WidgetPluginPath(const QDir &path) :
    m_path(path),
    m_loaded(false)
{
}

// Determine a unique list of library files in that directory
QStringList WidgetPluginPath::libraryFilePaths(const QDir &dir)
{
    const QFileInfoList infoList = dir.entryInfoList(QDir::Files|QDir::Readable|QDir::NoDotAndDotDot);
    if (infoList.empty())
        return {};
      // Load symbolic links but make sure all file names are unique as not
    // to fall for something like 'libplugin.so.1 -> libplugin.so'
    QStringList result;
    const auto icend = infoList.constEnd();
    for (auto it = infoList.constBegin(); it != icend; ++it) {
        QString fileName;
        if (it->isSymLink()) {
            const QFileInfo linkTarget = QFileInfo(it->symLinkTarget());
            if (linkTarget.exists() && linkTarget.isFile())
                fileName = linkTarget.absoluteFilePath();
        } else {
            fileName = it->absoluteFilePath();
        }
        if (!fileName.isEmpty() && QLibrary::isLibrary(fileName) && !result.contains(fileName))
            result += fileName;
    }

    if (debug)
        qDebug() << "Library files in directory" << dir << ": " << result;

    return result;
}

void WidgetPluginPath::clear()
{
    m_loaded = false;
    m_plugins.clear();
}

void WidgetPluginPath::ensureLoaded()
{
    if (!m_loaded) {
        const QStringList libraryFiles = libraryFilePaths(m_path);
        if (debug)
            qDebug() << "Checking " << libraryFiles.size() << " plugins " << m_path.absolutePath();
        for (const QString &libFile : libraryFiles)
            m_plugins.push_back(WidgetPluginData(libFile));
        m_loaded = true;
    }
}

void WidgetPluginPath::getInstances(IWidgetPluginList *list)
{
    ensureLoaded();
    // Compile list of instances
    if (m_plugins.empty())
        return;
    const auto end = m_plugins.end();
    for (auto it = m_plugins.begin(); it != end; ++it)
        if (IWidgetPlugin *i = instance(*it))
            list->push_back(i);
}

QStandardItem *WidgetPluginPath::createModelItem()
{
    ensureLoaded();
    // Create a list of plugin lib files with classes.
    // If there are failed ones, create a separate "Failed"
    // category at the end
    QStandardItem *pathItem = new QStandardItem(m_path.absolutePath());
    QStandardItem *failedCategory = nullptr;
    const auto end = m_plugins.end();
    for (auto it = m_plugins.begin(); it != end; ++it) {
        QStandardItem *pluginItem = new QStandardItem(Utils::FilePath::fromString(it->path).fileName());
        if (instance(*it)) {
            pluginItem->appendRow(new QStandardItem(QString::fromUtf8(it->instanceGuard->metaObject()->className())));
            pathItem->appendRow(pluginItem);
        } else {
            pluginItem->setToolTip(it->errorMessage);
            if (!failedCategory) {
                const QString failed = QCoreApplication::translate("PluginManager", "Failed Plugins");
                failedCategory = new QStandardItem(failed);
            }
            failedCategory->appendRow(pluginItem);
        }
    }
    if (failedCategory)
        pathItem->appendRow(failedCategory);
    return pathItem;
}

} // namespace Internal
} // namespace QmlDesigner

