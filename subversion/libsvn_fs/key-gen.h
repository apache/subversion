/* key-gen.c --- manufacturing sequential keys for some db tables
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_KEY_GEN_H
#define SVN_LIBSVN_FS_KEY_GEN_H

#include "apr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return the value of the string of digits at DATA as an ASCII
   decimal number.  The string is at most LEN bytes long.  The value
   of the number is at most MAX.  Set *END to the address of the first
   byte after the number, or zero if an error occurred while
   converting the number (overflow, for example).

   We would like to use strtoul, but that family of functions is
   locale-dependent, whereas we're trying to parse data in a
   local-independent format.  */

apr_size_t svn_fs__getsize (const char *data, apr_size_t len,
                            const char **endptr, apr_size_t max);


/* Store the ASCII decimal representation of VALUE at DATA.  Return
   the length of the representation if all goes well; return zero if
   the result doesn't fit in LEN bytes.  */
int svn_fs__putsize (char *data, apr_size_t len, apr_size_t value);


/* In the `representations' and `strings', the value at this key is
   the key to use when storing a new rep or string. */
extern const char svn_fs__next_key_key[];


/* Generate the next key after a given alphanumeric key.
 *
 * The first *LEN bytes of THIS are an ascii representation of a
 * number in base 36: digits 0-9 have their usual values, and a-z have
 * values 10-35.
 *
 * The new key is stored in NEXT, null-terminated.  NEXT must be at
 * least *LEN + 2 bytes long -- one extra byte to hold a possible
 * overflow column, and one for null termination.  On return, *LEN
 * will be set to the length of the new key, not counting the null
 * terminator.  In other words, the outgoing *LEN will be either equal
 * to the incoming, or to the incoming + 1.
 *
 * If THIS contains anything other than digits and lower-case
 * alphabetic characters, or if it starts with `0' but is not the
 * string "0", then *LEN is set to zero and the effect on NEXT
 * is undefined.
 */
void svn_fs__next_key (const char *this, apr_size_t *len, char *next);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_KEY_GEN_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
