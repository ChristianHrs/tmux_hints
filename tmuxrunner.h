#pragma once

#include "tmuxbindings.h"

#include <KRunner/AbstractRunner>
#include <KRunner/Action>
#include <KRunner/QueryMatch>

#include <QElapsedTimer>
#include <QMutex>

struct Command;
struct Option;

class TmuxRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    TmuxRunner(QObject *parent, const KPluginMetaData &metaData);

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;

private:
    struct Session {
        QString name;
        int windows = 0;
        bool attached = false;
    };
    struct Target {
        QString target; // e.g. "work:2" or "work:2.1"
        QString label;  // human readable description
    };
    struct Snapshot {
        QList<Session> sessions;
        QList<Target> windows;
        QList<Target> panes;
        QString prefix = QStringLiteral("C-b");
        QList<TmuxBinding> bindings;
        bool liveBindings = false; // read from a running server (includes your own bindings)
    };

    Snapshot snapshot();

    KRunner::QueryMatch makeMatch(const QString &text, const QString &subtext, qreal relevance, const QStringList &data);
    KRunner::QueryMatch bindingMatch(const TmuxBinding &b, const QString &prefix, qreal relevance, bool descFirst);

    void matchCommandNames(const QString &partial, QList<KRunner::QueryMatch> &out);
    void matchArguments(const Command &cmd, const QStringList &args, const QString &partial,
                        const QString &base, bool preview, QList<KRunner::QueryMatch> &out);
    void matchValues(const Option &opt, const QString &partial, const QString &base, QList<KRunner::QueryMatch> &out);
    void matchPrefixMode(const QString &query, const QString &typedKey, const QString &filter, QList<KRunner::QueryMatch> &out);
    int matchBindingSearch(const QStringList &words, bool strong, QList<KRunner::QueryMatch> &out);

    const KRunner::Action m_runAction;
    const KRunner::Action m_copyAction;
    const KRunner::Action m_attachDetachAction;

    QMutex m_mutex;
    Snapshot m_cache;
    QElapsedTimer m_lastRefresh;
};
