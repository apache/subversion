/* commit.c --- editor for committing changes to a filesystem.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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


#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_md5.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_md5.h"
#include "svn_props.h"
#include "svn_private_config.h"



/*** Editor batons. ***/

struct edit_baton
{
  apr_pool_t *pool;

  /** Supplied when the editor is created: **/

  /* The user doing the commit.  Presumably, some higher layer has
     already authenticated this user. */
  const char *user;

  /* Commit message for this commit. */
  const char *log_msg;

  /* Callback to run when the commit is done. */
  svn_commit_callback2_t commit_callback;
  void *commit_callback_baton;

  /* Callback to check authorizations on paths. */
  svn_repos_authz_callback_t authz_callback;
  void *authz_baton;

  /* The already-open svn repository to commit to. */
  svn_repos_t *repos;

  /* URL to the root of the open repository. */
  const char *repos_url;

  /* The name of the repository (here for convenience). */
  const char *repos_name;

  /* The filesystem associated with the REPOS above (here for
     convenience). */
  svn_fs_t *fs;

  /* Location in fs where the edit will begin. */
  const char *base_path;

  /* Does this set of interfaces 'own' the commit transaction? */
  svn_boolean_t txn_owner;

  /* svn transaction associated with this edit (created in
     open_root, or supplied by the public API caller). */
  svn_fs_txn_t *txn;

  /** Filled in during open_root: **/

  /* The name of the transaction. */
  const char *txn_name;

  /* The object representing the root directory of the svn txn. */
  svn_fs_root_t *txn_root;

  /** Filled in when the edit is closed: **/

  /* The new revision created by this commit. */
  svn_revnum_t *new_rev;

  /* The date (according to the repository) of this commit. */
  const char **committed_date;

  /* The author (also according to the repository) of this commit. */
  const char **committed_author;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;
  const char *path; /* the -absolute- path to this dir in the fs */
  svn_revnum_t base_rev;        /* the revision I'm based on  */
  svn_boolean_t was_copied; /* was this directory added with history? */
  apr_pool_t *pool; /* my personal pool, in which I am allocated. */
};


struct file_baton
{
  struct edit_baton *edit_baton;
  const char *path; /* the -absolute- path to this file in the fs */
};



/* Create and return a generic out-of-dateness error. */
static svn_error_t *
out_of_date(const char *path, const char *txn_name)
{
  return svn_error_createf(SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
                           _("Out of date: '%s' in transaction '%s'"),
                           path, txn_name);
}



/* If EDITOR_BATON contains a valid authz callback, verify that the
   REQUIRED access to PATH in ROOT is authorized.  Return an error
   appropriate for throwing out of the commit editor with SVN_ERR.  If
   no authz callback is present in EDITOR_BATON, then authorize all
   paths.  Use POOL for temporary allocation only. */
static svn_error_t *
check_authz(struct edit_baton *editor_baton, const char *path,
            svn_fs_root_t *root, svn_repos_authz_access_t required,
            apr_pool_t *pool)
{
  if (editor_baton->authz_callback)
    {
      svn_boolean_t allowed;

      SVN_ERR(editor_baton->authz_callback(required, &allowed, root, path,
                                           editor_baton->authz_baton, pool));
      if (!allowed)
        return svn_error_create(required & svn_authz_write ?
                                SVN_ERR_AUTHZ_UNWRITABLE :
                                SVN_ERR_AUTHZ_UNREADABLE,
                                NULL, "Access denied");
    }

  return SVN_NO_ERROR;
}


