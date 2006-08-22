/*
 * ra_plugin.c : the main RA module for local repository access
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

#include "ra_local.h"
#include "svn_ra.h"
#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_path.h"
#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <assert.h>

/*----------------------------------------------------------------*/


/* The reporter vtable needed by do_update() */
typedef struct reporter_baton_t
{
  svn_ra_local__session_baton_t *session;
  void *report_baton;

} reporter_baton_t;


static void *
make_reporter_baton(svn_ra_local__session_baton_t *session,
                    void *report_baton,
                    apr_pool_t *pool)
{
  reporter_baton_t *rbaton = apr_palloc(pool, sizeof(*rbaton));
  rbaton->session = session;
  rbaton->report_baton = report_baton;
  return rbaton;
}


static svn_error_t *
reporter_set_path(void *reporter_baton,
                  const char *path,
                  svn_revnum_t revision,
                  svn_boolean_t start_empty,
                  const char *lock_token,
                  apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_set_path2(rbaton->report_baton, path,
                             revision, start_empty, lock_token, pool);
}


static svn_error_t *
reporter_delete_path(void *reporter_baton,
                     const char *path,
                     apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_delete_path(rbaton->report_baton, path, pool);
}


static svn_error_t *
reporter_link_path(void *reporter_baton,
                   const char *path,
                   const char *url,
                   svn_revnum_t revision,
                   svn_boolean_t start_empty,
                   const char *lock_token,
                   apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  const char *fs_path = NULL;
  const char *repos_url_decoded;
  int repos_url_len;

  url = svn_path_uri_decode(url, pool);
  repos_url_decoded = svn_path_uri_decode(rbaton->session->repos_url, pool);
  repos_url_len = strlen(repos_url_decoded);
  if (strncmp(url, repos_url_decoded, repos_url_len) != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s'\n"
                               "is not the same repository as\n"
                               "'%s'"), url, rbaton->session->repos_url);
  fs_path = url + repos_url_len;

  return svn_repos_link_path2(rbaton->report_baton, path, fs_path, revision,
                              start_empty, lock_token, pool);
}


static svn_error_t *
reporter_finish_report(void *reporter_baton,
                       apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_finish_report(rbaton->report_baton, pool);
}


static svn_error_t *
reporter_abort_report(void *reporter_baton,
                      apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_abort_report(rbaton->report_baton, pool);
}


static const svn_ra_reporter2_t ra_local_reporter = 
{
  reporter_set_path,
  reporter_delete_path,
  reporter_link_path,
  reporter_finish_report,
  reporter_abort_report
};

static svn_error_t *
svn_ra_local__get_file_revs(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_ra_file_rev_handler_t handler,
                            void *handler_baton,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sbaton = session->priv;
  const char *abs_path = sbaton->fs_path->data;

  /* Concatenate paths */
  abs_path = svn_path_join(abs_path, path, pool);

  return svn_repos_get_file_revs(sbaton->repos, abs_path, start, end, NULL,
                                 NULL, handler, handler_baton, pool);
}

/* Pool cleanup handler: Ensure that the access descriptor of the filesystem
   DATA is set to NULL. */
static apr_status_t
cleanup_access(void *data)
{
  svn_error_t *serr;
  svn_fs_t *fs = data;

  serr = svn_fs_set_access(fs, NULL);

  if (serr)
    {
      apr_status_t apr_err = serr->apr_err;
      svn_error_clear(serr);
      return apr_err;
    }

  return APR_SUCCESS;
}

