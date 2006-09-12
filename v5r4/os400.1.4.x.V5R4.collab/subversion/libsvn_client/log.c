/*
 * log.c:  return log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "client.h"

#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"

#include "svn_private_config.h"


/*** Getting update information ***/



/*** Public Interface. ***/


svn_error_t *
svn_client_log3(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *url_or_path;
  const char *ignored_url;
  const char *base_name = NULL;
  apr_array_header_t *condensed_targets;
  svn_revnum_t ignored_revnum;
  svn_opt_revision_t session_opt_rev;

  if ((start->kind == svn_opt_revision_unspecified)
      || (end->kind == svn_opt_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, NULL,
         _("Missing required revision specification"));
    }

  url_or_path = APR_ARRAY_IDX(targets, 0, const char *);

  /* Use the passed URL, if there is one.  */
  if (svn_path_is_url(url_or_path))
    {
      if (peg_revision->kind == svn_opt_revision_base
          || peg_revision->kind == svn_opt_revision_committed
          || peg_revision->kind == svn_opt_revision_previous)
        return svn_error_create
          (SVN_ERR_CLIENT_BAD_REVISION, NULL,
           _("Revision type requires a working copy path, not a URL"));

      /* Initialize this array, since we'll be building it below */
      condensed_targets = apr_array_make(pool, 1, sizeof(const char *));

      /* The logic here is this: If we get passed one argument, we assume
         it is the full URL to a file/dir we want log info for. If we get
         a URL plus some paths, then we assume that the URL is the base,
         and that the paths passed are relative to it.  */
      if (targets->nelts > 1)
        {
          int i;

          /* We have some paths, let's use them. Start after the URL.  */
          for (i = 1; i < targets->nelts; i++)
            (*((const char **)apr_array_push(condensed_targets))) =
                APR_ARRAY_IDX(targets, i, const char *);
        }
      else
        {
          /* If we have a single URL, then the session will be rooted at
             it, so just send an empty string for the paths we are
             interested in. */
          (*((const char **)apr_array_push(condensed_targets))) = "";
        }
    }
  else
    {
      svn_wc_adm_access_t *adm_access;
      apr_array_header_t *target_urls;
      apr_array_header_t *real_targets;
      int i;
      
      /* Get URLs for each target */
      target_urls = apr_array_make(pool, 1, sizeof(const char *));
      real_targets = apr_array_make(pool, 1, sizeof(const char *));
      for (i = 0; i < targets->nelts; i++) 
        {
          const svn_wc_entry_t *entry;
          const char *URL;
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target,
                                         FALSE, 0, ctx->cancel_func,
                                         ctx->cancel_baton, pool));
          SVN_ERR(svn_wc_entry(&entry, target, adm_access, FALSE, pool));
          if (! entry)
            return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                     _("'%s' is not under version control"),
                                     svn_path_local_style(target, pool));
          
          if (! entry->url)
            return svn_error_createf
              (SVN_ERR_ENTRY_MISSING_URL, NULL,
               _("Entry '%s' has no URL"),
               svn_path_local_style(target, pool));

          URL = apr_pstrdup(pool, entry->url);
          SVN_ERR(svn_wc_adm_close(adm_access));
          (*((const char **)apr_array_push(target_urls))) = URL;
          (*((const char **)apr_array_push(real_targets))) = target;
        }

      /* if we have no valid target_urls, just exit. */
      if (target_urls->nelts == 0)
        return SVN_NO_ERROR;

      /* Find the base URL and condensed targets relative to it. */
      SVN_ERR(svn_path_condense_targets(&url_or_path, &condensed_targets,
                                        target_urls, TRUE, pool));

      if (condensed_targets->nelts == 0)
        (*((const char **)apr_array_push(condensed_targets))) = "";

      /* 'targets' now becomes 'real_targets', which has bogus,
         unversioned things removed from it. */
      targets = real_targets;
    }

  /* Determine the revision to open the RA session to. */
  if (start->kind == svn_opt_revision_number &&
      end->kind == svn_opt_revision_number)
    session_opt_rev = (start->value.number > end->value.number ?
                       *start : *end);
  else if (start->kind == svn_opt_revision_date &&
           end->kind == svn_opt_revision_date)
    session_opt_rev = (start->value.date > end->value.date ? *start : *end);
  else
    session_opt_rev.kind = svn_opt_revision_unspecified;

  {
    const char *target;

    /* If this is a revision type that requires access to the working copy,
     * we use our initial target path to figure out where to root the RA
     * session, otherwise we use our URL. */
    if (peg_revision->kind == svn_opt_revision_base
        || peg_revision->kind == svn_opt_revision_committed
        || peg_revision->kind == svn_opt_revision_previous)
      SVN_ERR(svn_path_condense_targets(&target, NULL, targets, TRUE, pool));
    else
      target = url_or_path;

    SVN_ERR(svn_client__ra_session_from_path(&ra_session, &ignored_revnum,
                                             &ignored_url, target,
                                             peg_revision, &session_opt_rev,
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
    svn_error_t *err = SVN_NO_ERROR;  /* Because we might have no targets. */
    svn_revnum_t start_revnum, end_revnum;

    svn_boolean_t start_is_local = svn_client__revision_is_local(start);
    svn_boolean_t end_is_local = svn_client__revision_is_local(end);

    if (! start_is_local)
      SVN_ERR(svn_client__get_revision_number
              (&start_revnum, ra_session, start, base_name, pool));

    if (! end_is_local)
      SVN_ERR(svn_client__get_revision_number
              (&end_revnum, ra_session, end, base_name, pool));

    if (start_is_local || end_is_local)
      {
        /* ### FIXME: At least one revision is locally dynamic, that
         * is, we're in a case similar to one of these:
         *
         *   $ svn log -rCOMMITTED foo.txt bar.c
         *   $ svn log -rCOMMITTED:42 foo.txt bar.c
         *
         * We'll iterate over each target in turn, getting the logs
         * for the named range.  This means that certain revisions may
         * be printed out more than once.  I think that's okay
         * behavior, since the sense of the command is that one wants
         * a particular range of logs for *this* file, then another
         * range for *that* file, and so on.  But we should
         * probably put some sort of separator header between the log
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
              SVN_ERR(svn_client__get_revision_number
                      (&start_revnum, ra_session, start, target, pool));
            
            if (end_is_local)
              SVN_ERR(svn_client__get_revision_number
                      (&end_revnum, ra_session, end, target, pool));

            err = svn_ra_get_log(ra_session,
                                 condensed_targets,
                                 start_revnum,
                                 end_revnum,
                                 limit,
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
        err = svn_ra_get_log(ra_session,
                             condensed_targets,
                             start_revnum,
                             end_revnum,
                             limit,
                             discover_changed_paths,
                             strict_node_history,
                             receiver,
                             receiver_baton,
                             pool);
      }
  
    return err;
  }
}

svn_error_t *
svn_client_log2(const apr_array_header_t *targets,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;
  return svn_client_log3(targets, &peg_revision, start, end, limit,
                         discover_changed_paths, strict_node_history,
                         receiver, receiver_baton, ctx, pool);
}

svn_error_t *
svn_client_log(const apr_array_header_t *targets,
               const svn_opt_revision_t *start,
               const svn_opt_revision_t *end,
               svn_boolean_t discover_changed_paths,
               svn_boolean_t strict_node_history,
               svn_log_message_receiver_t receiver,
               void *receiver_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  err = svn_client_log2(targets, start, end, 0, discover_changed_paths,
                        strict_node_history, receiver, receiver_baton, ctx,
                        pool);
    
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

      /* We don't need to check if HEAD is 0, because that must be the case,
       * by logical deduction: The revision range specified is HEAD:1.
       * HEAD cannot not exist, so the revision to which "no such revision"
       * applies is 1. If revision 1 does not exist, then HEAD is 0.
       * Hence, we deduce the repository is empty without needing access
       * to further information. */

      svn_error_clear(err);
      err = SVN_NO_ERROR;
          
      /* Log receivers are free to handle revision 0 specially... But
         just in case some don't, we make up a message here. */
      SVN_ERR(receiver(receiver_baton,
                       NULL, 0, "", "", _("No commits in repository"),
                       pool));
    }

  return err;
}
