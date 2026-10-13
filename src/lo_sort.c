/*
 *      lo_sort.c - Comparison and ordering of lines.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *      and extended with the full Notepad++ line-sort matrix.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include "lo_sort.h"

#include <string.h>


/* --------------------------------------------------------------------- */
/* Character-level helpers                                                 */
/* --------------------------------------------------------------------- */

/*
 * Decode the character at 's', storing its byte length in *len.
 *
 * Malformed input must not be able to stall the comparison or walk off the
 * end of the buffer, so an invalid sequence is reported as its single raw
 * byte with a length of 1. Valid UTF-8 keeps its true length.
 */
static gint
lo_next_char(const gchar *s, const gchar *end, gint *len)
{
	const guchar c0 = (guchar)s[0];
	gint need;

	if (c0 < 0x80)
	{
		*len = 1;
		return (gint)c0;
	}
	else if (c0 < 0xC0)          /* stray continuation byte */
	{
		*len = 1;
		return (gint)c0;
	}
	else if (c0 < 0xE0)
		need = 2;
	else if (c0 < 0xF0)
		need = 3;
	else
		need = 4;

	if (end - s < need || !g_utf8_validate(s, need, NULL))
	{
		*len = 1;
		return (gint)c0;
	}

	*len = need;
	return (gint)g_utf8_get_char(s);
}


/*
 * Compare two characters.
 *
 * With collation on, g_utf8_collate() decides, which makes 'e' sort beside
 * 'e' rather than after every ASCII letter. Collators are allowed to
 * return an arbitrary magnitude, so the result is normalised to -1/0/1 to
 * keep the merge sort's invariants intact.
 */
static gint
lo_char_cmp(gint ca, gint cb, gboolean use_collation)
{
	if (ca == cb)
		return 0;

	if (use_collation)
	{
		gchar sa[8], sb[8];
		gint la, lb, r;

		la = g_unichar_to_utf8((gunichar)ca, sa);
		lb = g_unichar_to_utf8((gunichar)cb, sb);
		if (la <= 0)
			la = g_unichar_to_utf8((gunichar)'?', sa);
		if (lb <= 0)
			lb = g_unichar_to_utf8((gunichar)'?', sb);

		r = g_utf8_collate(sa, sb);
		return r < 0 ? -1 : (r > 0 ? 1 : 0);
	}

	return ca < cb ? -1 : 1;
}


/* Case-fold a single character for the case-insensitive modes. */
static gint
lo_fold(gint c)
{
	return (gint)g_unichar_tolower((gunichar)c);
}


/* --------------------------------------------------------------------- */
/* Natural ordering                                                       */
/* --------------------------------------------------------------------- */

/*
 * Compare two runs of decimal digits as numbers, without converting them
 * to an integer, so arbitrarily long runs ("00000000000000000009") compare
 * correctly instead of overflowing into each other.
 *
 * Leading zeros are insignificant, so they are skipped first; the run with
 * fewer significant digits is the smaller number.
 */
static gint
lo_cmp_digit_runs(const gchar *a, gint alen, const gchar *b, gint blen)
{
	while (alen > 0 && *a == '0') { a++; alen--; }
	while (blen > 0 && *b == '0') { b++; blen--; }

	if (alen != blen)
		return alen < blen ? -1 : 1;

	if (alen > 0)
	{
		gint d = strncmp(a, b, (size_t)alen);

		if (d != 0)
			return d < 0 ? -1 : 1;
	}

	return 0;
}


/*
 * Natural ("human") ordering: runs of digits compare numerically, anything
 * else compares by character.
 *
 * This is what makes "file9.txt" sort before "file10.txt", which plain
 * lexicographic ordering gets backwards.
 *
 * When both lines hold digit runs of equal numeric value but different
 * widths ("1" vs "01"), the shorter run sorts first. Without that
 * tie-break the two lines would compare equal and their relative order
 * would be decided by nothing but their original position.
 */
