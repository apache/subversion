/*
 * commit.c :  routines for committing changes to the server
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



#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_wc.h"

#include "ra_session.h"


typedef struct
{
  svn_ra_session_t *ras;
  const char *activity_url;
  apr_hash_t *workrsrc;         /* PATH -> WORKING RESOURCE */

  /* This is how we pass back the new revision number to our callers. */
  svn_revnum_t *new_revision;

} commit_ctx_t;


static svn_error_t *
create_activity (commit_ctx_t *cc)
{
  /* ### send a REPORT request (DAV:repository-report) to find out where to
     ### create the activity.
     ### NOTE: we should cache this in the admin subdir
  */

  /* ### send the MKACTIVITY request
     ### need GUID generation
  */

  return NULL;
}

#if 0  /* With -Wall, we keep getting a warning that this is defined
          but not used.  It aids Emacs reflexes to have no warnings at
          all, so #if this out until it's actually used.  -kff*/  
static svn_error_t *
checkout_resource (commit_ctx_t *cc, const char *src_url, const char **wr_url)
{
  /* ### examine cc->workrsrc -- we may already have a WR */
  return NULL;
}
#endif /* 0 */

static svn_error_t *
commit_delete (svn_string_t *name,
               void *parent_baton)
{
  /* ### CHECKOUT, then DELETE */
  return NULL;
}

static svn_error_t *
commit_add_dir (svn_string_t *name,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_revnum_t ancestor_revision,
                void **child_baton)
{
  /* ### CHECKOUT parent, then MKCOL */
  return NULL;
}

static svn_error_t *
commit_rep_dir (svn_string_t *name,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_revnum_t ancestor_revision,
                void **child_baton)
{
  /* ### if replacing with ancestor of something else, then CHECKOUT target
     ### and COPY ancestor over the target
     ### replace w/o an ancestor is just a signal for change within the
     ### dir and we do nothing
  */
  return NULL;
}

static svn_error_t *
commit_change_dir_prop (void *dir_baton,
                        svn_string_t *name,
                        svn_string_t *value)
{
  /* ### CHECKOUT, then PROPPATCH */
  return NULL;
}

static svn_error_t *
commit_close_dir (void *dir_baton)
{
  /* ### nothing? */

  /* ### finish of the top-level dir... right point for commit?
     ### (MERGE, DELETE on the activity)
  */
  return NULL;
}

static svn_error_t *
commit_add_file (svn_string_t *name,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_revnum_t ancestor_revision,
                 void **file_baton)
{
  /* ### CHECKOUT parent (then PUT in apply_txdelta) */
  return NULL;
}

static svn_error_t *
commit_rep_file (svn_string_t *name,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_revnum_t ancestor_revision,
                 void **file_baton)
{
  /* ### CHECKOUT (then PUT in apply_txdelta) */
  /* ### if replacing with a specific ancestor, then COPY */
  return NULL;
}

static svn_error_t *
commit_apply_txdelta (void *file_baton, 
                      svn_txdelta_window_handler_t **handler,
                      void **handler_baton)
{
  /* ### PUT */
  return NULL;
}

static svn_error_t *
commit_change_file_prop (void *file_baton,
                         svn_string_t *name,
                         svn_string_t *value)
{
  /* CHECKOUT, then PROPPATCH */
  return NULL;
}

static svn_error_t *
commit_close_file (void *file_baton)
{
  /* ### nothing? */
  return NULL;
}

static svn_error_t *
commit_close_edit (void *edit_baton)
{
  commit_ctx_t *cc = (commit_ctx_t *) edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;

  /* todo: set new_revision according to response from server. */

  /* Make sure the caller (most likely the working copy library, or
     maybe its caller) knows the new revision. */
  *(cc->new_revision) = new_revision;

  /* ### nothing? */
  return NULL;
}

/*
** This structure is used during the commit process. An external caller
** uses these callbacks to describe all the changes in the working copy
** that must be committed to the server.
*/
static const svn_delta_edit_fns_t commit_editor = {
  NULL,  /* commit_replace_root, someday */
  commit_delete,
  commit_add_dir,
  commit_rep_dir,
  commit_change_dir_prop,
  commit_close_dir,
  commit_add_file,
  commit_rep_file,
  commit_apply_txdelta,
  commit_change_file_prop,
  commit_close_file,
  commit_close_edit
};

svn_error_t *
svn_ra_get_commit_editor(svn_ra_session_t *ras,
                         const svn_delta_edit_fns_t **editor,
                         void **edit_baton,
                         svn_revnum_t *new_revision)
{
  commit_ctx_t *cc = apr_pcalloc(ras->pool, sizeof(*cc));
  svn_error_t *err;

  cc->ras = ras;
  err = create_activity(cc);
  if (err)
    return err;

  /* Record where the caller wants the new revision number stored. */
  cc->new_revision = new_revision;

  *edit_baton = cc;

  *editor = &commit_editor;

  return NULL;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
