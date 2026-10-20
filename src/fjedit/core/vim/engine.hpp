#pragma once

// Vim on one view of a document: its modes, and what each key typed does in
// the mode the view is in. It takes the keys one at a time, as the keyboard
// gives them (a character typed, or a key that types none), and acts at
// every caret through the CommandContext the view lends it. The view feeds
// it, draws its carets in the shape the mode gives, and hands on the `:`
// commands that are the program's for the program to run.
//
//     fjedit::vim::Engine vim;
//     for (const fjedit::Chord& key : keys) {
//         if (std::optional<fjedit::vim::ExCommand> handed = vim.take(key, context)) run(*handed);
//     }
//
// In normal mode each caret is an empty selection whose caret sits before
// the character Vim's block covers, and never past a line's last character
// unless the line is empty.

#include "fjedit/core/commands.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/core/vim/ex.hpp"
#include "fjedit/core/vim/motions.hpp"
#include "fjedit/core/vim/pattern.hpp"
#include "fjedit/core/vim/registers.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fjedit::vim {

enum class Mode : uint8_t {
    /// Keys are commands, each caret on a character.
    Normal,
    /// Keys type text, each caret between characters.
    Insert,
    /// Keys type text over the characters at the carets, `R`.
    Replace,
    /// Keys move one end of each selection, `v`; an operator acts on the
    /// selections. They take the characters at both ends.
    Visual,
    /// The same with whole lines, `V`.
    VisualLine,
    /// A block of columns, `Ctrl-V`: a selection on each of its lines.
    VisualBlock,
    /// The command line, `:`, `/` and `?`: keys type a command or a pattern
    /// into it.
    Command,
};

/// Whether `mode` is one of the visual modes.
[[nodiscard]] constexpr bool visual(Mode mode) {
    return mode == Mode::Visual || mode == Mode::VisualLine || mode == Mode::VisualBlock;
}

/// The keymap mode whose bindings Vim in `mode` looks up before its own
/// keys: insert's in insert and replace mode, visual's in the visual modes,
/// the command line's while it is typed, normal's otherwise.
[[nodiscard]] std::string_view keymap_mode(Mode mode);

/// What `:set` changes, shared by the engines of every view. Vim's own
/// settings are kept here; the view's two, which Preferences keep, only as
/// `:set` made them for the session, for the program to show over
/// Preferences', with what the views show now for `:set` to say.
struct Settings {
    /// ignorecase and smartcase.
    PatternSettings pattern;
    /// hlsearch: the last search's matches stay shown.
    bool hlsearch{true};
    /// relativenumber and scrolloff as `:set` made them; nothing where it
    /// didn't.
    std::optional<bool> relative_numbers;
    std::optional<std::size_t> around_caret;
    /// What the views show, Preferences' or `:set`'s.
    bool relative_numbers_shown{false};
    std::size_t around_caret_shown{0};
};

/// The last search: what it looks for as Vim and as the find read it, and
/// which way `n` goes.
struct Search {
    std::string text;
    Pattern pattern;
    bool forward{true};
};

/// The command line while it is typed: `:`, `/` or `?`, then what is typed.
struct CommandLine {
    char kind{'/'};
    std::string text;
};

/// What a `:` line will do when Enter runs it, in plain words, said while
/// it is typed (sheet 49): "Replace every gust with wind on lines 44 to 46".
/// A name, a pattern, a path or keys typed stand in the words between \x01
/// and \x02.
struct Outcome {
    /// It runs; it will be refused for the unsaved changes it would lose,
    /// the words saying what would go ahead; or it won't run, the words
    /// saying why.
    enum class Kind : uint8_t { Runs, Refused, Fails };
    Kind kind{Kind::Runs};
    std::string text;

    bool operator==(const Outcome&) const = default;
};

/// While a `:` line's command name is typed: the commands it can still
/// become, what is typed of the name, which a list picks out in each, and
/// the one Enter runs now, when it is one of them.
struct NameChoices {
    std::string typed;
    std::vector<ExCommandName> names;
    std::optional<std::size_t> lit;
};

/// One of a folder's entries, as the `:` line lists them after `:e`.
struct PathEntry {
    std::string name;
    bool folder{false};

    bool operator==(const PathEntry&) const = default;
};

/// Lists the folder `folder`, as typed after `:e` ("src/", empty for where
/// `:e` begins), the way the program lists files; those hidden too with
/// `hidden`. None when it can't be read. The engine never reads the disk.
using PathLister = std::function<std::vector<PathEntry>(std::string_view folder, bool hidden)>;

