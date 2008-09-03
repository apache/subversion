/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @file svn_utf_private.h
 * @brief UTF validation routines
 */

#ifndef SVN_UTF_PRIVATE_H
#define SVN_UTF_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return TRUE if the string SRC of length LEN is a valid UTF-8 encoding
 * according to the rules laid down by the Unicode 4.0 standard, FALSE
 * otherwise.  This function is faster than svn_utf__last_valid().
 */
svn_boolean_t
svn_utf__is_valid(const char *src, apr_size_t len);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_UTF_PRIVATE_H */
