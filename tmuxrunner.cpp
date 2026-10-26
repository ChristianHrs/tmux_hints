#include "tmuxrunner.h"
#include "tmuxcommands.h"

#include <KPluginFactory>
#include <KRunner/RunnerContext>
#include <KShell>
#include <KTerminalLauncherJob>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <utility>

K_PLUGIN_CLASS_WITH_JSON(TmuxRunner, "tmuxrunner.json")

namespace
{
const QString kIcon = QStringLiteral("utilities-terminal");

// Match data: [kind, payload, (optional unique suffix)]
const QString kComplete = QStringLiteral("complete"); // payload = new query text
const QString kRun = QStringLiteral("run");           // payload = command line
const QString kSession = QStringLiteral("session");   // payload = session name

QStringList tokenize(const QString &s)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return s.split(ws, Qt::SkipEmptyParts);
}

// Words of a tmux command with quotes/braces stripped, for matching.
QStringList commandWords(const QString &cmd)
{
    static const QRegularExpression sep(QStringLiteral("[\\s'\"{}]+"));
    return cmd.split(sep, Qt::SkipEmptyParts);
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

// ---- Key bindings --------------------------------------------------------

// "C-s" -> "Ctrl+S", "M-Left" -> "Alt+Left", "PPage" -> "Page Up"
QString humanizeKey(const QString &key)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("BSpace"), QStringLiteral("Backspace")},
        {QStringLiteral("PPage"), QStringLiteral("Page Up")},
        {QStringLiteral("NPage"), QStringLiteral("Page Down")},
        {QStringLiteral("Escape"), QStringLiteral("Esc")},
        {QStringLiteral("BTab"), QStringLiteral("Shift+Tab")},
        {QStringLiteral("DC"), QStringLiteral("Delete")},
        {QStringLiteral("IC"), QStringLiteral("Insert")},
    };
    QString rest = key;
    QStringList parts;
    while (rest.size() > 2 && rest.at(1) == QLatin1Char('-')) {
        const QChar m = rest.at(0);
        if (m == u'C') {
            parts << QStringLiteral("Ctrl");
        } else if (m == u'M') {
            parts << QStringLiteral("Alt");
        } else if (m == u'S') {
            parts << QStringLiteral("Shift");
        } else {
            break;
        }
        rest = rest.mid(2);
    }
    rest = names.value(rest, rest);
    if (!parts.isEmpty() && rest.size() == 1) {
        rest = rest.toUpper();
    }
    parts << rest;
    return parts.join(QLatin1Char('+'));
}

QString prefixLetter(const QString &prefix)
{
    return prefix.size() == 3 && prefix.startsWith(QLatin1String("C-")) ? prefix.mid(2).toLower() : QString();
}

// Used when no tmux server is running: look for "set -g prefix C-s" in the config.
QString prefixFromConfig()
{
    const QStringList files{
        QDir::homePath() + QStringLiteral("/.tmux.conf"),
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/tmux/tmux.conf"),
    };
    static const QRegularExpression re(QStringLiteral("^\\s*set(?:-option)?\\s+(?:-\\w+\\s+)*prefix\\s+(\\S+)"));
    QString found;
    for (const QString &path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream in(&f);
        while (!in.atEnd()) {
            const auto m = re.match(in.readLine());
            if (m.hasMatch()) {
                found = m.captured(1);
                found.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
            }
        }
    }
    return found;
}

// Best description for a command. Nested commands (inside confirm-before or
// command-prompt) win over the wrapper, more specific entries win over generic ones.
const KnownBinding *findKnownBinding(const QString &command)
{
    const QStringList words = commandWords(command);
    if (words.isEmpty()) {
        return nullptr;
    }
    const KnownBinding *best = nullptr;
    double bestScore = 0;
    for (const KnownBinding &k : knownBindings()) {
        const QStringList need = commandWords(k.command);
        if (need.isEmpty() || !std::all_of(need.cbegin(), need.cend(), [&](const QString &w) {
                return words.contains(w);
            })) {
            continue;
        }
        const double score = need.size() + (need.first() != words.first() ? 0.5 : 0.0);
        if (score > bestScore) {
            best = &k;
            bestScore = score;
        }
    }
    return best;
}