/// While a path is typed after `:e`: the folder typed of it, the name begun
/// after that, which a list picks out, the folder's entries whose names
/// begin so, and the one Tab put in.
struct PathChoices {
    std::string folder;
    std::string typed;
    std::vector<PathEntry> entries;
    std::optional<std::size_t> lit;
};

/// What the program knows of its files that the `:` line's words need.
struct FileFacts {
    /// The name of the file the view shows ("windmill.cpp"); empty for none
    /// known.
    std::string file;
    /// Closing the view loses unsaved changes: its file has them and no
    /// other view shows it.
    bool closing_loses{false};
    /// The name of a file with unsaved changes, which quitting would lose;
    /// empty when none has.
    std::string unsaved;
};

/// What a key had to say, as Vim's command line says it: an error
/// ("E486: Pattern not found: spin") or a note ("search hit BOTTOM,
/// continuing at TOP"); empty text when nothing.
struct Message {
    std::string text;
    bool error{false};
};

namespace detail {

// What Vim remembers from one command to the next for its motions: what f,
// t, F and T looked for, the last search, whether its matches show, the
// settings patterns are read with, and what the last motion had to say.
struct Remembered {
    std::optional<CharSearch> chars;
    std::optional<Search> search;
    bool shown{false};
    PatternSettings settings;
    Message message;
};

} // namespace detail

/// The keys that do in `mode` what the command `name` does, as Vim has them
/// (`u` for edit.undo in normal mode, `"+y` for edit.copy in visual mode);
/// nothing when Vim has none for it there.
[[nodiscard]] std::optional<std::vector<Chord>> keys_for(std::string_view name, Mode mode);

/// How a view draws the carets in a mode (sheet 47, C1).
enum class CaretShape : uint8_t {
    /// The whole of the character's cell, the character cut out of it:
    /// normal mode.
    Block,
    /// A 2 px bar before the character, blinking: insert mode.
    Bar,
    /// A line under the character: replace mode.
    Underline,
    /// The lower half of the character's cell: an operator waiting for its
    /// motion.
    HalfBlock,
};

class Engine {
public:
    /// An engine in normal mode, keeping what is yanked and deleted in
    /// `registers` and what `:set` changes in `settings`, which the engines
    /// of other views may share.
    explicit Engine(std::shared_ptr<Registers> registers = std::make_shared<Registers>(),
                    std::shared_ptr<Settings> settings = std::make_shared<Settings>())
        : registers_(std::move(registers)), settings_(std::move(settings)) {}

    /// Takes `key` in the mode it is in, acting at every selection of
    /// `context`. Returns a `:` command that is the program's, when the key
    /// ended one: the file written, quit or edited, the tabs stepped, or the
    /// groups split and gone between (`:w`, `:q!`, `:e path`, `:bn`, `:sp`;
    /// `:wincmd w`, `h` or `l`). Ctrl-W's keys come as the command Vim runs
    /// for them (Ctrl-W q as `:quit`). Each is read as far as Vim reads it:
    /// a range, or an argument it takes none of, is said instead.
    std::optional<ExCommand> take(const Chord& key, CommandContext& context);

