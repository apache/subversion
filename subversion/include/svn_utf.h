/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_utf.h
 * @brief UTF-8 conversion routines
 */



#ifndef SVN_UTF_H
#define SVN_UTF_H

#include "svn_error.h"
#include "svn_delta.h"
#include <apr_xlate.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Set @a *dest to a utf8-encoded stringbuf from native stringbuf @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_stringbuf_to_utf8 (svn_stringbuf_t **dest,
                                        const svn_stringbuf_t *src,
                                        apr_pool_t *pool);


/** Set @a *dest to a utf8-encoded string from native string @a src; allocate
 * @a *dest in @a pool.
 */
svn_error_t *svn_utf_string_to_utf8 (const svn_string_t **dest,
                                     const svn_string_t *src,
                                     apr_pool_t *pool);


/** Set @a *dest to a utf8-encoded stringbuf from native C string @a src;
 * allocate @a *dest in @a pool.
 *
 * Set @a *dest to a utf8-encoded stringbuf from native C string @a src;
 * allocate @a *dest in @a pool.   Use @a xlator to do the conversion;  if
 * @c NULL, then use the environment's default locale.
 */
svn_error_t *svn_utf_cstring_to_utf8_stringbuf (svn_stringbuf_t **dest,
                                                const char *src,
                                                apr_xlate_t *xlator,
                                                apr_pool_t *pool);


/** Set @a *dest to a utf8-encoded C string from native C string @a src;
 * allocate @a *dest in @a pool.
 *
 * Set @a *dest to a utf8-encoded C string from native C string @a src;
 * allocate @a *dest in @a pool.  Use @a xlator to do the conversion; if 
 * @c NULL, then use the environment's default locale.
 */
svn_error_t *svn_utf_cstring_to_utf8 (const char **dest,
                                      const char *src,
                                      apr_xlate_t *xlator,
                                      apr_pool_t *pool);


/** Set @a *dest to a natively-encoded stringbuf from utf8 stringbuf @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_stringbuf_from_utf8 (svn_stringbuf_t **dest,
					  const svn_stringbuf_t *src,
					  apr_pool_t *pool);


/** Set @a *dest to a natively-encoded string from utf8 string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_string_from_utf8 (const svn_string_t **dest,
                                       const svn_string_t *src,
                                       apr_pool_t *pool);


/** Set @a *dest to a natively-encoded C string from utf8 C string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_utf8 (const char **dest,
                                        const char *src,
                                        apr_pool_t *pool);


/** Return a fuzzily native-encoded C string from utf8 C string @a src,
 * allocated in @a pool.
 *
 * Return a fuzzily native-encoded C string from utf8 C string SRC,
 * allocated in @a pool.  A fuzzy recoding leaves all 7-bit ascii
 * characters the same, and substitutes "?\\XXX" for others, where XXX
 * is the unsigned decimal code for that character.
 *
 * This function cannot error; it is guaranteed to return something.
 * First it will recode as described above and then attempt to convert
 * the (new) 7-bit UTF-8 string to native encoding.  If that fails, it
 * will return the raw fuzzily recoded string, which may or may not be
 * meaningful in the client's locale, but is (presumably) better than
 * nothing.
 *
 * ### Notes:
 *
 * Improvement is possible, even imminent.  The original problem was
 * that if you converted a UTF-8 string (say, a log message) into a
 * locale that couldn't represent all the characters, you'd just get a
 * static placeholder saying "[unconvertible log message]".  Then
 * Justin Erenkrantz pointed out how on platforms that didn't support
 * conversion at all, "svn log" would still fail completely when it
 * encountered unconvertible data.
 *
 * Now for both cases, the caller can at least fall back on this
 * function, which converts the message as best it can, substituting
 * ?\\XXX escape codes for the non-ascii characters.
 *
 * Ultimately, some callers may prefer the iconv "//TRANSLIT" option,
 * so when we can detect that at configure time, things will change.
 * Also, this should (?) be moved to apr/apu eventually.
 *
 * See http://subversion.tigris.org/issues/show_bug.cgi?id=807 for
 * details.
 */
const char *svn_utf_cstring_from_utf8_fuzzy (const char *src,
                                             apr_pool_t *pool);


/** Set @a *dest to a natively-encoded C string from utf8 stringbuf @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_utf8_stringbuf (const char **dest,
                                                  const svn_stringbuf_t *src,
                                                  apr_pool_t *pool);


/** Set @a *dest to a natively-encoded C string from utf8 string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_utf8_string (const char **dest,
                                               const svn_string_t *src,
                                               apr_pool_t *pool);


/** Convert @a utf8_string to native encoding and store in @a buf, storing
 * no more than @a bufsize octets.
 *
 * Convert @a utf8_string to native encoding and store in @a buf, storing
 * no more than @a bufsize octets.  Note: this function is meant for
 * error message printing.
 */
const char *svn_utf_utf8_to_native (const char *utf8_string,
                                    char *buf,
                                    apr_size_t bufsize);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_XML_H */
