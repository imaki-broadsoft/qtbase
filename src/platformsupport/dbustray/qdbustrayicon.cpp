/****************************************************************************
**
** Copyright (C) 2015 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QT_NO_SYSTEMTRAYICON

#include "qdbustrayicon_p.h"
#include "qdbusmenuconnection_p.h"
#include "qstatusnotifieritemadaptor_p.h"
#include "qdbusmenuadaptor_p.h"
#include "dbusmenu/qdbusplatformmenu_p.h"

#include <qplatformmenu.h>
#include <qstring.h>
#include <qdebug.h>
#include <qrect.h>
#include <qloggingcategory.h>
#include <qplatformintegration.h>
#include <qplatformservices.h>
#include <private/qguiapplication_p.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcTray, "qt.qpa.tray")

static const QString KDEItemFormat = QStringLiteral("org.kde.StatusNotifierItem-%1-%2");
static const QString TempFileTemplate =  QDir::tempPath() + QStringLiteral("/qt-trayicon-XXXXXX.png");
static int instanceCount = 0;

/*!
    \class QDBusTrayIcon
    \internal
*/

QDBusTrayIcon::QDBusTrayIcon()
    : m_dbusConnection(Q_NULLPTR)
    , m_adaptor(new QStatusNotifierItemAdaptor(this))
    , m_menuAdaptor(Q_NULLPTR)
    , m_menu(Q_NULLPTR)
    , m_instanceId(KDEItemFormat.arg(QCoreApplication::applicationPid()).arg(++instanceCount))
    , m_category(QStringLiteral("ApplicationStatus"))
    , m_defaultStatus(QStringLiteral("Active")) // be visible all the time.  QSystemTrayIcon has no API to control this.
    , m_status(m_defaultStatus)
    , m_tempIcon(Q_NULLPTR)
    , m_tempAttentionIcon(Q_NULLPTR)
{
    qCDebug(qLcTray);
    if (instanceCount == 1) {
        QDBusMenuItem::registerDBusTypes();
        qDBusRegisterMetaType<QXdgDBusImageStruct>();
        qDBusRegisterMetaType<QXdgDBusImageVector>();
        qDBusRegisterMetaType<QXdgDBusToolTipStruct>();
    }
    connect(this, SIGNAL(statusChanged(QString)), m_adaptor, SIGNAL(NewStatus(QString)));
    connect(this, SIGNAL(tooltipChanged()), m_adaptor, SIGNAL(NewToolTip()));
    connect(this, SIGNAL(iconChanged()), m_adaptor, SIGNAL(NewIcon()));
    connect(this, SIGNAL(attention()), m_adaptor, SIGNAL(NewAttentionIcon()));
    connect(this, SIGNAL(attention()), m_adaptor, SIGNAL(NewTitle()));
    connect(&m_attentionTimer, SIGNAL(timeout()), this, SLOT(attentionTimerExpired()));
    m_attentionTimer.setSingleShot(true);
}

QDBusTrayIcon::~QDBusTrayIcon()
{
}

void QDBusTrayIcon::init()
{
    qCDebug(qLcTray) << "registering" << m_instanceId;
    dBusConnection()->registerTrayIcon(this);
}

void QDBusTrayIcon::cleanup()
{
    qCDebug(qLcTray) << "unregistering" << m_instanceId;
    dBusConnection()->unregisterTrayIcon(this);
    delete m_dbusConnection;
    m_dbusConnection = Q_NULLPTR;
}

void QDBusTrayIcon::activate(int x, int y)
{
    qCDebug(qLcTray) << x << y;
    setStatus(QStringLiteral("Active"));
}

void QDBusTrayIcon::attentionTimerExpired()
{
    m_messageTitle = QString();
    m_message = QString();
    m_attentionIcon = QIcon();
    emit attention();
    emit tooltipChanged();
    setStatus(m_defaultStatus);
}

void QDBusTrayIcon::setStatus(const QString &status)
{
    qCDebug(qLcTray) << status;
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(m_status);
}

QTemporaryFile *QDBusTrayIcon::tempIcon(const QIcon &icon)
{
    // Hack for Unity, which doesn't handle icons sent across D-Bus:
    // save the icon to a temp file and set the icon name to that filename.
    static bool necessary = (QGuiApplicationPrivate::platformIntegration()->services()->desktopEnvironment().split(':').contains("UNITY"));
    if (!necessary)
        return Q_NULLPTR;
    QTemporaryFile *ret = new QTemporaryFile(TempFileTemplate, this);
    QSize tempSize;
    Q_FOREACH (const QSize &size, icon.availableSizes())
        if (size.width() > tempSize.width())
            tempSize = size;
    ret->open();
    icon.pixmap(tempSize).save(ret);
    ret->close();
    return ret;
}

