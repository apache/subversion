/*
 * ====================================================================
 * Copyright (c) 2005-2008 CollabNet.  All rights reserved.
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

#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_auth.h"
#include "svn_opt.h"
#include "svn_ra.h"
#include "svn_subst.h"
#include "svn_string.h"

#include "private/svn_opt_private.h"

#include "svn_private_config.h"

#include <apr_network_io.h>
#include <apr_signal.h>
#include <apr_uuid.h>

static svn_opt_subcommand_t initialize_cmd,
                            synchronize_cmd,
                            copy_revprops_cmd,
                            info_cmd,
                            help_cmd;

enum {
  svnsync_opt_non_interactive = SVN_OPT_FIRST_LONGOPT_ID,
  svnsync_opt_no_auth_cache,
  svnsync_opt_auth_username,
  svnsync_opt_auth_password,
  svnsync_opt_source_username,
  svnsync_opt_source_password,
  svnsync_opt_sync_username,
  svnsync_opt_sync_password,
  svnsync_opt_config_dir,
  svnsync_opt_version,
  svnsync_opt_trust_server_cert
};

#define SVNSYNC_OPTS_DEFAULT svnsync_opt_non_interactive, \
                             svnsync_opt_no_auth_cache, \
                             svnsync_opt_auth_username, \
                             svnsync_opt_auth_password, \
                             svnsync_opt_trust_server_cert, \
                             svnsync_opt_source_username, \
                             svnsync_opt_source_password, \
                             svnsync_opt_sync_username, \
                             svnsync_opt_sync_password, \
                             svnsync_opt_config_dir

static const svn_opt_subcommand_desc2_t svnsync_cmd_table[] =
  {
    { "initialize", initialize_cmd, { "init" },
      N_("usage: svnsync initialize DEST_URL SOURCE_URL\n"
         "\n"
         "Initialize a destination repository for synchronization from\n"
         "another repository.\n"
         "\n"
         "The destination URL must point to the root of a repository with\n"
         "no committed revisions.  The destination repository must allow\n"
         "revision property changes.\n"
         "\n"
         "If the source URL is not the root of a repository, only the\n"
         "specified part of the repository will be synchronized.\n"
         "\n"
         "You should not commit to, or make revision property changes in,\n"
         "the destination repository by any method other than 'svnsync'.\n"
         "In other words, the destination repository should be a read-only\n"
         "mirror of the source repository.\n"),
      { SVNSYNC_OPTS_DEFAULT, 'q' } },
    { "synchronize", synchronize_cmd, { "sync" },
      N_("usage: svnsync synchronize DEST_URL\n"
         "\n"
         "Transfer all pending revisions to the destination from the source\n"
         "with which it was initialized.\n"),
      { SVNSYNC_OPTS_DEFAULT, 'q' } },
    { "copy-revprops", copy_revprops_cmd, { 0 },
      N_("usage: svnsync copy-revprops DEST_URL [REV[:REV2]]\n"
         "\n"
         "Copy the revision properties in a given range of revisions to the\n"
         "destination from the source with which it was initialized.\n"
         "\n"
         "If REV and REV2 are provided, copy properties for the revisions\n"
         "specified by that range, inclusively.  If only REV is provided,\n"
         "copy properties for that revision alone.  If REV is not provided,\n"
         "copy properties for all revisions previously transferred to the\n"
         "destination.\n"
         "\n"
         "REV and REV2 must be revisions which were previously transferred\n"
         "to the destination.  You may use \"HEAD\" for either revision to\n"
         "mean \"the last revision transferred\".\n"),
      { SVNSYNC_OPTS_DEFAULT, 'q' } },
    { "info", info_cmd, { 0 },
      N_("usage: svnsync info DEST_URL\n"
         "\n"
         "Print information about the synchronization destination repository\n"
         "located at DEST_URL.\n"),
      { SVNSYNC_OPTS_DEFAULT } },
    { "help", help_cmd, { "?", "h" },
      N_("usage: svnsync help [SUBCOMMAND...]\n"
         "\n"
         "Describe the usage of this program or its subcommands.\n"),
      { 0 } },
    { NULL, NULL, { 0 }, NULL, { 0 } }
  };

static const apr_getopt_option_t svnsync_options[] =
  {
    {"quiet",          'q', 0,
                       N_("print as little as possible") },
    {"non-interactive", svnsync_opt_non_interactive, 0,
                       N_("do no interactive prompting") },
    {"no-auth-cache",  svnsync_opt_no_auth_cache, 0,
                       N_("do not cache authentication tokens") },
    {"username",       svnsync_opt_auth_username, 1,
                       N_("specify a username ARG (deprecated;\n"
                          "                             "
                          "see --source-username and --sync-username)") },
    {"password",       svnsync_opt_auth_password, 1,
                       N_("specify a password ARG (deprecated;\n"
                          "                             "
                          "see --source-password and --sync-password)") },
    {"trust-server-cert", svnsync_opt_trust_server_cert, 0,
                       N_("accept unknown SSL server certificates without\n"
                          "                             "
                          "prompting (but only with '--non-interactive')") },
    {"source-username", svnsync_opt_source_username, 1,
                       N_("connect to source repository with username ARG") },
    {"source-password", svnsync_opt_source_password, 1,
                       N_("connect to source repository with password ARG") },
    {"sync-username",  svnsync_opt_sync_username, 1,
                       N_("connect to sync repository with username ARG") },
    {"sync-password",  svnsync_opt_sync_password, 1,
                       N_("connect to sync repository with password ARG") },
    {"config-dir",     svnsync_opt_config_dir, 1,
                       N_("read user configuration files from directory ARG")},
    {"version",        svnsync_opt_version, 0,
                       N_("show program version information")},
    {"help",           'h', 0,
                       N_("show help on a subcommand")},
    {NULL,             '?', 0,
                       N_("show help on a subcommand")},
    { 0, 0, 0, 0 }
  };

typedef struct {
  svn_boolean_t non_interactive;
  svn_boolean_t trust_server_cert;
  svn_boolean_t no_auth_cache;
  svn_auth_baton_t *source_auth_baton;
  svn_auth_baton_t *sync_auth_baton;
  const char *source_username;
  const char *source_password;
  const char *sync_username;
  const char *sync_password;
  const char *config_dir;
  apr_hash_t *config;
  svn_boolean_t quiet;
  svn_boolean_t version;
  svn_boolean_t help;
} opt_baton_t;




/*** Helper functions ***/


/* Global record of whether the user has requested cancellation. */
static volatile sig_atomic_t cancelled = FALSE;


/* Callback function for apr_signal(). */
static void
signal_handler(int signum)
{
  apr_signal(signum, SIG_IGN);
  cancelled = TRUE;
}


/* Cancellation callback function. */
static svn_error_t *
check_cancel(void *baton)
{
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}


/* Check that the version of libraries in use match what we expect. */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_ra",    svn_ra_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list(&my_version, checklist);
}


/* Acquire a lock (of sorts) on the repository associated with the
 * given RA SESSION.
 */