/*** Editor functions ***/

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct dir_baton *dirb;
  struct edit_baton *eb = edit_baton;
  svn_revnum_t youngest;

  /* Ignore BASE_REVISION.  We always build our transaction against
     HEAD.  However, we will keep it in our dir baton for out of
     dateness checks.  */
  SVN_ERR(svn_fs_youngest_rev(&youngest, eb->fs, eb->pool));

  /* Unless we've been instructed to use a specific transaction, we'll
     make our own. */
  if (eb->txn_owner)
    {
      SVN_ERR(svn_repos_fs_begin_txn_for_commit(&(eb->txn),
                                                eb->repos, 
                                                youngest,
                                                eb->user, 
                                                eb->log_msg,
                                                eb->pool));
    }
  else /* Even if we aren't the owner of the transaction, we might
          have been instructed to set some properties. */
    {
      svn_string_t propval;
      if (eb->user)
        {
          propval.data = eb->user;
          propval.len = strlen(eb->user);
          SVN_ERR(svn_fs_change_txn_prop(eb->txn, SVN_PROP_REVISION_AUTHOR,
                                         &propval, pool));
        }
      if (eb->log_msg)
        {
          propval.data = eb->log_msg;
          propval.len = strlen(eb->log_msg);
          SVN_ERR(svn_fs_change_txn_prop(eb->txn, SVN_PROP_REVISION_LOG,
                                         &propval, pool));
        }
    }
  SVN_ERR(svn_fs_txn_name(&(eb->txn_name), eb->txn, eb->pool));
  SVN_ERR(svn_fs_txn_root(&(eb->txn_root), eb->txn, eb->pool));

  /* Create a root dir baton.  The `base_path' field is an -absolute-
     path in the filesystem, upon which all further editor paths are
     based. */
  dirb = apr_pcalloc(pool, sizeof(*dirb));
  dirb->edit_baton = edit_baton;
  dirb->parent = NULL;
  dirb->pool = pool;
  dirb->was_copied = FALSE;
  dirb->path = apr_pstrdup(pool, eb->base_path);
  dirb->base_rev = base_revision;

  *root_baton = dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *parent = parent_baton;
  struct edit_baton *eb = parent->edit_baton;
  svn_node_kind_t kind;
  svn_revnum_t cr_rev;
  svn_repos_authz_access_t required = svn_authz_write;
  const char *full_path = svn_path_join(eb->base_path, path, pool);

  /* Check PATH in our transaction.  */
  SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, pool));

  /* Deletion requires a recursive write access, as well as write
     access to the parent directory. */
  if (kind == svn_node_dir)
    required |= svn_authz_recursive;
  SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                      required, pool));
  SVN_ERR(check_authz(eb, parent->path, eb->txn_root,
                      svn_authz_write, pool));

  /* If PATH doesn't exist in the txn, that's fine (merge
     allows this). */
  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* Now, make sure we're deleting the node we *think* we're
     deleting, else return an out-of-dateness error. */
  SVN_ERR(svn_fs_node_created_rev(&cr_rev, eb->txn_root, full_path, pool));
  if (SVN_IS_VALID_REVNUM(revision) && (revision < cr_rev))
    return out_of_date(full_path, eb->txn_name);
  
  /* This routine is a mindless wrapper.  We call svn_fs_delete_tree
     because that will delete files and recursively delete
     directories.  */
  return svn_fs_delete(eb->txn_root, full_path, pool);
}




