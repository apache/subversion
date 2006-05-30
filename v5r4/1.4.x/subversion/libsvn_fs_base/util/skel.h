/* skel.h : interface to `skeleton' functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_SKEL_H
#define SVN_LIBSVN_FS_SKEL_H

#include <apr_pools.h>

#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* What is a skel?  */

/* Subversion needs to read a lot of structured data from database
   records.  Instead of writing a half-dozen parsers and getting lazy
   about error-checking, we define a reasonably dense, open-ended
   syntax for strings and lists, and then use that for the concrete
   representation of files, directories, property lists, etc.  This
   lets us handle all the fussy character-by-character testing and
   sanity checks all in one place, allowing the users of this library
   to focus on higher-level consistency.

   A `skeleton' (or `skel') is either an atom, or a list.  A list may
   contain zero or more elements, each of which may be an atom or a
   list.

   Here's a description of the syntax of a skel:

   A "whitespace" byte is either 9, 10, 12, 13, or 32 (ASCII tab,
   newline, form feed, and space).

   A "digit" byte is 48 -- 57 (ASCII digits).

   A "name" byte is 65 -- 90, or 97 -- 122 (ASCII upper- and
   lower-case characters).

   An atom has one the following two forms:
   - any string of bytes whose first byte is a name character, and
     which contains no whitespace characters, bytes 40 (ASCII '(') or
     bytes 41 (ASCII ')') (`implicit-length form'), or
   - a string of digit bytes, followed by exactly one whitespace
     character, followed by N bytes, where N is the value of the digit
     bytes as a decimal number (`explicit-length form').

   In the first case, the `contents' of the atom are the entire string
   of characters.  In the second case, the contents of the atom are
   the N bytes after the count and whitespace.

   A list consists of a byte 40 (ASCII '('), followed by a series of
   atoms or lists, followed by a byte 41 (ASCII ')').  There may be
   zero or more whitespace characters after the '(' and before the
   ')', and between any pair of elements.  If two consecutive elements
   are atoms, they must be separated by at least one whitespace
   character.  */


/* The `skel' structure.  */

/* A structure representing the results of parsing an array of bytes
   as a skel.  */
struct skel_t {

  /* True if the string was an atom, false if it was a list.

     If the string is an atom, DATA points to the beginning of its
     contents, and LEN gives the content length, in bytes.

     If the string is a list, DATA and LEN delimit the entire body of
     the list.  */
  svn_boolean_t is_atom;

  const char *data;
  apr_size_t len;

  /* If the string is a list, CHILDREN is a pointer to a
     null-terminated linked list of skel objects representing the
     elements of the list, linked through their NEXT pointers.  */
  struct skel_t *children;
  struct skel_t *next;
};
typedef struct skel_t skel_t;



/* Operations on skels.  */


/* Parse the LEN bytes at DATA as the concrete representation of a
   skel, and return a skel object allocated from POOL describing its
   contents.  If the data is not a properly-formed SKEL object, return
   zero.

   The returned skel objects point into the block indicated by DATA
   and LEN; we don't copy the contents. */
skel_t *svn_fs_base__parse_skel(const char *data, apr_size_t len,
                                apr_pool_t *pool);


/* Create an atom skel whose contents are the C string STR, allocated
   from POOL.  */
skel_t *svn_fs_base__str_atom(const char *str, apr_pool_t *pool);


/* Create an atom skel whose contents are the LEN bytes at ADDR,
   allocated from POOL.  */
skel_t *svn_fs_base__mem_atom(const void *addr, apr_size_t len,
                              apr_pool_t *pool);


/* Create an empty list skel, allocated from POOL.  */
skel_t *svn_fs_base__make_empty_list(apr_pool_t *pool);


/* Prepend SKEL to LIST.  */
void svn_fs_base__prepend(skel_t *skel, skel_t *list);


/* Append SKEL to LIST.  This is not as efficient as prepending skels,
   so prepend in places where you can sensibly do so, and you want to
   save a couple clock cycles. */
void svn_fs_base__append(skel_t *skel, skel_t *list);


/* Return a string whose contents are a concrete representation of
   SKEL.  Allocate the string from POOL.  */
svn_stringbuf_t *svn_fs_base__unparse_skel(skel_t *skel, apr_pool_t *pool);


/* Return true iff SKEL is an atom whose data is the same as STR.  */
svn_boolean_t svn_fs_base__matches_atom(skel_t *skel, const char *str);


/* Return true iff SKEL is an atom whose data is the same as STR.  */
svn_boolean_t svn_fs_base__atom_matches_string(skel_t *skel,
                                               const svn_string_t *str);


/* Return the length of the list skel SKEL.  Atoms have a length of -1.  */
int svn_fs_base__list_length(skel_t *skel);


/* Return TRUE if SKEL1 and SKEL2 are the same in structure and contents,
   or 0 if they are not.  This is like a lisp `equal' not `eq': atoms
   are equal if their lengths and contents are the same, lists are
   equal if they have the same number and order of equal elements. */
svn_boolean_t svn_fs_base__skels_are_equal(skel_t *skel1, skel_t *skel2);


/* Make a copy of SKEL and its data in POOL.  */
skel_t *svn_fs_base__copy_skel(skel_t *skel, apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_SKEL_H */
