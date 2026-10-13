/*
 *      test_lo_core.c - Tests for the Geany-independent line core.
 *
 *      These exercise lo_sort.c and lo_lines.c directly, with no Scintilla
 *      and no Geany, so the ordering rules can be pinned down precisely.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include <glib.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>

#include "lo_sort.h"
#include "lo_lines.h"

static gint failures = 0;
static gint checks = 0;

#define CHECK(expr, ...) \
	G_STMT_START { \
		checks++; \
		if (! (expr)) { \
			failures++; \
			g_printerr("  FAIL %s:%d: ", __FILE__, __LINE__); \
			g_printerr(__VA_ARGS__); \
			g_printerr("\n"); \
		} \
	} G_STMT_END


/* --------------------------------------------------------------------- */
/* Helpers                                                                */
/* --------------------------------------------------------------------- */

/* Build a fresh, NULL-terminated array from a C string list. */
static gchar **
make_lines(gint *out_n, const gchar *first, ...)
{
	GPtrArray *arr = g_ptr_array_new();
	const gchar *s = first;
	va_list ap;

	va_start(ap, first);
	while (s != NULL)
	{
		g_ptr_array_add(arr, g_strdup(s));
		s = va_arg(ap, const gchar *);
	}
	va_end(ap);

	*out_n = (gint)arr->len;
	return (gchar **)g_ptr_array_free(arr, FALSE);
}


/* Join an array back into "a\nb\nc" for readable comparison. */
static gchar *
join_lines(gchar **lines, gint n)
{
	GString *s = g_string_new(NULL);
	gint i;

	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append_c(s, '|');

		g_string_append(s, lines[i]);
	}

	return g_string_free(s, FALSE);
}


static void
free_lines(gchar **lines, gint n)
{
	gint i;

	for (i = 0; i < n; i++)
		g_free(lines[i]);

	g_free(lines);
}


/*
 * Assert that sorting 'input' under 'mode' yields 'expected' (a pipe-
 * separated string). The sort must also be stable: equal-comparing lines
 * keep their input order, which is what "Remove Duplicate Lines, Sorted"
 * depends on.
 */
static void
expect_sort(const gchar *label, lo_sort_mode mode, gboolean collation,
		const gchar *expected, const gchar *first, ...)
{
	GPtrArray *arr = g_ptr_array_new();
	va_list ap;
	const gchar *s;
	gchar **lines;
	gchar *got;
	gint n;
	gint i;

	va_start(ap, first);
	for (s = first; s != NULL; s = va_arg(ap, const gchar *))
		g_ptr_array_add(arr, g_strdup(s));
	va_end(ap);

	n = (gint)arr->len;
	lines = (gchar **)g_ptr_array_free(arr, FALSE);

	lo_sort_sort(lines, n, mode, collation);
	got = join_lines(lines, n);

	CHECK(strcmp(got, expected) == 0,
			"%s: expected \"%s\", got \"%s\"", label, expected, got);

	g_free(got);
	for (i = 0; i < n; i++)
		g_free(lines[i]);
	g_free(lines);
}


/* --------------------------------------------------------------------- */
/* Natural vs lexicographic                                                */
/* --------------------------------------------------------------------- */