static svn_error_t *
get_username(svn_ra_session_t *session,
             apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;
  svn_auth_iterstate_t *iterstate;
  svn_fs_access_t *access_ctx;

  /* If we've already found the username don't ask for it again. */
  if (! baton->username)
    {
      /* Get a username somehow, so we have some svn:author property to
         attach to a commit. */
      if (baton->callbacks->auth_baton)
        {
          void *creds;
          svn_auth_cred_username_t *username_creds;
          SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                             SVN_AUTH_CRED_USERNAME,
                                             baton->uuid, /* realmstring */
                                             baton->callbacks->auth_baton,
                                             pool));
          
          /* No point in calling next_creds(), since that assumes that the
             first_creds() somehow failed to authenticate.  But there's no
             challenge going on, so we use whatever creds we get back on
             the first try. */
          username_creds = creds;
          if (username_creds && username_creds->username)
            {
              baton->username = apr_pstrdup(session->pool,
                                            username_creds->username);
              svn_error_clear(svn_auth_save_credentials(iterstate, pool));
            }
          else
            baton->username = "";
        }
      else
        baton->username = "";
    }

  /* If we have a real username, attach it to the filesystem so that it can
     be used to validate locks.  Even if there already is a user context
     associated, it may contain irrelevant lock tokens, so always create a new.
  */
  if (*baton->username)
    {
      SVN_ERR(svn_fs_create_access(&access_ctx, baton->username,
                                   pool));
      SVN_ERR(svn_fs_set_access(baton->fs, access_ctx));

      /* Make sure this context is disassociated when the pool gets
         destroyed. */
      apr_pool_cleanup_register(pool, baton->fs, cleanup_access,
                                apr_pool_cleanup_null);
    }

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------*/

/** The RA vtable routines **/

#define RA_LOCAL_DESCRIPTION \
        N_("Module for accessing a repository on local disk.")

static const char *
svn_ra_local__get_description(void)
{
  return _(RA_LOCAL_DESCRIPTION);
}

static const char * const *
svn_ra_local__get_schemes(apr_pool_t *pool)
{
  static const char *schemes[] = { "file", NULL };

  return schemes;
}