static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copy_path,
              svn_revnum_t copy_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *new_dirb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *full_path = svn_path_join(eb->base_path, path, pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_boolean_t was_copied = FALSE;

  /* Sanity check. */  
  if (copy_path && (! SVN_IS_VALID_REVNUM(copy_revision)))
    return svn_error_createf 
      (SVN_ERR_FS_GENERAL, NULL,
       _("Got source path but no source revision for '%s'"), full_path);

  if (copy_path)
    {
      const char *fs_path;
      svn_fs_root_t *copy_root;
      svn_node_kind_t kind;
      int repos_url_len;

      /* Copy requires recursive write access to the destination path
         and write access to the parent path. */
      SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                          svn_authz_write | svn_authz_recursive,
                          subpool));
      SVN_ERR(check_authz(eb, pb->path, eb->txn_root,
                          svn_authz_write, subpool));

      /* Check PATH in our transaction.  Make sure it does not exist
         unless its parent directory was copied (in which case, the
         thing might have been copied in as well), else return an
         out-of-dateness error. */
      SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, subpool));
      if ((kind != svn_node_none) && (! pb->was_copied))
        return out_of_date(full_path, eb->txn_name);

      /* For now, require that the url come from the same repository
         that this commit is operating on. */
      copy_path = svn_path_uri_decode(copy_path, subpool);
      repos_url_len = strlen(eb->repos_url);
      if (strncmp(copy_path, eb->repos_url, repos_url_len) != 0)
        return svn_error_createf 
          (SVN_ERR_FS_GENERAL, NULL,
           _("Source url '%s' is from different repository"), copy_path);

      fs_path = apr_pstrdup(subpool, copy_path + repos_url_len);

      /* Now use the "fs_path" as an absolute path within the
         repository to make the copy from. */      
      SVN_ERR(svn_fs_revision_root(&copy_root, eb->fs,
                                   copy_revision, subpool));

      /* Copy also requires recursive read access to the source
         path. */
      SVN_ERR(check_authz(eb, fs_path, copy_root,
                          svn_authz_read | svn_authz_recursive,
                          subpool));

      SVN_ERR(svn_fs_copy(copy_root, fs_path,
                          eb->txn_root, full_path, subpool));
      was_copied = TRUE;
    }
  else
    {
      /* No ancestry given, just make a new directory.  We don't
         bother with an out-of-dateness check here because
         svn_fs_make_dir will error out if PATH already exists.
         Verify write access to the full path and the parent
         directory. */
      SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                          svn_authz_write, subpool));
      SVN_ERR(check_authz(eb, pb->path, eb->txn_root,
                          svn_authz_write, subpool));
      SVN_ERR(svn_fs_make_dir(eb->txn_root, full_path, subpool));
    }

  /* Cleanup our temporary subpool. */
  svn_pool_destroy(subpool);

  /* Build a new dir baton for this directory. */
  new_dirb = apr_pcalloc(pool, sizeof(*new_dirb));
  new_dirb->edit_baton = eb;
  new_dirb->parent = pb;
  new_dirb->pool = pool;
  new_dirb->path = full_path;
  new_dirb->was_copied = was_copied;
  new_dirb->base_rev = SVN_INVALID_REVNUM;

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *new_dirb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_node_kind_t kind;
  const char *full_path = svn_path_join(eb->base_path, path, pool);

  /* Check PATH in our transaction.  If it does not exist,
     return a 'Path not present' error. */
  SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                             _("Path '%s' not present"),
                             path);

  /* Build a new dir baton for this directory */
  new_dirb = apr_pcalloc(pool, sizeof(*new_dirb));
  new_dirb->edit_baton = eb;
  new_dirb->parent = pb;
  new_dirb->pool = pool;
  new_dirb->path = full_path;
  new_dirb->was_copied = pb->was_copied;
  new_dirb->base_rev = base_revision;

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;

  /* Check for write authorization. */
  SVN_ERR(check_authz(fb->edit_baton, fb->path,
                      fb->edit_baton->txn_root,
                      svn_authz_write, pool));

  return svn_fs_apply_textdelta(handler, handler_baton, 
                                fb->edit_baton->txn_root, 
                                fb->path,
                                base_checksum,
                                NULL,
                                pool);
}