double knownIndex(const KnownBinding *k)
{
    return static_cast<double>(k - knownBindings().constData());
}

const KnownBinding *knownByCommand(const QString &command)
{
    for (const KnownBinding &k : knownBindings()) {
        if (k.command == command) {
            return &k;
        }
    }
    return nullptr;
}

bool isMouseKey(const QString &key)
{
    static const QRegularExpression re(QStringLiteral("Mouse|Wheel|Click|Drag"));
    return re.match(key).hasMatch();
}

void describe(TmuxBinding &b, const QString &note)
{
    static const QRegularExpression windowN(QStringLiteral("^select-window -t :?=?(\\d+)$"));
    const auto wn = windowN.match(commandWords(b.command).join(QLatin1Char(' ')));
    if (wn.hasMatch()) {
        b.desc = QStringLiteral("Go to window ") + wn.captured(1);
        b.keywords = QStringLiteral("number window tab switch go select index");
        b.order = 5.5;
        return;
    }
    if (const KnownBinding *k = findKnownBinding(b.command)) {
        b.desc = k->desc;
        b.keywords = k->keywords;
        b.order = knownIndex(k);
        return;
    }
    const Command *c = findCommand(commandWords(b.command).value(0), false);
    b.desc = !note.isEmpty() ? note : c ? c->desc : QStringLiteral("Custom binding");
    b.order = 1000; // not in our list: show after the common ones
}

// Parses `tmux list-keys`, e.g.:  bind-key -r -T prefix C-Up resize-pane -U
QList<TmuxBinding> parseBindings(const QStringList &lines)
{
    QList<TmuxBinding> out;
    bool digits = false;
    for (const QString &line : lines) {
        KShell::Errors err = KShell::NoError;
        const QStringList t = KShell::splitArgs(line, KShell::NoOptions, &err);
        if (err != KShell::NoError || t.size() < 3 || t.first() != QLatin1String("bind-key")) {
            continue;
        }
        TmuxBinding b;
        b.table = QStringLiteral("prefix");
        QString note;
        int i = 1;
        while (i < t.size()) {
            const QString &f = t.at(i);
            if (f == QLatin1String("-r")) {
                ++i;
            } else if (f == QLatin1String("-n")) {
                b.table = QStringLiteral("root");
                ++i;
            } else if (f == QLatin1String("-T") && i + 1 < t.size()) {
                b.table = t.at(i + 1);
                i += 2;
            } else if (f == QLatin1String("-N") && i + 1 < t.size()) {
                note = t.at(i + 1);
                i += 2;
            } else {
                break;
            }
        }
        if (i >= t.size() || (b.table != QLatin1String("prefix") && b.table != QLatin1String("root"))) {
            continue; // copy-mode tables etc.
        }
        b.key = t.at(i);
        b.command = KShell::joinArgs(t.mid(i + 1));
        if (b.command.isEmpty() || isMouseKey(b.key)) {
            continue;
        }

        // Collapse the ten "go to window N" bindings into one entry.
        static const QRegularExpression windowN(QStringLiteral("^select-window -t :?=?\\d$"));
        if (b.table == QLatin1String("prefix") && b.key.size() == 1 && b.key.at(0).isDigit()
            && windowN.match(commandWords(b.command).join(QLatin1Char(' '))).hasMatch()) {
            digits = true;
            continue;
        }

        describe(b, note);
        out << b;
    }

    if (digits) {
        if (const KnownBinding *k = knownByCommand(QStringLiteral("select-window -t :=N"))) {
            out << TmuxBinding{QStringLiteral("prefix"), k->key, QStringLiteral("select-window -t :=N"), k->desc, k->keywords, knownIndex(k)};
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const TmuxBinding &a, const TmuxBinding &b) {
        return a.order < b.order;
    });
    return out;
}

QList<TmuxBinding> defaultBindings(const QString &prefix)
{
    QList<TmuxBinding> out;
    for (const KnownBinding &k : knownBindings()) {
        const bool isSendPrefix = k.command == QLatin1String("send-prefix");
        out << TmuxBinding{QStringLiteral("prefix"), isSendPrefix ? prefix : k.key, k.command, k.desc, k.keywords, knownIndex(&k)};
    }
    return out;
}

