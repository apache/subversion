/*
 * ====================================================================
 * Copyright (c) 2005-2006 CollabNet.  All rights reserved.
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

#include "svn_private_config.h"

#include <apr_network_io.h>
#include <apr_signal.h>
#include <apr_uuid.h>

static svn_opt_subcommand_t initialize_cmd,
                            synchronize_cmd,
                            copy_revprops_cmd,
                            help_cmd;

enum {
  svnsync_opt_non_interactive = SVN_OPT_FIRST_LONGOPT_ID,
  svnsync_opt_no_auth_cache,
  svnsync_opt_auth_username,
  svnsync_opt_auth_password,
  svnsync_opt_config_dir,
  svnsync_opt_version
};

#define SVNSYNC_OPTS_DEFAULT svnsync_opt_non_interactive, \
                             svnsync_opt_no_auth_cache, \
                             svnsync_opt_auth_username, \
                             svnsync_opt_auth_password, \
                             svnsync_opt_config_dir

static const svn_opt_subcommand_desc_t svnsync_cmd_table[] =
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
         "You should not commit to, or make revision property changes in,\n"
         "the destination repository by any method other than 'svnsync'.\n"
         "In other words, the destination repository should be a read-only\n"
         "mirror of the source repository.\n"),
      { SVNSYNC_OPTS_DEFAULT } },
    { "synchronize", synchronize_cmd, { "sync" },
      N_("usage: svnsync synchronize DEST_URL\n"
         "\n"
         "Transfer all pending revisions from source to destination.\n"),
      { SVNSYNC_OPTS_DEFAULT } },
    { "copy-revprops", copy_revprops_cmd, { 0 },
      N_("usage: svnsync copy-revprops DEST_URL REV\n"
         "\n"
         "Copy all revision properties for revision REV from source to\n"
         "destination.\n"),
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
    {"non-interactive", svnsync_opt_non_interactive, 0,
                       N_("do no interactive prompting") },
    {"no-auth-cache",  svnsync_opt_no_auth_cache, 0,
                       N_("do not cache authentication tokens") },
    {"username",       svnsync_opt_auth_username, 1,
                       N_("specify a username ARG") },
    {"password",       svnsync_opt_auth_password, 1,
                       N_("specify a password ARG") },
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
  svn_auth_baton_t *auth_baton;
  svn_boolean_t non_interactive;
  svn_boolean_t no_auth_cache;
  const char *auth_username;
  const char *auth_password;
  const char *config_dir;
  apr_hash_t *config;
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

  for (i = 0; i < 10; ++i)
    {
      svn_pool_clear(subpool);

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
      else
        {
          SVN_ERR(svn_ra_change_rev_prop(session, 0, SVNSYNC_PROP_LOCK,
                                         mylocktoken, subpool));
        }
    }

  return svn_error_createf(APR_EINVAL, NULL,
                           "Couldn't get lock on destination repos "
                           "after %d attempts\n", i);
}


typedef svn_error_t *(*with_locked_func_t)(svn_ra_session_t *session,
                                           void *baton,
                                           apr_pool_t *pool);


/* Lock the repository associated with RA SESSION, then execute the
 * given FUNC/BATON pair while holding the lock.  Finally, drop the
 * lock once it finishes.
 */
static svn_error_t *
with_locked(svn_ra_session_t *session,
            with_locked_func_t func,
            void *baton,
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
  const char *path;

  SVN_ERR(svn_io_temp_dir(&path, pool));

  path = svn_path_join(path, "tempfile", pool);

  SVN_ERR(svn_io_open_unique_file2(fp, NULL, path, ".tmp",
                                   svn_io_file_del_on_close, pool));

  return SVN_NO_ERROR;
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

  SVN_ERR(svn_ra_get_repos_root(sess, &sess_root, pool));

  if (strcmp(url, sess_root) == 0)
    return SVN_NO_ERROR;
  else
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Session is rooted at '%s' but the repos root is '%s'"),
       url, sess_root);
}


/* Copy all the revision properties, except for those that have the
 * "svn:sync-" prefix, from revision REV of the repository associated
 * with RA session FROM_SESSION, to the repository associated with RA
 * session TO_SESSION.
 *
 * If SYNC is TRUE, then properties on the destination revision that
 * do not exist on the source revision will be removed.
 */
