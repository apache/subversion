/*
 * log.c:  return log messages
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

/* ==================================================================== */



/*** Includes. ***/

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "client.h"

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"



/*** Getting update information ***/



/*** Public Interface. ***/


svn_error_t *
svn_client_log (svn_client_auth_baton_t *auth_baton,
                const apr_array_header_t *targets,
                const svn_client_revision_t *start,
                const svn_client_revision_t *end,
                svn_boolean_t discover_changed_paths,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *session;
  svn_stringbuf_t *URL;
  svn_stringbuf_t *basename = NULL;
  svn_string_t path_str;
  apr_array_header_t *condensed_targets;
  svn_revnum_t start_revnum, end_revnum;
  svn_error_t *err;

  if ((start->kind == svn_client_revision_unspecified)
      || (end->kind == svn_client_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, pool,
         "svn_client_log: caller failed to supply revision");
    }

  start_revnum = end_revnum = SVN_INVALID_REVNUM;

  path_str.data = (APR_ARRAY_IDX(targets, 0, svn_stringbuf_t *))->data;
  path_str.len = strlen(path_str.data);

  /* Use the passed URL, if there is one.  */
  if (svn_path_is_url (&path_str))
    {
      /* Set the URL from our first target */
      URL = svn_path_uri_encode(&path_str, pool);

      /* Initialize this array, since we'll be building it below */
      condensed_targets = apr_array_make (pool, 1, sizeof(svn_stringbuf_t *));

      /* The logic here is this: If we get passed one argument, we assume
         it is the full URL to a file/dir we want log info for. If we get
         a URL plus some paths, then we assume that the URL is the base,
         and that the paths passed are relative to it.  */
      if (targets->nelts > 1)
        {
          int i;

          /* We have some paths, let's use them. Start after the URL.  */
          for (i = 1; i < targets->nelts; i++)
            (*((svn_stringbuf_t **)apr_array_push (condensed_targets))) =
                APR_ARRAY_IDX(targets, i, svn_stringbuf_t *);
        }
      else
        {
          /* If we have a single URL, then the session will be rooted at
             it, so just send an empty stringbuf for the paths we are
             interested in. */
          (*((svn_stringbuf_t **)apr_array_push (condensed_targets))) = 
              svn_stringbuf_create ("", pool);
        }
    }
  else
    {
      svn_wc_entry_t *entry;

      /* Use local working copy.  */

      SVN_ERR (svn_path_condense_targets (&basename, &condensed_targets,
                                          targets, pool));

      if (condensed_targets->nelts == 0)
        (*((svn_stringbuf_t**)apr_array_push (condensed_targets))) =
            svn_stringbuf_create("", pool);

      SVN_ERR (svn_wc_entry (&entry, basename, pool));
      if (! entry)
        return svn_error_createf
          (SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL, pool,
          "svn_client_log: %s is not under revision control", basename->data);
      if (! entry->url)
        return svn_error_createf
          (SVN_ERR_ENTRY_MISSING_URL, 0, NULL, pool,
          "svn_client_log: entry '%s' has no URL", basename->data);
      URL = svn_stringbuf_dup (entry->url, pool);
    }

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

  /* Open a repository session to the URL. If we got here from a full URL
     passed to the command line, then we don't pass basename or
     do_auth/use_admin to open the ra_session.  */
  if (NULL != basename)
    SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, basename,
                                          NULL, TRUE, TRUE, TRUE, 
                                          auth_baton, pool));
  else
    SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL, 
                                          NULL, FALSE, FALSE, TRUE, 
                                          auth_baton, pool));

  /* Get the revisions based on the users "hints".  */
  SVN_ERR (svn_client__get_revision_number
           (&start_revnum, ra_lib, session, start, 
            basename ? basename->data : NULL, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, end, 
            basename ? basename->data : NULL, pool));

  err = ra_lib->get_log (session,
                         condensed_targets,
                         start_revnum,
                         end_revnum,
                         discover_changed_paths,
                         receiver,
                         receiver_baton);
  
  /* Special case: If there have been no commits, we'll get an error
   * for requesting log of a revision higher than 0.  But the
   * default behavior of "svn log" is to give revisions HEAD through
   * 1, on the assumption that HEAD >= 1.
   *
   * So if we got that error for that reason, and it looks like the
   * user was just depending on the defaults (rather than explicitly
   * requestion the log for revision 1), then we don't error.  Instead
   * we just invoke the receiver manually on a hand-constructed log
   * message for revision 0.
   *
   * See also http://subversion.tigris.org/issues/show_bug.cgi?id=692.
   */
  if (err && (err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
      && (start->kind == svn_client_revision_head)
      && ((end->kind == svn_client_revision_number)
          && (end->value.number == 1)))
    {
      svn_revnum_t youngest_rev;
      
      SVN_ERR (ra_lib->get_latest_revnum (session, &youngest_rev));
      if (youngest_rev == 0)
        {
          err = SVN_NO_ERROR;

          /* Log receivers are free to handle revision 0 specially... But
             just in case some don't, we make up a message here. */
          SVN_ERR (receiver (receiver_baton,
                             NULL, 0, "", "", "No commits in repository.",
                             pool));
        }
    }

  /* We're done with the RA session. */
  SVN_ERR (ra_lib->close (session));

  return err;
}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