QStringList searchTerms(const QStringList &words)
{
    static const QSet<QString> stop{
        QStringLiteral("how"), QStringLiteral("to"), QStringLiteral("do"), QStringLiteral("i"),
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("my"),
        QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("with"), QStringLiteral("tmux"),
        QStringLiteral("shortcut"), QStringLiteral("shortcuts"), QStringLiteral("hotkey"), QStringLiteral("key"),
    };
    QStringList out;
    for (const QString &w : words) {
        const QString l = w.toLower();
        if (!stop.contains(l)) {
            out << l;
        }
    }
    return out;
}

// Every search term must be the start of some word in the description/keywords/command.
bool bindingMatches(const TmuxBinding &b, const QStringList &terms)
{
    static const QRegularExpression sep(QStringLiteral("[^\\w-]+"));
    const QString text = b.desc + QLatin1Char(' ') + b.keywords + QLatin1Char(' ') + commandWords(b.command).join(QLatin1Char(' '));
    const QStringList hay = text.toLower().split(sep, Qt::SkipEmptyParts);
    for (const QString &t : terms) {
        if (!std::any_of(hay.cbegin(), hay.cend(), [&](const QString &h) {
                return h.startsWith(t);
            })) {
            return false;
        }
    }
    return true;
}

QString shortcutText(const TmuxBinding &b, const QString &prefix)
{
    if (b.table == QLatin1String("root")) {
        return humanizeKey(b.key) + QStringLiteral(" (no prefix)");
    }
    return humanizeKey(prefix) + QStringLiteral(" then ") + humanizeKey(b.key);
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
    addSyntax(QStringLiteral("tmux ctrl + b"), QStringLiteral("Lists the shortcuts for your prefix key (use your own prefix, or type \"tmux prefix\")"));
    addSyntax(QStringLiteral("tmux split screen"), QStringLiteral("Finds the keyboard shortcut for what you describe"));
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

        // Your real prefix and bindings, including everything from your tmux.conf
        const QStringList prefix = runTmux({QStringLiteral("show-options"), QStringLiteral("-gv"), QStringLiteral("prefix")});
        if (!prefix.isEmpty() && !prefix.first().trimmed().isEmpty()) {
            s.prefix = prefix.first().trimmed();
        }
        s.bindings = parseBindings(runTmux({QStringLiteral("list-keys")}));
        s.liveBindings = !s.bindings.isEmpty();
    }

    if (!s.liveBindings) {
        const QString configured = prefixFromConfig();
        if (!configured.isEmpty()) {
            s.prefix = configured;
        }
        s.bindings = defaultBindings(s.prefix);
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

KRunner::QueryMatch TmuxRunner::bindingMatch(const TmuxBinding &b, const QString &prefix, qreal relevance, bool descFirst)
{
    const QString shortcut = shortcutText(b, prefix);
    const QString command = QStringLiteral("tmux ") + b.command;
    const QStringList data{kComplete, command + QLatin1Char(' '), b.table + QLatin1Char(':') + b.key};

    // Search results lead with what it does; the prefix list leads with the keys.
    auto m = descFirst ? makeMatch(b.desc, QStringLiteral("Shortcut: ") + shortcut + QStringLiteral("  ·  ") + command, relevance, data)
                       : makeMatch(shortcut, b.desc + QStringLiteral("  ·  ") + command, relevance, data);
    m.setActions({m_runAction, m_copyAction});
    return m;
}

void TmuxRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query();
    static const QRegularExpression head(QStringLiteral("^tmux(\\s|$)"), QRegularExpression::CaseInsensitiveOption);
    if (!head.match(query).hasMatch()) {
        return;
    }

    // "tmux ctrl + s", "tmux ctrl+s split", "tmux C-s %", "tmux prefix"
    static const QRegularExpression prefixRe(
        QStringLiteral("^\\s*(?:prefix|ctrl|control|c-|\\^)\\s*[-+]?\\s*(?:([a-z0-9])(?![a-z0-9]))?\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto pm = prefixRe.match(query.mid(4));
    if (pm.hasMatch()) {
        QList<KRunner::QueryMatch> matches;
        matchPrefixMode(query, pm.captured(1), pm.captured(2).trimmed(), matches);
        if (context.isValid()) {
            context.addMatches(matches);
        }
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

    // Plain-words search over shortcuts: "tmux split scr" -> "Split the pane ... Ctrl+S then %"
    QStringList words = tokens;
    if (!partial.isEmpty()) {
        words << partial;
    }
    QList<KRunner::QueryMatch> searchMatches;
    bool strongSearch = false;
    const bool hasFlag = std::any_of(words.cbegin(), words.cend(), [](const QString &w) {
        return w.startsWith(QLatin1Char('-'));
    });
    if (!words.isEmpty() && !hasFlag) {
        // Several words, or a word that isn't a command, reads like a description.
        strongSearch = words.size() >= 2 || !findCommand(words.first());
        matchBindingSearch(words, strongSearch, searchMatches);
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
    } else if (searchMatches.isEmpty()) {
        matches << makeMatch(QStringLiteral("Unknown tmux command: %1").arg(tokens.first()),
                             QStringLiteral("Type \"tmux \" to see all commands, or describe what you want, e.g. \"tmux split screen\""),
                             0.1, {kComplete, QStringLiteral("tmux ")});
    }

    // If it reads like a description, the shortcuts beat "Run: tmux split scr".
    if (strongSearch && !searchMatches.isEmpty()) {
        for (KRunner::QueryMatch &m : matches) {
            if (m.data().toStringList().value(0) == kRun) {
                m.setRelevance(0.6);
            }
        }
    }
    matches += searchMatches;

    if (context.isValid()) {
        context.addMatches(matches);
    }
}

void TmuxRunner::matchPrefixMode(const QString &query, const QString &typedKey, const QString &filter, QList<KRunner::QueryMatch> &out)
{
    const Snapshot snap = snapshot();
    const QString prefixText = humanizeKey(snap.prefix);

    if (!typedKey.isEmpty() && typedKey.toLower() != prefixLetter(snap.prefix)) {
        out << makeMatch(QStringLiteral("Your tmux prefix is %1, not Ctrl+%2").arg(prefixText, typedKey.toUpper()),
                         QStringLiteral("Showing your shortcuts anyway"), 1.0, {kComplete, query});
    } else if (filter.isEmpty()) {
        out << makeMatch(QStringLiteral("Prefix: %1  (press it, let go, then the key)").arg(prefixText),
                         snap.liveBindings ? QStringLiteral("Read from your running tmux server, including your own bindings")
                                           : QStringLiteral("tmux isn't running, so these are the default bindings"),
                         1.0, {kComplete, query});
    }

    const QStringList terms = searchTerms(tokenize(filter));
    int n = 0;
    for (const TmuxBinding &b : snap.bindings) {
        const bool keyHit = !filter.isEmpty()
            && (b.key == filter || humanizeKey(b.key).compare(filter, Qt::CaseInsensitive) == 0);
        if (!filter.isEmpty() && !keyHit && (terms.isEmpty() || !bindingMatches(b, terms))) {
            continue;
        }
        out << bindingMatch(b, snap.prefix, keyHit ? 0.99 : 0.95 - n * 0.003, false);
        ++n;
    }

    if (n == 0) {
        out << makeMatch(QStringLiteral("No shortcut matches \"%1\"").arg(filter),
                         QStringLiteral("Try a word like split, window, zoom, copy or detach"), 0.5, {kComplete, query});
    }
}

int TmuxRunner::matchBindingSearch(const QStringList &words, bool strong, QList<KRunner::QueryMatch> &out)
{
    const QStringList terms = searchTerms(words);
    if (terms.isEmpty()) {
        return 0;
    }
    const Snapshot snap = snapshot();
    int n = 0;
    for (const TmuxBinding &b : snap.bindings) {
        if (!bindingMatches(b, terms)) {
            continue;
        }
        out << bindingMatch(b, snap.prefix, (strong ? 0.97 : 0.7) - n * 0.002, true);
        if (++n >= 8) {
            break;
        }
    }
    return n;
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
