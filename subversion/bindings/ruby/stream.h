/*
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
#ifndef SVN_RUBY__STREAM_H
#define SVN_RUBY__STREAM_H

#include <svn_io.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

VALUE svn_ruby_stream_new (VALUE class, svn_stream_t *stream, apr_pool_t *pool);
svn_stream_t *svn_ruby_stream (VALUE aStream);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RUBY__STREAM_H */
