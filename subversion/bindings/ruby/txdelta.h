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
#ifndef TXDELTA_H
#define TXDELTA_H
VALUE svn_ruby_txdelta_new (svn_txdelta_window_handler_t handler,
                            void *handler_baton,
                            apr_pool_t *pool);

void svn_ruby_txdelta (VALUE txdelta,
                       svn_txdelta_window_handler_t *handler,
                       void **baton);
#endif /* TXDELTA_H */