static gint
lo_cmp_natural(const gchar *a, const gchar *b, gboolean nocase,
		gboolean use_collation)
{
	const gchar *ae = a + strlen(a);
	const gchar *be = b + strlen(b);

	while (a < ae && b < be)
	{
		if (g_ascii_isdigit(*a) && g_ascii_isdigit(*b))
		{
			const gchar *as = a, *bs = b;
			gint alen, blen, d;

			while (a < ae && g_ascii_isdigit(*a)) a++;
			while (b < be && g_ascii_isdigit(*b)) b++;

			alen = (gint)(a - as);
			blen = (gint)(b - bs);

			d = lo_cmp_digit_runs(as, alen, bs, blen);
			if (d != 0)
				return d;

			if (alen != blen)
				return alen < blen ? -1 : 1;
		}
		else
		{
			gint alen = 0, blen = 0;
			gint ca = lo_next_char(a, ae, &alen);
			gint cb = lo_next_char(b, be, &blen);
			gint d;

			if (nocase)
			{
				ca = lo_fold(ca);
				cb = lo_fold(cb);
			}

			d = lo_char_cmp(ca, cb, use_collation);
			if (d != 0)
				return d;

			a += alen;
			b += blen;
		}
	}

	/* One or both lines are exhausted; the shorter remainder is smaller. */
	if (a < ae)
		return 1;
	if (b < be)
		return -1;
	return 0;
}


/* --------------------------------------------------------------------- */
/* Numeric ordering                                                       */
/* --------------------------------------------------------------------- */

/* Result of reading a whole line as a number in some base. */
typedef struct
{
	gboolean negative;
	gboolean valid;        /* at least one digit of the base was seen */
	const gchar *digits;   /* significant digits, leading zeros skipped */
	gint ndigits;
} lo_number;


/*
 * Read a whole line as a number in 'base'.
 *
 * Leading whitespace and a sign are accepted, and a "0x"/"0o" prefix is
 * stripped for the matching base so that pasted hex and octal literals sort
 * as their values rather than as 0. Scanning stops at the first character
 * that is not a digit of that base, so "10abc" reads as 10 under base 10 and
 * a line with no valid digits at all reads as zero.
 *
 * The magnitude is kept as a digit string rather than an integer, so values
 * wider than any fixed-width type still compare correctly.
 */
static void
lo_scan_number(const gchar *s, gint base, lo_number *out)
{
	const gchar *p = s;
	const gchar *sig = NULL;
	gint nsig = 0;

	out->negative = FALSE;
	out->valid = FALSE;
	out->digits = NULL;
	out->ndigits = 0;

	while (g_ascii_isspace(*p))
		p++;

	if (*p == '+')
		p++;
	else if (*p == '-')
	{
		out->negative = TRUE;
		p++;
	}

	if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	else if (base == 8 && p[0] == '0' && (p[1] == 'o' || p[1] == 'O'))
		p += 2;

	for ( ; *p != '\0'; p++)
	{
		gint d;

		if (g_ascii_isdigit(*p))
			d = *p - '0';
		else if (g_ascii_isalpha(*p))
			d = g_ascii_tolower(*p) - 'a' + 10;
		else
			break;

		if (d >= base)
			break;

		out->valid = TRUE;

		/* Leading zeros are not significant: "007" is the magnitude 7. */
		if (nsig == 0)
		{
			if (d == 0)
				continue;

			sig = p;
		}

		nsig++;
	}

	out->digits = sig;
	out->ndigits = nsig;
}


/*
 * Compare two lines as numbers of the given base.
 *
 * More significant digits means a larger magnitude, so the comparison is by
 * digit count first and only then by the digits themselves. That keeps
 * arbitrary-width values ordered without ever converting them to a type
 * too narrow to hold them.
 */
static gint
lo_cmp_numbers(const gchar *a, const gchar *b, gint base)
{
	lo_number na, nb;

	lo_scan_number(a, base, &na);
	lo_scan_number(b, base, &nb);

	/* A line with no digits is zero, and zero is never negative. */
	if (!na.valid)
		na.negative = FALSE;
	if (!nb.valid)
		nb.negative = FALSE;
	if (na.ndigits == 0)
		na.negative = FALSE;
	if (nb.ndigits == 0)
		nb.negative = FALSE;

	if (na.negative != nb.negative)
		return na.negative ? -1 : 1;

	if (na.ndigits != nb.ndigits)
	{
		gint d = na.ndigits < nb.ndigits ? -1 : 1;

		/* A larger magnitude sorts lower when both are negative. */
		return na.negative ? -d : d;
	}

	if (na.ndigits > 0)
	{
		gint d = strncmp(na.digits, nb.digits, (size_t)na.ndigits);

		if (d != 0)
		{
			d = d < 0 ? -1 : 1;
			return na.negative ? -d : d;
		}
	}

	return 0;
}


