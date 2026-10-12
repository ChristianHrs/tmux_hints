#include "tmuxrunner.h"
#include "tmuxcommands.h"

#include <KPluginFactory>
#include <KRunner/RunnerContext>
#include <KShell>
#include <KTerminalLauncherJob>

#include <QClipboard>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

K_PLUGIN_CLASS_WITH_JSON(TmuxRunner, "tmuxrunner.json")

namespace
{
const QString kIcon = QStringLiteral("utilities-terminal");

// Match data kinds: [kind, payload]
const QString kComplete = QStringLiteral("complete"); // payload = new query text
const QString kRun = QStringLiteral("run");           // payload = command line
const QString kSession = QStringLiteral("session");   // payload = session name

QStringList tokenize(const QString &s)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return s.split(ws, Qt::SkipEmptyParts);
}

QStringList runTmux(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("tmux"), args);
    if (!p.waitForStarted(300)) {
        return {};
    }
    if (!p.waitForFinished(800)) {
        p.kill();
        p.waitForFinished(100);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        return {}; // e.g. "no server running"
    }
    return QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// Exact name/alias first, then a unique prefix (tmux itself accepts those too).
const Command *findCommand(const QString &token, bool allowPrefix = true)
{
    for (const Command &c : commands()) {
        if (c.name == token || c.aliases.contains(token)) {
            return &c;
        }
    }
    if (!allowPrefix || token.isEmpty()) {
        return nullptr;
    }
    const Command *found = nullptr;
    for (const Command &c : commands()) {
        if (c.name.startsWith(token)) {
            if (found) {
                return nullptr; // ambiguous
            }
            found = &c;
        }
    }
    return found;
}

const Option *findOption(const Command &cmd, QChar flag)
{
    for (const Option &o : cmd.options) {
        if (o.flag == flag) {
            return &o;
        }
    }
    return nullptr;
}

struct ParseState {
    QSet<QChar> used;
    const Option *pending = nullptr; // flag waiting for its argument
    int positional = 0;
};

ParseState parseArgs(const Command &cmd, const QStringList &args)
{
    ParseState st;
    for (const QString &tok : args) {
        if (st.pending) {
            st.pending = nullptr; // this token was the flag's argument
            continue;
        }
        if (tok.size() > 1 && tok.startsWith(QLatin1Char('-'))) {
            // Handles combined flags like "-dr" and attached args like "-tmysession"
            for (int i = 1; i < tok.size(); ++i) {
                const Option *o = findOption(cmd, tok.at(i));
                if (!o) {
                    continue;
                }
                st.used.insert(o->flag);
                if (o->arg != Arg::None) {
                    if (i == tok.size() - 1) {
                        st.pending = o;
                    }
                    break; // the rest of the token is the argument
                }
            }
        } else {
            ++st.positional;
        }
    }
    return st;
}

// Does this command take over the terminal (attach / new-session without -d)?
bool commandNeedsClient(const QString &line)
{
    const QStringList t = tokenize(line);
    if (t.size() < 2) {
        return true; // bare "tmux" == new-session
    }
    const Command *cmd = findCommand(t.at(1));
    if (!cmd || !cmd->needsClient) {
        return false;
    }
    if (cmd->name == QLatin1String("new-session")) {
        return !parseArgs(*cmd, t.mid(2)).used.contains(u'd');
    }
    return true;
}

void launchInTerminal(const QString &command)
{
    // Interactive commands replace the shell; others keep the window open so you can read the output.
    const QString script = commandNeedsClient(command)
        ? QStringLiteral("exec ") + command
        : command + QStringLiteral("; printf '\\n[exit %s] Press Enter to close' \"$?\"; read _");
    const QString full = QStringLiteral("sh -c ") + KShell::quoteArg(script);

    // run() may be called off the GUI thread; jobs and clipboard belong on the main thread.
    QMetaObject::invokeMethod(
        qApp,
        [full] {
            auto *job = new KTerminalLauncherJob(full);
            job->start();
        },
        Qt::QueuedConnection);
}

void copyToClipboard(const QString &text)
{
    QMetaObject::invokeMethod(
        qApp,
        [text] {
            QGuiApplication::clipboard()->setText(text);
        },
        Qt::QueuedConnection);
}
} // namespace

