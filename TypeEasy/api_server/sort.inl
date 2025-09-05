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
 * This file contains a sort function that is not available on some platforms.
 * It is included in civetweb.c, and does not need to be compiled otherwise.
 */


#if defined(USE_QSORT_R)

/* Use qsort_r */
static void
mg_sort(void *base,
        size_t nel,
        size_t width,
        int (*comp)(const void *, const void *, void *),
        void *thunk)
{
	qsort_r(base, nel, width, comp, thunk);
}

#elif defined(_WIN32)

/* Use qsort_s */
static void
mg_sort(void *base,
        size_t nel,
        size_t width,
        int (*comp)(const void *, const void *, void *),
        void *thunk)
{
	qsort_s(base, nel, width, comp, thunk);
}

#else

/* No system provided qsort_r or qsort_s - use local implementation. */

struct mg_sort_thunk_data {
	int (*comp)(const void *, const void *, void *);
	void *thunk;
};

static int
mg_sort_comp_wrapper(const void *p1, const void *p2)
{
	extern struct mg_sort_thunk_data mg_sort_thunk_data_g;
	return mg_sort_thunk_data_g.comp(p1, p2, mg_sort_thunk_data_g.thunk);
}

struct mg_sort_thunk_data mg_sort_thunk_data_g;

static void
mg_sort(void *base,
        size_t nel,
        size_t width,
        int (*comp)(const void *, const void *, void *),
        void *thunk)
{
	mg_sort_thunk_data_g.comp = comp;
	mg_sort_thunk_data_g.thunk = thunk;
	qsort(base, nel, width, mg_sort_comp_wrapper);
}

#endif