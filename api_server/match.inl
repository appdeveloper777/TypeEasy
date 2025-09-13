/* Copyright (c) 2013-2017 the Civetweb developers
 * Copyright (c) 2004-2013 Sergey Lyubka
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*
 * This file contains an implementation of pattern matching function
 * with some extended glob features.
 * It is included in civetweb.c, and does not need to be compiled otherwise.
 */


/* mg_match_prefix is a helper function for match_prefix.
 * It has the same functionality but does not need an mg_match_context.
 */
static ptrdiff_t
mg_match_prefix(const char *pattern,
                size_t pattern_len,
                const char *str,
                int is_case_sensitive)
{
	const char *p = pattern, *s = str;
	ptrdiff_t i = 0, j = 0, star_index = -1, s_index = -1;

	while (s[i] != '\0') {
		/* We are not at the end of the string. */
		if ((p[j] != '\0') && (p[j] != '*')
		    && ((is_case_sensitive ? p[j] : (char)tolower((unsigned char)p[j]))
		        == (is_case_sensitive ? s[i]
		                               : (char)tolower((unsigned char)s[i])))) {
			/* The characters match. */
			i++;
			j++;
		} else if ((p[j] != '\0') && (p[j] == '*')) {
			/* We have a *. */
			star_index = j;
			s_index = i;
			j++;
		} else if (star_index != -1) {
			/* There was a * before, and we did not match. */
			j = star_index + 1;
			s_index++;
			i = s_index;
		} else {
			/* No * and no match. */
			return -1;
		}
	}

	/* We are at the end of the string. */
	/* Check if there are trailing stars. */
	while ((p[j] != '\0') && (p[j] == '*')) {
		j++;
	}

	/* If we are at the end of the pattern, we have a match. */
	if (p[j] == '\0') {
		return (ptrdiff_t)pattern_len;
	}

	return -1;
}


/* This is a pattern matching function with some extended glob features.
 * It is not intended to be used for many thousands of files in a directory.
 *
 * It returns the number of matching characters, or -1 if no match.
 *
 * The matching is case-insensitive by default.
 * This can be changed by the match context.
 *
 * The pattern may be a comma separated list of patterns.
 * E.g., "**.c,**.h" matches all files with a .c or .h extension.
 *
 * The following wildcards are supported:
 *   ?     matches any single character.
 *   *     matches any sequence of characters.
 *   **    matches any sequence of characters, including directory separators.
 *
 * A pattern starting with "!" is a negation pattern.
 * It is not a match if the rest of the pattern matches.
 * E.g., "**.c,!x.c" matches all .c files, but not x.c.
 *
 * A pattern starting with a \ is a verbatim pattern.
 * It will be compared literally, without wildcard processing.
 * E.g., "\a.c" matches the file "a.c" only.
 *
 * Note that this function is not using the mg_match_prefix function.
 * It is a different implementation, but the behavior for the '*'
 * wildcard is equivalent.
 */
static ptrdiff_t
match_prefix_strlen(const char *pattern, const char *str)
{
	const char *or_str;
	ptrdiff_t res = -1;

	if (!pattern || !str) {
		return -1;
	}

	if ((or_str = strchr(pattern, ',')) != NULL) {
		res = match_prefix_strlen(or_str + 1, str);
		if (res > 0) {
			return res;
		}
		return match_prefix_strlen(
		    mg_strndup(pattern, (size_t)(or_str - pattern)), str);
	}

	if (*pattern == '!') {
		res = match_prefix_strlen(pattern + 1, str);
		return (res <= 0) ? -1 : 0;
	}

	if (*pattern == '\\') {
		pattern++;
		return (mg_strcasecmp(pattern, str) == 0) ? (ptrdiff_t)strlen(str) : -1;
	}

	if (!strcmp(pattern, "**")) {
		return (ptrdiff_t)strlen(str);
	}

	if (mg_match_prefix(pattern, strlen(pattern), str, 0) > 0) {
		res = (ptrdiff_t)strlen(str);
	}

	return res;
}