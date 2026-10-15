/*
 *      test_lo_doc.c - Tests for the document text layer.
 *
 *      The Scintilla-facing code in lo_edit.c is a thin wrapper over these
 *      functions, so exercising them here covers the parts most likely to
 *      corrupt a file: line-terminator handling and block reordering.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "lo_doc.h"

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

/* Escape both sides: a lone CR and a lone LF render identically on a
 * terminal, so the raw form of these failures is unreadable. */
#define CHECK_STR(actual, expected) \
	G_STMT_START { \
		const gchar *_a = (actual); \
		const gchar *_e = (expected); \
		checks++; \
		if (strcmp(_a, _e) != 0) { \
			gchar *_ea = g_strescape(_e, NULL); \
			gchar *_aa = g_strescape(_a, NULL); \
			failures++; \
			g_printerr("  FAIL %s:%d: expected [%s], got [%s]\n", \
					__FILE__, __LINE__, _ea, _aa); \
			g_free(_ea); \
			g_free(_aa); \
		} \
	} G_STMT_END


/* Extract [start..end] and render it back as "a|b|c" for comparison. */
static gchar *
extract_join(const gchar *text, gint start, gint end, gint *n,
		gboolean *terminated)
{
	gchar **lines = lo_doc_extract(text, start, end, n, terminated);
	GString *s = g_string_new(NULL);
	gint i;

	for (i = 0; i < *n; i++)
	{
		if (i > 0)
			g_string_append_c(s, '|');

		g_string_append(s, lines[i]);
	}

	g_strfreev(lines);

	return g_string_free(s, FALSE);
}


static void
test_line_count(void)
{
	g_print("line counting\n");

	CHECK(lo_doc_line_count("") == 1, "an empty document is one empty line");
	CHECK(lo_doc_line_count("a") == 1, "no terminator is one line");
	CHECK(lo_doc_line_count("a\n") == 1, "a trailing terminator adds no line");
	CHECK(lo_doc_line_count("a\nb") == 2, "two lines, last unterminated");
	CHECK(lo_doc_line_count("a\nb\n") == 2, "trailing terminator adds no line");
	CHECK(lo_doc_line_count("\n") == 1, "a lone terminator is one empty line");
	CHECK(lo_doc_line_count("\n\n") == 2, "two lone terminators, two lines");
	CHECK(lo_doc_line_count("a\r\nb\r\n") == 2, "CRLF documents count the same");
	CHECK(lo_doc_line_count("a\n\nb") == 3, "a blank line in the middle counts");
}


static void
test_eol_detection(void)
{
	g_print("line terminator detection\n");

	CHECK_STR(lo_doc_eol("a\r\nb"), "\r\n");
	CHECK_STR(lo_doc_eol("a\nb"), "\n");
	CHECK_STR(lo_doc_eol("ab"), "\n");       /* no terminator: default to LF */
	CHECK_STR(lo_doc_eol("a\rb"), "\r");
}


static void
test_extract(void)
{
	gint n = 0;
	gboolean term = FALSE;
	gchar *got;

	g_print("extracting lines\n");

	got = extract_join("a\nb\nc\n", 0, 2, &n, &term);
	CHECK_STR(got, "a|b|c");
	CHECK(n == 3, "expected 3 lines, got %d", n);
	CHECK(term, "the last line of a terminated document is terminated");
	g_free(got);

	got = extract_join("a\nb\nc", 0, 2, &n, &term);
	CHECK_STR(got, "a|b|c");
	CHECK(n == 3, "expected 3 lines, got %d", n);
	CHECK(!term, "an unterminated final line must not report as terminated");
	g_free(got);

	/* A middle range keeps the terminator state of ITS last line. */
	got = extract_join("a\nb\nc", 0, 1, &n, &term);
	CHECK_STR(got, "a|b");
	CHECK(term, "a non-final line is always terminated");
	g_free(got);

	/* CRLF must not leak carriage returns into the extracted text. */
	got = extract_join("a\r\nb\r\n", 0, 1, &n, &term);
	CHECK_STR(got, "a|b");
	CHECK(term, "CRLF final line is terminated");
	g_free(got);

	/* A range reaching past the end stops at the last real line. */
	got = extract_join("a\nb", 0, 99, &n, &term);
	CHECK_STR(got, "a|b");
	CHECK(n == 2, "expected the range to clamp to 2 lines, got %d", n);
	g_free(got);

	/* Empty lines survive extraction. */
	got = extract_join("a\n\n\nb\n", 0, 3, &n, &term);
	CHECK_STR(got, "a|||b");
	g_free(got);

	/* Trailing whitespace is content, not a terminator. */
	got = extract_join("a  \nb\t\n", 0, 1, &n, &term);
	CHECK_STR(got, "a  |b\t");
	g_free(got);
}