    /// The selections put where the mode keeps them, after something other
    /// than the keys moved them: in normal mode each caret on a character
    /// rather than past a line's last; a selection begins visual mode when
    /// `by_mouse` made it (a drag, a double click), and otherwise becomes a
    /// caret at its start (a definition gone to). In visual mode, carets
    /// alone end it, and selections the mouse made become its own. In insert
    /// mode a caret the mouse moved ends the change `.` makes again there.
    void settle(CommandContext& context, bool by_mouse = false);

    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] CaretShape caret_shape() const;

    /// In visual mode, where each block caret is: on the character at the
    /// end of each selection that moves, one for a block. Empty in the other
    /// modes, whose carets are the selections' own.
    [[nodiscard]] std::vector<Position> cursors() const;

    /// The keys of a command typed so far, in Vim's notation, as its
    /// showcmd shows them (`2d`, `g`); empty while none waits.
    [[nodiscard]] std::string pending() const;

    /// Whether the next key is the character a command waits for, as `f`,
    /// `t` and `r` wait for theirs, or a register's name after `"`: a key no
    /// binding may take.
    [[nodiscard]] bool wants_character() const;

    /// Drops a command half typed, as a binding run instead of it does. A
    /// binding run as Ctrl-O's one command is that command: the mode goes
    /// back to the one Ctrl-O left.
    void cancel(CommandContext& context);

    /// In insert mode, an entry of the completion list taken: `text` put in
    /// place of `replaced`, the word's text before the caret. `.` and the
    /// insert's count make the same change again.
    void completed(std::string_view replaced, std::string_view text);

    /// The command line while one is typed; null otherwise.
    [[nodiscard]] const CommandLine* command_line() const { return typing_ ? &typing_->line : nullptr; }

    /// What the `:` line being typed will do when Enter runs it, read as it
    /// stands, with what the program knows in `files`: its range as the
    /// lines it names in `context`, and what follows the name. Nothing while
    /// no `:` line is typed, or nothing is typed on it.
    [[nodiscard]] std::optional<Outcome> outcome(const CommandContext& context, const FileFacts& files) const;

    /// While the `:` line's command name is typed, the commands it can
    /// still become (sheet 49, H3). Tab puts the next of them in place of
    /// the name and Shift-Tab the one before, round to the name as typed
    /// past either end; whatever else is typed ends the round there.
    [[nodiscard]] std::optional<NameChoices> name_choices() const;

    /// The command `index` of name_choices() put in place of the name
    /// typed, as Tab would put it: one picked with the mouse.
    void choose_name(std::size_t index);

    /// How the `:` line lists a folder after `:e`: the program's way.
    /// Without one, no path is listed.
    void list_paths_with(PathLister lister) { list_paths_ = std::move(lister); }

    /// While a path is typed after `:e`, the entries of its folder it can
    /// become. Tab puts the next in place of the name begun and Shift-Tab
    /// the one before, round to the name as typed; when one alone matches
    /// it goes in and the round ends, a folder with a / after it, so its
    /// entries list next.
    [[nodiscard]] std::optional<PathChoices> path_choices() const;

    /// The entry `index` of path_choices() put in, the round ended: one
    /// picked with the mouse, a folder going in.
    void choose_path(std::size_t index);

    /// The search whose matches a view shows: a pattern typed after `/` or
    /// `?` as it is typed, otherwise the last search until Ctrl-L or `:noh`
    /// hides it; null while none shows.
    [[nodiscard]] const Pattern* shown_search() const;

    /// A search made elsewhere (a view's find), which n and N go on with.
    void search_for(Search search);

    /// Into insert mode at the selections as they are, for typing something
    /// other than Vim's keys began (a name renamed where it stands);
    /// end_insert() leaves it, each caret settled as normal mode keeps it.
    /// Neither touches the history, and nothing typed between them is a
    /// change `.` makes again.
    void begin_insert();
    void end_insert(CommandContext& context);

    /// What the last key had to say; empty text when nothing. The next key
    /// clears it.
    [[nodiscard]] const Message& message() const { return remembered_.message; }

    /// Says `message` as the last key's: what the program had to say of a
    /// command handed to it ("E37: No write since last change").
    void say(Message message) { remembered_.message = std::move(message); }

    /// What is yanked and deleted.
    [[nodiscard]] Registers& registers() { return *registers_; }

    /// What `:set` changed.
    [[nodiscard]] Settings& settings() { return *settings_; }