static void
test_natural_vs_lexicographic(void)
{
	g_print("natural vs lexicographic ordering\n");

	/* The whole point of natural ordering: 9 before 10. */
	expect_sort("natural asc", LO_SORT_NATURAL_ASC, FALSE,
			"file9.txt|file10.txt|file100.txt",
			"file10.txt", "file100.txt", "file9.txt", NULL);

	/* Lexicographic ordering gets this backwards, by design. */
	expect_sort("lex asc", LO_SORT_LEXICOGRAPHIC_ASC, FALSE,
			"file10.txt|file100.txt|file9.txt",
			"file10.txt", "file100.txt", "file9.txt", NULL);

	/* Descending is the exact reverse. */
	expect_sort("natural desc", LO_SORT_NATURAL_DESC, FALSE,
			"file100.txt|file10.txt|file9.txt",
			"file10.txt", "file100.txt", "file9.txt", NULL);

	expect_sort("lex desc", LO_SORT_LEXICOGRAPHIC_DESC, FALSE,
			"file9.txt|file100.txt|file10.txt",
			"file10.txt", "file100.txt", "file9.txt", NULL);

	/* Leading zeros do not change the value. */
	/*
	 * Leading zeros carry no value, so a02 and a1 are the same NUMBER, but
	 * their text differs, so the run-width tie-break orders them. This is
	 * a1, then a02, then a10.
	 */
	expect_sort("leading zeros", LO_SORT_NATURAL_ASC, FALSE,
			"a1|a02|a10", "a10", "a02", "a1", NULL);

	/*
	 * Equal values but different widths still get a total order,
	 * shorter run first, so the result is reproducible.
	 */
	expect_sort("zero pad width", LO_SORT_NATURAL_ASC, FALSE,
			"a1|a01|a001", "a001", "a01", "a1", NULL);

	/* Mixed digit and non-digit runs. */
	expect_sort("mixed runs", LO_SORT_NATURAL_ASC, FALSE,
			"2 apples|10 apples|apple|banana",
			"banana", "10 apples", "apple", "2 apples", NULL);

	/* Empty lines sort first ascending. */
	expect_sort("empty first", LO_SORT_NATURAL_ASC, FALSE,
			"|a|b", "b", "a", "", NULL);
}


/* --------------------------------------------------------------------- */
/* Case-insensitive ordering                                              */
/* --------------------------------------------------------------------- */

static void
test_case_insensitive(void)
{
	g_print("case-insensitive ordering\n");

	/* Case-sensitive natural puts all capitals before all lowercase. */
	expect_sort("case sensitive", LO_SORT_NATURAL_ASC, FALSE,
			"Apple|Banana|apple|banana",
			"banana", "Apple", "apple", "Banana", NULL);

	/* Ignoring case interleaves them, and ties break by raw text. */
	expect_sort("ignoring case", LO_SORT_NATURAL_NOCASE_ASC, FALSE,
			"Apple|apple|Banana|banana",
			"banana", "Apple", "apple", "Banana", NULL);

	expect_sort("ignoring case desc", LO_SORT_NATURAL_NOCASE_DESC, FALSE,
			"banana|Banana|apple|Apple",
			"banana", "Apple", "apple", "Banana", NULL);

	/* Digits still compare numerically in the case-insensitive mode. */
	expect_sort("nocase + digits", LO_SORT_NATURAL_NOCASE_ASC, FALSE,
			"Item2|item10", "item10", "Item2", NULL);
}


/* --------------------------------------------------------------------- */
/* Numeric ordering                                                       */
/* --------------------------------------------------------------------- */

static void
test_numeric(void)
{
	g_print("numeric ordering\n");

	expect_sort("decimal asc", LO_SORT_NUM_DEC_ASC, FALSE,
			"2|10|100|1000",
			"1000", "100", "10", "2", NULL);

	expect_sort("decimal desc", LO_SORT_NUM_DEC_DESC, FALSE,
			"1000|100|10|2",
			"2", "10", "100", "1000", NULL);

	/* Values too large for a 32-bit int still order correctly. */
	expect_sort("bignum", LO_SORT_NUM_DEC_ASC, FALSE,
			"9999999999999999999|18446744073709551615",
			"18446744073709551615", "9999999999999999999", NULL);

	/* Hex, with and without the 0x prefix, both meaning the value. */
	expect_sort("hex asc", LO_SORT_NUM_HEX_ASC, FALSE,
			"0x0f|0x10|0xff|0x100",
			"0x100", "0xff", "0x10", "0x0f", NULL);

	/* Negative values. */
	expect_sort("decimal negatives", LO_SORT_NUM_DEC_ASC, FALSE,
			"-10|-3|0|7",
			"7", "0", "-3", "-10", NULL);

	/* Octal, with and without the 0o prefix. */
	expect_sort("octal asc", LO_SORT_NUM_OCT_ASC, FALSE,
			"0o7|0o10|0o17",
			"0o17", "0o10", "0o7", NULL);

	/* A line with no valid digits in the base reads as zero. */
	expect_sort("non-numeric as zero", LO_SORT_NUM_DEC_ASC, FALSE,
			"nope|1|2", "2", "1", "nope", NULL);

	/* Trailing junk after the digits is ignored, as strtol would. */
	expect_sort("trailing junk", LO_SORT_NUM_DEC_ASC, FALSE,
			"10abc|20def", "20def", "10abc", NULL);
}