static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copy_path,
         svn_revnum_t copy_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *full_path = svn_path_join(eb->base_path, path, pool);
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Sanity check. */  
  if (copy_path && (! SVN_IS_VALID_REVNUM(copy_revision)))
    return svn_error_createf 
      (SVN_ERR_FS_GENERAL, NULL,
       _("Got source path but no source revision for '%s'"), full_path);

  if (copy_path)
    {      
      const char *fs_path;
      svn_fs_root_t *copy_root;
      svn_node_kind_t kind;
      int repos_url_len;

      /* Copy requires recursive write to the destination path and
         parent path. */
      SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                          svn_authz_write, subpool));
      SVN_ERR(check_authz(eb, pb->path, eb->txn_root,
                          svn_authz_write, subpool));

      /* Check PATH in our transaction.  Make sure it does not exist
         unless its parent directory was copied (in which case, the
         thing might have been copied in as well), else return an
         out-of-dateness error. */
      SVN_ERR(svn_fs_check_path(&kind, eb->txn_root, full_path, subpool));
      if ((kind != svn_node_none) && (! pb->was_copied))
        return out_of_date(full_path, eb->txn_name);

      /* For now, require that the url come from the same repository
         that this commit is operating on. */
      copy_path = svn_path_uri_decode(copy_path, subpool);
      repos_url_len = strlen(eb->repos_url);
      if (strncmp(copy_path, eb->repos_url, repos_url_len) != 0)
            return svn_error_createf 
              (SVN_ERR_FS_GENERAL, NULL,
               _("Source url '%s' is from different repository"), copy_path);
      
      fs_path = apr_pstrdup(subpool, copy_path + repos_url_len);

      /* Now use the "fs_path" as an absolute path within the
         repository to make the copy from. */      
      SVN_ERR(svn_fs_revision_root(&copy_root, eb->fs,
                                   copy_revision, subpool));

      /* Copy also requires read access to the source */
      SVN_ERR(check_authz(eb, fs_path, copy_root,
                          svn_authz_read, subpool));

      SVN_ERR(svn_fs_copy(copy_root, fs_path, 
                          eb->txn_root, full_path, subpool));
    }
  else
    {
      /* No ancestry given, just make a new, empty file.  Note that we
         don't perform an existence check here like the copy-from case
         does -- that's because svn_fs_make_file() already errors out
         if the file already exists.  Verify write access to the full
         path and to the parent. */
      SVN_ERR(check_authz(eb, full_path, eb->txn_root, svn_authz_write,
                          subpool));
      SVN_ERR(check_authz(eb, pb->path, eb->txn_root, svn_authz_write,
                          subpool));
      SVN_ERR(svn_fs_make_file(eb->txn_root, full_path, subpool));
    }

  /* Cleanup our temporary subpool. */
  svn_pool_destroy(subpool);

  /* Build a new file baton */
  new_fb = apr_pcalloc(pool, sizeof(*new_fb));
  new_fb->edit_baton = eb;
  new_fb->path = full_path;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}




static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_revnum_t cr_rev;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *full_path = svn_path_join(eb->base_path, path, pool);

  /* Check for read authorization. */
  SVN_ERR(check_authz(eb, full_path, eb->txn_root,
                      svn_authz_read, subpool));

  /* Get this node's creation revision (doubles as an existence check). */
  SVN_ERR(svn_fs_node_created_rev(&cr_rev, eb->txn_root, full_path, 
                                  subpool));
  
  /* If the node our caller has is an older revision number than the
     one in our transaction, return an out-of-dateness error. */
  if (SVN_IS_VALID_REVNUM(base_revision) && (base_revision < cr_rev))
    return out_of_date(full_path, eb->txn_name);

  /* Build a new file baton */
  new_fb = apr_pcalloc(pool, sizeof(*new_fb));
  new_fb->edit_baton = eb;
  new_fb->path = full_path;

  *file_baton = new_fb;

  /* Destory the work subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  /* Check for write authorization. */
  SVN_ERR(check_authz(eb, fb->path, eb->txn_root,
                      svn_authz_write, pool));

  return svn_repos_fs_change_node_prop(eb->txn_root, fb->path, 
                                       name, value, pool);
}