static svn_error_t *
copy_revprops(svn_ra_session_t *from_session,
              svn_ra_session_t *to_session,
              svn_revnum_t rev,
              svn_boolean_t sync,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *revprops, *existing_props;
  svn_boolean_t saw_sync_props = FALSE;
  apr_hash_index_t *hi;

  if (sync)
    SVN_ERR(svn_ra_rev_proplist(to_session, rev, &existing_props, pool));

  SVN_ERR(svn_ra_rev_proplist(from_session, rev, &revprops, pool));

  for (hi = apr_hash_first(pool, revprops); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);

      if (strncmp(key, SVNSYNC_PROP_PREFIX,
                  sizeof(SVNSYNC_PROP_PREFIX) - 1) == 0)
        saw_sync_props = TRUE;
      else
        SVN_ERR(svn_ra_change_rev_prop(to_session, rev, key, val, subpool));

      if (sync)
        apr_hash_set(existing_props, key, APR_HASH_KEY_STRING, NULL);
    }

  if (sync)
    {
      for (hi = apr_hash_first(pool, existing_props);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *name;

          svn_pool_clear(subpool);

          apr_hash_this(hi, &name, NULL, NULL);

          SVN_ERR(svn_ra_change_rev_prop(to_session, rev, name, NULL,
                                         subpool));
        }
    }

  if (saw_sync_props)
    SVN_ERR(svn_cmdline_printf(subpool, 
                               _("Copied properties for revision %ld "
                                 "(%s* properties skipped).\n"),
                               rev, SVNSYNC_PROP_PREFIX));
  else
    SVN_ERR(svn_cmdline_printf(subpool, 
                               _("Copied properties for revision %ld.\n"), 
                               rev));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/*** `svnsync init' ***/

/* Baton for initializing the destination repository while locked. */
typedef struct {
  const char *from_url;
  const char *to_url;
  apr_hash_t *config;
  svn_ra_callbacks2_t *callbacks;
} init_baton_t;


/* Initialize the repository associated with RA session TO_SESSION,
 * using information found in baton B, while the repository is
 * locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_initialize(svn_ra_session_t *to_session, void *b, apr_pool_t *pool)
{
  svn_ra_session_t *from_session;
  init_baton_t *baton = b;
  svn_string_t *from_url;
  svn_revnum_t latest;
  const char *uuid;

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

  SVN_ERR(svn_ra_open2(&from_session, baton->from_url, baton->callbacks,
                       baton, baton->config, pool));

  SVN_ERR(check_if_session_is_at_repos_root(from_session, baton->from_url,
                                            pool));

  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                                 svn_string_create(baton->from_url, pool),
                                 pool));

  SVN_ERR(svn_ra_get_uuid(from_session, &uuid, pool));

  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                                 svn_string_create(uuid, pool), pool));

  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                                 svn_string_create("0", pool), pool));

  /* Finally, copy all non-svnsync revprops from rev 0 of the source
     repos into the dest repos. */

  SVN_ERR(copy_revprops(from_session, to_session, 0, FALSE, pool));

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
  svn_ra_callbacks2_t callbacks = { 0 };
  const char *to_url, *from_url;
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *args;
  init_baton_t baton;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 2, pool));

  to_url = svn_path_canonicalize(APR_ARRAY_IDX(args, 0, const char *), pool);
  from_url = svn_path_canonicalize(APR_ARRAY_IDX(args, 1, const char *), pool);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, 
                             _("Path '%s' is not a URL"), to_url);
  if (! svn_path_is_url(from_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, 
                             _("Path '%s' is not a URL"), from_url);

  baton.to_url = svn_path_canonicalize(to_url, pool);
  baton.from_url = svn_path_canonicalize(from_url, pool);
  baton.config = opt_baton->config;

  callbacks.open_tmp_file = open_tmp_file;
  callbacks.auth_baton = opt_baton->auth_baton;

  baton.callbacks = &callbacks;

  SVN_ERR(svn_ra_open2(&to_session,
                       baton.to_url,
                       &callbacks,
                       &baton,
                       baton.config,
                       pool));

  SVN_ERR(check_if_session_is_at_repos_root(to_session, baton.to_url, pool));

  SVN_ERR(with_locked(to_session, do_initialize, &baton, pool));

  return SVN_NO_ERROR;
}



/*** Syncronization Editor ***/

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
  svn_revnum_t base_revision;
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

  if (copyfrom_path)
    copyfrom_path = apr_psprintf(pool, "%s%s", eb->to_url,
                                 svn_path_uri_encode(copyfrom_path, pool));

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

  /* only regular properties can pass over libsvn_ra */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->change_dir_prop(db->wrapped_node_baton,
                                             name, value, pool);
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

  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

