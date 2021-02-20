#include "connlistmodel.h"

#include <QFont>
#include <QIcon>

#include <sqlite/sqlitedb.h>
#include <sqlite/sqlitestmt.h>

#include "../log/logentryblockedip.h"
#include "../stat/statmanager.h"
#include "../util/app/appinfocache.h"
#include "../util/fileutil.h"
#include "../util/iconcache.h"
#include "../util/net/netutil.h"

ConnListModel::ConnListModel(StatManager *statManager, QObject *parent) :
    TableSqlModel(parent), m_statManager(statManager)
{
}

SqliteDb *ConnListModel::sqliteDb() const
{
    return statManager()->sqliteDb();
}

void ConnListModel::setAppInfoCache(AppInfoCache *v)
{
    m_appInfoCache = v;

    connect(appInfoCache(), &AppInfoCache::cacheChanged, this, &ConnListModel::refresh);
}

void ConnListModel::handleLogBlockedIp(const LogEntryBlockedIp &entry, qint64 unixTime)
{
    if (statManager()->logBlockedIp(entry.inbound(), entry.blockReason(), entry.ipProto(),
                entry.localPort(), entry.remotePort(), entry.localIp(), entry.remoteIp(),
                entry.pid(), entry.path(), unixTime)) {
        reset();
    }
}

int ConnListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 7;
}

QVariant ConnListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return tr("Program");
        case 1:
            return tr("Process ID");
        case 2:
            return tr("Protocol");
        case 3:
            return tr("Local IP:Port");
        case 4:
            return tr("Remote IP:Port");
        case 5:
            return tr("Dir.");
        case 6:
            return tr("Time");
        }
    }
    return QVariant();
}

QVariant ConnListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    switch (role) {
    // Label
    case Qt::DisplayRole:
    case Qt::ToolTipRole: {
        const int row = index.row();
        const int column = index.column();

        const auto connRow = connRowAt(row);

        switch (column) {
        case 0: {
            const auto appInfo = appInfoCache()->appInfo(connRow.appPath);
            const auto appName = !appInfo.fileDescription.isEmpty()
                    ? appInfo.fileDescription
                    : FileUtil::fileName(connRow.appPath);
            return appName;
        }
        case 1:
            return connRow.pid;
        case 2:
            return NetUtil::protocolName(connRow.ipProto);
        case 3:
            return NetUtil::ip4ToText(connRow.localIp) + ':' + QString::number(connRow.localPort);
        case 4:
            return NetUtil::ip4ToText(connRow.remoteIp) + ':' + QString::number(connRow.remotePort);
        case 5:
            return connRow.inbound ? tr("In") : tr("Out");
        case 6:
            return connRow.connTime;
        }

        break;
    }

    // Icon
    case Qt::DecorationRole: {
        const int column = index.column();

        if (column == 0 || column == 5) {
            const int row = index.row();
            const auto connRow = connRowAt(row);

            switch (column) {
            case 0:
                return appInfoCache()->appIcon(connRow.appPath);
            case 5:
                return connRow.blocked ? IconCache::icon(":/icons/sign-ban.png")
                                       : IconCache::icon(":/icons/sign-check.png");
            }
        }

        break;
    }
    }

    return QVariant();
}

const ConnRow &ConnListModel::connRowAt(int row) const
{
    updateRowCache(row);

    return m_connRow;
}

bool ConnListModel::updateTableRow(int row) const
{
    SqliteStmt stmt;
    if (!(sqliteDb()->prepare(stmt, sql().toLatin1(), { row })
                && stmt.step() == SqliteStmt::StepRow))
        return false;

    m_connRow.connId = stmt.columnInt64(0);
    m_connRow.appId = stmt.columnInt64(1);
    m_connRow.connTime = stmt.columnDateTime(2);
    m_connRow.pid = stmt.columnInt(3);
    m_connRow.inbound = stmt.columnBool(4);
    m_connRow.blocked = stmt.columnBool(5);
    m_connRow.ipProto = stmt.columnInt(6);
    m_connRow.localPort = stmt.columnInt(7);
    m_connRow.remotePort = stmt.columnInt(8);
    m_connRow.localIp = stmt.columnInt(9);
    m_connRow.remoteIp = stmt.columnInt(10);
    m_connRow.appPath = stmt.columnText(11);

    return true;
}

QString ConnListModel::sqlBase() const
{
    return "SELECT"
           "    t.conn_id,"
           "    t.app_id,"
           "    t.conn_time,"
           "    t.process_id,"
           "    t.inbound,"
           "    t.blocked,"
           "    t.ip_proto,"
           "    t.local_port,"
           "    t.remote_port,"
           "    t.local_ip,"
           "    t.remote_ip,"
           "    a.path"
           "  FROM conn t"
           "    JOIN app a ON a.app_id = t.app_id";
}