/*
 * apply_edits.c:  shared code for checkouts and updates
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

/* ==================================================================== */



/*** Includes. ***/

#include <assert.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Helpers ***/

static svn_error_t *
generic_read (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  apr_file_t *src = (apr_file_t *) baton;
  apr_status_t stat;
  
  stat = apr_full_read (src, buffer, (apr_size_t) *len, (apr_size_t *) len);
  
  if (stat && !APR_STATUS_IS_EOF(stat))
    return
      svn_error_create (stat, 0, NULL, pool,
                        "error reading incoming delta stream");
  
  else 
    return 0;  
}


static svn_error_t *
apply_delta (const svn_delta_edit_fns_t *before_editor,
             void *before_edit_baton,
             const svn_delta_edit_fns_t *after_editor,
             void *after_edit_baton,
             void *delta_src,
             svn_read_fn_t *read_fn,
             svn_string_t *dest,
             svn_string_t *repos,            /* ignored if update */
             svn_string_t *ancestor_path,    /* ignored if update */
             svn_revnum_t ancestor_revision,  /* ignored if update */
             apr_pool_t *pool,
             svn_boolean_t is_update)
{
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  svn_error_t *err;

  if (! ancestor_path)
    ancestor_path = svn_string_create ("", pool);
  if (ancestor_revision == SVN_INVALID_REVNUM)
    ancestor_revision = 1;
      
  if (is_update)
    {
      err = svn_wc_get_update_editor (dest,
                                      ancestor_revision,
                                      &editor,
                                      &edit_baton,
                                      pool);
    }
  else /* checkout */
    {
      err = svn_wc_get_checkout_editor (dest,
                                        repos,
                                        ancestor_path,
                                        ancestor_revision,
                                        &editor,
                                        &edit_baton,
                                        pool);
    }
  if (err)
    return err;

  svn_delta_wrap_editor (&editor,
                         &edit_baton,
                         before_editor,
                         before_edit_baton,
                         editor,
                         edit_baton,
                         after_editor,
                         after_edit_baton,
                         pool);

  return svn_delta_xml_auto_parse (read_fn,
                                   delta_src,
                                   editor,
                                   edit_baton,
                                   ancestor_path,
                                   ancestor_revision,
                                   pool);
}



static svn_error_t *
do_edits (const svn_delta_edit_fns_t *before_editor,
          void *before_edit_baton,
          const svn_delta_edit_fns_t *after_editor,
          void *after_edit_baton,
          svn_string_t *path,
          svn_string_t *xml_src,
          svn_string_t *ancestor_path,    /* ignored if update */
          svn_revnum_t ancestor_revision,  /* ignored if update */
          apr_pool_t *pool,
          svn_boolean_t is_update)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *in = NULL;

  /* kff todo: obviously, this will work differently. :-) */
  const char *repos = ":ssh:jrandom@subversion.tigris.org/repos";

  assert (path != NULL);
  assert (xml_src != NULL);

  /* Open the XML source file. */
  apr_err = apr_open (&in, xml_src->data,
                      (APR_READ | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "unable to open %s", xml_src->data);

  /* Check out the delta. */
  err = apply_delta (before_editor,
                     before_edit_baton,
                     after_editor,
                     after_edit_baton,
                     in,
                     generic_read,
                     path,
                     svn_string_create (repos, pool),
                     ancestor_path,
                     ancestor_revision,
                     pool,
                     is_update);
  if (err)
    {
      apr_close (in);
      return err;
    }

  apr_close (in);

  return SVN_NO_ERROR;
}



/*** Public Interfaces. ***/

svn_error_t *
svn_client__checkout_internal (const svn_delta_edit_fns_t *before_editor,
                               void *before_edit_baton,
                               const svn_delta_edit_fns_t *after_editor,
                               void *after_edit_baton,
                               svn_string_t *path,
                               svn_string_t *xml_src,
                               svn_string_t *ancestor_path,
                               svn_revnum_t ancestor_revision,
                               apr_pool_t *pool)
{
  return do_edits (before_editor, before_edit_baton,
                   after_editor, after_edit_baton,
                   path, xml_src, ancestor_path, ancestor_revision, pool, 0);
}


svn_error_t *
svn_client__update_internal (const svn_delta_edit_fns_t *before_editor,
                             void *before_edit_baton,
                             const svn_delta_edit_fns_t *after_editor,
                             void *after_edit_baton,
                             svn_string_t *path,
                             svn_string_t *xml_src,
                             svn_revnum_t ancestor_revision,
                             apr_pool_t *pool)
{
  return do_edits (before_editor, before_edit_baton,
                   after_editor, after_edit_baton,
                   path, xml_src, NULL, ancestor_revision, pool, 1);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