/*** Editor factory function ***/

/* Set WRAPPED_EDITOR and WRAPPED_EDIT_BATON to an editor/baton pair
 * that wraps our own commit EDITOR/EDIT_BATON.  BASE_REVISION is the
 * revision on which the driver of this returned editor will be basing
 * the commit.  TO_URL is the URL of the root of the repository into
 * which the commit is being made.
 */
static svn_error_t *
get_sync_editor(const svn_delta_editor_t *wrapped_editor,
                void *wrapped_edit_baton,
                svn_revnum_t base_revision,
                const char *to_url,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  edit_baton_t *eb = apr_palloc(pool, sizeof(*eb));

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

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->called_open_root = FALSE;
  eb->base_revision = base_revision;
  eb->to_url = to_url;

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/*** `svnsync sync' ***/

/* Baton for syncronizing the destination repository while locked. */
typedef struct {
  apr_hash_t *config;
  svn_ra_callbacks2_t *callbacks;
  const char *to_url;
  svn_revnum_t committed_rev;
} sync_baton_t;


/* Implements `svn_commit_callback2_t' interface. */
static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  sync_baton_t *sb = baton;

  SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld.\n"),
                             commit_info->revision));

  sb->committed_rev = commit_info->revision;

  return SVN_NO_ERROR;
}


/* Set *FROM_SESSION to an RA session associated with the source
 * repository of the syncronization, as determined by reading
 * svn:sync- properties from the destination repository (associated
 * with TO_SESSION).  Set LAST_MERGED_REV to the value of the property
 * which records the most recently syncronized revision.  
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
  const char *uuid;

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                          &from_uuid, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                          last_merged_rev, pool));

  if (! from_url || ! from_uuid || ! *last_merged_rev)
    return svn_error_create
      (APR_EINVAL, NULL, _("Destination repository has not been initialized"));

  SVN_ERR(svn_ra_open2(from_session, from_url->data, callbacks, baton,
                       config, pool));

  SVN_ERR(check_if_session_is_at_repos_root(*from_session, from_url->data,
                                            pool));

  /* Ok, now sanity check the UUID of the source repository, it
     wouldn't be a good thing to sync from a different repository. */

  SVN_ERR(svn_ra_get_uuid(*from_session, &uuid, pool));

  if (strcmp(uuid, from_uuid->data) != 0)
    return svn_error_createf(APR_EINVAL, NULL,
                             _("UUID of source repository (%s) does not "
                               "match expected UUID (%s)"),
                             uuid, from_uuid->data);

  return SVN_NO_ERROR;
}


