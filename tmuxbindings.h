#pragma once

// Human descriptions and search keywords for tmux key bindings.
//
// Bindings are matched by the *command* they run, not the key, so if you rebind
// split-window -h from % to |, your | still gets the right description.
// The order of this list is the order shortcuts are shown in (most common first).

#include <QList>
#include <QString>

struct KnownBinding {
    QString key;      // default key in the prefix table
    QString command;  // words that identify the command
    QString desc;
    QString keywords; // extra words for "tmux split screen"-style searches
};

// A binding as shown to the user (either read live from tmux or from the defaults below).
struct TmuxBinding {
    QString table;    // "prefix" or "root"
    QString key;      // tmux key name, e.g. "%" or "C-Up"
    QString command;
    QString desc;
    QString keywords;
    double order = 0;
};

inline const QList<KnownBinding> &knownBindings()
{
    static const QList<KnownBinding> list{
        {"%", "split-window -h", "Split the pane left/right (new pane on the right)",
         "split screen vertical vertically side by side columns new pane divide"},
        {"\"", "split-window", "Split the pane top/bottom (new pane below)",
         "split screen horizontal horizontally stacked rows below new pane divide"},
        {"c", "new-window", "Create a new window", "new create open window tab"},
        {"n", "next-window", "Go to the next window", "next switch window tab move go"},
        {"p", "previous-window", "Go to the previous window", "previous prev back switch window tab move go"},
        {"0 … 9", "select-window -t :=N", "Go to window 0–9", "number window tab switch go select index"},
        {"d", "detach-client", "Detach: leave tmux, the session keeps running",
         "detach leave exit quit background close disconnect"},
        {"z", "resize-pane -Z", "Zoom: make the pane fill the window (again to undo)",
         "zoom maximize maximise fullscreen full screen unzoom toggle"},
        {"Up", "select-pane -U", "Move to the pane above", "move go switch focus navigate pane arrow up above"},
        {"Down", "select-pane -D", "Move to the pane below", "move go switch focus navigate pane arrow down below"},
        {"Left", "select-pane -L", "Move to the pane on the left", "move go switch focus navigate pane arrow left"},
        {"Right", "select-pane -R", "Move to the pane on the right", "move go switch focus navigate pane arrow right"},
        {"o", "select-pane -t :.+", "Go to the next pane", "next pane cycle switch focus move go"},
        {"x", "kill-pane", "Close the current pane (asks first)", "close kill pane exit quit remove delete"},
        {"&", "kill-window", "Close the current window (asks first)", "close kill window tab exit quit remove delete"},
        {"w", "choose-tree -Zw", "Pick a window from a tree of all sessions",
         "choose list switch window windows session sessions overview tree browse"},
        {"s", "choose-tree -Zs", "Pick a session from a list", "choose list switch session sessions browse"},
        {",", "rename-window", "Rename the current window", "rename name title window tab"},
        {"$", "rename-session", "Rename the current session", "rename name title session"},
        {"[", "copy-mode", "Scroll / copy mode: scroll back and select text",
         "scroll scrollback history copy select search up page"},
        {"]", "paste-buffer", "Paste the most recently copied text", "paste insert clipboard buffer"},
        {"?", "list-keys", "Show all key bindings", "help keys bindings shortcuts list cheatsheet"},
        {":", "command-prompt", "Open the tmux command prompt", "command prompt type run"},
        {"q", "display-panes", "Show pane numbers (press a number to jump)", "pane panes numbers show identify jump"},
        {";", "last-pane", "Go back to the previously active pane", "last previous pane toggle back"},
        {"l", "last-window", "Go back to the previously active window", "last previous window tab toggle back"},
        {"(", "switch-client -p", "Switch to the previous session", "previous session switch"},
        {")", "switch-client -n", "Switch to the next session", "next session switch"},
        {"L", "switch-client -l", "Switch to the last used session", "last session switch toggle"},
        {"!", "break-pane", "Move the pane into its own new window", "break pane window pop out separate"},
        {"Space", "next-layout", "Cycle through pane layouts", "layout layouts arrange rearrange cycle"},
        {"E", "select-layout -E", "Make the panes evenly sized", "even equal spread balance layout resize"},
        {"{", "swap-pane -U", "Swap the pane with the previous one", "swap move pane exchange"},
        {"}", "swap-pane -D", "Swap the pane with the next one", "swap move pane exchange"},
        {"C-o", "rotate-window", "Rotate the panes in the window", "rotate move panes cycle"},
        {"C-Up", "resize-pane -U", "Resize the pane upwards (hold to repeat)", "resize size bigger smaller grow shrink up"},
        {"C-Down", "resize-pane -D", "Resize the pane downwards (hold to repeat)", "resize size bigger smaller grow shrink down"},
        {"C-Left", "resize-pane -L", "Resize the pane to the left (hold to repeat)", "resize size bigger smaller grow shrink left"},
        {"C-Right", "resize-pane -R", "Resize the pane to the right (hold to repeat)", "resize size bigger smaller grow shrink right"},
        {"f", "find-window", "Find a window by name or content", "find search window"},
        {"'", "select-window", "Go to a window by number (asks for it)", "go window number index prompt"},
        {".", "move-window", "Move the window to another number", "move window number index renumber"},
        {"t", "clock-mode", "Show a big clock", "clock time"},
        {"D", "choose-client", "Pick a client to detach", "detach client choose"},
        {"r", "refresh-client", "Redraw the screen", "refresh redraw repaint"},
        {"=", "choose-buffer", "Pick a paste buffer to paste", "paste buffer choose clipboard"},
        {"#", "list-buffers", "List paste buffers", "list buffers paste"},
        {"-", "delete-buffer", "Delete the latest paste buffer", "delete buffer"},
        {"~", "show-messages", "Show recent tmux messages", "messages log"},
        {"m", "select-pane -m", "Mark the pane", "mark pane"},
        {"M", "select-pane -M", "Clear the marked pane", "unmark mark pane"},
        {"C-z", "suspend-client", "Suspend tmux (like Ctrl+Z in a shell)", "suspend background"},
        {"C-b", "send-prefix", "Send the prefix key to the program in the pane", "send prefix passthrough"},
    };
    return list;
}
