/*
 * log.c:  return log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_compat.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_sorts.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Getting misc. information ***/

/* A log callback conforming to the svn_log_entry_receiver_t
   interface for obtaining the last revision of a node at a path and
   storing it in *BATON (an svn_revnum_t). */
static svn_error_t *
revnum_receiver(void *baton,
                svn_log_entry_t *log_entry,
                apr_pool_t *pool)
{
  if (SVN_IS_VALID_REVNUM(log_entry->revision))
    *((svn_revnum_t *) baton) = log_entry->revision;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__oldest_rev_at_path(svn_revnum_t *oldest_rev,
                               svn_ra_session_t *ra_session,
                               const char *rel_path,
                               svn_revnum_t rev,
                               apr_pool_t *pool)
{
  apr_array_header_t *rel_paths = apr_array_make(pool, 1, sizeof(rel_path));
  apr_array_header_t *revprops = apr_array_make(pool, 0, sizeof(char *));
  *oldest_rev = SVN_INVALID_REVNUM;
  APR_ARRAY_PUSH(rel_paths, const char *) = rel_path;

  /* Trace back in history to find the revision at which this node
     was created (copied or added). */
  return svn_ra_get_log2(ra_session, rel_paths, 1, rev, 1, FALSE, TRUE,
                         FALSE, revprops, revnum_receiver, oldest_rev, pool);
}

/* The baton for use with copyfrom_info_receiver(). */
typedef struct
{
  const char *target_path;
  const char *path;
  svn_revnum_t rev;
  apr_pool_t *pool;
} copyfrom_info_t;

/* A log callback conforming to the svn_log_message_receiver_t
   interface for obtaining the copy source of a node at a path and
   storing it in *BATON (a struct copyfrom_info_t *).
   Implements svn_log_entry_receiver_t. */
static svn_error_t *
copyfrom_info_receiver(void *baton,
                       svn_log_entry_t *log_entry,
                       apr_pool_t *pool)
{
  copyfrom_info_t *copyfrom_info = baton;
  if (copyfrom_info->path)
    /* The copy source has already been found. */
    return SVN_NO_ERROR;

  if (log_entry->changed_paths)
    {
      int i;
      const char *path;
      svn_log_changed_path_t *changed_path;
      /* Sort paths into depth-first order. */
      apr_array_header_t *sorted_changed_paths =
        svn_sort__hash(log_entry->changed_paths,
                       svn_sort_compare_items_as_paths, pool);

      for (i = (sorted_changed_paths->nelts -1) ; i >= 0 ; i--)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX(sorted_changed_paths, i,
                                                  svn_sort__item_t);
          path = item->key;
          changed_path = item->value;

          /* Consider only the path we're interested in. */
          if (changed_path->copyfrom_path &&
              SVN_IS_VALID_REVNUM(changed_path->copyfrom_rev) &&
              svn_path_is_ancestor(path, copyfrom_info->target_path))
            {
              /* Copy source found!  Determine path and note revision. */
              if (strcmp(path, copyfrom_info->target_path) == 0)
                {
                  /* We have the details for a direct copy to
                     copyfrom_info->target_path. */
                  copyfrom_info->path =
                    apr_pstrdup(copyfrom_info->pool,
                                changed_path->copyfrom_path);
                }
              else
                {
                  /* We have a parent of copyfrom_info->target_path. */
                  copyfrom_info->path =
                    apr_pstrcat(copyfrom_info->pool,
                                changed_path->copyfrom_path,
                                copyfrom_info->target_path +
                                strlen(path), NULL);
                }
              copyfrom_info->rev = changed_path->copyfrom_rev;
              break;
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_copy_source(const char *path_or_url,
                            const svn_opt_revision_t *revision,
                            const char **copyfrom_path,
                            svn_revnum_t *copyfrom_rev,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  svn_error_t *err;
  copyfrom_info_t copyfrom_info = { NULL, NULL, SVN_INVALID_REVNUM, pool };
  apr_array_header_t *targets = apr_array_make(pool, 1, sizeof(path_or_url));
  apr_pool_t *sesspool = svn_pool_create(pool);
  svn_ra_session_t *ra_session;
  svn_revnum_t at_rev;
  const char *at_url;

  SVN_ERR(svn_client__ra_session_from_path(&ra_session, &at_rev, &at_url,
                                           path_or_url, NULL,
                                           revision, revision,
                                           ctx, sesspool));
  SVN_ERR(svn_client__path_relative_to_root(&copyfrom_info.target_path,
                                            path_or_url, NULL, TRUE,
                                            ra_session, NULL, pool));
  APR_ARRAY_PUSH(targets, const char *) = "";

  /* Find the copy source.  Trace back in history to find the revision
     at which this node was created (copied or added). */
  err = svn_ra_get_log2(ra_session, targets, at_rev, 1, 0, TRUE,
                        TRUE, FALSE,
                        apr_array_make(pool, 0, sizeof(const char *)),
                        copyfrom_info_receiver, &copyfrom_info, pool);

  svn_pool_destroy(sesspool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND ||
          err->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
        {
          /* A locally-added but uncommitted versioned resource won't
             exist in the repository. */
            svn_error_clear(err);
            err = SVN_NO_ERROR;

            *copyfrom_path = NULL;
            *copyfrom_rev = SVN_INVALID_REVNUM;
        }
      return err;
    }

  *copyfrom_path = copyfrom_info.path;
  *copyfrom_rev = copyfrom_info.rev;
  return SVN_NO_ERROR;
}


/* compatibility with pre-1.5 servers, which send only author/date/log
 *revprops in log entries */
typedef struct
{
  svn_client_ctx_t *ctx;
  /* ra session for retrieving revprops from old servers */
  svn_ra_session_t *ra_session;
  /* caller's list of requested revprops, receiver, and baton */
  const apr_array_header_t *revprops;
  svn_log_entry_receiver_t receiver;
  void *baton;
} pre_15_receiver_baton_t;

static svn_error_t *
pre_15_receiver(void *baton, svn_log_entry_t *log_entry, apr_pool_t *pool)
{
  pre_15_receiver_baton_t *rb = baton;

  if (log_entry->revision == SVN_INVALID_REVNUM)
    return rb->receiver(rb->baton, log_entry, pool);

  /* If only some revprops are requested, get them one at a time on the
     second ra connection.  If all are requested, get them all with
     svn_ra_rev_proplist.  This avoids getting unrequested revprops (which
     may be arbitrarily large), but means one round-trip per requested
     revprop.  epg isn't entirely sure which should be optimized for. */
  if (rb->revprops)
    {
      int i;
      svn_boolean_t want_author, want_date, want_log;
      want_author = want_date = want_log = FALSE;
      for (i = 0; i < rb->revprops->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(rb->revprops, i, const char *);
          svn_string_t *value;

          /* If a standard revprop is requested, we know it is already in
             log_entry->revprops if available. */
          if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0)
            {
              want_author = TRUE;
              continue;
            }
          if (strcmp(name, SVN_PROP_REVISION_DATE) == 0)
            {
              want_date = TRUE;
              continue;
            }
          if (strcmp(name, SVN_PROP_REVISION_LOG) == 0)
            {
              want_log = TRUE;
              continue;
            }
          SVN_ERR(svn_ra_rev_prop(rb->ra_session, log_entry->revision,
                                  name, &value, pool));
          if (log_entry->revprops == NULL)
            log_entry->revprops = apr_hash_make(pool);
          apr_hash_set(log_entry->revprops, (const void *)name,
                       APR_HASH_KEY_STRING, (const void *)value);
        }
      if (log_entry->revprops)
        {
          /* Pre-1.5 servers send the standard revprops unconditionally;
             clear those the caller doesn't want. */
          if (!want_author)
            apr_hash_set(log_entry->revprops, SVN_PROP_REVISION_AUTHOR,
                         APR_HASH_KEY_STRING, NULL);
          if (!want_date)
            apr_hash_set(log_entry->revprops, SVN_PROP_REVISION_DATE,
                         APR_HASH_KEY_STRING, NULL);
          if (!want_log)
            apr_hash_set(log_entry->revprops, SVN_PROP_REVISION_LOG,
                         APR_HASH_KEY_STRING, NULL);
        }
    }
  else
    {
      SVN_ERR(svn_ra_rev_proplist(rb->ra_session, log_entry->revision,
                                  &log_entry->revprops, pool));
    }

  return rb->receiver(rb->baton, log_entry, pool);
}


/*** Public Interface. ***/


svn_error_t *
svn_client_log4(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_boolean_t include_merged_revisions,
                const apr_array_header_t *revprops,
                svn_log_entry_receiver_t real_receiver,
                void *real_receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *url_or_path;
  const char *actual_url;
  apr_array_header_t *condensed_targets;
  svn_revnum_t ignored_revnum;
  svn_opt_revision_t session_opt_rev;
  const char *ra_target;

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
      if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind) ||
          SVN_CLIENT__REVKIND_NEEDS_WC(start->kind) ||
          SVN_CLIENT__REVKIND_NEEDS_WC(end->kind))
          
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
            APR_ARRAY_PUSH(condensed_targets, const char *) =
                APR_ARRAY_IDX(targets, i, const char *);
        }
      else
        {
          /* If we have a single URL, then the session will be rooted at
             it, so just send an empty string for the paths we are
             interested in. */
          APR_ARRAY_PUSH(condensed_targets, const char *) = "";
        }
    }
  else
    {
      svn_wc_adm_access_t *adm_access;
      apr_array_header_t *target_urls;
      apr_array_header_t *real_targets;
      apr_pool_t *iterpool;
      int i;

      /* See FIXME about multiple wc targets, below. */
      if (targets->nelts > 1)
        return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                _("When specifying working copy paths, only "
                                  "one target may be given"));

      /* Get URLs for each target */
      target_urls = apr_array_make(pool, 1, sizeof(const char *));
      real_targets = apr_array_make(pool, 1, sizeof(const char *));
      iterpool = svn_pool_create(pool);
      for (i = 0; i < targets->nelts; i++)
        {
          const svn_wc_entry_t *entry;
          const char *URL;
          const char *target = APR_ARRAY_IDX(targets, i, const char *);

          svn_pool_clear(iterpool);
          SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target,
                                         FALSE, 0, ctx->cancel_func,
                                         ctx->cancel_baton, iterpool));
          SVN_ERR(svn_wc__entry_versioned(&entry, target, adm_access, FALSE,
                                          iterpool));

          if (! entry->url)
            return svn_error_createf
              (SVN_ERR_ENTRY_MISSING_URL, NULL,
               _("Entry '%s' has no URL"),
               svn_path_local_style(target, pool));

          URL = apr_pstrdup(pool, entry->url);
          SVN_ERR(svn_wc_adm_close(adm_access));
          APR_ARRAY_PUSH(target_urls, const char *) = URL;
          APR_ARRAY_PUSH(real_targets, const char *) = target;
        }
      svn_pool_destroy(iterpool);

      /* if we have no valid target_urls, just exit. */
      if (target_urls->nelts == 0)
        return SVN_NO_ERROR;

      /* Find the base URL and condensed targets relative to it. */
      SVN_ERR(svn_path_condense_targets(&url_or_path, &condensed_targets,
                                        target_urls, TRUE, pool));

      if (condensed_targets->nelts == 0)
        APR_ARRAY_PUSH(condensed_targets, const char *) = "";

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
    /* If this is a revision type that requires access to the working copy,
     * we use our initial target path to figure out where to root the RA
     * session, otherwise we use our URL. */
    if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind))
      SVN_ERR(svn_path_condense_targets(&ra_target, NULL, targets, TRUE, pool));
    else
      ra_target = url_or_path;

    SVN_ERR(svn_client__ra_session_from_path(&ra_session, &ignored_revnum,
                                             &actual_url, ra_target, NULL,
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
   *
   * ### FIXME: However, we can't yet handle multiple wc targets anyway.
   *
   * We used to iterate over each target in turn, getting the logs for
   * the named range.  This led to revisions being printed in strange
   * order or being printed more than once.  This is issue 1550.
   *
   * In r11599, jpieper blocked multiple wc targets in svn/log-cmd.c,
   * meaning this block not only doesn't work right in that case, but isn't
   * even testable that way (svn has no unit test suite; we can only test
   * via the svn command).  So, that check is now moved into this function
   * (see above).
   *
   * kfogel ponders future enhancements in r4186:
   * I think that's okay behavior, since the sense of the command is
   * that one wants a particular range of logs for *this* file, then
   * another range for *that* file, and so on.  But we should
   * probably put some sort of separator header between the log
   * groups.  Of course, libsvn_client can't just print stuff out --
   * it has to take a callback from the client to do that.  So we
   * need to define that callback interface, then have the command
   * line client pass one down here.
   *
   * epg wonders if the repository could send a unified stream of log
   * entries if the paths and revisions were passed down.
   */
  {
    svn_revnum_t start_revnum, end_revnum, youngest_rev = SVN_INVALID_REVNUM;
    const char *path = APR_ARRAY_IDX(targets, 0, const char *);
    svn_boolean_t has_log_revprops;

    SVN_ERR(svn_client__get_revision_number
            (&start_revnum, &youngest_rev, ra_session, start, path, pool));
    SVN_ERR(svn_client__get_revision_number
            (&end_revnum, &youngest_rev, ra_session, end, path, pool));

    SVN_ERR(svn_ra_has_capability(ra_session, &has_log_revprops,
                                  SVN_RA_CAPABILITY_LOG_REVPROPS, pool));
    if (has_log_revprops)
      return svn_ra_get_log2(ra_session,
                             condensed_targets,
                             start_revnum,
                             end_revnum,
                             limit,
                             discover_changed_paths,
                             strict_node_history,
                             include_merged_revisions,
                             revprops,
                             real_receiver,
                             real_receiver_baton,
                             pool);
    else
      {
        /* See above pre-1.5 notes. */
        pre_15_receiver_baton_t rb;
        rb.ctx = ctx;
        SVN_ERR(svn_client_open_ra_session(&rb.ra_session, actual_url,
                                           ctx, pool));
        rb.revprops = revprops;
        rb.receiver = real_receiver;
        rb.baton = real_receiver_baton;
        SVN_ERR(svn_client__ra_session_from_path(&ra_session,
                                                 &ignored_revnum,
                                                 &actual_url, ra_target, NULL,
                                                 peg_revision,
                                                 &session_opt_rev,
                                                 ctx, pool));
        return svn_ra_get_log2(ra_session,
                               condensed_targets,
                               start_revnum,
                               end_revnum,
                               limit,
                               discover_changed_paths,
                               strict_node_history,
                               include_merged_revisions,
                               svn_compat_log_revprops_in(pool),
                               pre_15_receiver,
                               &rb,
                               pool);
      }
  }
}

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
  svn_log_entry_receiver_t receiver2;
  void *receiver2_baton;

  svn_compat_wrap_log_receiver(&receiver2, &receiver2_baton,
                               receiver, receiver_baton,
                               pool);

  return svn_client_log4(targets, peg_revision, start, end, limit,
                         discover_changed_paths, strict_node_history, FALSE,
                         svn_compat_log_revprops_in(pool),
                         receiver2, receiver2_baton, ctx, pool);
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
