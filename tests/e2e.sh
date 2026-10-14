#!/usr/bin/env bash
#
# e2e.sh - Drive every menu entry of the plugin inside a real Geany.
#
# The unit tests cover the Geany-independent core. The glue that moves
# text between Scintilla and that core can only be exercised in Geany
# itself, so this script starts a throwaway Geany on a virtual X display,
# binds the operation under test to a key, presses it, saves, and compares
# the file with what the operation should have produced.
#
# Run with the plugin built under AddressSanitizer (make e2e does this), so
# a bad free aborts Geany instead of corrupting the heap quietly.
#
# Usage: tests/e2e.sh PLUGIN.so
#
# Needs geany, Xvfb (xvfb-run) and xdotool. Never touches ~/.config/geany:
# each case gets its own configuration directory.

set -u -o pipefail

# Re-run under a private X display, so nothing lands on the user's screen.
if [ -z "${LOPP_E2E_INNER:-}" ]; then
	unset WAYLAND_DISPLAY
	export GDK_BACKEND=x11
	LOPP_E2E_INNER=1 exec xvfb-run -a -s '-screen 0 1280x1024x24' "$0" "$@"
fi

PLUGIN=$(readlink -f "${1:?usage: e2e.sh PLUGIN.so}")
ASAN_LIB=${ASAN_LIB:-$(gcc -print-file-name=libasan.so)}
KEY='ctrl+shift+F12'
KEY_CONF='<Primary><Shift>F12'

WORK=$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/lopp-e2e.XXXXXX")
# A crash is the expected failure here; keep it out of apport/systemd-coredump.
ulimit -c 0

[ -n "${LOPP_E2E_KEEP:-}" ] || trap 'rm -rf -- "$WORK"' EXIT

pass=0
fail=0