QDBusMenuConnection * QDBusTrayIcon::dBusConnection()
{
    if (!m_dbusConnection)
        m_dbusConnection = new QDBusMenuConnection(this);
    return m_dbusConnection;
}

void QDBusTrayIcon::updateIcon(const QIcon &icon)
{
    m_iconName = icon.name();
    m_icon = icon;
    if (m_iconName.isEmpty()) {
        if (m_tempIcon)
            delete m_tempIcon;
        m_tempIcon = tempIcon(icon);
        if (m_tempIcon)
            m_iconName = m_tempIcon->fileName();
    }
    qCDebug(qLcTray) << m_iconName << icon.availableSizes();
    emit iconChanged();
}

void QDBusTrayIcon::updateToolTip(const QString &tooltip)
{
    qCDebug(qLcTray) << tooltip;
    m_tooltip = tooltip;
    emit tooltipChanged();
}

QPlatformMenu *QDBusTrayIcon::createMenu() const
{
    qCDebug(qLcTray);
    if (!m_menu)
        const_cast<QDBusTrayIcon *>(this)->m_menu = new QDBusPlatformMenu();
    return m_menu;
}

void QDBusTrayIcon::updateMenu(QPlatformMenu * menu)
{
    qCDebug(qLcTray) << menu;
    if (!m_menu)
        m_menu = qobject_cast<QDBusPlatformMenu *>(menu);
    if (!m_menuAdaptor) {
        m_menuAdaptor = new QDBusMenuAdaptor(m_menu);
        // TODO connect(m_menu, , m_menuAdaptor, SIGNAL(ItemActivationRequested(int,uint)));
        connect(m_menu, SIGNAL(propertiesUpdated(QDBusMenuItemList,QDBusMenuItemKeysList)),
                m_menuAdaptor, SIGNAL(ItemsPropertiesUpdated(QDBusMenuItemList,QDBusMenuItemKeysList)));
        connect(m_menu, SIGNAL(updated(uint,int)),
                m_menuAdaptor, SIGNAL(LayoutUpdated(uint,int)));
    }
    m_menu->emitUpdated();
}

void QDBusTrayIcon::contextMenu(int x, int y)
{
    qCDebug(qLcTray) << x << y;
}

void QDBusTrayIcon::showMessage(const QString &title, const QString &msg, const QIcon &icon,
                                QPlatformSystemTrayIcon::MessageIcon iconType, int msecs)
{
    m_messageTitle = title;
    m_message = msg;
    m_attentionIcon = icon;
    switch (iconType) {
    case Information:
        m_attentionIconName = QStringLiteral("dialog-information");
        break;
    case Warning:
        m_attentionIconName = QStringLiteral("dialog-warning");
        break;
    case Critical:
        m_attentionIconName = QStringLiteral("dialog-error");
        break;
    default:
        m_attentionIconName.clear();
        break;
    }
    if (m_attentionIconName.isEmpty()) {
        if (m_tempAttentionIcon)
            delete m_tempAttentionIcon;
        m_tempAttentionIcon = tempIcon(icon);
        if (m_tempAttentionIcon)
            m_attentionIconName = m_tempAttentionIcon->fileName();
    }
    qCDebug(qLcTray) << title << msg <<
        QPlatformSystemTrayIcon::metaObject()->enumerator(
            QPlatformSystemTrayIcon::staticMetaObject.indexOfEnumerator("MessageIcon")).valueToKey(iconType)
        << m_attentionIconName << msecs;
    setStatus(QStringLiteral("NeedsAttention"));
    m_attentionTimer.start(msecs);
    emit tooltipChanged();
    emit attention();
}

bool QDBusTrayIcon::isSystemTrayAvailable() const
{
    QDBusMenuConnection * conn = const_cast<QDBusTrayIcon *>(this)->dBusConnection();

    // If the KDE watcher service is registered, we must be on a desktop
    // where a StatusNotifier-conforming system tray exists.
    qCDebug(qLcTray) << conn->isWatcherRegistered();
    return conn->isWatcherRegistered();
}

QT_END_NAMESPACE
#endif //QT_NO_SYSTEMTRAYICON

