/*
 * log.c:  return log messages
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
svn_client_log (const apr_array_header_t *targets,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *session;
  const char *path;
  const char *base_url;
  const char *base_name = NULL;
  const char *auth_dir;
  apr_array_header_t *condensed_targets;
  svn_revnum_t start_revnum, end_revnum;
  svn_error_t *err = SVN_NO_ERROR;  /* Because we might have no targets. */

  if ((start->kind == svn_opt_revision_unspecified)
      || (end->kind == svn_opt_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, NULL,
         "svn_client_log: caller failed to supply revision");
    }

  start_revnum = end_revnum = SVN_INVALID_REVNUM;

  path = (APR_ARRAY_IDX(targets, 0, const char *));

  /* Use the passed URL, if there is one.  */
  if (svn_path_is_url (path))
    {
      base_url = path;

      /* Initialize this array, since we'll be building it below */
      condensed_targets = apr_array_make (pool, 1, sizeof (const char *));

      /* The logic here is this: If we get passed one argument, we assume
         it is the full URL to a file/dir we want log info for. If we get
         a URL plus some paths, then we assume that the URL is the base,
         and that the paths passed are relative to it.  */
      if (targets->nelts > 1)
        {
          int i;

          /* We have some paths, let's use them. Start after the URL.  */
          for (i = 1; i < targets->nelts; i++)
            (*((const char **)apr_array_push (condensed_targets))) =
                APR_ARRAY_IDX(targets, i, const char *);
        }
      else
        {
          /* If we have a single URL, then the session will be rooted at
             it, so just send an empty string for the paths we are
             interested in. */
          (*((const char **)apr_array_push (condensed_targets))) = "";
        }
    }
  else
    {
      svn_wc_adm_access_t *adm_access;
      apr_array_header_t *target_urls;
      apr_array_header_t *real_targets;
      int i;
      
      /* Get URLs for each target */
      target_urls = apr_array_make (pool, 1, sizeof (const char *));
      real_targets = apr_array_make (pool, 1, sizeof (const char *));
      for (i = 0; i < targets->nelts; i++) 
        {
          const svn_wc_entry_t *entry;
          const char *URL;
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target,
                                          FALSE, FALSE, pool));
          SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
          if (! entry)
            {
              /* Remove unversioned target from list, send 'skip' signal. */
              if (ctx->notify_func)
                (*ctx->notify_func) (ctx->notify_baton, target,
                                     svn_wc_notify_skip, svn_node_unknown,
                                     NULL, svn_wc_notify_state_unknown,
                                     svn_wc_notify_state_unknown,
                                     SVN_INVALID_REVNUM);

              
              continue;
            }
          if (! entry->url)
            return svn_error_createf
              (SVN_ERR_ENTRY_MISSING_URL, NULL,
              "svn_client_log: entry '%s' has no URL", target);
          URL = apr_pstrdup (pool, entry->url);
          SVN_ERR (svn_wc_adm_close (adm_access));
          (*((const char **)apr_array_push (target_urls))) = URL;
          (*((const char **)apr_array_push (real_targets))) = target;
        }

      /* if we have no valid target_urls, just exit. */
      if (target_urls->nelts == 0)
        return SVN_NO_ERROR;

      /* Find the base URL and condensed targets relative to it. */
      SVN_ERR (svn_path_condense_targets (&base_url, &condensed_targets,
                                          target_urls, TRUE, pool));

      if (condensed_targets->nelts == 0)
        (*((const char **)apr_array_push (condensed_targets))) = "";

      /* 'targets' now becomes 'real_targets', which has bogus,
         unversioned things removed from it. */
      targets = real_targets;
    }

  /* Get the RA library that handles BASE_URL */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, base_url, pool));

  /* Open a repository session to the BASE_URL.  If we got here from a full 
     URL passed to the command line, then if the current directory is a
     working copy, we pass it as base_name for authentication
     purposes.  But we make sure to treat it as read-only, since when
     one operates on URLs, one doesn't expect it to change anything in
     the working copy. */
  SVN_ERR (svn_path_condense_targets (&base_name, NULL, targets, TRUE, pool)); 
  if (NULL != base_name)
    SVN_ERR (svn_client__open_ra_session (&session, ra_lib, base_url, 
                                          base_name, NULL, NULL, TRUE, TRUE, 
                                          ctx, pool));
  else
    {
      SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", pool));
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, base_url,
                                            auth_dir,
                                            NULL, NULL, FALSE, TRUE, 
                                            ctx, pool));
    }

  /* It's a bit complex to correctly handle the special revision words
   * such as "BASE", "COMMITTED", and "PREV".  For example, if the
   * user runs
   *
   *   $ svn log -rCOMMITTED foo.txt bar.c
   *
   * which committed rev should be used?  The younger of the two?  The
   * first one?  Should we just error?
   *
   * None of the above, I think.  Rather, the committed rev of each
   * target in turn should be used.  This is what most users would
   * expect, and is the most useful interpretation.  Of course, this
   * goes for the other dynamic (i.e., local) revision words too.
   *
   * Note that the code to do this is a bit more complex than a simple
   * loop, because the user might run
   *
   *    $ svn log -rCOMMITTED:42 foo.txt bar.c
   *
   * in which case we want to avoid recomputing the static revision on
   * every iteration.
   */
  {
    svn_boolean_t start_is_local = svn_client__revision_is_local (start);
    svn_boolean_t end_is_local = svn_client__revision_is_local (end);

    if (! start_is_local)
      SVN_ERR (svn_client__get_revision_number
               (&start_revnum, ra_lib, session, start, base_name, pool));

    if (! end_is_local)
      SVN_ERR (svn_client__get_revision_number
               (&end_revnum, ra_lib, session, end, base_name, pool));

    if (start_is_local || end_is_local)
      {
        /* ### FIXME: At least one revision is locally dynamic, that
         * is, we're in a case similar to one of these:
         *
         *   $ svn log -rCOMMITTED foo.txt bar.c
         *   $ svn log -rCOMMITTED:42 foo.txt bar.c
         *
         * We'll to iterate over each target in turn, getting the logs
         * for the named range.  This means that certain revisions may
         * be printed out more than once.  I think that's okay
         * behavior, since the sense of the command is that one wants
         * a particular range of logs for *this* file, then another
         * range for *that* file, and so on.  But we should at
         * probably some sort of separator header between the log
         * groups.  Of course, libsvn_client can't just print stuff
         * out -- it has to take a callback from the client to do
         * that.  So we need to define that callback interface, then
         * have the command line client pass one down here.
         *
         * In any case, at least it will behave uncontroversially when
         * passed only one argument, which I would think is the common
         * case when passing a local dynamic revision word.
         */

        int i;

        for (i = 0; i < targets->nelts; i++)
          {
            const char *target = ((const char **)targets->elts)[i];

            if (start_is_local)
              SVN_ERR (svn_client__get_revision_number
                       (&start_revnum, ra_lib, session, start, target, pool));
            
            if (end_is_local)
              SVN_ERR (svn_client__get_revision_number
                       (&end_revnum, ra_lib, session, end, target, pool));

            err = ra_lib->get_log (session,
                                   condensed_targets,
                                   start_revnum,
                                   end_revnum,
                                   discover_changed_paths,
                                   strict_node_history,
                                   receiver,
                                   receiver_baton,
                                   pool);
            if (err)
              break;
          }
      }
    else  /* both revisions are static, so no loop needed */
      {
        err = ra_lib->get_log (session,
                               condensed_targets,
                               start_revnum,
                               end_revnum,
                               discover_changed_paths,
                               strict_node_history,
                               receiver,
                               receiver_baton,
                               pool);
      }
    
    /* Special case: If there have been no commits, we'll get an error
     * for requesting log of a revision higher than 0.  But the
     * default behavior of "svn log" is to give revisions HEAD through
     * 1, on the assumption that HEAD >= 1.
     *
     * So if we got that error for that reason, and it looks like the
     * user was just depending on the defaults (rather than explicitly
     * requesting the log for revision 1), then we don't error.  Instead
     * we just invoke the receiver manually on a hand-constructed log
     * message for revision 0.
     *
     * See also http://subversion.tigris.org/issues/show_bug.cgi?id=692.
     */
    if (err && (err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
        && (start->kind == svn_opt_revision_head)
        && ((end->kind == svn_opt_revision_number)
            && (end->value.number == 1)))
      {
        svn_revnum_t youngest_rev;
        
        SVN_ERR (ra_lib->get_latest_revnum (session, &youngest_rev, pool));
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
  }
  
  return err;
}
