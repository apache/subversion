/*  svn_utf.h:  UTF-8 conversion routines
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



#ifndef SVN_UTF_H
#define SVN_UTF_H

#include "svn_error.h"
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Set *DEST to a utf8-encoded stringbuf from native stringbuf SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_stringbuf_to_utf8 (const svn_stringbuf_t *src,
                                        svn_stringbuf_t **dest,
                                        apr_pool_t *pool);


/* Set *DEST to a utf8-encoded stringbuf from native C string SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_cstring_to_utf8_stringbuf (const char *src,
                                                svn_stringbuf_t **dest,
                                                apr_pool_t *pool);


/* Set *DEST to a utf8-encoded C string from native C string SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_cstring_to_utf8 (const char *src,
                                      const char **dest,
                                      apr_pool_t *pool);


/* Set *DEST to a natively-encoded stringbuf from utf8 stringbuf SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_stringbuf_from_utf8 (const svn_stringbuf_t *src,
					  svn_stringbuf_t **dest,
					  apr_pool_t *pool);


/* Set *DEST to a natively-encoded string from utf8 string SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_string_from_utf8 (const svn_string_t *src,
                                       const svn_string_t **dest,
                                       apr_pool_t *pool);


/* Set *DEST to a natively-encoded C string from utf8 C string SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_cstring_from_utf8 (const char *src,
                                        const char **dest,
                                        apr_pool_t *pool);


/* Set *DEST to a natively-encoded C string from utf8 stringbuf SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_cstring_from_utf8_stringbuf (const svn_stringbuf_t *src,
                                                  const char **dest,
                                                  apr_pool_t *pool);


/* Set *DEST to a natively-encoded C string from utf8 string SRC;
   allocate *DEST in POOL. */
svn_error_t *svn_utf_cstring_from_utf8_string (const svn_string_t *src,
                                               const char **dest,
                                               apr_pool_t *pool);


/* Convert UTF8_STRING to native encoding and store in BUF, storing
   no more than BUFSIZE octets.  Note: this function is meant for
   error message printing. */
const char *svn_utf_utf8_to_native (const char *utf8_string,
                                    char *buf,
                                    apr_size_t bufsize);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_XML_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