static svn_error_t *
get_lock(svn_ra_session_t *session, apr_pool_t *pool)
{
  char hostname_str[APRMAXHOSTLEN + 1] = { 0 };
  svn_string_t *mylocktoken, *reposlocktoken;
  apr_status_t apr_err;
  apr_pool_t *subpool;
  int i;

  apr_err = apr_gethostname(hostname_str, sizeof(hostname_str), pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get local hostname"));

  mylocktoken = svn_string_createf(pool, "%s:%s", hostname_str,
                                   svn_uuid_generate(pool));

  subpool = svn_pool_create(pool);

#define SVNSYNC_LOCK_RETRIES 10
  for (i = 0; i < SVNSYNC_LOCK_RETRIES; ++i)
    {
      svn_pool_clear(subpool);
      SVN_ERR(check_cancel(NULL));

      SVN_ERR(svn_ra_rev_prop(session, 0, SVNSYNC_PROP_LOCK, &reposlocktoken,
                              subpool));

      if (reposlocktoken)
        {
          /* Did we get it?   If so, we're done, otherwise we sleep. */
          if (strcmp(reposlocktoken->data, mylocktoken->data) == 0)
            return SVN_NO_ERROR;
          else
            {
              SVN_ERR(svn_cmdline_printf
                      (pool, _("Failed to get lock on destination "
                               "repos, currently held by '%s'\n"),
                       reposlocktoken->data));

              apr_sleep(apr_time_from_sec(1));
            }
        }
      else if (i < SVNSYNC_LOCK_RETRIES - 1)
        {
          /* Except in the very last iteration, try to set the lock. */
          SVN_ERR(svn_ra_change_rev_prop(session, 0, SVNSYNC_PROP_LOCK,
                                         mylocktoken, subpool));
        }
    }

  return svn_error_createf(APR_EINVAL, NULL,
                           "Couldn't get lock on destination repos "
                           "after %d attempts\n", i);
}


/* Baton for the various subcommands to share. */
typedef struct {
  /* common to all subcommands */
  apr_hash_t *config;
  svn_ra_callbacks2_t source_callbacks;
  svn_ra_callbacks2_t sync_callbacks;
  svn_boolean_t quiet;
  const char *to_url;

  /* initialize only */
  const char *from_url;

  /* synchronize only */
  svn_revnum_t committed_rev;

  /* copy-revprops only */
  svn_revnum_t start_rev;
  svn_revnum_t end_rev;

} subcommand_baton_t;

typedef svn_error_t *(*with_locked_func_t)(svn_ra_session_t *session,
                                           subcommand_baton_t *baton,
                                           apr_pool_t *pool);


/* Lock the repository associated with RA SESSION, then execute the
 * given FUNC/BATON pair while holding the lock.  Finally, drop the
 * lock once it finishes.
 */
static svn_error_t *
with_locked(svn_ra_session_t *session,
            with_locked_func_t func,
            subcommand_baton_t *baton,
            apr_pool_t *pool)
{
  svn_error_t *err, *err2;

  SVN_ERR(get_lock(session, pool));

  err = func(session, baton, pool);

  err2 = svn_ra_change_rev_prop(session, 0, SVNSYNC_PROP_LOCK, NULL, pool);
  if (err2 && err)
    {
      svn_error_clear(err2); /* XXX what to do here? */

      return err;
    }
  else if (err2)
    {
      return err2;
    }
  else
    {
      return err;
    }
}


/* Callback function for the RA session's open_tmp_file()
 * requirements.
 */
static svn_error_t *
open_tmp_file(apr_file_t **fp, void *callback_baton, apr_pool_t *pool)
{
  return svn_io_open_unique_file3(fp, NULL, NULL,
                                  svn_io_file_del_on_pool_cleanup,
                                  pool, pool);
}


/* Return SVN_NO_ERROR iff URL identifies the root directory of the
 * repository associated with RA session SESS.
 */
static svn_error_t *
check_if_session_is_at_repos_root(svn_ra_session_t *sess,
                                  const char *url,
                                  apr_pool_t *pool)
{
  const char *sess_root;

  SVN_ERR(svn_ra_get_repos_root2(sess, &sess_root, pool));

  if (strcmp(url, sess_root) == 0)
    return SVN_NO_ERROR;
  else
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Session is rooted at '%s' but the repos root is '%s'"),
       url, sess_root);
}


/* Remove the properties in TARGET_PROPS but not in SOURCE_PROPS from
 * revision REV of the repository associated with RA session SESSION.
 *
 * All allocations will be done in a subpool of POOL.
 */
static svn_error_t *
remove_props_not_in_source(svn_ra_session_t *session,
                           svn_revnum_t rev,
                           apr_hash_t *source_props,
                           apr_hash_t *target_props,
                           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, target_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;

      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, NULL);

      /* Delete property if the key can't be found in SOURCE_PROPS. */
      if (! apr_hash_get(source_props, key, APR_HASH_KEY_STRING))
        SVN_ERR(svn_ra_change_rev_prop(session, rev, key, NULL,
                                       subpool));
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Filter callback function.
 * Takes a property name KEY, and is expected to return TRUE if the property
 * should be filtered out (ie. not be copied to the target list), or FALSE if
 * not.
 */
typedef svn_boolean_t (*filter_func_t)(const char *key);

/* Make a new set of properties, by copying those properties in PROPS for which
 * the filter FILTER returns FALSE.
 *
 * The number of filtered properties will be stored in FILTERED_COUNT.
 *
 * The returned set of properties is allocated from POOL.
 */
static apr_hash_t *
filter_props(int *filtered_count, apr_hash_t *props,
             filter_func_t filter,
             apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *filtered = apr_hash_make(pool);
  *filtered_count = 0;

  for (hi = apr_hash_first(pool, props); hi ; hi = apr_hash_next(hi))
    {
      void *val;
      const void *key;
      apr_ssize_t len;

      apr_hash_this(hi, &key, &len, &val);

      /* Copy all properties:
          - not matching the exclude pattern if provided OR
          - matching the include pattern if provided */
      if (!filter || filter(key) == FALSE)
        {
          apr_hash_set(filtered, key, APR_HASH_KEY_STRING, val);
        }
      else
        {
          *filtered_count += 1;
        }
    }

  return filtered;
}


/* Write the set of revision properties REV_PROPS to revision REV to the
 * repository associated with RA session SESSION.
 *
 * All allocations will be done in a subpool of POOL.
 */