static svn_error_t *
svn_ra_local__open(svn_ra_session_t *session,
                   const char *repos_URL,
                   const svn_ra_callbacks2_t *callbacks,
                   void *callback_baton,
                   apr_hash_t *config,
                   apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton;
  const char *fs_path;
  
  /* Allocate and stash the session_baton args we have already. */
  baton = apr_pcalloc(pool, sizeof(*baton));
  baton->callbacks = callbacks;
  baton->callback_baton = callback_baton;
  
  /* Look through the URL, figure out which part points to the
     repository, and which part is the path *within* the
     repository. */
  SVN_ERR_W(svn_ra_local__split_URL(&(baton->repos),
                                    &(baton->repos_url),
                                    &fs_path,
                                    repos_URL,
                                    session->pool),
            _("Unable to open an ra_local session to URL"));
  baton->fs_path = svn_stringbuf_create(fs_path, session->pool);

  /* Cache the filesystem object from the repos here for
     convenience. */
  baton->fs = svn_repos_fs(baton->repos);

  /* Cache the repository UUID as well */
  SVN_ERR(svn_fs_get_uuid(baton->fs, &baton->uuid, session->pool));

  /* Be sure username is NULL so we know to look it up / ask for it */
  baton->username = NULL;

  session->priv = baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__reparent(svn_ra_session_t *session,
                       const char *url,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  svn_stringbuf_set(baton->fs_path,
                    svn_path_uri_decode(url + strlen(baton->repos_url),
                                        pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_latest_revnum(svn_ra_session_t *session,
                                svn_revnum_t *latest_revnum,
                                apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  SVN_ERR(svn_fs_youngest_rev(latest_revnum, baton->fs, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
svn_ra_local__get_dated_revision(svn_ra_session_t *session,
                                 svn_revnum_t *revision,
                                 apr_time_t tm,
                                 apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  SVN_ERR(svn_repos_dated_revision(revision, baton->repos, tm, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__change_rev_prop(svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  SVN_ERR(get_username(session, pool));

  SVN_ERR(svn_repos_fs_change_rev_prop2(baton->repos, rev, baton->username,
                                        name, value, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__get_uuid(svn_ra_session_t *session,
                       const char **uuid,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  *uuid = baton->uuid;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_repos_root(svn_ra_session_t *session,
                             const char **url,
                             apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  *url = baton->repos_url;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__rev_proplist(svn_ra_session_t *session,
                           svn_revnum_t rev,
                           apr_hash_t **props,
                           apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  SVN_ERR(svn_repos_fs_revision_proplist(props, baton->repos, rev,
                                         NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__rev_prop(svn_ra_session_t *session,
                       svn_revnum_t rev,
                       const char *name,
                       svn_string_t **value,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton = session->priv;

  SVN_ERR(svn_repos_fs_revision_prop(value, baton->repos, rev, name,
                                     NULL, NULL, pool));

  return SVN_NO_ERROR;
}


struct deltify_etc_baton
{
  svn_fs_t *fs;                    /* the fs to deltify in */
  svn_repos_t *repos;              /* repos for unlocking */
  const char *fs_path;             /* fs-path part of split session URL */
  apr_hash_t *lock_tokens;         /* tokens to unlock, if any */
  apr_pool_t *pool;                /* pool for scratch work */
  svn_commit_callback2_t callback;  /* the original callback */
  void *callback_baton;            /* the original callback's baton */
};

/* This implements 'svn_commit_callback_t'.  Its invokes the original
   (wrapped) callback, but also does deltification on the new revision and
   possibly unlocks committed paths.
   BATON is 'struct deltify_etc_baton *'. */
static svn_error_t * 
deltify_etc(const svn_commit_info_t *commit_info,
            void *baton, apr_pool_t *pool)
{
  struct deltify_etc_baton *db = baton;
  svn_error_t *err1, *err2;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  /* Invoke the original callback first, in case someone's waiting to
     know the revision number so they can go off and annotate an
     issue or something. */
  err1 = (*db->callback)(commit_info, db->callback_baton, pool);

  /* Maybe unlock the paths. */
  if (db->lock_tokens)
    {
      iterpool = svn_pool_create(db->pool);
      for (hi = apr_hash_first(db->pool, db->lock_tokens); hi;
           hi = apr_hash_next(hi))
        {
          const void *rel_path;
          void *val;
          const char *abs_path, *token;

          svn_pool_clear(iterpool);
          apr_hash_this(hi, &rel_path, NULL, &val);
          token = val;
          abs_path = svn_path_join(db->fs_path, rel_path, iterpool);
          /* We may get errors here if the lock was broken or stolen
             after the commit succeeded.  This is fine and should be
             ignored. */
          svn_error_clear(svn_repos_fs_unlock(db->repos, abs_path, token,
                                              FALSE, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  /* But, deltification shouldn't be stopped just because someone's
     random callback failed, so proceed unconditionally on to
     deltification. */
  err2 = svn_fs_deltify_revision(db->fs, commit_info->revision, db->pool);

  /* It's more interesting if the original callback failed, so let
     that one dominate. */
  if (err1)
    {
      svn_error_clear(err2);
      return err1;
    }

  return err2;
}


static svn_error_t *
svn_ra_local__get_commit_editor(svn_ra_session_t *session,
                                const svn_delta_editor_t **editor,
                                void **edit_baton,
                                const char *log_msg,
                                svn_commit_callback2_t callback,
                                void *callback_baton,
                                apr_hash_t *lock_tokens,
                                svn_boolean_t keep_locks,
                                apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess_baton = session->priv;
  struct deltify_etc_baton *db = apr_palloc(pool, sizeof(*db));
  apr_hash_index_t *hi;
  svn_fs_access_t *fs_access;

  db->fs = sess_baton->fs;
  db->repos = sess_baton->repos;
  db->fs_path = sess_baton->fs_path->data;
  if (! keep_locks)
    db->lock_tokens = lock_tokens;
  else
    db->lock_tokens = NULL;
  db->pool = pool;
  db->callback = callback;
  db->callback_baton = callback_baton;

  SVN_ERR(get_username(session, pool));

  /* If there are lock tokens to add, do so. */
  if (lock_tokens)
    {
      SVN_ERR(svn_fs_get_access(&fs_access, sess_baton->fs));

      /* If there is no access context, the filesystem will scream if a
         lock is needed. */      
      if (fs_access)
        {
          for (hi = apr_hash_first(pool, lock_tokens); hi;
               hi = apr_hash_next(hi))
            {
              void *val;
              const char *token;

              apr_hash_this(hi, NULL, NULL, &val);
              token = val;
              SVN_ERR(svn_fs_access_add_lock_token(fs_access, token));
            }
        }
    }
              
  /* Get the repos commit-editor */     
  SVN_ERR(svn_repos_get_commit_editor4
          (editor, edit_baton, sess_baton->repos, NULL,
           svn_path_uri_decode(sess_baton->repos_url, pool),
           sess_baton->fs_path->data,
           sess_baton->username, log_msg,
           deltify_etc, db, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
make_reporter(svn_ra_session_t *session,
              const svn_ra_reporter2_t **reporter,
              void **report_baton,
              svn_revnum_t revision,
              const char *target,
              const char *other_url,
              svn_boolean_t text_deltas,
              svn_boolean_t recurse,
              svn_boolean_t ignore_ancestry,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sbaton = session->priv;
  void *rbaton;
  int repos_url_len;
  const char *other_fs_path = NULL;
  const char *repos_url_decoded;

  /* Get the HEAD revision if one is not supplied. */
  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_ra_local__get_latest_revnum(session, &revision, pool));

  /* If OTHER_URL was provided, validate it and convert it into a
     regular filesystem path. */
  if (other_url)
    {
      other_url = svn_path_uri_decode(other_url, pool);
      repos_url_decoded = svn_path_uri_decode(sbaton->repos_url, pool);
      repos_url_len = strlen(repos_url_decoded);
      
      /* Sanity check:  the other_url better be in the same repository as
         the original session url! */
      if (strncmp(other_url, repos_url_decoded, repos_url_len) != 0)
        return svn_error_createf 
          (SVN_ERR_RA_ILLEGAL_URL, NULL,
           _("'%s'\n"
             "is not the same repository as\n"
             "'%s'"), other_url, sbaton->repos_url);

      other_fs_path = other_url + repos_url_len;
    }

  /* Pass back our reporter */
  *reporter = &ra_local_reporter;

  SVN_ERR(get_username(session, pool));
  
  /* Build a reporter baton. */
  SVN_ERR(svn_repos_begin_report(&rbaton,
                                 revision,
                                 sbaton->username,
                                 sbaton->repos, 
                                 sbaton->fs_path->data,
                                 target, 
                                 other_fs_path,
                                 text_deltas,
                                 recurse,
                                 ignore_ancestry,
                                 editor, 
                                 edit_baton,
                                 NULL,
                                 NULL,
                                 pool));
  
  /* Wrap the report baton given us by the repos layer with our own
     reporter baton. */
  *report_baton = make_reporter_baton(sbaton, rbaton, pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__do_update(svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t update_revision,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       NULL,
                       TRUE,
                       recurse,
                       FALSE,
                       update_editor,
                       update_baton,
                       pool);
}


static svn_error_t *
svn_ra_local__do_switch(svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t update_revision,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const char *switch_url,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       switch_url,
                       TRUE,
                       recurse,
                       TRUE,
                       update_editor,
                       update_baton,
                       pool);
}


static svn_error_t *
svn_ra_local__do_status(svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        const char *status_target,
                        svn_revnum_t revision,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *status_editor,
                        void *status_baton,
                        apr_pool_t *pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       revision,
                       status_target,
                       NULL,
                       FALSE,
                       recurse,
                       FALSE,
                       status_editor,
                       status_baton,
                       pool);
}


static svn_error_t *
svn_ra_local__do_diff(svn_ra_session_t *session,
                      const svn_ra_reporter2_t **reporter,
                      void **report_baton,
                      svn_revnum_t update_revision,
                      const char *update_target,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t text_deltas,
                      const char *switch_url,
                      const svn_delta_editor_t *update_editor,
                      void *update_baton,
                      apr_pool_t *pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       switch_url,
                       text_deltas,
                       recurse,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       pool);
}


static svn_error_t *
svn_ra_local__get_log(svn_ra_session_t *session,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      int limit,
                      svn_boolean_t discover_changed_paths,
                      svn_boolean_t strict_node_history,
                      svn_log_message_receiver_t receiver,
                      void *receiver_baton,
                      apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sbaton = session->priv;
  int i;
  apr_array_header_t *abs_paths =
    apr_array_make(pool, 0, sizeof(const char *));

  if (paths)
    {
      for (i = 0; i < paths->nelts; i++)
        {
          const char *abs_path = "";
          const char *relative_path = (((const char **)(paths)->elts)[i]);
          
          /* Append the relative paths to the base FS path to get an
             absolute repository path. */
          abs_path = svn_path_join(sbaton->fs_path->data, relative_path,
                                   pool);
          (*((const char **)(apr_array_push(abs_paths)))) = abs_path;
        }
    }

  return svn_repos_get_logs3(sbaton->repos,
                             abs_paths,
                             start,
                             end,
                             limit,
                             discover_changed_paths,
                             strict_node_history,
                             NULL, NULL,
                             receiver,
                             receiver_baton,
                             pool);
}


static svn_error_t *
svn_ra_local__do_check_path(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t revision,
                            svn_node_kind_t *kind,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sbaton = session->priv;
  svn_fs_root_t *root;
  const char *abs_path = sbaton->fs_path->data;

  /* ### Not sure if this counts as a workaround or not.  The
     session baton uses the empty string to mean root, and not
     sure that should change.  However, it would be better to use
     a path library function to add this separator -- hardcoding
     it is totally bogus.  See issue #559, though it may be only
     tangentially related. */
  if (abs_path[0] == '\0')
    abs_path = "/";

  /* If we were given a relative path to append, append it. */
  if (path)
    abs_path = svn_path_join(abs_path, path, pool);

  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_fs_youngest_rev(&revision, sbaton->fs, pool));
  SVN_ERR(svn_fs_revision_root(&root, sbaton->fs, revision, pool));
  SVN_ERR(svn_fs_check_path(kind, root, abs_path, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__stat(svn_ra_session_t *session,
                   const char *path,
                   svn_revnum_t revision,
                   svn_dirent_t **dirent,
                   apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sbaton = session->priv;
  svn_fs_root_t *root;
  const char *abs_path = sbaton->fs_path->data;
  
  /* ### see note above in __do_check_path() */
  if (abs_path[0] == '\0')
    abs_path = "/";

  if (path)
    abs_path = svn_path_join(abs_path, path, pool);

  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_fs_youngest_rev(&revision, sbaton->fs, pool));
  SVN_ERR(svn_fs_revision_root(&root, sbaton->fs, revision, pool));

  SVN_ERR(svn_repos_stat(dirent, root, abs_path, pool));

  return SVN_NO_ERROR;
}




static svn_error_t *
get_node_props(apr_hash_t **props,
               svn_ra_local__session_baton_t *sbaton,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  svn_revnum_t cmt_rev;
  const char *cmt_date, *cmt_author;

  /* Create a hash with props attached to the fs node. */
  SVN_ERR(svn_fs_node_proplist(props, root, path, pool));
      
  /* Now add some non-tweakable metadata to the hash as well... */
    
  /* The so-called 'entryprops' with info about CR & friends. */
  SVN_ERR(svn_repos_get_committed_info(&cmt_rev, &cmt_date,
                                       &cmt_author, root, path, pool));

  apr_hash_set(*props, 
               SVN_PROP_ENTRY_COMMITTED_REV, 
               APR_HASH_KEY_STRING, 
               svn_string_createf(pool, "%ld", cmt_rev));
  apr_hash_set(*props, 
               SVN_PROP_ENTRY_COMMITTED_DATE, 
               APR_HASH_KEY_STRING, 
               cmt_date ? svn_string_create(cmt_date, pool) : NULL);
  apr_hash_set(*props, 
               SVN_PROP_ENTRY_LAST_AUTHOR, 
               APR_HASH_KEY_STRING, 
               cmt_author ? svn_string_create(cmt_author, pool) : NULL);
  apr_hash_set(*props, 
               SVN_PROP_ENTRY_UUID,
               APR_HASH_KEY_STRING, 
               svn_string_create(sbaton->uuid, pool));

  /* We have no 'wcprops' in ra_local, but might someday. */
  
  return SVN_NO_ERROR;
}


/* Getting just one file. */
static svn_error_t *
svn_ra_local__get_file(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t revision,
                       svn_stream_t *stream,
                       svn_revnum_t *fetched_rev,
                       apr_hash_t **props,
                       apr_pool_t *pool)
{
  svn_fs_root_t *root;
  svn_stream_t *contents;
  svn_revnum_t youngest_rev;
  svn_ra_local__session_baton_t *sbaton = session->priv;
  const char *abs_path = sbaton->fs_path->data;

  /* ### Not sure if this counts as a workaround or not.  The
     session baton uses the empty string to mean root, and not
     sure that should change.  However, it would be better to use
     a path library function to add this separator -- hardcoding
     it is totally bogus.  See issue #559, though it may be only
     tangentially related. */
  if (abs_path[0] == '\0')
    abs_path = "/";

  /* If we were given a relative path to append, append it. */
  if (path)
    abs_path = svn_path_join(abs_path, path, pool);

  /* Open the revision's root. */
  if (! SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, sbaton->fs, pool));
      SVN_ERR(svn_fs_revision_root(&root, sbaton->fs, youngest_rev, pool));
      if (fetched_rev != NULL)
        *fetched_rev = youngest_rev;
    }
  else
    SVN_ERR(svn_fs_revision_root(&root, sbaton->fs, revision, pool));

  if (stream)
    {
      /* Get a stream representing the file's contents. */
      SVN_ERR(svn_fs_file_contents(&contents, root, abs_path, pool));
      
      /* Now push data from the fs stream back at the caller's stream.
         Note that this particular RA layer does not computing a
         checksum as we go, and confirming it against the repository's
         checksum when done.  That's because it calls
         svn_fs_file_contents() directly, which already checks the
         stored checksum, and all we're doing here is writing bytes in
         a loop.  Truly, Nothing Can Go Wrong :-).  But RA layers that
         go over a network should confirm the checksum. */
      SVN_ERR(svn_stream_copy(contents, stream, pool));
      SVN_ERR(svn_stream_close(contents));
    }

  /* Handle props if requested. */
  if (props)
    SVN_ERR(get_node_props(props, sbaton, root, abs_path, pool));

  return SVN_NO_ERROR;
}



/* Getting a directory's entries */
static svn_error_t *
svn_ra_local__get_dir(svn_ra_session_t *session,
                      apr_hash_t **dirents,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      const char *path,
                      svn_revnum_t revision,
                      apr_uint32_t dirent_fields,
                      apr_pool_t *pool)
{
  svn_fs_root_t *root;
  svn_revnum_t youngest_rev;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_ra_local__session_baton_t *sbaton = session->priv;
  const char *abs_path = sbaton->fs_path->data;
  apr_pool_t *subpool;

  /* ### Not sure if this counts as a workaround or not.  The
     session baton uses the empty string to mean root, and not
     sure that should change.  However, it would be better to use
     a path library function to add this separator -- hardcoding
     it is totally bogus.  See issue #559, though it may be only
     tangentially related. */
  if (abs_path[0] == '\0')
    abs_path = "/";

  /* If we were given a relative path to append, append it. */
  if (path)
    abs_path = svn_path_join(abs_path, path, pool);

  /* Open the revision's root. */
  if (! SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, sbaton->fs, pool));
      SVN_ERR(svn_fs_revision_root(&root, sbaton->fs, youngest_rev, pool));
      if (fetched_rev != NULL)
        *fetched_rev = youngest_rev;
    }
  else
    SVN_ERR(svn_fs_revision_root(&root, sbaton->fs, revision, pool));

  if (dirents)
    {
      /* Get the dir's entries. */
      SVN_ERR(svn_fs_dir_entries(&entries, root, abs_path, pool));
      
      /* Loop over the fs dirents, and build a hash of general
         svn_dirent_t's. */
      *dirents = apr_hash_make(pool);
      subpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          apr_hash_t *prophash;
          const char *datestring, *entryname, *fullpath;
          svn_fs_dirent_t *fs_entry;
          svn_dirent_t *entry = apr_pcalloc(pool, sizeof(*entry));

          svn_pool_clear(subpool);

          apr_hash_this(hi, &key, NULL, &val);
          entryname = (const char *) key;
          fs_entry = (svn_fs_dirent_t *) val;

          fullpath = svn_path_join(abs_path, entryname, subpool);
        
          if (dirent_fields & SVN_DIRENT_KIND)
            {  
              /* node kind */
              entry->kind = fs_entry->kind;
            }

          if (dirent_fields & SVN_DIRENT_SIZE)
            {          
              /* size  */
              if (entry->kind == svn_node_dir)
                entry->size = 0;
              else
                SVN_ERR(svn_fs_file_length(&(entry->size), root,
                                           fullpath, subpool));
            }

          if (dirent_fields & SVN_DIRENT_HAS_PROPS)
            { 
              /* has_props? */
              SVN_ERR(svn_fs_node_proplist(&prophash, root, fullpath,
                                           subpool));
              entry->has_props = (apr_hash_count(prophash)) ? TRUE : FALSE;
            }

          if ((dirent_fields & SVN_DIRENT_TIME)
              || (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
              || (dirent_fields & SVN_DIRENT_CREATED_REV))
            { 
              /* created_rev & friends */
              SVN_ERR(svn_repos_get_committed_info(&(entry->created_rev),
                                                   &datestring,
                                                   &(entry->last_author),
                                                   root, fullpath, subpool));
              if (datestring)
                SVN_ERR(svn_time_from_cstring(&(entry->time), datestring,
                                              pool));
              if (entry->last_author)
                entry->last_author = apr_pstrdup(pool, entry->last_author);
            }
 
          /* Store. */
          apr_hash_set(*dirents, entryname, APR_HASH_KEY_STRING, entry);
        }
      svn_pool_destroy(subpool);
    }

  /* Handle props if requested. */
  if (props)
    SVN_ERR(get_node_props(props, sbaton, root, abs_path, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_locations(svn_ra_session_t *session,
                            apr_hash_t **locations,
                            const char *relative_path,
                            svn_revnum_t peg_revision,
                            apr_array_header_t *location_revisions,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sbaton = session->priv;
  const char *abs_path;

  /* Append the relative path to the base FS path to get an
     absolute repository path. */
  abs_path = svn_path_join(sbaton->fs_path->data, relative_path, pool);

  SVN_ERR(svn_repos_trace_node_locations(sbaton->fs, locations, abs_path,
                                         peg_revision, location_revisions,
                                         NULL, NULL, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
svn_ra_local__lock(svn_ra_session_t *session,
                   apr_hash_t *path_revs,
                   const char *comment,
                   svn_boolean_t force,
                   svn_ra_lock_callback_t lock_func, 
                   void *lock_baton,
                   apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* A username is absolutely required to lock a path. */
  SVN_ERR(get_username(session, pool));

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      svn_lock_t *lock;
      const void *key;
      const char *path;
      void *val;
      svn_revnum_t *revnum;
      const char *abs_path;
      svn_error_t *err, *callback_err = NULL;
 
      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      revnum = val;

      abs_path = svn_path_join(sess->fs_path->data, path, iterpool);

      /* This wrapper will call pre- and post-lock hooks. */
      err = svn_repos_fs_lock(&lock, sess->repos, abs_path, NULL, comment,
                              FALSE /* not DAV comment */,
                              0 /* no expiration */, *revnum, force, 
                              iterpool);

      if (err && !SVN_ERR_IS_LOCK_ERROR(err))
        return err;

      if (lock_func)
        callback_err = lock_func(lock_baton, path, TRUE, err ? NULL : lock,
                                 err, iterpool);

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__unlock(svn_ra_session_t *session,
                     apr_hash_t *path_tokens,
                     svn_boolean_t force,
                     svn_ra_lock_callback_t lock_func, 
                     void *lock_baton,
                     apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* A username is absolutely required to unlock a path. */
  SVN_ERR(get_username(session, pool));

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;
      void *val;
      const char *abs_path, *token;
      svn_error_t *err, *callback_err = NULL;
 
      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      /* Since we can't store NULL values in a hash, we turn "" to
         NULL here. */
      if (strcmp(val, "") != 0)
        token = val;
      else
        token = NULL;

      abs_path = svn_path_join(sess->fs_path->data, path, iterpool);

      /* This wrapper will call pre- and post-unlock hooks. */
      err = svn_repos_fs_unlock(sess->repos, abs_path, token, force,
                                iterpool);

      if (err && !SVN_ERR_IS_UNLOCK_ERROR(err))
        return err;

      if (lock_func)
        callback_err = lock_func(lock_baton, path, FALSE, NULL, err, iterpool);

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}



static svn_error_t *
svn_ra_local__get_lock(svn_ra_session_t *session,
                       svn_lock_t **lock,
                       const char *path,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path;

  /* Get the absolute path. */
  abs_path = svn_path_join(sess->fs_path->data, path, pool);

  SVN_ERR(svn_fs_get_lock(lock, sess->fs, abs_path, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
svn_ra_local__get_locks(svn_ra_session_t *session,
                        apr_hash_t **locks,
                        const char *path,
                        apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path;

  /* Get the absolute path. */
  abs_path = svn_path_join(sess->fs_path->data, path, pool);

  /* Kinda silly to call the repos wrapper, since we have no authz
     func to give it.  But heck, why not. */
  SVN_ERR(svn_repos_fs_get_locks(locks, sess->repos, abs_path,
                                 NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__replay(svn_ra_session_t *session,
                     svn_revnum_t revision,
                     svn_revnum_t low_water_mark,
                     svn_boolean_t send_deltas,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  svn_fs_root_t *root;

  SVN_ERR(svn_fs_revision_root(&root, svn_repos_fs(sess->repos),
                               revision, pool));

  SVN_ERR(svn_repos_replay2(root, sess->fs_path->data, low_water_mark,
                            send_deltas, editor, edit_baton, NULL, NULL,
                            pool));

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------*/

static const svn_version_t *
ra_local_version(void)
{
  SVN_VERSION_BODY;
}

/** The ra_vtable **/

static const svn_ra__vtable_t ra_local_vtable =
{
  ra_local_version,
  svn_ra_local__get_description,
  svn_ra_local__get_schemes,
  svn_ra_local__open,
  svn_ra_local__reparent,
  svn_ra_local__get_latest_revnum,
  svn_ra_local__get_dated_revision,
  svn_ra_local__change_rev_prop,
  svn_ra_local__rev_proplist,
  svn_ra_local__rev_prop,
  svn_ra_local__get_commit_editor,
  svn_ra_local__get_file,
  svn_ra_local__get_dir,
  svn_ra_local__do_update,
  svn_ra_local__do_switch,
  svn_ra_local__do_status,
  svn_ra_local__do_diff,
  svn_ra_local__get_log,
  svn_ra_local__do_check_path,
  svn_ra_local__stat,
  svn_ra_local__get_uuid,
  svn_ra_local__get_repos_root,
  svn_ra_local__get_locations,
  svn_ra_local__get_file_revs,
  svn_ra_local__lock,
  svn_ra_local__unlock,
  svn_ra_local__get_lock,
  svn_ra_local__get_locks,
  svn_ra_local__replay,
};


/*----------------------------------------------------------------*/

/** The One Public Routine, called by libsvn_ra **/

svn_error_t *
svn_ra_local__init(const svn_version_t *loader_version,
                   const svn_ra__vtable_t **vtable,
                   apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_repos", svn_repos_version },
      { "svn_fs",    svn_fs_version },
      { NULL, NULL }
    };

  
  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Unsupported RA loader version (%d) for "
                               "ra_local"),
                             loader_version->major);

  SVN_ERR(svn_ver_check_list(ra_local_version(), checklist));

#ifndef SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL
  /* This assumes that POOL was the pool used to load the dso. */
  SVN_ERR(svn_fs_initialize(pool));
#endif

  *vtable = &ra_local_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for the 1.1 and before API. */
#define NAME "ra_local"
#define DESCRIPTION RA_LOCAL_DESCRIPTION
#define VTBL ra_local_vtable
#define INITFUNC svn_ra_local__init
#define COMPAT_INITFUNC svn_ra_local_init
#include "../libsvn_ra/wrapper_template.h"
