#include "statblockmanager.h"

#include <QLoggingCategory>

#include <sqlite/sqlitedb.h>
#include <sqlite/sqlitestmt.h>

#include <conf/confmanager.h>
#include <conf/firewallconf.h>
#include <util/ioc/ioccontainer.h>

#include "deleteconnblockjob.h"
#include "logblockedipjob.h"
#include "statblockworker.h"
#include "statsql.h"

namespace {

const QLoggingCategory LC("statBlock");

constexpr int DATABASE_USER_VERSION = 7;

bool migrateFunc(SqliteDb *db, int version, bool isNewDb, void *ctx)
{
    Q_UNUSED(ctx);

    if (isNewDb)
        return true;

    switch (version) {
    case 7: {
        const QString srcSchema = SqliteDb::migrationOldSchemaName();
        const QString dstSchema = SqliteDb::migrationNewSchemaName();

        db->executeStr(QString("INSERT INTO %1 (%3) SELECT %3 FROM %2;")
                               .arg(SqliteDb::entityName(dstSchema, "app"),
                                       SqliteDb::entityName(srcSchema, "app"),
                                       "app_id, path, creat_time"));

        // Union the "conn" & "conn_block" tables
        db->executeStr(QString("INSERT INTO %1 (%4) SELECT %4 FROM %2 JOIN %3 USING(conn_id);")
                               .arg(SqliteDb::entityName(dstSchema, "conn_block"),
                                       SqliteDb::entityName(srcSchema, "conn"),
                                       SqliteDb::entityName(srcSchema, "conn_block"),
                                       "conn_id, app_id, conn_time, process_id, inbound,"
                                       " inherited, ip_proto, local_port, remote_port,"
                                       " local_ip, remote_ip, local_ip6, remote_ip6,"
                                       " block_reason"));
    } break;
    }

    return true;
}

}

StatBlockManager::StatBlockManager(const QString &filePath, QObject *parent, quint32 openFlags) :
    WorkerManager(parent),
    m_isConnIdRangeUpdated(false),
    m_sqliteDb(new SqliteDb(filePath, openFlags, this)),
    m_roSqliteDb((openFlags & SqliteDb::OpenReadWrite) != 0
                    ? new SqliteDb(filePath, SqliteDb::OpenDefaultReadOnly, this)
                    : m_sqliteDb),
    m_connChangedTimer(500)
{
    connect(&m_connChangedTimer, &QTimer::timeout, this, &StatBlockManager::connChanged);
}

void StatBlockManager::emitConnChanged()
{
    m_connChangedTimer.startTrigger();
}

void StatBlockManager::setUp()
{
    setupWorker();
    setupConfManager();

    if (!sqliteDb()->open()) {
        qCCritical(LC) << "File open error:" << sqliteDb()->filePath()
                       << sqliteDb()->errorMessage();
        return;
    }

    SqliteDb::MigrateOptions opt = {
        .sqlDir = ":/stat/migrations/block",
        .version = DATABASE_USER_VERSION,
        .recreate = true,
        // COMPAT: Union the "conn" & "conn_block" tables
        .autoCopyTables = (DATABASE_USER_VERSION != 7),
        .migrateFunc = &migrateFunc,
    };

    if (!sqliteDb()->migrate(opt)) {
        qCCritical(LC) << "Migration error" << sqliteDb()->filePath();
        return;
    }

    if (roSqliteDb() != sqliteDb() && !roSqliteDb()->open()) {
        qCCritical(LC) << "File open error:" << roSqliteDb()->filePath()
                       << roSqliteDb()->errorMessage();
        return;
    }

    updateConnIdRange();
}

void StatBlockManager::updateConnIdRange()
{
    if (isConnIdRangeUpdated())
        return;

    setIsConnIdRangeUpdated(true);

    const auto vars = roSqliteDb()->executeEx(StatSql::sqlSelectMinMaxConnBlockId, {}, 2).toList();
    m_connIdMin = vars.value(0).toLongLong();
    m_connIdMax = vars.value(1).toLongLong();
}

void StatBlockManager::logBlockedIp(const LogEntryBlockedIp &entry)
{
    enqueueJob(new LogBlockedIpJob(entry));
}

void StatBlockManager::deleteConn(qint64 connIdTo)
{
    enqueueJob(new DeleteConnBlockJob(connIdTo));
}

void StatBlockManager::onLogBlockedIpFinished(int count, qint64 newConnId)
{
    emitConnChanged();

    m_connIdMax = newConnId;
    if (m_connIdMin <= 0) {
        m_connIdMin = m_connIdMax;
    }

    constexpr int connBlockIncMax = 99;

    m_connInc += count;
    if (m_connInc < connBlockIncMax)
        return;

    m_connInc = 0;

    const int totalCount = m_connIdMax - m_connIdMin;
    const int oldCount = totalCount - m_keepCount;
    if (oldCount <= 0)
        return;

    deleteConn(m_connIdMin + oldCount);
}

void StatBlockManager::onDeleteConnBlockFinished(qint64 connIdTo)
{
    emitConnChanged();

    if (connIdTo > 0) {
        m_connIdMin = connIdTo + 1;
    } else {
        m_connIdMin = m_connIdMax;
    }

    if (m_connIdMin >= m_connIdMax) {
        m_connIdMin = m_connIdMax = 0;
    }
}

WorkerObject *StatBlockManager::createWorker()
{
    return new StatBlockWorker(this);
}

void StatBlockManager::setupWorker()
{
    setMaxWorkersCount(1);

    connect(this, &StatBlockManager::logBlockedIpFinished, this,
            &StatBlockManager::onLogBlockedIpFinished);
    connect(this, &StatBlockManager::deleteConnBlockFinished, this,
            &StatBlockManager::onDeleteConnBlockFinished);
}

void StatBlockManager::setupConfManager()
{
    auto confManager = IoC()->setUpDependency<ConfManager>();

    connect(confManager, &ConfManager::iniChanged, this, &StatBlockManager::setupByConf);
}

void StatBlockManager::setupByConf(const IniOptions &ini)
{
    m_keepCount = ini.blockedIpKeepCount();
}