static svn_error_t *
write_revprops(int *filtered_count,
               svn_ra_session_t *session,
               svn_revnum_t rev,
               apr_hash_t *rev_props,
               apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  *filtered_count = 0;

  for (hi = apr_hash_first(pool, rev_props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);

      if (strncmp(key, SVNSYNC_PROP_PREFIX,
                  sizeof(SVNSYNC_PROP_PREFIX) - 1) != 0)
        {
          SVN_ERR(svn_ra_change_rev_prop(session, rev, key, val, subpool));
        }
      else
        {
          *filtered_count += 1;
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Normalize the line ending style of *STR, so that it contains only
 * LF (\n) line endings. After return, *STR may point at a new
 * svn_string_t* allocated from POOL.
 *
 * *WAS_NORMALIZED is set to TRUE when *STR needed to be normalized,
 * and to FALSE if *STR remains unchanged.
 */
static svn_error_t*
normalize_string(const svn_string_t **str,
                 svn_boolean_t *was_normalized,
                 apr_pool_t *pool)
{
  *was_normalized = FALSE;

  if (*str == NULL)
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT((*str)->data != NULL);

  /* Detect inconsistent line ending style simply by looking
     for carriage return (\r) characters. */
  if (strchr((*str)->data, '\r') != NULL)
    {
      /* Found some. Normalize. */
      const char* cstring = NULL;
      SVN_ERR(svn_subst_translate_cstring2((*str)->data, &cstring,
                                           "\n", TRUE,
                                           NULL, FALSE,
                                           pool));
      *str = svn_string_create(cstring, pool);
      *was_normalized = TRUE;
    }

  return SVN_NO_ERROR;
}


/* Normalize the line ending style of the values of properties in REV_PROPS
 * that "need translation" (according to svn_prop_needs_translation(),
 * currently all svn:* props) so that they contain only LF (\n) line endings.
 * The number of properties that needed normalization is returned in
 * *NORMALIZED_COUNT.
 */
static svn_error_t *
normalize_revprops(apr_hash_t *rev_props,
                   int *normalized_count,
                   apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  *normalized_count = 0;

  for (hi = apr_hash_first(pool, rev_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *key;
      const svn_string_t *val;

      apr_hash_this(hi, (const void**)&key, NULL, (void**)&val);

      if (svn_prop_needs_translation(key))
        {
          svn_boolean_t was_normalized;
          SVN_ERR(normalize_string(&val,
                                   &was_normalized,
                                   pool));
          if (was_normalized)
            {
              /* Replace the existing prop value. */
              apr_hash_set(rev_props, key, APR_HASH_KEY_STRING, val);
              /* And count this. */
              (*normalized_count)++;
            }
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
log_properties_copied(svn_boolean_t syncprops_found,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  if (syncprops_found)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("Copied properties for revision %ld "
                                 "(%s* properties skipped).\n"),
                               rev, SVNSYNC_PROP_PREFIX));
  else
    SVN_ERR(svn_cmdline_printf(pool,
                               _("Copied properties for revision %ld.\n"),
                               rev));

  return SVN_NO_ERROR;
}

static svn_error_t *
log_properties_normalized(int normalized_rev_props_count,
                          int normalized_node_props_count,
                          apr_pool_t *pool)
{
  if (normalized_rev_props_count > 0 || normalized_node_props_count > 0)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("NOTE: Normalized %s* properties "
                                 "to LF line endings (%d rev-props, "
                                 "%d node-props).\n"),
                               SVN_PROP_PREFIX,
                               normalized_rev_props_count,
                               normalized_node_props_count));
  return SVN_NO_ERROR;
}


/* Copy all the revision properties, except for those that have the
 * "svn:sync-" prefix, from revision REV of the repository associated
 * with RA session FROM_SESSION, to the repository associated with RA
 * session TO_SESSION.
 *
 * If SYNC is TRUE, then properties on the destination revision that
 * do not exist on the source revision will be removed.
 *
 * Make sure the values of svn:* revision properties use only LF (\n)
 * lineending style, correcting their values as necessary. The number
 * of properties that were normalized is returned in *NORMALIZED_COUNT.
 */
static svn_error_t *
copy_revprops(svn_ra_session_t *from_session,
              svn_ra_session_t *to_session,
              svn_revnum_t rev,
              svn_boolean_t sync,
              svn_boolean_t quiet,
              int *normalized_count,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *existing_props, *rev_props;
  int filtered_count = 0;

  /* Get the list of revision properties on REV of TARGET. We're only interested
     in the property names, but we'll get the values 'for free'. */
  if (sync)
    SVN_ERR(svn_ra_rev_proplist(to_session, rev, &existing_props, subpool));

  /* Get the list of revision properties on REV of SOURCE. */
  SVN_ERR(svn_ra_rev_proplist(from_session, rev, &rev_props, subpool));

  /* If necessary, normalize line ending style, and return the count
     of changes in int *NORMALIZED_COUNT. */
  SVN_ERR(normalize_revprops(rev_props, normalized_count, pool));

  /* Copy all but the svn:svnsync properties. */
  SVN_ERR(write_revprops(&filtered_count, to_session, rev, rev_props, pool));

  /* Delete those properties that were in TARGET but not in SOURCE */
  if (sync)
    SVN_ERR(remove_props_not_in_source(to_session, rev,
                                       rev_props, existing_props, pool));

  if (! quiet)
    SVN_ERR(log_properties_copied(filtered_count > 0, rev, pool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Return a subcommand baton allocated from POOL and populated with
   data from the provided parameters, which include the global
   OPT_BATON options structure and a handful of other options.  Not
   all parameters are used in all subcommands -- see
   subcommand_baton_t's definition for details. */
static subcommand_baton_t *
make_subcommand_baton(opt_baton_t *opt_baton,
                      const char *to_url,
                      const char *from_url,
                      svn_revnum_t start_rev,
                      svn_revnum_t end_rev,
                      apr_pool_t *pool)
{
  subcommand_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  b->config = opt_baton->config;
  b->source_callbacks.open_tmp_file = open_tmp_file;
  b->source_callbacks.auth_baton = opt_baton->source_auth_baton;
  b->sync_callbacks.open_tmp_file = open_tmp_file;
  b->sync_callbacks.auth_baton = opt_baton->sync_auth_baton;
  b->quiet = opt_baton->quiet;
  b->to_url = to_url;
  b->from_url = from_url;
  b->start_rev = start_rev;
  b->end_rev = end_rev;
  return b;
}


/*** `svnsync init' ***/

/* Initialize the repository associated with RA session TO_SESSION,
 * using information found in baton B, while the repository is
 * locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_initialize(svn_ra_session_t *to_session,
              subcommand_baton_t *baton,
              apr_pool_t *pool)
{
  svn_ra_session_t *from_session;
  svn_string_t *from_url;
  svn_revnum_t latest;
  const char *uuid, *root_url;
  int normalized_rev_props_count;

  /* First, sanity check to see that we're copying into a brand new repos. */

  SVN_ERR(svn_ra_get_latest_revnum(to_session, &latest, pool));

  if (latest != 0)
    return svn_error_create
      (APR_EINVAL, NULL,
       _("Cannot initialize a repository with content in it"));

  /* And check to see if anyone's run initialize on it before...  We
     may want a --force option to override this check. */

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));

  if (from_url)
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Destination repository is already synchronizing from '%s'"),
       from_url->data);

  /* Now fill in our bookkeeping info in the dest repository. */

  SVN_ERR(svn_ra_open3(&from_session, baton->from_url, NULL,
                       &(baton->source_callbacks), baton,
                       baton->config, pool));
  SVN_ERR(svn_ra_get_repos_root2(from_session, &root_url, pool));

  /* If we're doing a partial replay, we have to check first if the server
     supports this. */
  if (svn_path_is_child(root_url, baton->from_url, pool))
    {
      svn_boolean_t server_supports_partial_replay;
      svn_error_t *err = svn_ra_has_capability(from_session,
                                               &server_supports_partial_replay,
                                               SVN_RA_CAPABILITY_PARTIAL_REPLAY,
                                               pool);
      if (err && err->apr_err == SVN_ERR_UNKNOWN_CAPABILITY)
        {
          svn_error_clear(err);
          server_supports_partial_replay = FALSE;
        }

      if (!server_supports_partial_replay)
        return svn_error_create(SVN_ERR_RA_PARTIAL_REPLAY_NOT_SUPPORTED, NULL,
                                NULL);
    }

  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                                 svn_string_create(baton->from_url, pool),
                                 pool));

  SVN_ERR(svn_ra_get_uuid2(from_session, &uuid, pool));

  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                                 svn_string_create(uuid, pool), pool));

  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                                 svn_string_create("0", pool), pool));

  /* Finally, copy all non-svnsync revprops from rev 0 of the source
     repos into the dest repos. */

  SVN_ERR(copy_revprops(from_session, to_session, 0, FALSE,
                        baton->quiet, &normalized_rev_props_count, pool));

  /* Notify about normalized props, if any. */
  SVN_ERR(log_properties_normalized(normalized_rev_props_count, 0, pool));

  /* TODO: It would be nice if we could set the dest repos UUID to be
     equal to the UUID of the source repos, at least optionally.  That
     way people could check out/log/diff using a local fast mirror,
     but switch --relocate to the actual final repository in order to
     make changes...  But at this time, the RA layer doesn't have a
     way to set a UUID. */

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: init */
static svn_error_t *
initialize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  const char *to_url, *from_url;
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  to_url = APR_ARRAY_IDX(targets, 0, const char *);
  from_url = APR_ARRAY_IDX(targets, 1, const char *);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);
  if (! svn_path_is_url(from_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), from_url);

  baton = make_subcommand_baton(opt_baton, to_url, from_url, 0, 0, pool);
  SVN_ERR(svn_ra_open3(&to_session, baton->to_url, NULL,
                       &(baton->sync_callbacks), baton, baton->config, pool));
  SVN_ERR(check_if_session_is_at_repos_root(to_session, baton->to_url, pool));
  SVN_ERR(with_locked(to_session, do_initialize, baton, pool));

  return SVN_NO_ERROR;
}



/*** Synchronization Editor ***/

/* This editor has a couple of jobs.
 *
 * First, it needs to filter out the propchanges that can't be passed over
 * libsvn_ra.
 *
 * Second, it needs to adjust for the fact that we might not actually have
 * permission to see all of the data from the remote repository, which means
 * we could get revisions that are totally empty from our point of view.
 *
 * Third, it needs to adjust copyfrom paths, adding the root url for the
 * destination repository to the beginning of them.
 */