# run_case NAME KEYNAME INPUT EXPECTED [PRE_KEYS] [POST_KEYS]
#
# PRE_KEYS are xdotool key names pressed before the operation (to move the
# caret or make a selection); POST_KEYS after it (to answer a dialog).
_run_case()
{
	local name=$1 keyname=$2 input=$3 expected=$4
	local pre=${5:-} post=${6:-}
	local dir="$WORK/$name"
	local file="$dir/doc.txt"
	local pid win status

	mkdir -p "$dir/cfg/plugins"
	printf '%s' "$input" > "$file"

	# Geany refuses to load an active plugin from outside its plugin
	# search path, so the build has to sit in the config's plugins dir.
	cp "$PLUGIN" "$dir/cfg/plugins/"

	cat > "$dir/cfg/geany.conf" <<-EOF
	[geany]
	pref_main_load_session=false
	pref_main_save_winpos=false
	pref_editor_new_line=false
	pref_editor_newline_strip=false
	pref_editor_trail_space=false
	[plugins]
	load_plugins=true
	active_plugins=$dir/cfg/plugins/${PLUGIN##*/};
	EOF

	cat > "$dir/cfg/keybindings.conf" <<-EOF
	[Line Operations++]
	$keyname=$KEY_CONF
	EOF

	LD_PRELOAD="$ASAN_LIB" \
	ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:log_path=$dir/asan" \
	geany -c "$dir/cfg" -i -s -n -m "$file" >"$dir/geany.log" 2>&1 &
	pid=$!

	win=$(timeout 20 xdotool search --sync --onlyvisible --name 'doc\.txt' \
			| head -1)

	if [ -z "$win" ]; then
		echo "FAIL $name: Geany window never appeared"
		kill "$pid" 2>/dev/null
		fail=$((fail + 1))
		return
	fi

	xdotool windowfocus --sync "$win" >/dev/null 2>&1
	sleep 0.5

	# shellcheck disable=SC2086  # word splitting is the point here
	[ -n "$pre" ] && xdotool key --delay 80 $pre
	xdotool key "$KEY"
	sleep 0.5
	# shellcheck disable=SC2086
	[ -n "$post" ] && xdotool key --delay 150 $post
	sleep 0.3

	xdotool key ctrl+s
	sleep 0.5

	# Still running after the save means the operation did not crash it.
	# ASan can take a moment to write its report, so check for that too.
	if kill -0 "$pid" 2>/dev/null; then
		kill "$pid" 2>/dev/null
		wait "$pid" 2>/dev/null
		status=alive
	else
		wait "$pid" 2>/dev/null
		status="exit $?"
	fi

	if [ "$status" != alive ] || compgen -G "$dir/asan*" >/dev/null; then
		echo "FAIL $name: Geany crashed ($status)"
		cat "$dir"/asan* 2>/dev/null | grep -m4 -E 'ERROR|^ +#[0-9] ' \
			| sed 's/^ */     /'
		fail=$((fail + 1))
		return
	fi

	if cmp -s "$file" <(printf '%s' "$expected"); then
		echo "ok   $name"
		pass=$((pass + 1))
	else
		local got
		got=$(cat "$file"; printf x)
		echo "FAIL $name: expected $(printf '%q' "$expected"), got $(printf '%q' "${got%x}")"
		fail=$((fail + 1))
	fi
}


run_case() { _run_case "$@"; }


cases()
{
	run_case sort_asc sort_ascending $'b\na\nb\nc\n' $'a\nb\nb\nc\n'
	run_case sort_desc sort_descending $'b\na\nc\n' $'c\nb\na\n'
	run_case sort_lex sort_lexicographically_ascending \
			$'b10\nb9\nB1\n' $'B1\nb10\nb9\n'
	run_case sort_dec sort_numeral_decimal_ascending \
			$'10\n9\n-1\n' $'-1\n9\n10\n'
	run_case sort_no_final_eol sort_ascending $'b\na' $'a\nb'
	run_case sort_selection sort_ascending $'d\nc\nb\na\n' $'d\nb\nc\na\n' \
			'Down shift+Down shift+Down'

	run_case dedupe_sorted remove_duplicate_lines_sorted \
			$'b\na\nb\nc\n' $'a\nb\nc\n'
	run_case dedupe_ordered remove_duplicate_lines_ordered \
			$'b\na\nb\nc\n' $'b\na\nc\n'
	run_case dedupe_consecutive remove_consecutive_duplicate_lines \
			$'a\na\nb\na\n' $'a\nb\na\n'
	run_case remove_unique remove_unique_lines $'b\na\nb\nc\n' $'b\nb\n'
	run_case keep_unique keep_unique_lines $'b\na\nb\nc\n' $'a\nc\n'

	run_case remove_empty remove_empty_lines $'a\n\nb\n\n' $'a\nb\n'
	run_case remove_whitespace remove_whitespace_lines \
			$'a\n  \nb\n' $'a\nb\n'
	run_case remove_every_nth remove_every_nth_line \
			$'1\n2\n3\n4\n' $'1\n3\n' '' 'Return'

	run_case trim_leading trim_leading_whitespace $'  a\n\tb\n' $'a\nb\n'
	run_case trim_trailing trim_trailing_whitespace $'a  \nb\t\n' $'a\nb\n'
	run_case join join_lines $'a\nb\nc\n' $'a b c\n'
	run_case split split_lines $'a, b, c\n' $'a\nb\nc\n'

	run_case duplicate duplicate_current_line $'a\nb\n' $'a\na\nb\n'
	run_case move_down move_line_down $'a\nb\nc\n' $'b\na\nc\n'
	run_case move_up move_line_up $'a\nb\nc\n' $'b\na\nc\n' 'Down'
}


if [ $# -gt 1 ]; then
	# Only the named cases, for debugging: e2e.sh PLUGIN.so sort_asc ...
	only=" ${*:2} "
	run_case()
	{
		case "$only" in *" $1 "*) _run_case "$@" ;; esac
	}
fi

cases

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
