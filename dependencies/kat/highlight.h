/*
 * MIT License
 *
 * Copyright (c) 2020 Davidson Francis <davidsondfgl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include "hashtable.h"

/* External definitions. */
extern char *kat_COLORS[];
extern int LENGTHS[];
extern unsigned char symbols_table[];
extern int CURRENT_THEME;

/* Colors constants. */
#define RESET_COLOR   "\033[0m"
#define PREPROC_COLOR    0
#define TYPES_COLOR      1
#define KWRDS_COLOR      2
#define NUMBER_COLOR     3
#define STRING_COLOR     4
#define COMMENT_COLOR    5
#define FUNC_CALL_COLOR  6
#define SYMBOL_COLOR     7

/* Always inline. */
#    if defined(__GNUC__) || defined(__GNUG__) || defined(__clang__)
#        define INLINE  __attribute__((always_inline)) inline
#    else
#        define INLINE inline
#    endif

/* Highlighted line. */
struct highlighted_line
{
    size_t idx;
    size_t size;
};

/**
 * Allocates a new Highlighted Buffer line.
 *
 * A Highlighted Buffer Line is a facility that transparently allows
 * the user to append chars and strings inside a buffer without having
 * to worry about space, reallocs and so on, something similiar to
 * SDS strings.
 *
 * @returns Return a pointer to the highlighted line.
 */
extern char* highlight_alloc_line(void);

/**
 * Deallocate a Highlighted Line Buffer.
 *
 * @param line Highlighted Line Buffer to be deallocated.
 */
extern void highlight_free(char *line);

/**
 * For a given line @p line and a (already) allocated
 * highlighted line buffer @p hl, highlights the
 * line and returns @p hl with the highlighted line.
 *
 * @param line Line (null terminated string) to be highlighted.
 * @param hl Pre-allocated Highlighted Line buffer.
 *
 * @return Returns a Highlighted Line Buffer.
 */
extern char *highlight_line(const char *line, char *hl, size_t str_size);

/**
 * Initialize the syntax highlight engine.
 *
 * @param theme_file Theme file, if NULL, will use
 * the internal theme.
 *
 * @return Returns 0 if success and 1 otherwise.
 */
extern int highlight_init(const char *theme_file);

/**
 * Finishes the highlight 'engine'.
 */
extern void highlight_finish(void);

/* ----------------------- Inline functions. ----------------------- */

/**
 * Appends a single char @p c into the highlighted buffer
 * @p line.
 *
 * @param line Highlighted Buffer.
 * @param c Char to be highlighted.
 *
 * @return Returns a pointer to the highlighted buffer containing
 * the appended character,
 */
INLINE char* add_char_to_hl(char *line, char c)
{
    struct highlighted_line *hl;
    hl = ((struct highlighted_line *)line - 1);
    
    if (hl->idx >= hl->size)
    {
        hl->size += 32;
        hl = realloc(hl, sizeof(struct highlighted_line) +
                     (sizeof(char) * hl->size));
    }
    line = (char*)(hl+1);
    line[hl->idx++] = c;
    return (line);
}

/**
 * Appends a new string @p str of size @p size into the
 * highlighted buffer @p line.
 *
 * @param line Highlighted Buffer.
 * @param str String to be appended.
 * @param size Size of the given string @p str.
 *
 * @return Returns a pointer to the highlighted buffer containing
 * the appended string.
 */
INLINE char* add_str_to_hl(char *line, const char *str, size_t size)
{
    struct highlighted_line *hl;
    hl = ((struct highlighted_line *)line - 1);
    
    if (!size)
        size = strlen(str);
    
    if ( (hl->size - hl->idx) < size )
    {
        /* Make room for the string and adds 32 extra chars. */
        hl->size += (size - (hl->size - hl->idx)) + 32;
        hl = realloc(hl, sizeof(struct highlighted_line) +
                     (sizeof(char) * hl->size));
    }
    line = (char*)(hl+1);
    memcpy(line+hl->idx, str, size);
    hl->idx += size;
    return (line);
}

/**
 * Checks if the given character belongs to a valid keyword
 * or not.
 *
 * @param c Character to be checked.
 *
 * @return Returns 1 if the character belongs to a valid
 * keyword, 0 otherwise.
 */
INLINE static int is_char_keyword(char c)
{
    return (isalpha(c) || isdigit(c) || c == '_');
}

/**
 * Highlight (or not) a given symbol @p c.
 *
 * @param c Symbol to be highlighted.
 * @param hl Highlighted Line Buffer.
 */
INLINE static int highlight_symbol(char c, char **hl)
{
    if (symbols_table[(unsigned char)c])
    {
        *hl = add_str_to_hl(*hl, kat_COLORS[CURRENT_THEME+SYMBOL_COLOR],
                            LENGTHS[CURRENT_THEME+SYMBOL_COLOR]);
        *hl = add_char_to_hl(*hl, c);
        *hl = add_str_to_hl(*hl, RESET_COLOR, 4);
        return (1);
    }
    return (0);
}

#endif /* HIGHLIGHT_H */