/* Edit baton */
typedef struct {
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;
  const char *to_url;  /* URL we're copying into, for correct copyfrom URLs */
  svn_boolean_t called_open_root;
  svn_boolean_t got_textdeltas;
  svn_revnum_t base_revision;
  svn_boolean_t quiet;
  svn_boolean_t strip_mergeinfo;    /* Are we stripping svn:mergeinfo? */
  svn_boolean_t migrate_svnmerge;   /* Are we converting svnmerge.py data? */
  svn_boolean_t mergeinfo_stripped; /* Did we strip svn:mergeinfo? */
  svn_boolean_t svnmerge_migrated;  /* Did we convert svnmerge.py data? */
  svn_boolean_t svnmerge_blocked;   /* Was there any blocked svnmerge data? */
  int *normalized_node_props_counter;  /* Where to count normalizations? */
} edit_baton_t;


/* A dual-purpose baton for files and directories. */
typedef struct {
  void *edit_baton;
  void *wrapped_node_baton;
} node_baton_t;


/*** Editor vtable functions ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
  return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                 target_revision, pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  edit_baton_t *eb = edit_baton;
  node_baton_t *dir_baton = apr_palloc(pool, sizeof(*dir_baton));

  SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                        base_revision, pool,
                                        &dir_baton->wrapped_node_baton));

  eb->called_open_root = TRUE;
  dir_baton->edit_baton = edit_baton;
  *root_baton = dir_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;

  return eb->wrapped_editor->delete_entry(path, base_revision,
                                          pb->wrapped_node_baton, pool);
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *b = apr_palloc(pool, sizeof(*b));

  /* if copyfrom_path starts with '/' join rest of copyfrom_path leaving
   * leading '/' with canonicalized url eb->to_url.
   */
  if (copyfrom_path && copyfrom_path[0] == '/')
    copyfrom_path = svn_path_url_add_component2(eb->to_url,
                                                copyfrom_path + 1, pool);

  SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_node_baton,
                                            copyfrom_path,
                                            copyfrom_rev, pool,
                                            &b->wrapped_node_baton));

  b->edit_baton = eb;
  *child_baton = b;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *db = apr_palloc(pool, sizeof(*db));

  SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_node_baton,
                                             base_revision, pool,
                                             &db->wrapped_node_baton));

  db->edit_baton = eb;
  *child_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  if (copyfrom_path)
    copyfrom_path = apr_psprintf(pool, "%s%s", eb->to_url,
                                 svn_path_uri_encode(copyfrom_path, pool));

  SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_node_baton,
                                       copyfrom_path, copyfrom_rev,
                                       pool, &fb->wrapped_node_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(eb->wrapped_editor->open_file(path, pb->wrapped_node_baton,
                                        base_revision, pool,
                                        &fb->wrapped_node_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  if (! eb->quiet)
    {
      if (! eb->got_textdeltas)
        SVN_ERR(svn_cmdline_printf(pool, _("Transmitting file data ")));
      SVN_ERR(svn_cmdline_printf(pool, "."));
      SVN_ERR(svn_cmdline_fflush(stdout));
    }

  eb->got_textdeltas = TRUE;
  return eb->wrapped_editor->apply_textdelta(fb->wrapped_node_baton,
                                             base_checksum, pool,
                                             handler, handler_baton);
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
  return eb->wrapped_editor->close_file(fb->wrapped_node_baton,
                                        text_checksum, pool);
}

static svn_error_t *
absent_file(const char *path,
            void *file_baton,
            apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
  return eb->wrapped_editor->absent_file(path, fb->wrapped_node_baton, pool);
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  return eb->wrapped_editor->close_directory(db->wrapped_node_baton, pool);
}

static svn_error_t *
absent_directory(const char *path,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  return eb->wrapped_editor->absent_directory(path, db->wrapped_node_baton,
                                              pool);
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

  /* only regular properties can pass over libsvn_ra */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  /* Maybe drop svn:mergeinfo.  */
  if (eb->strip_mergeinfo && (strcmp(name, SVN_PROP_MERGEINFO) == 0))
    {
      eb->mergeinfo_stripped = TRUE;
      return SVN_NO_ERROR;
    }

  /* Maybe drop (errantly set, as this is a file) svnmerge.py properties. */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-integrated") == 0))
    {
      eb->svnmerge_migrated = TRUE;
      return SVN_NO_ERROR;
    }

  /* Remember if we see any svnmerge-blocked properties.  (They really
     shouldn't be here, as this is a file, but whatever...)  */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-blocked") == 0))
    {
      eb->svnmerge_blocked = TRUE;
    }

  /* Normalize svn:* properties as necessary. */
  if (svn_prop_needs_translation(name))
    {
      svn_boolean_t was_normalized;
      SVN_ERR(normalize_string(&value, &was_normalized, pool));
      if (was_normalized)
        (*(eb->normalized_node_props_counter))++;
    }

  return eb->wrapped_editor->change_file_prop(fb->wrapped_node_baton,
                                              name, value, pool);
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  svn_string_t *real_value = (svn_string_t *)value;

  /* Only regular properties can pass over libsvn_ra */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  /* Maybe drop svn:mergeinfo.  */
  if (eb->strip_mergeinfo && (strcmp(name, SVN_PROP_MERGEINFO) == 0))
    {
      eb->mergeinfo_stripped = TRUE;
      return SVN_NO_ERROR;
    }

  /* Maybe convert svnmerge-integrated data into svn:mergeinfo.  (We
     ignore svnmerge-blocked for now.) */
  /* ### FIXME: Consult the mirror repository's HEAD prop values and
     ### merge svn:mergeinfo, svnmerge-integrated, and svnmerge-blocked. */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-integrated") == 0))
    {
      if (value)
        {
          /* svnmerge-integrated differs from svn:mergeinfo in a pair
             of ways.  First, it can use tabs, newlines, or spaces to
             delimit source information.  Secondly, the source paths
             are relative URLs, whereas svn:mergeinfo uses relative
             paths (not URI-encoded). */
          svn_error_t *err;
          svn_stringbuf_t *mergeinfo_buf = svn_stringbuf_create("", pool);
          svn_mergeinfo_t mergeinfo;
          int i;
          apr_array_header_t *sources =
            svn_cstring_split(value->data, " \t\n", TRUE, pool);

          for (i = 0; i < sources->nelts; i++)
            {
              const char *rel_path;
              apr_array_header_t *path_revs =
                svn_cstring_split(APR_ARRAY_IDX(sources, i, const char *),
                                  ":", TRUE, pool);

              /* ### TODO: Warn? */
              if (path_revs->nelts != 2)
                continue;

              /* Append this source's mergeinfo data. */
              rel_path = APR_ARRAY_IDX(path_revs, 0, const char *);
              rel_path = svn_path_uri_decode(rel_path, pool);
              svn_stringbuf_appendcstr(mergeinfo_buf, rel_path);
              svn_stringbuf_appendcstr(mergeinfo_buf, ":");
              svn_stringbuf_appendcstr(mergeinfo_buf,
                                       APR_ARRAY_IDX(path_revs, 1,
                                                     const char *));
              svn_stringbuf_appendcstr(mergeinfo_buf, "\n");
            }

          /* Try to parse the mergeinfo string we've created, just to
             check for bogosity.  If all goes well, we'll unparse it
             again and use that as our property value.  */
          err = svn_mergeinfo_parse(&mergeinfo, mergeinfo_buf->data, pool);
          if (err)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          SVN_ERR(svn_mergeinfo_to_string(&real_value, mergeinfo, pool));
        }
      name = SVN_PROP_MERGEINFO;
      eb->svnmerge_migrated = TRUE;
    }

  /* Remember if we see any svnmerge-blocked properties. */
  if (eb->migrate_svnmerge && (strcmp(name, "svnmerge-blocked") == 0))
    {
      eb->svnmerge_blocked = TRUE;
    }

  /* Normalize svn:* properties as necessary. */
  if (svn_prop_needs_translation(name))
    {
      svn_boolean_t was_normalized;
      SVN_ERR(normalize_string((const svn_string_t**)&real_value,
                               &was_normalized, pool));
      if (was_normalized)
        (*(eb->normalized_node_props_counter))++;
    }

  return eb->wrapped_editor->change_dir_prop(db->wrapped_node_baton,
                                             name, real_value, pool);
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;

  /* If we haven't opened the root yet, that means we're transfering
     an empty revision, probably because we aren't allowed to see the
     contents for some reason.  In any event, we need to open the root
     and close it again, before we can close out the edit, or the
     commit will fail. */

  if (! eb->called_open_root)
    {
      void *baton;
      SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                            eb->base_revision, pool,
                                            &baton));
      SVN_ERR(eb->wrapped_editor->close_directory(baton, pool));
    }

  if (! eb->quiet)
    {
      if (eb->got_textdeltas)
        SVN_ERR(svn_cmdline_printf(pool, "\n"));
      if (eb->mergeinfo_stripped)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Dropped Subversion mergeinfo "
                                   "from this revision.\n"));
      if (eb->svnmerge_migrated)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Migrated 'svnmerge-integrated' in "
                                   "this revision.\n"));
      if (eb->svnmerge_blocked)
        SVN_ERR(svn_cmdline_printf(pool,
                                   "NOTE: Saw 'svnmerge-blocked' in this "
                                   "revision (but didn't migrate it).\n"));
    }

  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
  return eb->wrapped_editor->abort_edit(eb->wrapped_edit_baton, pool);
}