/* The radix each numeric mode reads its lines in. */
static gint
lo_sort_mode_base(lo_sort_mode mode)
{
	switch (mode)
	{
		case LO_SORT_NUM_HEX_ASC:
		case LO_SORT_NUM_HEX_DESC:
			return 16;
		case LO_SORT_NUM_OCT_ASC:
		case LO_SORT_NUM_OCT_DESC:
			return 8;
		default:
			return 10;
	}
}


/* --------------------------------------------------------------------- */
/* Mode dispatch                                                          */
/* --------------------------------------------------------------------- */

gboolean
lo_sort_mode_is_desc(lo_sort_mode mode)
{
	switch (mode)
	{
		case LO_SORT_NATURAL_DESC:
		case LO_SORT_LEXICOGRAPHIC_DESC:
		case LO_SORT_NATURAL_NOCASE_DESC:
		case LO_SORT_NUM_DEC_DESC:
		case LO_SORT_NUM_HEX_DESC:
		case LO_SORT_NUM_OCT_DESC:
			return TRUE;
		default:
			return FALSE;
	}
}


gboolean
lo_sort_mode_is_nocase(lo_sort_mode mode)
{
	return mode == LO_SORT_NATURAL_NOCASE_ASC ||
			mode == LO_SORT_NATURAL_NOCASE_DESC;
}


gboolean
lo_sort_mode_is_numeric(lo_sort_mode mode)
{
	switch (mode)
	{
		case LO_SORT_NUM_DEC_ASC:
		case LO_SORT_NUM_DEC_DESC:
		case LO_SORT_NUM_HEX_ASC:
		case LO_SORT_NUM_HEX_DESC:
		case LO_SORT_NUM_OCT_ASC:
		case LO_SORT_NUM_OCT_DESC:
			return TRUE;
		default:
			return FALSE;
	}
}


const gchar *
lo_sort_mode_label(lo_sort_mode mode)
{
	switch (mode)
	{
		case LO_SORT_NATURAL_ASC:        return "natural ascending";
		case LO_SORT_NATURAL_DESC:       return "natural descending";
		case LO_SORT_LEXICOGRAPHIC_ASC:  return "lexicographic ascending";
		case LO_SORT_LEXICOGRAPHIC_DESC: return "lexicographic descending";
		case LO_SORT_NATURAL_NOCASE_ASC: return "natural (ignoring case) ascending";
		case LO_SORT_NATURAL_NOCASE_DESC:return "natural (ignoring case) descending";
		case LO_SORT_NUM_DEC_ASC:        return "decimal ascending";
		case LO_SORT_NUM_DEC_DESC:       return "decimal descending";
		case LO_SORT_NUM_HEX_ASC:        return "hexadecimal ascending";
		case LO_SORT_NUM_HEX_DESC:       return "hexadecimal descending";
		case LO_SORT_NUM_OCT_ASC:        return "octal ascending";
		case LO_SORT_NUM_OCT_DESC:       return "octal descending";
		default:                         return "unknown";
	}
}


/* --------------------------------------------------------------------- */
/* Public comparison entry point                                          */
/* --------------------------------------------------------------------- */

gint
lo_sort_compare(lo_sort_mode mode, const gchar *a, const gchar *b,
		gboolean use_collation)
{
	gint result;

	switch (mode)
	{
		case LO_SORT_NATURAL_ASC:
		case LO_SORT_NATURAL_DESC:
			result = lo_cmp_natural(a, b, FALSE, use_collation);
			break;

		case LO_SORT_NATURAL_NOCASE_ASC:
		case LO_SORT_NATURAL_NOCASE_DESC:
			result = lo_cmp_natural(a, b, TRUE, use_collation);
			/*
			 * Case-folded equality still needs a total order, otherwise
			 * "Apple" and "apple" would land in an order that depends
			 * only on where they happened to sit in the input. Falling
			 * back to the case-sensitive comparison settles it the same
			 * way every run.
			 */
			if (result == 0)
				result = lo_cmp_natural(a, b, FALSE, use_collation);
			break;

		case LO_SORT_NUM_DEC_ASC:
		case LO_SORT_NUM_DEC_DESC:
		case LO_SORT_NUM_HEX_ASC:
		case LO_SORT_NUM_HEX_DESC:
		case LO_SORT_NUM_OCT_ASC:
		case LO_SORT_NUM_OCT_DESC:
			result = lo_cmp_numbers(a, b, lo_sort_mode_base(mode));

			/*
			 * Equal values still need a total order, otherwise two
			 * spellings of the same number ("16" and "0x10") would
			 * swap places between runs. Compare the raw text ascending
			 * so the tie-break never flips with the mode.
			 */
			if (result == 0)
			{
				gint d = strcmp(a, b);

				result = (d < 0) ? -1 : (d > 0 ? 1 : 0);
			}
			break;

		case LO_SORT_LEXICOGRAPHIC_ASC:
		case LO_SORT_LEXICOGRAPHIC_DESC:
		default:
			/*
			 * Lexicographic means a plain comparison with no numeric
			 * awareness. With collation on, g_utf8_collate() supplies it.
			 */
			if (use_collation)
				result = g_utf8_collate(a, b);
			else
				result = strcmp(a, b);

			result = (result < 0) ? -1 : (result > 0 ? 1 : 0);
			break;
	}

	/* Every descending mode is its ascending counterpart, reversed. */
	if (lo_sort_mode_is_desc(mode))
		result = -result;

	return result;
}


