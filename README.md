# Geany - Line Operations++

[![CI](https://github.com/Avicennasis/geany-lopp/actions/workflows/test.yml/badge.svg)](https://github.com/Avicennasis/geany-lopp/actions/workflows/test.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/Avicennasis/geany-lopp/badge)](https://scorecard.dev/viewer/?uri=github.com/Avicennasis/geany-lopp)
[![License: GPL v2+](https://img.shields.io/badge/License-GPL_v2%2B-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

Notepad++'s **Edit ▸ Line Operations** command set, as a Geany plugin.

Derived from the [`lineoperations`](https://plugins.geany.org/lineoperations.html)
plugin in [geany-plugins](https://plugins.geany.org/) (Copyright 2015 Sylvan
Mostert, GPL-2+), extended with the parts Notepad++ has and Geany didn't.

**Licence: GPL-2.0-or-later.** See [`LICENSE`](LICENSE) — which explains why
that isn't MIT, and why Notepad++'s own licence isn't a constraint here.

Every operation applies to **the whole document**, or to **just the
selection** if you have one.

> **On the name.** The project is *Geany - Line Operations++* — the prefixed
> form is what it is called on GitHub, where it earns its keep as something
> to search for. Inside Geany it shows up as plain **Line Operations++**,
> because the plugin manager and keybinding dialog list nothing but Geany
> plugins and a "Geany - " prefix there is just noise.

---

## Install

```sh
make            # build the plugin and the test binaries
make test       # run the test suites
sudo make install
```

Restart Geany, then **Tools ▸ Plugin Manager ▸ Line Operations++ ▸ Load**.

Or add it to `~/.config/geany/geany.conf`:

```ini
[plugins]
active_plugins=/usr/lib/x86_64-linux-gnu/geany/lineoperations-plus.so
```

The menu appears under **Tools ▸ Line Operations++**. Every command has a
keybinding slot under **Edit ▸ Preferences ▸ Keybindings ▸ Line
Operations++**; none are bound by default, so nothing collides with your
existing shortcuts until you choose.

**Build dependencies:** `libgtk-3-dev` (without it `pkg-config geany` fails
on its `Requires: gtk+-3.0`) and the Geany headers, which the `geany-common`
package already ships in `/usr/include/geany`.

---

## Commands

### Line manipulation

| Command | What it does |
|---|---|
| **Duplicate Current Line** | Copies the caret's line, or every selected line, directly below itself. The caret follows the copy, so repeating stacks copies. |
| **Move Line Up / Down** | Swaps the line, or selected block, with its neighbour. Refused at the top and bottom of the document. |
| **Join Lines** | Collapses the range onto one line, joined by the configured separator (default a single space). |
| **Split Lines** | Splits each line on the configured substring (default `", "`). With the separator left empty, splits on runs of whitespace instead. |
| **Trim Leading Whitespace** | Strips whitespace from the start of each line. |
| **Trim Trailing Whitespace** | Strips whitespace from the end of each line. |

### Duplicate and empty-line removal

| Command | What it does |
|---|---|
| **Remove Duplicate Lines, Sorted** | Sorts using the currently selected sort mode, then keeps one line per run of equals. |
| **Remove Duplicate Lines, Ordered** | Keeps the first occurrence of each line and leaves the rest in their original order. |
| **Remove Consecutive Duplicate Lines** | Collapses runs of *adjacent* equal lines. Equal lines that aren't adjacent are both kept. |
| **Remove Empty Lines** | Drops lines with no characters. A line of spaces is **not** empty. |
| **Remove Whitespace Lines** | Drops lines holding only whitespace. Unicode-aware — U+00A0, U+2002 and U+3000 all count. |
| **Remove Unique Lines** | Drops lines occurring exactly once, keeping those that repeat. |
| **Keep Unique Lines** | The inverse: keeps lines occurring exactly once, drops repeats. |
| **Remove Every Nth Line** | Asks for N, then drops lines N, 2N, 3N… counting from the start of the range. |

### Sorting

The full Notepad++ matrix, ascending and descending:

| Mode | Orders by |
|---|---|
| **Ascending / Descending** | *Natural* order: runs of digits compare numerically, so `file9.txt` precedes `file10.txt`. |
| **Lexicographically Ascending / Descending** | Plain code-point comparison, no numeric awareness — so `file10.txt` precedes `file9.txt`. |
| **Ignoring Case Ascending / Descending** | Natural order, case-folded. |
| **Numerically (Decimal) Ascending / Descending** | Each line read as a decimal integer. |
| **Numerically (Hexadecimal) Ascending / Descending** | Each line read as base 16; an `0x` prefix is accepted. |
| **Numerically (Octal) Ascending / Descending** | Each line read as base 8; an `0o` prefix is accepted. |

---

## Behaviour worth knowing

Each of these is enforced by a test, and each is a place where a decision had
to be made rather than inherited.

**Notepad++'s "Ascending" is natural sort, not alphabetical.** That's why
`file9.txt` sorts before `file10.txt`. If you want plain `strcmp` order, you
want *Lexicographically Ascending* — Notepad++ keeps these as separate menu
entries precisely because they differ.

**Numeric sorts don't truncate at 64 bits.** Values are compared as digit
strings — significant-digit count first, then digit by digit — so
`18446744073709551615` sorts above `9999999999999999999` instead of both
saturating to the same value.

**"Equal" and "in this order" are different questions.** The comparator ends
with a tie-break so every sort is a *total* order; without it, two spellings
of one value would swap between runs. Duplicate removal uses a separate
equivalence test that ignores the tie-break, so under *Ignoring Case* the
pair `Apple` / `apple` still merges, and under *Hexadecimal* the pair `10` /
`0x10` still merges.

**Deduplication is never more aggressive than the mode you chose.** The
Ordered and Consecutive variants always use byte equality, so they will never
merge lines that merely differ in case.

**Sorting is stable.** A stable merge sort is used rather than `qsort`, which
glibc doesn't guarantee to be stable. That's what makes "which copy of a
duplicate survives" well defined instead of dependent on internal pivots.

**Trailing newlines and CRLF are preserved.** Running an operation over a file
with no final newline doesn't add one; running it over a CRLF file keeps CRLF.
The document's terminator style is found by scanning for the first terminator
in the text, not by looking at the first character — so a document whose first
line is empty is still handled. Every operation is a single undo step.

**Duplicate detection is linear.** Upstream compared every line to every later
line, which is quadratic — about five billion comparisons on a 100k-line log.
This uses a hash table: 200k lines in ~0.02 s.

---

## Preferences

**Tools ▸ Plugin Manager ▸ Line Operations++ ▸ Preferences**

- **Use collation based string compare** — compare characters with
  `g_utf8_collate()` so accented letters sort beside their base letters rather
  than after all ASCII. Affects the natural and lexicographic sorts only; the
  numeric sorts never compare characters.
- Join and split separators.
- The default interval for *Remove Every Nth Line*.

Settings live in `~/.config/geany/plugins/lineoperations-plus/general.conf`.

---

## Tests

```sh
make test     # 125 assertions
make asan     # the same, under Address/UB/LeakSanitizer
```

Two suites, both linking only Geany-independent code — no Scintilla, no GTK:

- **`tests/test_lo_core.c`** — the twelve sort modes, natural vs
  lexicographic, numeric and big-number ordering, stability, locale
  collation, malformed UTF-8, every dedupe variant, trimming, join and
  split, and a 200k-line scale check.
- **`tests/test_lo_doc.c`** — line counting, terminator detection,
  extraction, reassembly, extract/reassemble round trips over LF, CRLF,
  empty and unterminated documents, block reordering, and slicing.

`make asan` is the gate that caught the compaction-aliasing bug where a
surviving line was freed because its vacated slot still pointed at it.

### Not covered automatically

`src/lo_edit.c` is a thin Scintilla wrapper: extract lines, hand them to the
tested functions, write the result back. The text handling underneath is
covered above, but the Scintilla calls themselves are exercised only by
loading the plugin in Geany. A GUI harness that drives real Geany was
attempted and abandoned — it didn't run reliably headless, and a test that
can't be trusted to fail is worse than no test. Worth a few minutes of manual
clicking on the sort and move commands before relying on them.

---

## Layout

```
src/lo_sort.c          comparison and stable ordering    (pure)
src/lo_lines.c         line-array operations             (pure)
src/lo_doc.c           document text and terminators     (pure)
src/lo_edit.c          Scintilla glue over the above
src/lo_prefs.c         preferences
src/lineoperations.c   menu, dispatch, keybindings, plugin entry point
```

The split exists so the algorithms can be tested without a display.