/* Syncronize the repository associated with RA session TO_SESSION,
 * using information found in baton B, while the repository is
 * locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_synchronize(svn_ra_session_t *to_session, void *b, apr_pool_t *pool)
{
  svn_string_t *last_merged_rev;
  svn_revnum_t from_latest, current;
  svn_ra_session_t *from_session;
  sync_baton_t *baton = b;
  apr_pool_t *subpool;
  svn_string_t *currently_copying;
  svn_revnum_t to_latest, copying, last_merged;

  SVN_ERR(open_source_session(&from_session, &last_merged_rev, to_session,
                              baton->callbacks, baton->config, baton, pool));

  /* Check to see if we have revprops that still need to be copied for
     a prior revision we didn't finish copying.  But first, check for
     state sanity.  Remember, mirroring is not an atomic action,
     because revision properties are copied separately from the
     revision's contents.

     So, any time that currently-copying is not set, then
     last-merged-rev should be the HEAD revision of the destination
     repository.  That is, if we didn't fall over in the middle of a
     previous syncronization, then our destination repository should
     have exactly as many revisions in it as we've syncronized.

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
                                    to_latest, TRUE, pool));
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
        {
          return svn_error_createf
            (APR_EINVAL, NULL,
             _("Destination HEAD (%ld) is not the last merged revision (%ld); "
               "have you committed to the destination without using svnsync?"),
             to_latest, last_merged);
        }
    }

  /* Now check to see if there are any revisions to copy. */

  SVN_ERR(svn_ra_get_latest_revnum(from_session, &from_latest, pool));

  if (from_latest < atol(last_merged_rev->data))
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);

  /* Ok, so there are new revisions, iterate over them copying them
     into the destination repository. */

  for (current = atol(last_merged_rev->data) + 1;
       current <= from_latest;
       ++current)
    {
      const svn_delta_editor_t *commit_editor;
      const svn_delta_editor_t *cancel_editor;
      const svn_delta_editor_t *sync_editor;
      void *commit_baton;
      void *cancel_baton;
      void *sync_baton;

      svn_pool_clear(subpool);

      /* We set this property so that if we error out for some reason
         we can later determine where we were in the process of
         merging a revision.  If we had committed the change, but we
         hadn't finished copying the revprops we need to know that, so
         we can go back and finish the job before we move on.
        
         NOTE: We have to set this before we start the commit editor,
         because ra_svn doesn't let you change rev props during a
         commit. */
      SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                     SVNSYNC_PROP_CURRENTLY_COPYING,
                                     svn_string_createf(subpool, "%ld",
                                                        current),
                                     subpool));

      /* The actual copy is just a replay hooked up to a commit. */

      SVN_ERR(svn_ra_get_commit_editor2(to_session, &commit_editor,
                                        &commit_baton,
                                        "", /* empty log */
                                        commit_callback, baton,
                                        NULL, FALSE, subpool));

      /* There's one catch though, the diff shows us props we can't
         send over the RA interface, so we need an editor that's smart
         enough to filter those out for us.  */

      SVN_ERR(get_sync_editor(commit_editor, commit_baton, current - 1,
                              baton->to_url, &sync_editor, &sync_baton,
                              subpool));

      SVN_ERR(svn_delta_get_cancellation_editor(check_cancel, NULL,
                                                sync_editor, sync_baton,
                                                &cancel_editor,
                                                &cancel_baton,
                                                subpool));

      SVN_ERR(svn_ra_replay(from_session, current, 0, TRUE,
                            cancel_editor, cancel_baton, subpool));

      SVN_ERR(cancel_editor->close_edit(cancel_baton, subpool));

      /* Sanity check that we actually committed the revision we meant to. */
      if (baton->committed_rev != current)
        return svn_error_createf
                 (APR_EINVAL, NULL,
                  _("Commit created rev %ld but should have created %ld"),
                  baton->committed_rev, current);

      /* Ok, we're done with the data, now we just need to do the
         revprops and we're all set. */

      SVN_ERR(copy_revprops(from_session, to_session, current, TRUE, subpool));

      /* Ok, we're done, bring the last-merged-rev property up to date. */

      SVN_ERR(svn_ra_change_rev_prop
              (to_session,
               0,
               SVNSYNC_PROP_LAST_MERGED_REV,
               svn_string_create(apr_psprintf(subpool, "%ld", current),
                                 subpool),
               subpool));

      /* And finally drop the currently copying prop, since we're done
         with this revision. */

      SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                     SVNSYNC_PROP_CURRENTLY_COPYING,
                                     NULL, subpool));
    }

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: sync */
static svn_error_t *
synchronize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_callbacks2_t callbacks = { 0 };
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *args;
  sync_baton_t baton;
  const char *to_url;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));

  to_url = svn_path_canonicalize(APR_ARRAY_IDX(args, 0, const char *), pool);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, 
                             _("Path '%s' is not a URL"), to_url);

  callbacks.open_tmp_file = open_tmp_file;
  callbacks.auth_baton = opt_baton->auth_baton;

  baton.callbacks = &callbacks;
  baton.config = opt_baton->config;
  baton.to_url = to_url;

  SVN_ERR(svn_ra_open2(&to_session,
                       to_url,
                       baton.callbacks,
                       &baton,
                       baton.config,
                       pool));

  SVN_ERR(check_if_session_is_at_repos_root(to_session, to_url, pool));

  SVN_ERR(with_locked(to_session, do_synchronize, &baton, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync copy-revprops' ***/


/* Baton for copying revision properties to the destination repository
 * while locked.
 */
typedef struct {
  apr_hash_t *config;
  svn_ra_callbacks2_t *callbacks;
  const char *to_url;
  svn_revnum_t rev;
} copy_revprops_baton_t;


/* Copy revision properties to the repository associated with RA
 * session TO_SESSION, using information found in baton B, while the
 * repository is locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_copy_revprops(svn_ra_session_t *to_session, void *b, apr_pool_t *pool)
{
  copy_revprops_baton_t *baton = b;
  svn_ra_session_t *from_session;
  svn_string_t *last_merged_rev;

  SVN_ERR(open_source_session(&from_session, &last_merged_rev, to_session,
                              baton->callbacks, baton->config, baton, pool));

  if (baton->rev > SVN_STR_TO_REV(last_merged_rev->data))
    return svn_error_create
      (APR_EINVAL, NULL, _("Cannot copy revprops for a revision that has not "
                           "been synchronized yet"));

  SVN_ERR(copy_revprops(from_session, to_session, baton->rev, FALSE, pool));

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: copy-revprops */
static svn_error_t *
copy_revprops_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_callbacks2_t callbacks = { 0 };
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *args;
  copy_revprops_baton_t baton;
  const char *to_url;
  svn_revnum_t revision = SVN_INVALID_REVNUM;
  char *digits_end = NULL;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 2, pool));

  to_url = svn_path_canonicalize(APR_ARRAY_IDX(args, 0, const char *), pool);
  revision = strtol(APR_ARRAY_IDX(args, 1, const char *), &digits_end, 10);
  
  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, 
                             _("Path '%s' is not a URL"), to_url);
  if ((! SVN_IS_VALID_REVNUM(revision)) || (! digits_end) || *digits_end)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Invalid revision number"));

  callbacks.open_tmp_file = open_tmp_file;
  callbacks.auth_baton = opt_baton->auth_baton;

  baton.callbacks = &callbacks;
  baton.config = opt_baton->config;
  baton.to_url = to_url;
  baton.rev = revision;

  SVN_ERR(svn_ra_open2(&to_session,
                       to_url,
                       baton.callbacks,
                       &baton,
                       baton.config,
                       pool));

  SVN_ERR(check_if_session_is_at_repos_root(to_session, to_url, pool));

  SVN_ERR(with_locked(to_session, do_copy_revprops, &baton, pool));

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

  SVN_ERR(svn_opt_print_help(os, "svnsync",
                             opt_baton ? opt_baton->version : FALSE,
                             FALSE, version_footer->data, header,
                             svnsync_cmd_table, svnsync_options, NULL,
                             pool));

  return SVN_NO_ERROR;
}