/*** Editor factory function ***/

/* Set WRAPPED_EDITOR and WRAPPED_EDIT_BATON to an editor/baton pair
 * that wraps our own commit EDITOR/EDIT_BATON.  BASE_REVISION is the
 * revision on which the driver of this returned editor will be basing
 * the commit.  TO_URL is the URL of the root of the repository into
 * which the commit is being made.
 *
 * As the sync editor encounters property values, it might see the need to
 * normalize them (to LF line endings). Each carried out normalization adds 1
 * to the *NORMALIZED_NODE_PROPS_COUNTER (for notification).
 */
static svn_error_t *
get_sync_editor(const svn_delta_editor_t *wrapped_editor,
                void *wrapped_edit_baton,
                svn_revnum_t base_revision,
                const char *to_url,
                svn_boolean_t quiet,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                int *normalized_node_props_counter,
                apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  edit_baton_t *eb = apr_pcalloc(pool, sizeof(*eb));

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->absent_directory = absent_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->absent_file = absent_file;
  tree_editor->close_edit = close_edit;
  tree_editor->abort_edit = abort_edit;

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->base_revision = base_revision;
  eb->to_url = to_url;
  eb->quiet = quiet;
  eb->normalized_node_props_counter = normalized_node_props_counter;

  if (getenv("SVNSYNC_UNSUPPORTED_STRIP_MERGEINFO"))
    {
      eb->strip_mergeinfo = TRUE;
    }
  if (getenv("SVNSYNC_UNSUPPORTED_MIGRATE_SVNMERGE"))
    {
      /* Current we can't merge property values.  That's only possible
         if all the properties to be merged were always modified in
         exactly the same revisions, or if we allow ourselves to
         lookup the current state of properties in the sync
         destination.  So for now, migrating svnmerge.py data implies
         stripping pre-existing svn:mergeinfo. */
      /* ### FIXME: Do a real migration by consulting the mirror
         ### repository's HEAD propvalues and merging svn:mergeinfo,
         ### svnmerge-integrated, and svnmerge-blocked together. */
      eb->migrate_svnmerge = TRUE;
      eb->strip_mergeinfo = TRUE;
    }

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/*** `svnsync sync' ***/

/* Implements `svn_commit_callback2_t' interface. */
static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  subcommand_baton_t *sb = baton;

  if (! sb->quiet)
    {
      SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld.\n"),
                                 commit_info->revision));
    }

  sb->committed_rev = commit_info->revision;

  return SVN_NO_ERROR;
}


/* Set *FROM_SESSION to an RA session associated with the source
 * repository of the synchronization, as determined by reading
 * svn:sync- properties from the destination repository (associated
 * with TO_SESSION).  Set LAST_MERGED_REV to the value of the property
 * which records the most recently synchronized revision.
 *
 * CALLBACKS is a vtable of RA callbacks to provide when creating
 * *FROM_SESSION.  CONFIG is a configuration hash.
 */
static svn_error_t *
open_source_session(svn_ra_session_t **from_session,
                    svn_string_t **last_merged_rev,
                    svn_ra_session_t *to_session,
                    svn_ra_callbacks2_t *callbacks,
                    apr_hash_t *config,
                    void *baton,
                    apr_pool_t *pool)
{
  svn_string_t *from_url, *from_uuid;

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                          &from_uuid, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                          last_merged_rev, pool));

  if (! from_url || ! from_uuid || ! *last_merged_rev)
    return svn_error_create
      (APR_EINVAL, NULL,
       _("Destination repository has not been initialized"));

  /* Open the session to copy the revision data. */
  SVN_ERR(svn_ra_open3(from_session, from_url->data, from_uuid->data,
                       callbacks, baton, config, pool));

  return SVN_NO_ERROR;
}

/* Replay baton, used during sychnronization. */
typedef struct {
  svn_ra_session_t *from_session;
  svn_ra_session_t *to_session;
  subcommand_baton_t *sb;
  svn_boolean_t has_commit_revprops_capability;
  int normalized_rev_props_count;
  int normalized_node_props_count;
} replay_baton_t;

/* Return a replay baton allocated from POOL and populated with
   data from the provided parameters. */
static replay_baton_t *
make_replay_baton(svn_ra_session_t *from_session,
                  svn_ra_session_t *to_session,
                  subcommand_baton_t *sb, apr_pool_t *pool)
{
  replay_baton_t *rb = apr_pcalloc(pool, sizeof(*rb));
  rb->from_session = from_session;
  rb->to_session = to_session;
  rb->sb = sb;
  return rb;
}

/* Filter out svn:date and svn:author properties. */
static svn_boolean_t
filter_exclude_date_author_sync(const char *key)
{
  if (strcmp(key, SVN_PROP_REVISION_AUTHOR) == 0)
    return TRUE;
  else if (strcmp(key, SVN_PROP_REVISION_DATE) == 0)
    return TRUE;
  else if (strncmp(key, SVNSYNC_PROP_PREFIX,
                   sizeof(SVNSYNC_PROP_PREFIX) - 1) == 0)
    return TRUE;

  return FALSE;
}

/* Filter out all properties except svn:date and svn:author */
static svn_boolean_t
filter_include_date_author_sync(const char *key)
{
  return ! filter_exclude_date_author_sync(key);
}


/* Only exclude svn:log .*/
static svn_boolean_t
filter_exclude_log(const char *key)
{
  if (strcmp(key, SVN_PROP_REVISION_LOG) == 0)
    return TRUE;
  else
    return FALSE;
}

/* Only include svn:log. */
static svn_boolean_t
filter_include_log(const char *key)
{
  return ! filter_exclude_log(key);
}


/* Callback function for svn_ra_replay_range, invoked when starting to parse
 * a replay report.
 */