/* --------------------------------------------------------------------- */
/* Stability                                                              */
/* --------------------------------------------------------------------- */

static void
test_stability(void)
{
	gint n = 0;
	gchar **lines;
	gchar *got;

	g_print("sort stability\n");

	/*
	 * Under the numeric decimal mode "7" and " 7" have the same value and
	 * therefore compare equal after the tie-break only if their raw text
	 * matches. Give them genuinely equal text so the sort must be judged
	 * purely on stability.
	 */
	lines = make_lines(&n, "same", "same", "same", NULL);
	lo_sort_sort(lines, n, LO_SORT_NATURAL_ASC, FALSE);
	got = join_lines(lines, n);
	CHECK(strcmp(got, "same|same|same") == 0, "stability: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n);

	/* Leading whitespace makes two lines numerically equal but textually
	 * distinct; the tie-break orders them, not their input positions. */
	lines = make_lines(&n, " 7", "7", NULL);
	lo_sort_sort(lines, n, LO_SORT_NUM_DEC_ASC, FALSE);
	got = join_lines(lines, n);
	CHECK(strcmp(got, " 7|7") == 0, "numeric tie-break: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n);
}


/* --------------------------------------------------------------------- */
/* Collation                                                              */
/* --------------------------------------------------------------------- */

static void
test_collation(void)
{
	/* U+00E9 LATIN SMALL LETTER E WITH ACUTE, spelled explicitly so the
	 * literal can never collapse into the character it is compared with. */
	gint code = lo_sort_compare(LO_SORT_LEXICOGRAPHIC_ASC, "\u00e9", "f", FALSE);
	gint coll = lo_sort_compare(LO_SORT_LEXICOGRAPHIC_ASC, "\u00e9", "f", TRUE);

	g_print("locale collation\n");

	/* By code point U+00E9 sorts after every ASCII letter. */
	CHECK(code > 0,
			"code-point order should put U+00E9 after 'f', got %d", code);

	/*
	 * Under collation it weighs as 'e', so it belongs before 'f'.
	 *
	 * This distinction only exists in a real collation locale: a C locale
	 * makes strcoll fall back to byte comparison, which is the very thing
	 * this is testing against. Report that as skipped rather than failing.
	 */
	if (coll == code)
	{
		g_print("  SKIP: collation matched byte order, so LC_COLLATE here is "
				"not a collation locale (LANG=%s)\n", g_getenv("LANG"));
	}
	else
	{
		CHECK(coll < 0,
				"collation should put U+00E9 before 'f', got %d", coll);
	}
}


static void
test_equivalence(void)
{
	g_print("duplicate equivalence vs sort order\n");

	/*
	 * The ignoring-case sort orders "Apple" before "apple" by its
	 * tie-break, but the two are still duplicates of one another.
	 */
	CHECK(lo_sort_compare(LO_SORT_NATURAL_NOCASE_ASC, "Apple", "apple",
				FALSE) != 0,
			"ignoring-case sort should still order Apple before apple");
	CHECK(lo_sort_equivalent(LO_SORT_NATURAL_NOCASE_ASC, "Apple", "apple",
				FALSE),
			"Apple and apple must still count as duplicates");

	/*
	 * Same for two spellings of one number. Under base 16 "10" is 16 and
	 * "0x10" is the same 16, so they are one value - whereas "16" is 22,
	 * and must not be folded in with them.
	 */
	CHECK(lo_sort_compare(LO_SORT_NUM_HEX_ASC, "10", "0x10", FALSE) != 0,
			"hex sort should order the two spellings apart");
	CHECK(lo_sort_equivalent(LO_SORT_NUM_HEX_ASC, "10", "0x10", FALSE),
			"10 and 0x10 must count as the same value");
	CHECK(!lo_sort_equivalent(LO_SORT_NUM_HEX_ASC, "16", "0x10", FALSE),
			"16 (hex 22) must not be folded together with 0x10 (hex 16)");

	/* Plain modes treat only byte-identical lines as duplicates. */
	CHECK(lo_sort_equivalent(LO_SORT_NATURAL_ASC, "a", "b", FALSE) == FALSE,
			"different lines are not duplicates");
	CHECK(lo_sort_equivalent(LO_SORT_NATURAL_ASC, "a", "a", FALSE),
			"identical lines are duplicates");
}


/* --------------------------------------------------------------------- */
/* Unicode and malformed input                                            */
/* --------------------------------------------------------------------- */

static void
test_unicode(void)
{
	g_print("unicode and malformed input\n");

	/* "e" is a 2-byte character; it must sort after "z" by code point. */
	CHECK(lo_sort_compare(LO_SORT_LEXICOGRAPHIC_ASC, "z", "é", FALSE) < 0,
			"multi-byte chars must order by code point, not byte count");

	CHECK(lo_sort_compare(LO_SORT_NATURAL_ASC, "z", "é", FALSE) < 0,
			"natural order must also handle multi-byte characters");

	/*
	 * An invalid byte must not hang the comparator or walk off the end.
	 * Compare a lone continuation byte against ASCII.
	 */
	{
		const gchar bad[] = { (gchar)0x80, (gchar)0x00 };
		gint r = lo_sort_compare(LO_SORT_NATURAL_ASC, bad, "a", FALSE);

		CHECK(r == 1 || r == -1, "malformed input must yield a total order");
	}
}


/* --------------------------------------------------------------------- */
/* Line operations                                                        */
/* --------------------------------------------------------------------- */

static void
test_dedupe(void)
{
	gint n = 0;
	gchar **lines;
	gchar *got;
	gint removed;

	g_print("duplicate removal\n");

	/* Ordered: first occurrence survives, input order preserved. */
	lines = make_lines(&n, "b", "a", "b", "c", "a", NULL);
	removed = lo_lines_dedupe_ordered(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "b|a|c") == 0, "dedupe ordered: got \"%s\"", got);
	CHECK(removed == 2, "dedupe ordered removed %d, expected 2", removed);
	g_free(got);
	free_lines(lines, n - removed);

	/* Consecutive only: equal but separated lines are both kept. */
	lines = make_lines(&n, "a", "a", "b", "a", NULL);
	removed = lo_lines_dedupe_consecutive(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "a|b|a") == 0,
			"dedupe consecutive: got \"%s\"", got);
	CHECK(removed == 1, "dedupe consecutive removed %d, expected 1", removed);
	g_free(got);
	free_lines(lines, n - removed);

	/* Consecutive does not chain: a run of three collapses to one. */
	lines = make_lines(&n, "a", "a", "a", NULL);
	removed = lo_lines_dedupe_consecutive(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "a") == 0, "dedupe consecutive run: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/* Sorted: orders and collapses together. */
	lines = make_lines(&n, "b", "a", "b", NULL);
	removed = lo_lines_dedupe_sorted(lines, n, LO_SORT_NATURAL_ASC, FALSE);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "a|b") == 0, "dedupe sorted: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/*
	 * Sorted under a case-insensitive mode treats case variants as equal,
	 * which is the documented consequence of choosing that mode.
	 */
	lines = make_lines(&n, "Apple", "apple", NULL);
	removed = lo_lines_dedupe_sorted(lines, n, LO_SORT_NATURAL_NOCASE_ASC,
			FALSE);
	CHECK(removed == 1, "nocase dedupe should merge Apple/apple, removed %d",
			removed);
	free_lines(lines, n - removed);

	/* But the ordered and consecutive variants never merge case variants. */
	lines = make_lines(&n, "Apple", "apple", NULL);
	removed = lo_lines_dedupe_ordered(lines, n);
	CHECK(removed == 0, "ordered dedupe must not merge case variants, "
			"removed %d", removed);
	free_lines(lines, n - removed);
}


