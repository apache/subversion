/*
 * utf_impl.h :  private header for the charset converstion functions.
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



#ifndef SVN_LIBSVN_SUBR_UTF_IMPL_H
#define SVN_LIBSVN_SUBR_UTF_IMPL_H


#include <apr_pools.h>
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


const char *svn_utf__cstring_from_utf8_fuzzy(const char *src,
                                             apr_pool_t *pool,
                                             svn_error_t *(*convert_from_utf8)
                                             (const char **,
                                              const char *,
                                              apr_pool_t *));


/* Return a pointer to the first character after the last valid UTF-8
 * multi-byte character in the string SRC of length LEN.  If SRC is a valid
 * UTF-8 the return value will point to the byte after SRC+LEN, otherwise
 * it will point to the start of the first invalid multi-byte character.
 * In either case all the characters between SRC and the return pointer are
 * valid UTF-8.
 */
const char *svn_utf__last_valid(const char *src, apr_size_t len);

/* Return TRUE if the string SRC of length LEN is a valid UTF-8 encoding
 * according to the rules laid down by the Unicode 4.0 standard, FALSE
 * otherwise.  This function is faster than svn_utf__last_valid.
 */
svn_boolean_t svn_utf__is_valid(const char *src, apr_size_t len);

/* As for svn_utf__is_valid but SRC is NULL terminated. */
svn_boolean_t svn_utf__cstring_is_valid(const char *src);

/* As for svn_utf__last_valid but uses a different implementation without
   lookup tables.  It avoids the table memory use (about 400 bytes) but the
   function is longer (about 200 bytes extra) and likely to be slower when
   the string is valid.  If the string is invalid this function may be
   faster since it returns immediately rather than continuing to the end of
   the string.  The main reason this function exists is to test the table
   driven implementation.  */
const char *svn_utf__last_valid2(const char *src, apr_size_t len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_UTF_IMPL_H */