private:
    // Where a visual selection begins and where its caret is, a character
    // each (a block's corners), and the goal along the line the caret keeps.
    struct VisualEnds {
        Position anchor;
        Position caret;
        std::optional<float> goal;
    };

    // How much text a change made in visual mode took, which `.` takes again
    // from the caret: as many lines; on one line as many characters, over
    // several on to the same place along the last; a block as wide.
    struct VisualExtent {
        Mode mode{Mode::Visual};
        std::size_t lines{1};
        // Along the line from the first character to the last, on one line
        // or across a block.
        float width{0.0f};
        // Over several lines, where the last character is along its line.
        float end{0.0f};
        // A block on to each line's end, after $.
        bool to_line_ends{false};
    };

    // A change as `.` makes it again: the keys of the command that made it,
    // after its register and count, and what was typed in the insert it
    // began.
    struct Repeat {
        char reg{'"'};
        std::optional<std::size_t> count;
        std::vector<Chord> keys;
        std::vector<Chord> typed;
        std::optional<VisualExtent> visual;
    };

    // `key` taken in the mode the engine is in.
    void dispatch(const Chord& key, CommandContext& context);
    // `.`: the last change made again at every caret, `count` for its own
    // when given.
    void repeat(std::optional<std::size_t> count, CommandContext& context);
    // Visual mode selecting from each caret as much as `extent` says.
    void select_again(const VisualExtent& extent, CommandContext& context);
    // How much the visual selection takes, by the primary's ends.
    [[nodiscard]] VisualExtent extent(const CommandContext& context) const;
    // `key` typed in insert or replace mode, noted for `.` and the insert's
    // count; it begins a change of its own (an `i`) when none is being made.
    void note_typed(Chord key);
    // The caret moved in insert or replace mode: the change being made ends
    // there.
    void end_change_here();
    // An insert ending: what was typed in it typed again as many more times
    // as its count asks.
    void repeat_insert(CommandContext& context);

    // Into the command line `kind`, from the mode the engine is in, the keys
    // `before` (a register, a count, an operator) to come again before its
    // search, `text` typed on it already (`.,.+2` for a count before `:`).
    void begin_command_line(char kind, std::vector<Chord> before, bool operating, CommandContext& context,
                            std::string text = {});
    // A line typed after `:` run.
    void run_ex(std::string_view line, CommandContext& context);
    // A `:` command that is the program's, handed on from take() once it
    // reads as Vim reads it; what doesn't is said.
    void hand_on(ExCommand command);
    // The command `index` of the names the line as typed can become put in
    // place of the name, or with none the name as typed, as Tab puts them.
    void put_name(std::optional<std::size_t> index);
    // The entries of `typed`'s folder whose names begin with the name typed.
    [[nodiscard]] std::vector<PathEntry> paths_matching(const PathTyped& typed) const;
    // The entry `index` of those the path as typed can become put in place
    // of the name begun, or with none the name as typed; with `end`, the
    // round ends there.
    void put_path(std::optional<std::size_t> index, bool end);
    // What `line`, typed after `:`, will do (outcome()); `inside` a :g, the
    // command it runs at each line it marks, said of that line.
    [[nodiscard]] Outcome read_line(std::string_view line, const CommandContext& context, const FileFacts& files,
                                    bool inside = false) const;
    // :s, :g and :v, :normal and :sort over lines `first` to `last`.
    void run_substitute(const ExCommand& command, std::size_t first, std::size_t last, CommandContext& context);
    void run_global(const ExCommand& command, std::size_t first, std::size_t last, CommandContext& context);
    void run_normal(const ExCommand& command, std::size_t first, std::size_t last, CommandContext& context);
    void run_sort(const ExCommand& command, std::size_t first, std::size_t last, CommandContext& context);
    void run_set(const ExCommand& command, CommandContext& context);
    // Whether the setting `option` is on now, for the view `context` is of.
    [[nodiscard]] bool setting_on(SetOption option, const CommandContext& context) const;
    // The line `address` names, counted from 1 (0 for above the first), the
    // caret's line `current` counted from 0; an error worded as Vim's. A
    // pattern's search is made into `searched`, which an empty pattern after
    // it on the line goes on with, and which becomes the last search once
    // the line runs.
    ftk::Result<std::size_t> line_at(const ExAddress& address, std::size_t current, const CommandContext& context,
                                     std::optional<Search>& searched) const;
    // The first and last lines `range` names, counted from 0: the caret's
    // line where it names none, or the whole text with `whole`.
    ftk::Result<std::pair<std::size_t, std::size_t>> lines_of(const ExRange& range, bool whole,
                                                              const CommandContext& context,
                                                              std::optional<Search>& searched) const;
    void take_command(const Chord& key, CommandContext& context);
    // The pattern typed searched for as it is typed: its matches shown and,
    // from normal mode, the one it goes to selected.
    void preview(CommandContext& context);
    // Out of the command line, back where it began; with `run`, its search
    // made, the caret going to the match as n goes.
    void leave_command_line(bool run, CommandContext& context);

    void take_normal(const Chord& key, CommandContext& context);
    void take_insert(const Chord& key, CommandContext& context);
    void take_replace(const Chord& key, CommandContext& context);
    void take_visual(const Chord& key, CommandContext& context);
    // Out of insert or replace mode to normal mode, each caret onto the
    // character before it.
    void leave_insert(CommandContext& context);
    // `text` typed at every caret: put in, or in replace mode written over
    // the characters there, its line breaks put in still.
    void type_text(std::string_view text, CommandContext& context);
    // Back to the mode Ctrl-O left, its command done.
    void end_one_command(CommandContext& context);
    // Visual mode's operators and its other commands, `name` with the
    // character `argument` it takes, a count of `count` and the register
    // `reg`; false for a name that is none of them.
    bool act_on_visual(std::string_view name, std::string_view argument, std::size_t count, char reg,
                       CommandContext& context);

    // Into visual mode `mode` from normal mode or another visual mode.
    void enter_visual(Mode mode, CommandContext& context);
    // The selections visual mode's ends make.
    void show_visual(CommandContext& context);
    // Out to normal mode, each caret where its selection's was; the ends
    // kept for gv.
    void leave_visual(CommandContext& context);
    // Visual mode's ends for `selections` as something else made them.
    static std::vector<VisualEnds> ends_of(const Buffer& buffer, const Selections& selections);

    std::shared_ptr<Registers> registers_;
    std::shared_ptr<Settings> settings_;
    Mode mode_{Mode::Normal};
    // The keys of the normal-mode command typed so far, and whether they
    // are an operator waiting for what it acts on.
    std::vector<Chord> keys_;
    bool operator_waits_{false};
    // What f, t, F or T last looked for, the last search, and what the
    // last key had to say.
    detail::Remembered remembered_;
    // The command line being typed: the mode, the selections and visual
    // ends it began from, which the pattern typed is searched from; the keys
    // of the command it is the motion of (d/), which come again before the
    // search when it runs; and the history walked with Up and Down.
    struct Typing {
        CommandLine line;
        Mode from{Mode::Normal};
        std::vector<Selection> selections;
        std::size_t primary{0};
        std::vector<VisualEnds> visual;
        std::vector<Chord> before;
        bool operating{false};
        std::optional<std::size_t> history_at;
        std::string history_prefix;
        // Tab's round of command names: the line as typed before the first
        // Tab, and the name it put in, none once it came round to the name
        // as typed.
        std::optional<std::string> naming_from;
        std::optional<std::size_t> named;
    };
    std::optional<Typing> typing_;
    // The pattern typed as it is typed, when it reads.
    std::optional<Pattern> preview_;
    // A `:` command for the program, until take() hands it on.
    std::optional<ExCommand> handed_;
    // How a folder is listed after `:e`.
    PathLister list_paths_;
    // The searches typed and the lines typed after `:`, the latest last.
    std::vector<std::string> history_;
    std::vector<std::string> commands_;
    // The last :s, which `:s` alone, `&`, `g&` and ~ go on with: its pattern
    // as typed, its replacement, and whether it replaced every match.
    struct Substitute {
        std::string pattern;
        std::string with;
        bool every{false};
    };
    std::optional<Substitute> substitute_;
    // A :g is running its command on the lines it marked.
    bool in_global_{false};
    // How many rows Ctrl-D and Ctrl-U move once a count has said; half the
    // view until then.
    std::size_t half_page_{0};
    // Visual mode's ends, and the selections it last made from them: others
    // than those were made by the mouse or an edit.
    std::vector<VisualEnds> visual_;
    Selections shown_;
    // The last visual selection and its mode, which gv selects again.
    std::vector<VisualEnds> last_visual_;
    Mode last_visual_mode_{Mode::Visual};
    // An insert begun from a block, which leaves one caret when it ends.
    bool block_insert_{false};
    // In replace mode, what each caret wrote over, for Backspace to put back.
    std::vector<std::vector<std::string>> overwritten_;
    // Ctrl-R in insert mode waits for a register's name.
    bool register_next_{false};
    // Ctrl-O runs one command in normal mode, then goes back to the mode
    // it came from. For each caret, the line it was at the end of then.
    bool one_command_{false};
    Mode resume_{Mode::Insert};
    std::vector<std::optional<std::size_t>> left_line_ends_;
    // The last change, which `.` makes again; the one being made, until the
    // insert it began ends; and what that insert typed since it began or
    // the caret last moved.
    std::optional<Repeat> last_change_;
    std::optional<Repeat> making_;
    std::vector<Chord> inserted_;
    // While `.` makes a change again, no change is noted; while an insert's
    // count types it again, nothing typed is.
    bool replaying_{false};
    bool repeating_insert_{false};
    // Between begin_insert() and end_insert(), nothing typed is noted.
    bool quiet_{false};
    // How many more times the insert types what was typed in it when it
    // ends (3i), each time on a line it opens (3o).
    std::size_t insert_repeats_{0};
    bool insert_opens_lines_{false};
};

} // namespace fjedit::vim