/*
 * Whether two lines are duplicates of one another under 'mode'.
 *
 * This is deliberately NOT the same question as lo_sort_compare(). That
 * function finishes with a tie-break so the sort is a total order, which
 * means "Apple" and "apple" compare unequal under the ignoring-case mode.
 * Whether those are *duplicates* is a different question: under a
 * case-insensitive ordering they are the same line, and
 * "Remove Duplicate Lines, Sorted" should merge them. The numeric modes
 * behave the same way, so "16" and "0x10" are one value.
 */
gboolean
lo_sort_equivalent(lo_sort_mode mode, const gchar *a, const gchar *b,
		gboolean use_collation)
{
	switch (mode)
	{
		case LO_SORT_NATURAL_NOCASE_ASC:
		case LO_SORT_NATURAL_NOCASE_DESC:
			return lo_cmp_natural(a, b, TRUE, use_collation) == 0;

		case LO_SORT_NUM_DEC_ASC:
		case LO_SORT_NUM_DEC_DESC:
		case LO_SORT_NUM_HEX_ASC:
		case LO_SORT_NUM_HEX_DESC:
		case LO_SORT_NUM_OCT_ASC:
		case LO_SORT_NUM_OCT_DESC:
			return lo_cmp_numbers(a, b, lo_sort_mode_base(mode)) == 0;

		default:
			/*
			 * For the plain orders, "equal" is byte equality, which is
			 * what a natural comparison collapses to once the digit-run
			 * rearrangement can no longer change anything.
			 */
			return lo_cmp_natural(a, b, FALSE, use_collation) == 0;
	}
}


/* --------------------------------------------------------------------- */
/* Stable merge sort                                                      */
/* --------------------------------------------------------------------- */

/*
 * glibc's qsort is not required to be stable, and an unstable sort would
 * make "Remove Duplicate Lines, Sorted" drop an arbitrary member of each
 * run of equal lines. A bottom-up merge sort is stable, in-place apart
 * from one scratch buffer the size of the input, and has no recursion.
 */
void
lo_sort_sort(gchar **lines, gint n, lo_sort_mode mode, gboolean use_collation)
{
	gchar **scratch;
	gint width;

	if (n < 2)
		return;

	scratch = g_new(gchar *, (size_t)n);

	for (width = 1; width < n; width *= 2)
	{
		gint i;

		for (i = 0; i < n; i += 2 * width)
		{
			gint lo = i;
			gint mid = MIN(i + width, n);
			gint hi = MIN(i + 2 * width, n);
			gint l = lo, r = mid, k = lo;

			/* Any unpaired tail is already in order at the front. */
			if (mid >= hi)
			{
				memcpy(scratch + lo, lines + lo,
						(size_t)(hi - lo) * sizeof(gchar *));
				continue;
			}

			while (l < mid && r < hi)
			{
				/* '<= 0' keeps the left run first, which is what makes
				 * the sort stable. */
				if (lo_sort_compare(mode, lines[l], lines[r],
							use_collation) <= 0)
					scratch[k++] = lines[l++];
				else
					scratch[k++] = lines[r++];
			}

			while (l < mid)
				scratch[k++] = lines[l++];
			while (r < hi)
				scratch[k++] = lines[r++];
		}

		memcpy(lines, scratch, (size_t)n * sizeof(gchar *));
	}

	g_free(scratch);
}
