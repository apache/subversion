/*
 * utf_impl.h :  private header for the charset converstion functions.
 *
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
 */



#ifndef SVN_LIBSVN_SUBR_UTF_IMPL_H
#define SVN_LIBSVN_SUBR_UTF_IMPL_H


#include "svn_error.h"
#include "svn_pools.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


const char *svn_utf__cstring_from_utf8_fuzzy (const char *src,
                                              apr_pool_t *pool,
                                              svn_error_t *(*convert_from_utf8)
                                              (const char **,
                                               const char *,
                                               apr_pool_t *));


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_UTF_IMPL_H */