static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  if (text_checksum)
    {
      unsigned char digest[APR_MD5_DIGESTSIZE];
      const char *hex_digest;

      SVN_ERR(svn_fs_file_md5_checksum
              (digest, fb->edit_baton->txn_root, fb->path, pool));
      hex_digest = svn_md5_digest_to_cstring(digest, pool);

      if (hex_digest && strcmp(text_checksum, hex_digest) != 0)
        {
          return svn_error_createf
            (SVN_ERR_CHECKSUM_MISMATCH, NULL,
             _("Checksum mismatch for resulting fulltext\n"
               "(%s):\n"
               "   expected checksum:  %s\n"
               "   actual checksum:    %s\n"),
             fb->path, text_checksum, hex_digest);
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  /* Check for write authorization. */
  SVN_ERR(check_authz(eb, db->path, eb->txn_root,
                      svn_authz_write, pool));

  if (SVN_IS_VALID_REVNUM(db->base_rev))
    {
      /* Subversion rule:  propchanges can only happen on a directory
         which is up-to-date. */
      svn_revnum_t created_rev;
      SVN_ERR(svn_fs_node_created_rev(&created_rev,
                                      eb->txn_root, db->path, pool));

      if (db->base_rev < created_rev)
        return out_of_date(db->path, eb->txn_name);
    }

  return svn_repos_fs_change_node_prop(eb->txn_root, db->path, 
                                       name, value, pool);
}



static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;
  svn_error_t *err;
  const char *conflict;
  char *post_commit_err = NULL;

  /* If no transaction has been created (ie. if open_root wasn't
     called before close_edit), abort the operation here with an
     error. */
  if (! eb->txn)
    return svn_error_create(SVN_ERR_REPOS_BAD_ARGS, NULL,
                            "No valid transaction supplied to close_edit");

  /* Commit. */
  err = svn_repos_fs_commit_txn(&conflict, eb->repos, 
                                &new_revision, eb->txn, pool);

  /* We want to abort the transaction *unless* the error code tells us
     the commit succeeded and something just went wrong in post-commit. */
  if (err && (err->apr_err != SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED))
    {
      /* ### todo: we should check whether it really was a conflict,
         and return the conflict info if so? */

      /* If the commit failed, it's *probably* due to a conflict --
         that is, the txn being out-of-date.  The filesystem gives us
         the ability to continue diddling the transaction and try
         again; but let's face it: that's not how the cvs or svn works
         from a user interface standpoint.  Thus we don't make use of
         this fs feature (for now, at least.)

         So, in a nutshell: svn commits are an all-or-nothing deal.
         Each commit creates a new fs txn which either succeeds or is
         aborted completely.  No second chances;  the user simply
         needs to update and commit again  :)

         We ignore the possible error result from svn_fs_abort_txn();
         it's more important to return the original error. */
      svn_error_clear(svn_fs_abort_txn(eb->txn, pool));
      return err;
    }
  else if (err)
    {
      /* Post-commit hook's failure output can be passed back to the
         client. However, this cannot be a commit failure. Hence
         passing back the post-commit error message as a string to
         be displayed as a warning. */
      if (err->child && err->child->message)
        post_commit_err = apr_pstrdup(pool, err->child->message) ;
  
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  
  /* Pass new revision information to the caller's callback. */
  {
    svn_string_t *date, *author;
    svn_error_t *err2;
    svn_commit_info_t *commit_info;

    /* Even if there was a post-commit hook failure, it's more serious
       if one of the calls here fails, so we explicitly check for errors
       here, while saving the possible post-commit error for later. */

    err2 = svn_fs_revision_prop(&date, svn_repos_fs(eb->repos),
                                new_revision, SVN_PROP_REVISION_DATE,
                                pool);
    if (! err2)
      err2 =  svn_fs_revision_prop(&author, svn_repos_fs(eb->repos),
                                   new_revision, SVN_PROP_REVISION_AUTHOR,
                                   pool);

    if (! err2)
      {
        commit_info = svn_create_commit_info(pool);

        /* fill up the svn_commit_info structure */
        commit_info->revision = new_revision;
        commit_info->date = date ? date->data : NULL;
        commit_info->author = author ? author->data : NULL;
        commit_info->post_commit_err = post_commit_err;
        err2 = (*eb->commit_callback)(commit_info, 
                                      eb->commit_callback_baton,
                                      pool);
        if (err2)
          {
            svn_error_clear(err);
            return err2;
          }
      }
  }

  return err;
}


static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  if ((! eb->txn) || (! eb->txn_owner))
    return SVN_NO_ERROR;
  return svn_fs_abort_txn(eb->txn, pool);
}



/*** Public interfaces. ***/

