#pragma once

// Data-driven description of tmux commands, their flags and arguments.
// Add or tweak entries here; the runner logic doesn't need to change.

#include <QList>
#include <QString>
#include <QStringList>

enum class Arg {
    None,    // plain flag, no argument
    Text,    // free text, we can only show a hint
    Session, // suggest running sessions
    Window,  // suggest session:window
    Pane,    // suggest session:window.pane
    Choice,  // suggest from Option::choices
};

struct Option {
    QChar flag;
    QString desc;
    Arg arg = Arg::None;
    QString argName = {};
    QStringList choices = {};
};

struct Command {
    QString name;
    QStringList aliases;
    QString desc;
    QList<Option> options;
    QString positional = {};     // hint for trailing positional argument(s)
    QStringList choices = {};    // suggestions for the first positional argument
    bool needsClient = false;    // takes over the terminal (attach etc.)
};

inline const QList<Command> &commands()
{
    static const QList<Command> list = [] {
        auto ts = [](const QString &d = "Target session") { return Option{u't', d, Arg::Session, "target-session"}; };
        auto tw = [](const QString &d = "Target window") { return Option{u't', d, Arg::Window, "target-window"}; };
        auto tp = [](const QString &d = "Target pane") { return Option{u't', d, Arg::Pane, "target-pane"}; };
        auto fmt = [] { return Option{u'F', "Output format, e.g. '#{session_name}'", Arg::Text, "format"}; };
        auto filter = [] { return Option{u'f', "Only show entries matching this filter", Arg::Text, "filter"}; };
        auto startDir = [] { return Option{u'c', "Start directory", Arg::Text, "start-directory"}; };
        auto env = [] { return Option{u'e', "Set an environment variable", Arg::Text, "VAR=value"}; };
        const QStringList keyTables{"prefix", "root", "copy-mode", "copy-mode-vi"};

        return QList<Command>{
            // ---- Sessions -------------------------------------------------
            {"attach-session", {"attach", "a"}, "Attach this terminal to an existing session",
             {ts("Session to attach to"),
              {u'd', "Detach any other clients attached to the session"},
              {u'r', "Read-only: only detach/switch keys work"},
              {u'x', "With -d, send SIGHUP to the other clients' parent process"},
              {u'E', "Don't apply update-environment"},
              {u'c', "Set the session's working directory", Arg::Text, "working-directory"},
              {u'f', "Client flags (comma-separated)", Arg::Choice, "flags",
               {"read-only", "ignore-size", "active-pane", "no-output", "wait-exit"}}},
             {}, {}, true},

            {"new-session", {"new"}, "Create a new session (and attach to it unless -d)",
             {{u's', "Name for the new session", Arg::Text, "session-name"},
              {u'n', "Name for the first window", Arg::Text, "window-name"},
              {u'd', "Don't attach: create the session in the background"},
              {u'A', "Attach instead if a session with that name already exists"},
              {u'D', "With -A, detach other clients"},
              {u'X', "With -A, send SIGHUP to other clients' parent"},
              startDir(),
              env(),
              {u't', "Join a session group (share windows with this session)", Arg::Session, "group-name"},
              {u'x', "Width (with -d)", Arg::Text, "width"},
              {u'y', "Height (with -d)", Arg::Text, "height"},
              {u'P', "Print information about the new session"},
              fmt(),
              {u'E', "Don't apply update-environment"}},
             "shell-command", {}, true},

            {"list-sessions", {"ls"}, "List all sessions", {fmt(), filter()}},

            {"kill-session", {}, "Destroy a session and all its windows",
             {ts("Session to kill"),
              {u'a', "Kill all sessions except the target"},
              {u'C', "Clear alerts (bell, activity, silence) instead"}}},

            {"kill-server", {}, "Kill the tmux server: ends every session and client", {}},

            {"rename-session", {"rename"}, "Rename a session", {ts("Session to rename")}, "new-name"},

            {"has-session", {"has"}, "Check whether a session exists (exit code 0 if it does)", {ts()}},

            {"detach-client", {"detach"}, "Detach a client from its session",
             {{u's', "Detach every client attached to this session", Arg::Session, "target-session"},
              {u't', "Client to detach", Arg::Text, "target-client"},
              {u'a', "Detach all clients except the target"},
              {u'P', "Send SIGHUP to the client's parent (closes the shell)"},
              {u'E', "Run this command to replace the client", Arg::Text, "shell-command"}}},

            {"switch-client", {"switchc"}, "Switch a client to another session (run inside tmux)",
             {ts("Session to switch to"),
              {u'l', "Switch to the last session"},
              {u'n', "Switch to the next session"},
              {u'p', "Switch to the previous session"},
              {u'r', "Toggle read-only mode"},
              {u'c', "Client to switch", Arg::Text, "target-client"},
              {u'T', "Set the client's key table", Arg::Choice, "key-table", keyTables}}},

            {"list-clients", {"lsc"}, "List attached clients", {ts("Only clients on this session"), fmt(), filter()}},

            {"start-server", {"start"}, "Start the tmux server without creating a session", {}},

            // ---- Windows --------------------------------------------------
            {"new-window", {"neww"}, "Create a new window",
             {{u'n', "Window name", Arg::Text, "window-name"},
              tw("Where to create the window"),
              startDir(),
              {u'd', "Don't make the new window current"},
              {u'a', "Insert after the target window"},
              {u'b', "Insert before the target window"},
              {u'k', "Kill an existing window at the target index"},
              {u'S', "If a window with this name exists, select it"},
              env()},
             "shell-command"},

            {"list-windows", {"lsw"}, "List windows",
             {{u'a', "List all windows on the server"}, ts("Session to list"), fmt(), filter()}},

            {"kill-window", {"killw"}, "Kill a window",
             {tw("Window to kill"), {u'a', "Kill all windows except the target"}}},

            {"rename-window", {"renamew"}, "Rename a window", {tw("Window to rename")}, "new-name"},

            {"select-window", {"selectw"}, "Make a window current",
             {tw("Window to select"),
              {u'l', "Go to the last window"},
              {u'n', "Go to the next window"},
              {u'p', "Go to the previous window"},
              {u'T', "If already current, go to the last window instead"}}},

            {"next-window", {"next"}, "Move to the next window", {ts(), {u'a', "Next window with an alert"}}},
            {"previous-window", {"prev"}, "Move to the previous window", {ts(), {u'a', "Previous window with an alert"}}},
            {"last-window", {"last"}, "Move to the previously selected window", {ts()}},

            {"move-window", {"movew"}, "Move a window to another index or session",
             {{u's', "Window to move", Arg::Window, "src-window"},
              tw("Destination"),
              {u'r', "Renumber all windows in the session (with -t session)"},
              {u'a', "Move to the index after the destination"},
              {u'b', "Move to the index before the destination"},
              {u'd', "Don't make the moved window current"},
              {u'k', "Kill a window already at the destination"}}},

            {"swap-window", {"swapw"}, "Swap two windows",
             {{u's', "Source window", Arg::Window, "src-window"},
              tw("Destination window"),
              {u'd', "Don't change the current window"}}},

            // ---- Panes ----------------------------------------------------
            {"split-window", {"splitw"}, "Split a pane into two",
             {{u'h', "Split left/right (new pane to the right)"},
              {u'v', "Split top/bottom (new pane below, default)"},
              tp("Pane to split"),
              {u'b', "Put the new pane left/above instead"},
              {u'f', "New pane spans the full window width/height"},
              {u'l', "Size of the new pane (lines/columns or e.g. 30%)", Arg::Text, "size"},
              startDir(),
              {u'd', "Don't make the new pane active"},
              {u'Z', "Zoom the new pane"},
              env()},
             "shell-command"},

            {"list-panes", {"lsp"}, "List panes",
             {{u'a', "List all panes on the server"},
              {u's', "Target is a session: list all its panes"},
              {u't', "Window to list (or session with -s)", Arg::Window, "target"},
              fmt(), filter()}},

            {"kill-pane", {"killp"}, "Kill a pane",
             {tp("Pane to kill"), {u'a', "Kill all panes except the target"}}},

            {"select-pane", {"selectp"}, "Make a pane active",
             {tp("Pane to select"),
              {u'L', "Select the pane to the left"},
              {u'R', "Select the pane to the right"},
              {u'U', "Select the pane above"},
              {u'D', "Select the pane below"},
              {u'l', "Select the last active pane"},
              {u'Z', "Keep the window zoomed"},
              {u'T', "Set the pane title", Arg::Text, "title"},
              {u'm', "Mark the pane"},
              {u'M', "Clear the marked pane"},
              {u'd', "Disable input to the pane"},
              {u'e', "Enable input to the pane"}}},

            {"resize-pane", {"resizep"}, "Resize a pane or toggle zoom",
             {tp("Pane to resize"),
              {u'Z', "Toggle zoom (pane fills the window)"},
              {u'L', "Resize towards the left"},
              {u'R', "Resize towards the right"},
              {u'U', "Resize upwards"},
              {u'D', "Resize downwards"},
              {u'x', "Absolute width (cells or %)", Arg::Text, "width"},
              {u'y', "Absolute height (cells or %)", Arg::Text, "height"}},
             "adjustment (cells, default 1)"},

            {"swap-pane", {"swapp"}, "Swap two panes",
             {{u's', "Source pane", Arg::Pane, "src-pane"},
              tp("Destination pane"),
              {u'U', "Swap with the previous pane"},
              {u'D', "Swap with the next pane"},
              {u'd', "Don't change the active pane"},
              {u'Z', "Keep the window zoomed"}}},

            {"break-pane", {"breakp"}, "Move a pane out into its own window",
             {{u's', "Pane to break out", Arg::Pane, "src-pane"},
              tw("Destination window"),
              {u'n', "Name for the new window", Arg::Text, "window-name"},
              {u'd', "Don't make the new window current"},
              {u'P', "Print information about the new window"}}},

            {"join-pane", {"joinp"}, "Move a pane into another window",
             {{u's', "Pane to move", Arg::Pane, "src-pane"},
              tp("Destination pane"),
              {u'h', "Join side by side"},
              {u'v', "Join stacked (default)"},
              {u'b', "Place it before (left/above) the destination"},
              {u'l', "Size (lines/columns or %)", Arg::Text, "size"},
              {u'd', "Don't make it active"},
              {u'f', "Span the full window width/height"}}},

            {"select-layout", {"selectl"}, "Apply a pane layout",
             {tp("Pane in the window to lay out"),
              {u'n', "Next layout"},
              {u'p', "Previous layout"},
              {u'o', "Undo the last layout change"},
              {u'E', "Spread the current pane and its neighbours evenly"}},
             "layout-name",
             {"even-horizontal", "even-vertical", "main-horizontal", "main-vertical", "tiled"}},

            {"respawn-pane", {"respawnp"}, "Restart the command in a pane",
             {tp(), {u'k', "Kill the running command first"}, startDir()}, "shell-command"},

            {"pipe-pane", {"pipep"}, "Pipe a pane's output to a command (e.g. logging)",
             {tp(),
              {u'o', "Only open a pipe if none exists (toggle)"},
              {u'I', "Feed the command's stdout into the pane"},
              {u'O', "Send the pane's output to the command (default)"}},
             "shell-command (e.g. 'cat >> ~/pane.log')"},

            {"send-keys", {"send"}, "Send keys to a pane",
             {tp("Pane to send keys to"),
              {u'l', "Send keys literally (no key-name lookup)"},
              {u'R', "Reset the terminal state"},
              {u'X', "Send a copy-mode command", Arg::Text, "command"},
              {u'N', "Repeat count", Arg::Text, "count"},
              {u'H', "Keys are hex codes"}},
             "keys (e.g. 'make' Enter, or C-c)"},

            {"capture-pane", {"capturep"}, "Capture a pane's contents",
             {tp(),
              {u'p', "Print to stdout instead of a buffer"},
              {u'S', "Start line ('-' = start of history)", Arg::Text, "start-line"},
              {u'E', "End line ('-' = end of visible area)", Arg::Text, "end-line"},
              {u'J', "Join wrapped lines"},
              {u'e', "Include colour escape sequences"},
              {u'b', "Buffer name", Arg::Text, "buffer-name"}}},

            {"copy-mode", {}, "Enter copy (scrollback) mode",
             {tp(),
              {u'u', "Scroll up one page"},
              {u'e', "Exit when scrolled to the bottom"},
              {u'q', "Cancel copy mode"}}},

            {"choose-tree", {}, "Interactive session/window picker (run inside tmux)",
             {{u's', "Start with sessions collapsed"},
              {u'w', "Start with windows collapsed"},
              {u'Z', "Zoom the pane"},
              tp()}},

            {"display-message", {"display"}, "Show a message or expand a format",
             {tp(),
              {u'p', "Print to stdout"},
              {u'a', "List all format variables"},
              {u'd', "How long to display it (ms)", Arg::Text, "delay"}},
             "message (e.g. '#{session_name}')"},

            // ---- Config & keys --------------------------------------------
            {"source-file", {"source"}, "Load a config file",
             {{u'q', "Don't complain about missing files"},
              {u'n', "Parse only, don't run anything"},
              {u'v', "Show the parsed commands"}},
             "path (e.g. ~/.tmux.conf)"},

            {"set-option", {"set"}, "Set an option",
             {{u'g', "Global option"},
              {u's', "Server option"},
              {u'w', "Window option"},
              {u'p', "Pane option"},
              {u'u', "Unset the option"},
              {u'a', "Append to the current value"},
              {u'o', "Don't overwrite if already set"},
              {u'q', "No error for unknown options"},
              {u'F', "Expand formats in the value"},
              {u't', "Target session/window/pane", Arg::Pane, "target"}},
             "option value",
             {"mouse", "prefix", "base-index", "pane-base-index", "history-limit", "escape-time",
              "mode-keys", "status", "status-position", "default-terminal", "renumber-windows",
              "set-clipboard"}},

            {"show-options", {"show"}, "Show options",
             {{u'g', "Global options"},
              {u's', "Server options"},
              {u'w', "Window options"},
              {u'p', "Pane options"},
              {u'v', "Show only the value"},
              {u'A', "Include inherited options"},
              {u't', "Target session/window/pane", Arg::Pane, "target"}},
             "option"},

            {"bind-key", {"bind"}, "Bind a key to a command",
             {{u'n', "No prefix needed (same as -T root)"},
              {u'r', "Repeatable without pressing the prefix again"},
              {u'T', "Key table", Arg::Choice, "key-table", keyTables},
              {u'N', "Note shown by list-keys -N", Arg::Text, "note"}},
             "key command"},

            {"unbind-key", {"unbind"}, "Remove a key binding",
             {{u'a', "Remove all bindings (in the table)"},
              {u'n', "Binding without prefix (root table)"},
              {u'T', "Key table", Arg::Choice, "key-table", keyTables},
              {u'q', "Quiet: no error if not bound"}},
             "key"},

            {"list-keys", {"lsk"}, "List key bindings",
             {{u'T', "Key table", Arg::Choice, "key-table", keyTables},
              {u'N', "Show notes (like Prefix ?)"},
              {u'a', "With -N, include keys without notes"}},
             "key"},

            {"list-commands", {"lscm"}, "List all tmux commands", {fmt()}, "command"},

            // ---- Buffers --------------------------------------------------
            {"list-buffers", {"lsb"}, "List paste buffers", {fmt(), filter()}},

            {"paste-buffer", {"pasteb"}, "Paste a buffer into a pane",
             {{u'b', "Buffer name", Arg::Text, "buffer-name"},
              tp(),
              {u'd', "Delete the buffer after pasting"},
              {u'p', "Use bracketed paste if the app supports it"}}},

            {"save-buffer", {"saveb"}, "Save a buffer to a file",
             {{u'b', "Buffer name", Arg::Text, "buffer-name"},
              {u'a', "Append instead of overwriting"}},
             "path"},

            // ---- Commands used by the default key bindings ----------------
            {"confirm-before", {"confirm"}, "Ask y/n before running a command",
             {{u'p', "Prompt text", Arg::Text, "prompt"}, tp()}, "command"},
            {"command-prompt", {}, "Open the tmux command prompt",
             {{u'p', "Prompt text", Arg::Text, "prompts"}, {u'I', "Initial text", Arg::Text, "inputs"}, tp()},
             "template"},
            {"display-panes", {"displayp"}, "Show pane numbers in each pane",
             {{u'd', "How long to show them (ms)", Arg::Text, "duration"}}},
            {"last-pane", {"lastp"}, "Go back to the previously active pane", {tw()}},
            {"next-layout", {"nextl"}, "Switch to the next pane layout", {tw()}},
            {"rotate-window", {"rotatew"}, "Rotate the panes in a window",
             {tw(), {u'D', "Rotate downwards"}, {u'U', "Rotate upwards (default)"}}},
            {"find-window", {"findw"}, "Search windows by name, title or content",
             {{u'N', "Match window names only"}, {u'C', "Match visible content only"},
              {u'T', "Match pane titles only"}, tp()},
             "match-string"},
            {"clock-mode", {}, "Show a big clock in a pane", {tp()}},
            {"choose-client", {}, "Interactive client picker (run inside tmux)", {{u'Z', "Zoom the pane"}, tp()}},
            {"choose-buffer", {}, "Interactive paste-buffer picker (run inside tmux)", {{u'Z', "Zoom the pane"}, tp()}},
            {"refresh-client", {"refresh"}, "Redraw a client",
             {{u'S', "Only redraw the status line"}, {u't', "Client", Arg::Text, "target-client"}}},
            {"suspend-client", {"suspendc"}, "Suspend a client", {{u't', "Client", Arg::Text, "target-client"}}},
            {"show-messages", {"showmsgs"}, "Show the tmux message log", {{u't', "Client", Arg::Text, "target-client"}}},
            {"delete-buffer", {"deleteb"}, "Delete a paste buffer", {{u'b', "Buffer name", Arg::Text, "buffer-name"}}},
            {"send-prefix", {}, "Send the prefix key to a pane", {{u'2', "Send the secondary prefix"}, tp()}},
            {"clear-history", {"clearhist"}, "Clear a pane's scrollback history", {tp()}},
        };
    }();
    return list;
}