static svn_error_t *
replay_rev_started(svn_revnum_t revision,
                   void *replay_baton,
                   const svn_delta_editor_t **editor,
                   void **edit_baton,
                   apr_hash_t *rev_props,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *commit_editor;
  const svn_delta_editor_t *cancel_editor;
  const svn_delta_editor_t *sync_editor;
  void *commit_baton;
  void *cancel_baton;
  void *sync_baton;
  replay_baton_t *rb = replay_baton;
  apr_hash_t *filtered;
  int filtered_count;
  int normalized_count;

  /* We set this property so that if we error out for some reason
     we can later determine where we were in the process of
     merging a revision.  If we had committed the change, but we
     hadn't finished copying the revprops we need to know that, so
     we can go back and finish the job before we move on.

     NOTE: We have to set this before we start the commit editor,
     because ra_svn doesn't let you change rev props during a
     commit. */
  SVN_ERR(svn_ra_change_rev_prop(rb->to_session, 0,
                                 SVNSYNC_PROP_CURRENTLY_COPYING,
                                 svn_string_createf(pool, "%ld",
                                                    revision),
                                 pool));

  /* The actual copy is just a replay hooked up to a commit.  Include
     all the revision properties from the source repositories, except
     'svn:author' and 'svn:date', those are not guaranteed to get
     through the editor anyway.
     If we're syncing to an non-commit-revprops capable server, filter
     out all revprops except svn:log and add them later in
     revplay_rev_finished. */
  filtered = filter_props(&filtered_count, rev_props,
                          (rb->has_commit_revprops_capability
                            ? filter_exclude_date_author_sync
                            : filter_include_log),
                          pool);

  /* svn_ra_get_commit_editor3 requires the log message to be
     set. It's possible that we didn't receive 'svn:log' here, so we
     have to set it to at least the empty string. If there's a svn:log
     property on this revision, we will write the actual value in the
     replay_rev_finished callback. */
  if (! apr_hash_get(filtered, SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING))
    apr_hash_set(filtered, SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING,
                 svn_string_create("", pool));

  /* If necessary, normalize line ending style, and add the number
     of changes to the overall count in the replay baton. */
  SVN_ERR(normalize_revprops(filtered, &normalized_count, pool));
  rb->normalized_rev_props_count += normalized_count;

  SVN_ERR(svn_ra_get_commit_editor3(rb->to_session, &commit_editor,
                                    &commit_baton,
                                    filtered,
                                    commit_callback, rb->sb,
                                    NULL, FALSE, pool));

  /* There's one catch though, the diff shows us props we can't send
     over the RA interface, so we need an editor that's smart enough
     to filter those out for us.  */
  SVN_ERR(get_sync_editor(commit_editor, commit_baton, revision - 1,
                          rb->sb->to_url, rb->sb->quiet,
                          &sync_editor, &sync_baton, 
                          &(rb->normalized_node_props_count), pool));

  SVN_ERR(svn_delta_get_cancellation_editor(check_cancel, NULL,
                                            sync_editor, sync_baton,
                                            &cancel_editor,
                                            &cancel_baton,
                                            pool));
  *editor = cancel_editor;
  *edit_baton = cancel_baton;

  return SVN_NO_ERROR;
}

/* Callback function for svn_ra_replay_range, invoked when finishing parsing
 * a replay report.
 */
static svn_error_t *
replay_rev_finished(svn_revnum_t revision,
                    void *replay_baton,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    apr_hash_t *rev_props,
                    apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  replay_baton_t *rb = replay_baton;
  apr_hash_t *filtered, *existing_props;
  int filtered_count;
  int normalized_count;

  SVN_ERR(editor->close_edit(edit_baton, pool));

  /* Sanity check that we actually committed the revision we meant to. */
  if (rb->sb->committed_rev != revision)
    return svn_error_createf
             (APR_EINVAL, NULL,
              _("Commit created rev %ld but should have created %ld"),
              rb->sb->committed_rev, revision);

  SVN_ERR(svn_ra_rev_proplist(rb->to_session, revision, &existing_props,
                              subpool));


  /* Ok, we're done with the data, now we just need to copy the remaining
     'svn:date' and 'svn:author' revprops and we're all set.
     If the server doesn't support revprops-in-a-commit, we still have to
     set all revision properties except svn:log. */
  filtered = filter_props(&filtered_count, rev_props,
                          (rb->has_commit_revprops_capability
                            ? filter_include_date_author_sync
                            : filter_exclude_log),
                          subpool);

  /* If necessary, normalize line ending style, and add the number
     of changes to the overall count in the replay baton. */
  SVN_ERR(normalize_revprops(filtered, &normalized_count, pool));
  rb->normalized_rev_props_count += normalized_count;

  SVN_ERR(write_revprops(&filtered_count, rb->to_session, revision, filtered,
                         subpool));

  /* Remove all extra properties in TARGET. */
  SVN_ERR(remove_props_not_in_source(rb->to_session, revision,
                                     rev_props, existing_props, subpool));

  svn_pool_clear(subpool);

  /* Ok, we're done, bring the last-merged-rev property up to date. */
  SVN_ERR(svn_ra_change_rev_prop
          (rb->to_session,
           0,
           SVNSYNC_PROP_LAST_MERGED_REV,
           svn_string_create(apr_psprintf(pool, "%ld", revision),
                             subpool),
           subpool));

  /* And finally drop the currently copying prop, since we're done
     with this revision. */
  SVN_ERR(svn_ra_change_rev_prop(rb->to_session, 0,
                                 SVNSYNC_PROP_CURRENTLY_COPYING,
                                 NULL, subpool));

  /* Notify the user that we copied revision properties. */
  if (! rb->sb->quiet)
    SVN_ERR(log_properties_copied(filtered_count > 0, revision, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Synchronize the repository associated with RA session TO_SESSION,
 * using information found in baton B, while the repository is
 * locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_synchronize(svn_ra_session_t *to_session,
               subcommand_baton_t *baton, apr_pool_t *pool)
{
  svn_string_t *last_merged_rev;
  svn_revnum_t from_latest;
  svn_ra_session_t *from_session;
  svn_string_t *currently_copying;
  svn_revnum_t to_latest, copying, last_merged;
  svn_revnum_t start_revision, end_revision;
  replay_baton_t *rb;
  int normalized_rev_props_count = 0;

  SVN_ERR(open_source_session(&from_session,
                              &last_merged_rev, to_session,
                              &(baton->source_callbacks), baton->config,
                              baton, pool));

  /* Check to see if we have revprops that still need to be copied for
     a prior revision we didn't finish copying.  But first, check for
     state sanity.  Remember, mirroring is not an atomic action,
     because revision properties are copied separately from the
     revision's contents.

     So, any time that currently-copying is not set, then
     last-merged-rev should be the HEAD revision of the destination
     repository.  That is, if we didn't fall over in the middle of a
     previous synchronization, then our destination repository should
     have exactly as many revisions in it as we've synchronized.

     Alternately, if currently-copying *is* set, it must
     be either last-merged-rev or last-merged-rev + 1, and the HEAD
     revision must be equal to either last-merged-rev or
     currently-copying. If this is not the case, somebody has meddled
     with the destination without using svnsync.
  */

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_CURRENTLY_COPYING,
                          &currently_copying, pool));

  SVN_ERR(svn_ra_get_latest_revnum(to_session, &to_latest, pool));

  last_merged = SVN_STR_TO_REV(last_merged_rev->data);

  if (currently_copying)
    {
      copying = SVN_STR_TO_REV(currently_copying->data);

      if ((copying < last_merged)
          || (copying > (last_merged + 1))
          || ((to_latest != last_merged) && (to_latest != copying)))
        {
          return svn_error_createf
            (APR_EINVAL, NULL,
             _("Revision being currently copied (%ld), last merged revision "
               "(%ld), and destination HEAD (%ld) are inconsistent; have you "
               "committed to the destination without using svnsync?"),
             copying, last_merged, to_latest);
        }
      else if (copying == to_latest)
        {
          if (copying > last_merged)
            {
              SVN_ERR(copy_revprops(from_session, to_session,
                                    to_latest, TRUE, baton->quiet,
                                    &normalized_rev_props_count,
                                    pool));
              last_merged = copying;
              last_merged_rev = svn_string_create
                (apr_psprintf(pool, "%ld", last_merged), pool);
            }

          /* Now update last merged rev and drop currently changing.
             Note that the order here is significant, if we do them
             in the wrong order there are race conditions where we
             end up not being able to tell if there have been bogus
             (i.e. non-svnsync) commits to the dest repository. */

          SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                         SVNSYNC_PROP_LAST_MERGED_REV,
                                         last_merged_rev, pool));
          SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                         SVNSYNC_PROP_CURRENTLY_COPYING,
                                         NULL, pool));
        }
      /* If copying > to_latest, then we just fall through to
         attempting to copy the revision again. */
    }
  else
    {
      if (to_latest != last_merged)
        return svn_error_createf(APR_EINVAL, NULL,
                                 _("Destination HEAD (%ld) is not the last "
                                   "merged revision (%ld); have you "
                                   "committed to the destination without "
                                   "using svnsync?"),
                                 to_latest, last_merged);
    }

  /* Now check to see if there are any revisions to copy. */
  SVN_ERR(svn_ra_get_latest_revnum(from_session, &from_latest, pool));

  if (from_latest < atol(last_merged_rev->data))
    return SVN_NO_ERROR;

  /* Ok, so there are new revisions, iterate over them copying them
     into the destination repository. */
  rb = make_replay_baton(from_session, to_session, baton, pool);

  /* For compatibility with older svnserve versions, check first if we
     support adding revprops to the commit. */
  SVN_ERR(svn_ra_has_capability(rb->to_session,
                                &rb->has_commit_revprops_capability,
                                SVN_RA_CAPABILITY_COMMIT_REVPROPS,
                                pool));

  start_revision = atol(last_merged_rev->data) + 1;
  end_revision = from_latest;

  SVN_ERR(check_cancel(NULL));

  SVN_ERR(svn_ra_replay_range(from_session, start_revision, end_revision,
                              0, TRUE, replay_rev_started,
                              replay_rev_finished, rb, pool));

  SVN_ERR(log_properties_normalized(rb->normalized_rev_props_count
                                      + normalized_rev_props_count,
                                    rb->normalized_node_props_count,
                                    pool));


  return SVN_NO_ERROR;
}