static void
test_reassemble(void)
{
	gchar *lines[3];
	gchar *out;

	g_print("reassembling lines\n");

	lines[0] = (gchar *)"a";
	lines[1] = (gchar *)"b";
	lines[2] = (gchar *)"c";

	/* The trailing terminator is restored only when it was there before. */
	out = lo_doc_reassemble(lines, 3, "\n", TRUE);
	CHECK_STR(out, "a\nb\nc\n");
	g_free(out);

	out = lo_doc_reassemble(lines, 3, "\n", FALSE);
	CHECK_STR(out, "a\nb\nc");
	g_free(out);

	out = lo_doc_reassemble(lines, 3, "\r\n", TRUE);
	CHECK_STR(out, "a\r\nb\r\nc\r\n");
	g_free(out);

	/* An empty result must not gain a lone terminator. */
	out = lo_doc_reassemble(lines, 0, "\n", TRUE);
	CHECK_STR(out, "");
	g_free(out);

	/*
	 * This is the property that matters in the editor: running an
	 * operation over a file with no final newline must not add one.
	 */
	out = lo_doc_reassemble(lines, 2, "\n", FALSE);
	CHECK_STR(out, "a\nb");
	g_free(out);
}


static void
test_roundtrip(void)
{
	static const gchar *const cases[] = {
		"a\nb\nc\n",
		"a\nb\nc",
		"a\r\nb\r\nc\r\n",
		"\n",
		"",
		"only\n",
		"trailing   \n",
		NULL
	};
	gint i;

	g_print("extract/reassemble round trip\n");

	for (i = 0; cases[i] != NULL; i++)
	{
		const gchar *text = cases[i];
		gint n = 0;
		gboolean term = FALSE;
		gchar *lines, *again;
		gint count = lo_doc_line_count(text);

		lines = extract_join(text, 0, count - 1, &n, &term);

		{
			gchar **arr = lo_doc_extract(text, 0, count - 1, &n, &term);

			again = lo_doc_reassemble(arr, n, lo_doc_eol(text), term);
			g_strfreev(arr);
		}

		CHECK_STR(again, text);
		if (strcmp(again, text) != 0)
			g_printerr("       (input was \"%s\")\n", text);

		g_free(again);
		g_free(lines);
	}
}


/* A fresh L0..L3 array; freed with free_lines4, not g_strfreev (it is not
 * a heap array, so g_strfreev would hand a stack address to free). */
static gchar **
make_four(gint *n)
{
	gchar **lines = g_new(gchar *, 4);
	gint i;

	for (i = 0; i < 4; i++)
		lines[i] = g_strdup_printf("L%d", i);

	*n = 4;
	return lines;
}


static void
free_four(gchar **lines)
{
	gint i;

	for (i = 0; i < 4; i++)
		g_free(lines[i]);

	g_free(lines);
}


/* Move the given block and return "L0|L1|L2|L3" style rendering. */
static gchar *
moved(gint start, gint end, gboolean up)
{
	gint n = 0;
	gchar **lines = make_four(&n);
	gchar *out;

	lo_doc_move_block(lines, n, start, end, up);
	out = g_strjoinv("|", lines);
	free_four(lines);

	return out;
}


