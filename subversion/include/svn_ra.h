/*
 * svn_ra.h :  structures related to repository access
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_wc.h"

typedef struct svn_ra_session_t svn_ra_session_t;

svn_error_t * svn_ra_open (svn_ra_session_t **p_ras,
                           const char *repository,
                           apr_pool_t *pool);

void svn_ra_close (svn_ra_session_t *ras);

svn_error_t * svn_ra_checkout (svn_ra_session_t *ras,
                               const char *start_at_URL,
                               int recurse,
                               const svn_delta_edit_fns_t *editor,
                               void *edit_baton);

/* Return an *EDITOR and *EDIT_BATON for transmitting a commit to the
   server.  Also, the editor guarantees that if close_edit() returns
   successfully, that *NEW_REVISION will be set to the revision number
   resulting from the commit. */
svn_error_t *
svn_ra_get_commit_editor(svn_ra_session_t *ras,
                         const svn_delta_edit_fns_t **editor,
                         void **edit_baton,
                         svn_revnum_t *new_revision);


svn_error_t * svn_ra_get_update_editor(const svn_delta_edit_fns_t **editor,
                                       void **edit_baton,
                                       ... /* more params */);


#endif  /* SVN_RA_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