/* SUBCOMMAND: sync */
static svn_error_t *
synchronize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;
  const char *to_url;

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  to_url = APR_ARRAY_IDX(targets, 0, const char *);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);

  baton = make_subcommand_baton(opt_baton, to_url, NULL, 0, 0, pool);
  SVN_ERR(svn_ra_open3(&to_session, baton->to_url, NULL,
                       &(baton->sync_callbacks), baton, baton->config, pool));
  SVN_ERR(check_if_session_is_at_repos_root(to_session, baton->to_url, pool));
  SVN_ERR(with_locked(to_session, do_synchronize, baton, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync copy-revprops' ***/

/* Copy revision properties to the repository associated with RA
 * session TO_SESSION, using information found in baton B, while the
 * repository is locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_copy_revprops(svn_ra_session_t *to_session,
                 subcommand_baton_t *baton, apr_pool_t *pool)
{
  svn_ra_session_t *from_session;
  svn_string_t *last_merged_rev;
  svn_revnum_t i;
  svn_revnum_t step = 1;
  int normalized_rev_props_count = 0;

  SVN_ERR(open_source_session(&from_session, &last_merged_rev,
                              to_session,
                              &(baton->source_callbacks), baton->config,
                              baton, pool));

  /* An invalid revision means "last-synced" */
  if (! SVN_IS_VALID_REVNUM(baton->start_rev))
    baton->start_rev = SVN_STR_TO_REV(last_merged_rev->data);
  if (! SVN_IS_VALID_REVNUM(baton->end_rev))
    baton->end_rev = SVN_STR_TO_REV(last_merged_rev->data);

  /* Make sure we have revisions within the valid range. */
  if (baton->start_rev > SVN_STR_TO_REV(last_merged_rev->data))
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Cannot copy revprops for a revision (%ld) that has not "
         "been synchronized yet"), baton->start_rev);
  if (baton->end_rev > SVN_STR_TO_REV(last_merged_rev->data))
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Cannot copy revprops for a revision (%ld) that has not "
         "been synchronized yet"), baton->end_rev);

  /* Now, copy all the requested revisions, in the requested order. */
  step = (baton->start_rev > baton->end_rev) ? -1 : 1;
  for (i = baton->start_rev; i != baton->end_rev + step; i = i + step)
    {
      int normalized_count;
      SVN_ERR(check_cancel(NULL));
      SVN_ERR(copy_revprops(from_session, to_session, i, FALSE,
                            baton->quiet, &normalized_count, pool));
      normalized_rev_props_count += normalized_count;
    }

  /* Notify about normalized props, if any. */
  SVN_ERR(log_properties_normalized(normalized_rev_props_count, 0, pool));

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: copy-revprops */
static svn_error_t *
copy_revprops_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;
  const char *to_url;
  svn_opt_revision_t start_revision, end_revision;
  svn_revnum_t start_rev = 0, end_rev = SVN_INVALID_REVNUM;

  /* There should be either one or two arguments left to parse. */
  if (os->argc - os->ind > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
  if (os->argc - os->ind < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  /* If there are two args, the last one is a revision range.  We'll
     effectively pop it from the end of the list.  Why?  Because
     svn_opt__args_to_target_array() does waaaaay too many useful
     things for us not to use it.  */
  if (os->argc - os->ind == 2)
    {
      const char *rev_str = os->argv[--(os->argc)];

      start_revision.kind = svn_opt_revision_unspecified;
      end_revision.kind = svn_opt_revision_unspecified;
      if ((svn_opt_parse_revision(&start_revision, &end_revision,
                                  rev_str, pool) != 0)
          || ((start_revision.kind != svn_opt_revision_number)
              && (start_revision.kind != svn_opt_revision_head))
          || ((end_revision.kind != svn_opt_revision_number)
              && (end_revision.kind != svn_opt_revision_head)
              && (end_revision.kind != svn_opt_revision_unspecified)))
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' is not a valid revision range"),
                                 rev_str);

      /* Get the start revision, which must be either HEAD or a number
         (which is required to be a valid one). */
      if (start_revision.kind == svn_opt_revision_head)
        {
          start_rev = SVN_INVALID_REVNUM;
        }
      else
        {
          start_rev = start_revision.value.number;
          if (! SVN_IS_VALID_REVNUM(start_rev))
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Invalid revision number (%ld)"),
                                     start_rev);
        }

      /* Get the end revision, which must be unspecified (meaning,
         "same as the start_rev"), HEAD, or a number (which is
         required to be a valid one). */
      if (end_revision.kind == svn_opt_revision_unspecified)
        {
          end_rev = start_rev;
        }
      else if (end_revision.kind == svn_opt_revision_head)
        {
          end_rev = SVN_INVALID_REVNUM;
        }
      else
        {
          end_rev = end_revision.value.number;
          if (! SVN_IS_VALID_REVNUM(end_rev))
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Invalid revision number (%ld)"),
                                     end_rev);
        }
    }

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 1,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts != 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  to_url = APR_ARRAY_IDX(targets, 0, const char *);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);

  baton = make_subcommand_baton(opt_baton, to_url, NULL,
                                start_rev, end_rev, pool);
  SVN_ERR(svn_ra_open3(&to_session, baton->to_url, NULL,
                       &(baton->sync_callbacks), baton, baton->config, pool));
  SVN_ERR(check_if_session_is_at_repos_root(to_session, baton->to_url, pool));
  SVN_ERR(with_locked(to_session, do_copy_revprops, baton, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync info' ***/


/* SUBCOMMAND: info */
static svn_error_t *
info_cmd(apr_getopt_t *os, void *b, apr_pool_t * pool)
{
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;
  const char *to_url;
  svn_string_t *from_url, *from_uuid, *last_merged_rev;

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* Get the mirror repository URL, and verify that it is URL-ish. */
  to_url = APR_ARRAY_IDX(targets, 0, const char *);
  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);

  /* Open an RA session to the mirror repository URL. */
  baton = make_subcommand_baton(opt_baton, to_url, NULL, 0, 0, pool);
  SVN_ERR(svn_ra_open3(&to_session, baton->to_url, NULL,
                       &(baton->sync_callbacks), baton, baton->config, pool));
  SVN_ERR(check_if_session_is_at_repos_root(to_session, baton->to_url, pool));

  /* Verify that the repos has been initialized for synchronization. */
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));
  if (! from_url)
    return svn_error_createf
      (SVN_ERR_BAD_URL, NULL,
       _("Repository '%s' is not initialized for synchronization"), to_url);

  /* Fetch more of the magic properties, which are the source of our info. */
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                          &from_uuid, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                          &last_merged_rev, pool));

  /* Print the info. */
  SVN_ERR(svn_cmdline_printf(pool, _("Source URL: %s\n"), from_url->data));
  if (from_uuid)
    SVN_ERR(svn_cmdline_printf(pool, _("Source Repository UUID: %s\n"),
                               from_uuid->data));
  if (last_merged_rev)
    SVN_ERR(svn_cmdline_printf(pool, _("Last Merged Revision: %s\n"),
                               last_merged_rev->data));
  return SVN_NO_ERROR;
}