static void
test_move_block(void)
{
	gint n = 0;
	gchar **lines;
	gchar *out;

	g_print("moving a block of lines\n");

	/* A single line swaps with its neighbour, leaving the rest alone. */
	out = moved(2, 2, TRUE);
	CHECK_STR(out, "L0|L2|L1|L3");
	g_free(out);

	out = moved(1, 1, FALSE);
	CHECK_STR(out, "L0|L2|L1|L3");
	g_free(out);

	/*
	 * A multi-line block moves as a unit, so lines 1 and 2 jump above
	 * line 0 together rather than line 2 alone.
	 */
	out = moved(1, 2, TRUE);
	CHECK_STR(out, "L1|L2|L0|L3");
	g_free(out);

	out = moved(0, 2, FALSE);
	CHECK_STR(out, "L3|L0|L1|L2");
	g_free(out);

	/* Moving a block down past a single line. */
	out = moved(0, 1, FALSE);
	CHECK_STR(out, "L2|L0|L1|L3");
	g_free(out);

	/*
	 * At the top and bottom of the document there is nothing to swap
	 * with, so the request is refused and the array is left alone.
	 */
	lines = make_four(&n);
	CHECK(!lo_doc_move_block(lines, n, 0, 0, TRUE), "move up at top refused");
	CHECK(!lo_doc_move_block(lines, n, 3, 3, FALSE),
			"move down at bottom refused");

	out = g_strjoinv("|", lines);
	CHECK_STR(out, "L0|L1|L2|L3");
	g_free(out);

	/* Out-of-range requests are refused rather than corrupting the array. */
	CHECK(!lo_doc_move_block(lines, n, 2, 1, TRUE), "inverted range refused");
	CHECK(!lo_doc_move_block(lines, n, -1, 0, TRUE), "negative start refused");
	CHECK(!lo_doc_move_block(lines, n, 0, 99, FALSE),
			"overlong range refused");

	out = g_strjoinv("|", lines);
	CHECK_STR(out, "L0|L1|L2|L3");
	g_free(out);

	/* A refused move must leave every element reachable exactly once. */
	{
		GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
		gint i, unique = 0;

		for (i = 0; i < n; i++)
		{
			if (!g_hash_table_contains(seen, lines[i]))
				unique++;
			g_hash_table_add(seen, lines[i]);
		}

		CHECK(unique == 4, "expected 4 distinct lines, got %d", unique);
		g_hash_table_destroy(seen);
	}

	free_four(lines);
}


/*
 * The line operations compact the array in place, so after one runs the
 * slots past the new count hold stale pointers - freed strings, or second
 * copies of survivors - and the array was never NULL-terminated in the
 * first place. Only the first 'n' entries may be freed. Run under
 * make asan, freeing by terminator instead reports the overrun.
 */
static void
test_free_lines(void)
{
	gchar **lines = g_new(gchar *, 3);      /* exactly 3: no terminator */

	g_print("freeing a compacted line array\n");

	lines[0] = g_strdup("keep");
	lines[1] = g_strdup("drop");
	lines[2] = g_strdup("keep too");

	/* What a removal leaves behind: survivor moved down over the dropped
	 * line, and its old slot still pointing at it. */
	g_free(lines[1]);
	lines[1] = lines[2];

	lo_doc_free_lines(lines, 2);
	CHECK(TRUE, "freeing the live prefix must not touch the stale tail");

	lo_doc_free_lines(NULL, 0);
	CHECK(TRUE, "freeing nothing is a no-op");
}


static void
test_slice(void)
{
	gchar *s;

	g_print("slicing raw regions\n");

	s = lo_doc_slice("a\nb\nc\n", 0, 1);
	CHECK_STR(s, "a\nb\n");
	g_free(s);

	s = lo_doc_slice("a\nb\nc", 0, 1);
	CHECK_STR(s, "a\nb\n");
	g_free(s);

	s = lo_doc_slice("a\nb\nc", 1, 2);
	CHECK_STR(s, "b\nc");
	g_free(s);

	s = lo_doc_slice("a\nb\nc\n", 2, 2);
	CHECK_STR(s, "c\n");
	g_free(s);

	s = lo_doc_slice("a\r\nb\r\n", 0, 0);
	CHECK_STR(s, "a\r\n");
	g_free(s);
}


int
main(void)
{
	g_print("lo doc tests\n\n");

	test_line_count();
	test_eol_detection();
	test_extract();
	test_reassemble();
	test_roundtrip();
	test_move_block();
	test_free_lines();
	test_slice();

	g_print("\n%d checks, %d failures\n", checks, failures);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
