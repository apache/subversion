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
#ifndef SVN_RUBY_H
#define SVN_RUBY_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* New macro in 1.7 */
#ifndef StringValuePtr
#define StringValuePtr(v) RSTRING(v)->ptr
#endif

#ifndef LONG2NUM
#define LONG2NUM(v) INT2NUM(v)
#endif

#ifndef LONG2FIX
#define LONG2FIX(v) INT2FIX(v)
#endif

#include <svn_error.h>
extern VALUE svn_ruby_mSvn;

void svn_ruby_init_apr (void);
void svn_ruby_init_client (void);
void svn_ruby_init_delta_editor (void);
void svn_ruby_init_error (void);
void svn_ruby_init_fs (void);
void svn_ruby_init_fs_node (void);
void svn_ruby_init_fs_root (void);
void svn_ruby_init_fs_txn (void);
void svn_ruby_init_ra (void);
void svn_ruby_init_repos (void);
void svn_ruby_init_stream (void);
void svn_ruby_init_txdelta (void);
void svn_ruby_init_types (void);
void svn_ruby_init_wc (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RUBY_H */