TmuxRunner::TmuxRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
    , m_runAction(QStringLiteral("run"), QStringLiteral("system-run"), QStringLiteral("Run in a terminal"))
    , m_copyAction(QStringLiteral("copy"), QStringLiteral("edit-copy"), QStringLiteral("Copy command"))
    , m_attachDetachAction(QStringLiteral("attach-detach"), QStringLiteral("view-split-left-right"),
                           QStringLiteral("Attach and detach other clients"))
{
    setTriggerWords({QStringLiteral("tmux")});
    addSyntax(QStringLiteral("tmux"), QStringLiteral("Lists tmux commands and your running sessions"));
    addSyntax(QStringLiteral("tmux :q:"), QStringLiteral("Suggests the next command, flag or target for :q:"));
}

TmuxRunner::Snapshot TmuxRunner::snapshot()
{
    QMutexLocker lock(&m_mutex);
    if (m_lastRefresh.isValid() && m_lastRefresh.elapsed() < 2000) {
        return m_cache;
    }

    // Session names can't contain ':' (tmux replaces it), so names go first / free text goes last.
    Snapshot s;
    const QStringList sessions = runTmux({QStringLiteral("list-sessions"), QStringLiteral("-F"),
                                          QStringLiteral("#{session_windows}:#{session_attached}:#{session_name}")});
    for (const QString &line : sessions) {
        if (line.count(QLatin1Char(':')) < 2) {
            continue;
        }
        s.sessions.append({line.section(QLatin1Char(':'), 2), line.section(QLatin1Char(':'), 0, 0).toInt(),
                           line.section(QLatin1Char(':'), 1, 1).toInt() > 0});
    }

    if (!s.sessions.isEmpty()) {
        const QStringList windows = runTmux(
            {QStringLiteral("list-windows"), QStringLiteral("-a"), QStringLiteral("-F"),
             QStringLiteral("#{session_name}:#{window_index}:#{window_active}:#{window_panes}:#{window_name}")});
        for (const QString &line : windows) {
            const auto sec = [&](int i) { return line.section(QLatin1Char(':'), i, i); };
            const QString label = QStringLiteral("Window \"%1\" · %2 pane(s)%3")
                                      .arg(line.section(QLatin1Char(':'), 4), sec(3),
                                           sec(2) == QLatin1String("1") ? QStringLiteral(" · active") : QString());
            s.windows.append({sec(0) + QLatin1Char(':') + sec(1), label});
        }

        const QStringList panes = runTmux(
            {QStringLiteral("list-panes"), QStringLiteral("-a"), QStringLiteral("-F"),
             QStringLiteral("#{session_name}:#{window_index}:#{pane_index}:#{pane_active}:#{pane_current_command}:"
                            "#{pane_current_path}")});
        for (const QString &line : panes) {
            const auto sec = [&](int i) { return line.section(QLatin1Char(':'), i, i); };
            const QString label = QStringLiteral("Pane running %1 in %2%3")
                                      .arg(sec(4), line.section(QLatin1Char(':'), 5),
                                           sec(3) == QLatin1String("1") ? QStringLiteral(" · active") : QString());
            s.panes.append({sec(0) + QLatin1Char(':') + sec(1) + QLatin1Char('.') + sec(2), label});
        }
    }

    m_cache = s;
    m_lastRefresh.start();
    return s;
}

KRunner::QueryMatch TmuxRunner::makeMatch(const QString &text, const QString &subtext, qreal relevance, const QStringList &data)
{
    KRunner::QueryMatch m(this);
    m.setText(text);
    m.setSubtext(subtext);
    m.setIconName(kIcon);
    m.setRelevance(std::clamp(relevance, 0.0, 1.0));
    m.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::High);
    m.setMatchCategory(QStringLiteral("tmux"));
    m.setData(data);
    m.setId(data.join(QLatin1Char('|')));
    return m;
}

void TmuxRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query();
    static const QRegularExpression head(QStringLiteral("^tmux(\\s|$)"), QRegularExpression::CaseInsensitiveOption);
    if (!head.match(query).hasMatch()) {
        return;
    }

    QStringList tokens = tokenize(query);
    tokens.removeFirst(); // "tmux"

    // The last word is still being typed unless the query ends in whitespace.
    QString partial;
    if (!query.back().isSpace() && !tokens.isEmpty()) {
        partial = tokens.takeLast();
    }

    QString base = QStringLiteral("tmux ");
    for (const QString &t : std::as_const(tokens)) {
        base += t + QLatin1Char(' ');
    }

    QList<KRunner::QueryMatch> matches;
    if (tokens.isEmpty()) {
        matchCommandNames(partial, matches);
        // "tmux attach" (no trailing space yet): already preview what comes next.
        if (const Command *cmd = findCommand(partial, false)) {
            matchArguments(*cmd, {}, QString(), base + partial + QLatin1Char(' '), true, matches);
        }
    } else if (const Command *cmd = findCommand(tokens.first())) {
        matchArguments(*cmd, tokens.mid(1), partial, base, false, matches);
    } else {
        matches << makeMatch(QStringLiteral("Unknown tmux command: %1").arg(tokens.first()),
                             QStringLiteral("Type \"tmux \" to see all commands"), 0.1,
                             {kComplete, QStringLiteral("tmux ")});
    }

    if (context.isValid()) {
        context.addMatches(matches);
    }
}

void TmuxRunner::matchCommandNames(const QString &partial, QList<KRunner::QueryMatch> &out)
{
    // Your sessions first: one Enter to attach.
    const Snapshot snap = snapshot();
    for (const Session &s : snap.sessions) {
        if (!partial.isEmpty() && !s.name.startsWith(partial, Qt::CaseInsensitive)) {
            continue;
        }
        QString sub = QString::number(s.windows) + QStringLiteral(" window(s)");
        if (s.attached) {
            sub += QStringLiteral(" · attached elsewhere");
        }
        sub += QStringLiteral(" · tmux attach -t ") + s.name;

        auto m = makeMatch(QStringLiteral("Attach to session \"%1\"").arg(s.name), sub, 1.0, {kSession, s.name});
        m.setActions({m_attachDetachAction, m_copyAction});
        out << m;
    }

    const QList<Command> &cmds = commands();
    for (int i = 0; i < cmds.size(); ++i) {
        const Command &c = cmds.at(i);
        QString typed = c.name;
        bool exact = false;
        bool hit = partial.isEmpty();

        if (!partial.isEmpty()) {
            if (c.name == partial) {
                exact = hit = true;
            }
            for (const QString &a : c.aliases) {
                if (a == partial) {
                    exact = hit = true;
                    typed = a; // keep the user's short form
                }
            }
            if (!hit) {
                hit = c.name.startsWith(partial)
                    || std::any_of(c.aliases.cbegin(), c.aliases.cend(), [&](const QString &a) {
                           return a.startsWith(partial);
                       });
            }
        }
        if (!hit) {
            continue;
        }

        QString text = QStringLiteral("tmux ") + c.name;
        if (!c.aliases.isEmpty()) {
            text += QStringLiteral("  (") + c.aliases.join(QStringLiteral(", ")) + QLatin1Char(')');
        }
        auto m = makeMatch(text, c.desc, exact ? 0.99 : 0.9 - i * 0.002,
                           {kComplete, QStringLiteral("tmux ") + typed + QLatin1Char(' ')});
        m.setActions({m_runAction, m_copyAction});
        out << m;
    }
}