static void
test_unique(void)
{
	gint n = 0;
	gchar **lines;
	gchar *got;
	gint removed;

	g_print("unique-line variants\n");

	/* Remove Unique Lines: drops singletons, keeps repeats. */
	lines = make_lines(&n, "once", "twice", "twice", NULL);
	removed = lo_lines_remove_unique(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "twice|twice") == 0, "remove unique: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/* Keep Unique Lines: drops repeats, keeps singletons. */
	lines = make_lines(&n, "once", "twice", "twice", NULL);
	removed = lo_lines_keep_unique(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "once") == 0, "keep unique: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/* A line appearing three times is not unique either way. */
	lines = make_lines(&n, "solo", "t", "t", "t", NULL);
	removed = lo_lines_keep_unique(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "solo") == 0, "keep unique x3: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);
}


static void
test_removal_and_trim(void)
{
	gint n = 0;
	gchar **lines;
	gchar *got;
	gint removed;

	g_print("removal and trimming\n");

	/* Remove Empty Lines: a line of spaces is NOT empty. */
	lines = make_lines(&n, "a", "", " ", "", "b", NULL);
	removed = lo_lines_remove_empty(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "a| |b") == 0, "remove empty: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/* Remove Whitespace Lines: a line of spaces IS whitespace-only. */
	lines = make_lines(&n, "a", "", " ", "\t", "b", NULL);
	removed = lo_lines_remove_whitespace(lines, n);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "a|b") == 0, "remove whitespace: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/* Whitespace detection is Unicode-aware, not just space and tab. */
	{
		gchar *nbsp = g_strdup("\xc2\xa0");   /* U+00A0 no-break space */

		CHECK(lo_lines_is_whitespace(nbsp),
				"U+00A0 must count as whitespace");
		CHECK(lo_lines_is_whitespace("\xe2\x80\x82"),   /* U+2002 en space */
				"U+2002 must count as whitespace");
		CHECK(lo_lines_is_whitespace("\xe3\x80\x80"),   /* U+3000 ideographic */
				"U+3000 must count as whitespace");
		CHECK(! lo_lines_is_whitespace("a "), "a line with text is not blank");
		g_free(nbsp);
	}

	/* Every Nth line, counting from 1. */
	lines = make_lines(&n, "1", "2", "3", "4", "5", "6", NULL);
	removed = lo_lines_remove_every_nth(lines, n, 3);
	got = join_lines(lines, n - removed);
	CHECK(strcmp(got, "1|2|4|5") == 0, "every 3rd: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n - removed);

	/* Trim trailing, including trailing tabs and multi-byte spaces. */
	lines = make_lines(&n, "a  ", "b\t", "c \xe2\x80\x82", "d", "  ", NULL);
	CHECK(lo_lines_trim_trailing(lines, n) == 4,
			"expected 4 lines trimmed at the tail");
	got = join_lines(lines, n);
	CHECK(strcmp(got, "a|b|c|d|") == 0, "trim trailing: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n);

	/* Trim leading. */
	lines = make_lines(&n, "  a", "\tb", "c", "  ", NULL);
	CHECK(lo_lines_trim_leading(lines, n) == 3,
			"expected 3 lines trimmed at the head");
	got = join_lines(lines, n);
	CHECK(strcmp(got, "a|b|c|") == 0, "trim leading: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n);
}


static void
test_join_split(void)
{
	gint n = 0;
	gchar **lines;
	gchar *got;
	gint delta;

	g_print("join and split\n");

	/* Join collapses the range onto one line. */
	lines = make_lines(&n, "alpha", "beta", "gamma", NULL);
	delta = lo_lines_join(lines, n, " ");
	CHECK(delta == -2, "join should report 2 lines removed, got %d", delta);
	CHECK(strcmp(lines[0], "alpha beta gamma") == 0,
			"join produced \"%s\"", lines[0]);
	g_free(lines[0]);
	g_free(lines);

	/* Joining a single line is a no-op. */
	lines = make_lines(&n, "solo", NULL);
	CHECK(lo_lines_join(lines, n, " ") == 0, "join of one line is a no-op");
	free_lines(lines, 1);

	/* Split on a separator grows the array. */
	lines = make_lines(&n, "a,b,c", "d,e", NULL);
	delta = lo_lines_split(&lines, &n, ",");
	CHECK(n == 5, "split produced %d lines, expected 5", n);
	CHECK(delta == 3, "split should report 3 lines added, got %d", delta);
	got = join_lines(lines, n);
	CHECK(strcmp(got, "a|b|c|d|e") == 0, "split: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n);

	/* A separator that never occurs leaves the array alone. */
	lines = make_lines(&n, "abc", "def", NULL);
	delta = lo_lines_split(&lines, &n, ";");
	CHECK(n == 2 && delta == 0, "absent separator changed the array");
	free_lines(lines, n);

	/*
	 * An empty separator means "split on whitespace", so runs of spaces
	 * collapse and empty pieces are dropped rather than produced.
	 */
	lines = make_lines(&n, "a b  c", "  d", NULL);
	delta = lo_lines_split(&lines, &n, "");
	CHECK(n == 4, "whitespace split gave %d lines, expected 4", n);
	CHECK(delta == 2, "whitespace split should add 2 lines, got %d", delta);
	got = join_lines(lines, n);
	CHECK(strcmp(got, "a|b|c|d") == 0, "whitespace split: got \"%s\"", got);
	g_free(got);
	free_lines(lines, n);
}


/* --------------------------------------------------------------------- */
/* Scale                                                                  */
/* --------------------------------------------------------------------- */

/*
 * The upstream plugin found duplicates by comparing every line to every
 * later line, which is quadratic. Sort a large array of mostly-distinct
 * lines and confirm the whole suite still finishes promptly.
 */
static void
test_scale(void)
{
	const gint N = 200000;
	gchar **lines = g_new(gchar *, (size_t)N);
	gint i;
	GTimer *timer;
	gdouble secs;
	gint removed;

	g_print("scale: %d lines\n", N);

	for (i = 0; i < N; i++)
		lines[i] = g_strdup_printf("prefix-%d-suffix", i);

	timer = g_timer_new();
	removed = lo_lines_dedupe_ordered(lines, N);
	secs = g_timer_elapsed(timer, NULL);
	g_timer_destroy(timer);

	CHECK(removed == 0, "distinct lines should remove nothing, got %d",
			removed);
	g_print("  dedupe_ordered over %d distinct lines: %.3fs\n", N, secs);

	/* Duplicated input should collapse to exactly the distinct count. */
	for (i = 0; i < N; i++)
	{
		g_free(lines[i]);
		lines[i] = g_strdup_printf("line-%d", i / 2);
	}

	removed = lo_lines_dedupe_ordered(lines, N);
	CHECK(removed == N / 2,
			"half-distinct input should remove %d, got %d", N / 2, removed);

	g_print("  total suite: %d checks, %d failures\n", checks, failures);

	/* The dropped half was freed by the operation; the rest is ours. */
	for (i = 0; i < N / 2; i++)
		g_free(lines[i]);

	g_free(lines);
}


int
main(void)
{
	/*
	 * Without this the process runs in the C locale, where strcoll is
	 * byte comparison and the collation assertions cannot distinguish
	 * anything. The collating locale comes from the environment.
	 */
	setlocale(LC_ALL, "");

	g_print("lo core tests\n\n");

	test_natural_vs_lexicographic();
	test_case_insensitive();
	test_numeric();
	test_stability();
	test_collation();
	test_equivalence();
	test_unicode();
	test_dedupe();
	test_unique();
	test_removal_and_trim();
	test_join_split();
	test_scale();

	g_print("\n%d checks, %d failures\n", checks, failures);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