/*** Main ***/

int
main(int argc, const char *argv[])
{
  const svn_opt_subcommand_desc_t *subcommand = NULL;
  apr_array_header_t *received_opts;
  opt_baton_t opt_baton;
  svn_config_t *config;
  apr_status_t apr_err;
  apr_getopt_t *os;
  apr_pool_t *pool;
  svn_error_t *err;
  int opt_id, i;

  if (svn_cmdline_init("svnsync", stderr) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

  err = check_lib_versions();
  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "svnsync: ");
      return EXIT_FAILURE;
    }

  pool = svn_pool_create(NULL);

  err = svn_ra_initialize(pool);
  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "svnsync: ");
      return EXIT_FAILURE;
    }

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

          case svnsync_opt_no_auth_cache:
            opt_baton.no_auth_cache = TRUE;
            break;

          case svnsync_opt_auth_username:
            opt_baton.auth_username = opt_arg;
            break;

          case svnsync_opt_auth_password:
            opt_baton.auth_password = opt_arg;
            break;

          case svnsync_opt_config_dir:
            opt_baton.config_dir = opt_arg;
            break;

          case svnsync_opt_version:
            opt_baton.version = TRUE;
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
    subcommand = svn_opt_get_canonical_subcommand(svnsync_cmd_table, "help");

  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_baton.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc_t pseudo_cmd =
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
          subcommand = svn_opt_get_canonical_subcommand(svnsync_cmd_table,
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

      if (! svn_opt_subcommand_takes_option(subcommand, opt_id))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code(opt_id, svnsync_options);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            help_cmd(NULL, NULL, pool);
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool, _("subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svnsync help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }
    }

  err = svn_config_get_config(&opt_baton.config, NULL, pool);
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

  err = svn_cmdline_setup_auth_baton(&opt_baton.auth_baton,
                                     opt_baton.non_interactive,
                                     opt_baton.auth_username,
                                     opt_baton.auth_password,
                                     opt_baton.config_dir,
                                     opt_baton.no_auth_cache,
                                     config,
                                     check_cancel, NULL,
                                     pool);

  err = (*subcommand->cmd_func)(os, &opt_baton, pool);
  if (err)
    {
      /* Fix up stupid default error strings. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS)
        {
          svn_error_clear(err);
          err = svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                                 _("Not enough arguments provided; "
                                   "try 'svnsync help' for more info"));
        }
      svn_handle_error2(err, stderr, FALSE, "svnsync: ");
      svn_error_clear(err);

      return EXIT_FAILURE;
    }

  svn_pool_destroy(pool);

  return EXIT_SUCCESS;
}