void TmuxRunner::matchArguments(const Command &cmd, const QStringList &args, const QString &partial,
                                const QString &base, bool preview, QList<KRunner::QueryMatch> &out)
{
    const ParseState st = parseArgs(cmd, args);
    const QString line = (base + partial).trimmed();

    // "Run: <what you typed>" on top, unless a flag is still missing its argument.
    if (!preview && !(st.pending && partial.isEmpty()) && partial != QLatin1String("-")) {
        const QString how = commandNeedsClient(line) ? QStringLiteral(" · opens in a terminal")
                                                     : QStringLiteral(" · runs in a terminal and shows the output");
        auto m = makeMatch(QStringLiteral("Run: ") + line, cmd.desc + how, 1.0, {kRun, line});
        m.setActions({m_copyAction});
        out << m;
    }

    if (st.pending) {
        matchValues(*st.pending, partial, base, out);
        return;
    }

    // Remaining flags
    if (partial.isEmpty() || partial.startsWith(QLatin1Char('-'))) {
        for (int i = 0; i < cmd.options.size(); ++i) {
            const Option &o = cmd.options.at(i);
            if (st.used.contains(o.flag)) {
                continue;
            }
            const QString flag = QLatin1Char('-') + QString(o.flag);
            if (!partial.isEmpty() && !flag.startsWith(partial)) {
                continue;
            }
            QString text = flag;
            if (!o.argName.isEmpty()) {
                text += QStringLiteral(" <") + o.argName + QLatin1Char('>');
            }
            out << makeMatch(text, o.desc, 0.8 - i * 0.002 - (preview ? 0.05 : 0.0),
                             {kComplete, base + flag + QLatin1Char(' ')});
        }
    }

    // Positional argument
    if (!partial.startsWith(QLatin1Char('-')) && !cmd.positional.isEmpty() && st.positional == 0) {
        if (!cmd.choices.isEmpty()) {
            for (const QString &choice : cmd.choices) {
                if (partial.isEmpty() || choice.startsWith(partial)) {
                    out << makeMatch(choice, cmd.positional + QStringLiteral(" for ") + cmd.name, 0.85,
                                     {kComplete, base + choice + QLatin1Char(' ')});
                }
            }
        } else if (partial.isEmpty()) {
            out << makeMatch(QLatin1Char('<') + cmd.positional + QLatin1Char('>'),
                             QStringLiteral("Positional argument for ") + cmd.name, 0.5,
                             {kComplete, base});
        }
    }
}

void TmuxRunner::matchValues(const Option &opt, const QString &partial, const QString &base, QList<KRunner::QueryMatch> &out)
{
    const qsizetype before = out.size();
    int idx = 0;
    auto suggest = [&](const QString &value, const QString &sub) {
        if (!partial.isEmpty() && !value.startsWith(partial, Qt::CaseInsensitive)) {
            return;
        }
        out << makeMatch(value, sub, 0.9 - (idx++) * 0.001, {kComplete, base + value + QLatin1Char(' ')});
    };

    switch (opt.arg) {
    case Arg::Session: {
        for (const Session &s : snapshot().sessions) {
            suggest(s.name, QStringLiteral("Session · ") + QString::number(s.windows) + QStringLiteral(" window(s)")
                                + (s.attached ? QStringLiteral(" · attached") : QString()));
        }
        break;
    }
    case Arg::Window:
        for (const Target &w : snapshot().windows) {
            suggest(w.target, w.label);
        }
        break;
    case Arg::Pane:
        for (const Target &p : snapshot().panes) {
            suggest(p.target, p.label);
        }
        break;
    case Arg::Choice:
        for (const QString &c : opt.choices) {
            suggest(c, opt.desc);
        }
        break;
    case Arg::Text:
    case Arg::None:
        break;
    }

    if (out.size() == before) {
        QString sub = opt.desc;
        if (opt.arg == Arg::Session || opt.arg == Arg::Window || opt.arg == Arg::Pane) {
            sub += partial.isEmpty() ? QStringLiteral(" · no running tmux server found") : QStringLiteral(" · no match");
        }
        out << makeMatch(QLatin1Char('<') + opt.argName + QLatin1Char('>'), sub, 0.5, {kComplete, base + partial});
    }
}

void TmuxRunner::run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match)
{
    const QStringList data = match.data().toStringList();
    const QString kind = data.value(0);
    const QString payload = data.value(1);
    const KRunner::Action action = match.selectedAction();
    const QString actionId = action ? action.id() : QString();

    QString command = payload.trimmed();
    if (kind == kSession) {
        // "=name" makes tmux match the session name exactly instead of by prefix.
        command = QStringLiteral("tmux attach-session ")
            + (actionId == QLatin1String("attach-detach") ? QStringLiteral("-d ") : QString())
            + QStringLiteral("-t ") + KShell::quoteArg(QStringLiteral("=") + payload);
    }

    if (actionId == QLatin1String("copy")) {
        copyToClipboard(command);
        return;
    }

    if (kind == kComplete && actionId != QLatin1String("run")) {
        // Autocomplete: put the text back into KRunner and keep it open.
        context.requestQueryStringUpdate(payload, payload.size());
        return;
    }

    launchInTerminal(command);
}

#include "tmuxrunner.moc"
