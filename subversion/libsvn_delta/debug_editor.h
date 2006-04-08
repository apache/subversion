/*
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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


#ifndef SVN_DEBUG_EDITOR_H
#define SVN_DEBUG_EDITOR_H

#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Return a debug editor that wraps @a wrapped_editor.
 *
 * The debug editor simply prints an indication of what callbacks are being
 * called to @c stderr, and is only intended for use in debugging subversion
 * editors.
 */
svn_error_t *
svn_delta__get_debug_editor(const svn_delta_editor_t **editor,
                            void **edit_baton,
                            const svn_delta_editor_t *wrapped_editor,
                            void *wrapped_baton,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DEBUG_EDITOR_H */