/*** `svnsync help' ***/


/* SUBCOMMAND: help */
static svn_error_t *
help_cmd(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;

  const char *header =
    _("general usage: svnsync SUBCOMMAND DEST_URL  [ARGS & OPTIONS ...]\n"
      "Type 'svnsync help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnsync --version' to see the program version and RA modules.\n"
      "\n"
      "Available subcommands:\n");

  const char *ra_desc_start
    = _("The following repository access (RA) modules are available:\n\n");

  svn_stringbuf_t *version_footer = svn_stringbuf_create(ra_desc_start,
                                                         pool);

  SVN_ERR(svn_ra_print_modules(version_footer, pool));

  SVN_ERR(svn_opt_print_help3(os, "svnsync",
                              opt_baton ? opt_baton->version : FALSE,
                              FALSE, version_footer->data, header,
                              svnsync_cmd_table, svnsync_options, NULL,
                              NULL, pool));

  return SVN_NO_ERROR;
}



/*** Main ***/

int
main(int argc, const char *argv[])
{
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  apr_array_header_t *received_opts;
  opt_baton_t opt_baton;
  svn_config_t *config;
  apr_status_t apr_err;
  apr_getopt_t *os;
  apr_pool_t *pool;
  svn_error_t *err;
  int opt_id, i;
  const char *username = NULL, *source_username = NULL, *sync_username = NULL;
  const char *password = NULL, *source_password = NULL, *sync_password = NULL;

  if (svn_cmdline_init("svnsync", stderr) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

  err = check_lib_versions();
  if (err)
    return svn_cmdline_handle_exit_error(err, NULL, "svnsync: ");

  pool = svn_pool_create(NULL);

  err = svn_ra_initialize(pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");

  memset(&opt_baton, 0, sizeof(opt_baton));

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  if (argc <= 1)
    {
      help_cmd(NULL, NULL, pool);
      svn_pool_destroy(pool);
      return EXIT_FAILURE;
    }

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");

  os->interleave = 1;

  for (;;)
    {
      const char *opt_arg;

      apr_err = apr_getopt_long(os, svnsync_options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          help_cmd(NULL, NULL, pool);
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }

      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id)
        {
          case svnsync_opt_non_interactive:
            opt_baton.non_interactive = TRUE;
            break;

          case svnsync_opt_trust_server_cert:
            opt_baton.trust_server_cert = TRUE;
            break;

          case svnsync_opt_no_auth_cache:
            opt_baton.no_auth_cache = TRUE;
            break;

          case svnsync_opt_auth_username:
            username = opt_arg;
            break;

          case svnsync_opt_auth_password:
            password = opt_arg;
            break;

          case svnsync_opt_source_username:
            source_username = opt_arg;
            break;

          case svnsync_opt_source_password:
            source_password = opt_arg;
            break;

          case svnsync_opt_sync_username:
            sync_username = opt_arg;
            break;

          case svnsync_opt_sync_password:
            sync_password = opt_arg;
            break;

          case svnsync_opt_config_dir:
            opt_baton.config_dir = opt_arg;
            break;

          case svnsync_opt_version:
            opt_baton.version = TRUE;
            break;

          case 'q':
            opt_baton.quiet = TRUE;
            break;

          case '?':
          case 'h':
            opt_baton.help = TRUE;
            break;

          default:
            {
              help_cmd(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
    }

  if (opt_baton.help)
    subcommand = svn_opt_get_canonical_subcommand2(svnsync_cmd_table, "help");

  /* Disallow the mixing --username/password with their --source- and
     --sync- variants.  Treat "--username FOO" as "--source-username
     FOO --sync-username FOO"; ditto for "--password FOO". */
  if ((username || password)
      && (source_username || sync_username
          || source_password || sync_password))
    {
      err = svn_error_create
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot use --username or --password with any of "
           "--source-username, --source-password, --sync-username, "
           "or --sync-password.\n"));
      return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");
    }
  if (username)
    {
      source_username = username;
      sync_username = username;
    }
  if (password)
    {
      source_password = password;
      sync_password = password;
    }
  opt_baton.source_username = source_username;
  opt_baton.source_password = source_password;
  opt_baton.sync_username = sync_username;
  opt_baton.sync_password = sync_password;

  /* --trust-server-cert can only be used with --non-interactive */
  if (opt_baton.trust_server_cert && !opt_baton.non_interactive)
    {
      err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("--trust-server-cert requires "
                               "--non-interactive"));
      return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");
    }

  err = svn_config_ensure(opt_baton.config_dir, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "synsync: ");

  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_baton.version)
            {
              /* Use the "help" subcommand to handle "--version". */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", help_cmd, {0}, "",
                  {svnsync_opt_version,  /* must accept its own option */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              help_cmd(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand2(svnsync_cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              help_cmd(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
    }

  for (i = 0; i < received_opts->nelts; ++i)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svnsync_options, subcommand,
                                          pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            {
              help_cmd(NULL, NULL, pool);
            }
          else
            {
              err = svn_error_createf
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Subcommand '%s' doesn't accept option '%s'\n"
                   "Type 'svnsync help %s' for usage.\n"),
                 subcommand->name, optstr, subcommand->name);
              return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");
            }
        }
    }

  err = svn_config_get_config(&opt_baton.config, opt_baton.config_dir, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");

  config = apr_hash_get(opt_baton.config, SVN_CONFIG_CATEGORY_CONFIG,
                        APR_HASH_KEY_STRING);

  apr_signal(SIGINT, signal_handler);

#ifdef SIGBREAK
  /* SIGBREAK is a Win32 specific signal generated by ctrl-break. */
  apr_signal(SIGBREAK, signal_handler);
#endif

#ifdef SIGHUP
  apr_signal(SIGHUP, signal_handler);
#endif

#ifdef SIGTERM
  apr_signal(SIGTERM, signal_handler);
#endif

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it,
     otherwise working with large files when compiled against an APR
     that doesn't have large file support will crash the program,
     which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif

  err = svn_cmdline_create_auth_baton(&opt_baton.source_auth_baton,
                                      opt_baton.non_interactive,
                                      opt_baton.source_username,
                                      opt_baton.source_password,
                                      opt_baton.config_dir,
                                      opt_baton.no_auth_cache,
                                      opt_baton.trust_server_cert,
                                      config,
                                      check_cancel, NULL,
                                      pool);
  if (! err)
    err = svn_cmdline_create_auth_baton(&opt_baton.sync_auth_baton,
                                        opt_baton.non_interactive,
                                        opt_baton.sync_username,
                                        opt_baton.sync_password,
                                        opt_baton.config_dir,
                                        opt_baton.no_auth_cache,
                                        opt_baton.trust_server_cert,
                                        config,
                                        check_cancel, NULL,
                                        pool);
  if (! err)
    err = (*subcommand->cmd_func)(os, &opt_baton, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svnsync help' for more info"));
        }

      return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");
    }

  svn_pool_destroy(pool);

  return EXIT_SUCCESS;
}