svn_error_t *
svn_repos_get_commit_editor4(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url,
                             const char *base_path,
                             const char *user,
                             const char *log_msg,
                             svn_commit_callback2_t callback,
                             void *callback_baton,
                             svn_repos_authz_callback_t authz_callback,
                             void *authz_baton,
                             apr_pool_t *pool)
{
  svn_delta_editor_t *e;
  apr_pool_t *subpool = svn_pool_create(pool);
  struct edit_baton *eb;

  /* Do a global authz access lookup.  Users with no write access
     whatsoever to the repository don't get a commit editor. */
  if (authz_callback)
    {
      svn_boolean_t allowed;

      SVN_ERR(authz_callback(svn_authz_write, &allowed, NULL, NULL,
                             authz_baton, pool));
      if (!allowed)
        return svn_error_create(SVN_ERR_AUTHZ_UNWRITABLE, NULL,
                                "Not authorized to open a commit editor.");
    }

  /* Allocate the structures. */
  e = svn_delta_default_editor(pool);
  eb = apr_pcalloc(subpool, sizeof(*eb));

  /* Set up the editor. */
  e->open_root         = open_root;
  e->delete_entry      = delete_entry;
  e->add_directory     = add_directory;
  e->open_directory    = open_directory;
  e->change_dir_prop   = change_dir_prop;
  e->add_file          = add_file;
  e->open_file         = open_file;
  e->close_file        = close_file;
  e->apply_textdelta   = apply_textdelta;
  e->change_file_prop  = change_file_prop;
  e->close_edit        = close_edit;
  e->abort_edit        = abort_edit;

  /* Set up the edit baton. */
  eb->pool = subpool;
  eb->user = user ? apr_pstrdup(subpool, user) : NULL;
  eb->log_msg = apr_pstrdup(subpool, log_msg);
  eb->commit_callback = callback;
  eb->commit_callback_baton = callback_baton;
  eb->authz_callback = authz_callback;
  eb->authz_baton = authz_baton;
  eb->base_path = apr_pstrdup(subpool, base_path);
  eb->repos = repos;
  eb->repos_url = repos_url;
  eb->repos_name = svn_path_basename(svn_repos_path(repos, subpool),
                                     subpool);
  eb->fs = svn_repos_fs(repos);
  eb->txn = txn;
  eb->txn_owner = txn ? FALSE : TRUE;

  *edit_baton = eb;
  *editor = e;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_get_commit_editor3(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url,
                             const char *base_path,
                             const char *user,
                             const char *log_msg,
                             svn_commit_callback_t callback,
                             void *callback_baton,
                             svn_repos_authz_callback_t authz_callback,
                             void *authz_baton,
                             apr_pool_t *pool)
{
  svn_commit_callback2_t callback2;
  void *callback2_baton;

  svn_compat_wrap_commit_callback(&callback2, &callback2_baton,
                                  callback, callback_baton,
                                  pool);

  return svn_repos_get_commit_editor4(editor, edit_baton, repos, txn,
                                      repos_url, base_path, user,
                                      log_msg, callback2,
                                      callback2_baton, authz_callback,
                                      authz_baton, pool);
}


svn_error_t *
svn_repos_get_commit_editor2(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url,
                             const char *base_path,
                             const char *user,
                             const char *log_msg,
                             svn_commit_callback_t callback,
                             void *callback_baton,
                             apr_pool_t *pool)
{
  return svn_repos_get_commit_editor3(editor, edit_baton, repos, txn,
                                      repos_url, base_path, user,
                                      log_msg, callback, callback_baton,
                                      NULL, NULL, pool);
}


svn_error_t *
svn_repos_get_commit_editor(const svn_delta_editor_t **editor,
                            void **edit_baton,
                            svn_repos_t *repos,
                            const char *repos_url,
                            const char *base_path,
                            const char *user,
                            const char *log_msg,
                            svn_commit_callback_t callback,
                            void *callback_baton,
                            apr_pool_t *pool)
{
  return svn_repos_get_commit_editor2(editor, edit_baton, repos, NULL,
                                      repos_url, base_path, user,
                                      log_msg, callback,
                                      callback_baton, pool);
}
