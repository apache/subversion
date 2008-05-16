/*
 * copy.c:  wc 'copy' functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#include <string.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"
#include "props.h"
#include "translate.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* Helper function for svn_wc_copy2() which handles WC->WC copying of
   files which are scheduled for addition or unversioned.

   Copy file SRC_PATH to DST_BASENAME in DST_PARENT_ACCESS.

   DST_PARENT_ACCESS is a 0 depth locked access for a versioned directory
   in the same WC as SRC_PATH.

   If SRC_IS_ADDED is true then SRC_PATH is scheduled for addition and
   DST_BASENAME will also be scheduled for addition.

   If SRC_IS_ADDED is false then SRC_PATH is the unversioned child
   file of a versioned or added parent and DST_BASENAME is simply copied.

   Use POOL for all necessary allocations.
*/
static svn_error_t *
copy_added_file_administratively(const char *src_path,
                                 svn_boolean_t src_is_added,
                                 svn_wc_adm_access_t *dst_parent_access,
                                 const char *dst_basename,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 svn_wc_notify_func2_t notify_func,
                                 void *notify_baton,
                                 apr_pool_t *pool)
{
  const char *dst_path
    = svn_path_join(svn_wc_adm_access_path(dst_parent_access),
                    dst_basename, pool);

  /* Copy this file and possibly put it under version control. */
  SVN_ERR(svn_io_copy_file(src_path, dst_path, TRUE, pool));

  if (src_is_added)
    {
      SVN_ERR(svn_wc_add3(dst_path, dst_parent_access, svn_depth_infinity,
                          NULL, SVN_INVALID_REVNUM, cancel_func,
                          cancel_baton, notify_func,
                          notify_baton, pool));
    }

  return SVN_NO_ERROR;
}


/* Helper function for svn_wc_copy2() which handles WC->WC copying of
   directories which are scheduled for addition or unversioned.

   Recursively copy directory SRC_PATH and its children, excluding
   administrative directories, to DST_BASENAME in DST_PARENT_ACCESS.

   DST_PARENT_ACCESS is a 0 depth locked access for a versioned directory
   in the same WC as SRC_PATH.

   SRC_ACCESS is a -1 depth access for SRC_PATH

   If SRC_IS_ADDED is true then SRC_PATH is scheduled for addition and
   DST_BASENAME will also be scheduled for addition.

   If SRC_IS_ADDED is false then SRC_PATH is the unversioned child
   directory of a versioned or added parent and DST_BASENAME is simply
   copied.

   Use POOL for all necessary allocations.
*/
static svn_error_t *
copy_added_dir_administratively(const char *src_path,
                                svn_boolean_t src_is_added,
                                svn_wc_adm_access_t *dst_parent_access,
                                svn_wc_adm_access_t *src_access,
                                const char *dst_basename,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                svn_wc_notify_func2_t notify_func,
                                void *notify_baton,
                                apr_pool_t *pool)
{
  const char *dst_parent = svn_wc_adm_access_path(dst_parent_access);

  if (! src_is_added)
    {
      /* src_path is the top of an unversioned tree, just copy
         the whole thing and we are done. */
      SVN_ERR(svn_io_copy_dir_recursively(src_path, dst_parent, dst_basename,
                                          TRUE, cancel_func, cancel_baton,
                                          pool));
    }
  else
    {
      const svn_wc_entry_t *entry;
      svn_wc_adm_access_t *dst_child_dir_access;
      svn_wc_adm_access_t *src_child_dir_access;
      apr_dir_t *dir;
      apr_finfo_t this_entry;
      svn_error_t *err;
      apr_pool_t *subpool;
      apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;
      /* The 'dst_path' is simply dst_parent/dst_basename */
      const char *dst_path = svn_path_join(dst_parent, dst_basename, pool);

      /* Check cancellation; note that this catches recursive calls too. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* "Copy" the dir dst_path and schedule it, and possibly
         its children, for addition. */
      SVN_ERR(svn_io_dir_make(dst_path, APR_OS_DEFAULT, pool));

      /* Add the directory, adding locking access for dst_path
         to dst_parent_access at the same time. */
      SVN_ERR(svn_wc_add3(dst_path, dst_parent_access, svn_depth_infinity, NULL,
                          SVN_INVALID_REVNUM, cancel_func, cancel_baton,
                          notify_func, notify_baton, pool));

      /* Get the accesses for the newly added dir and its source, we'll
         need both to process any of SRC_PATHS's children below. */
      SVN_ERR(svn_wc_adm_retrieve(&dst_child_dir_access, dst_parent_access,
                                  dst_path, pool));
      SVN_ERR(svn_wc_adm_retrieve(&src_child_dir_access, src_access,
                                  src_path, pool));

      SVN_ERR(svn_io_dir_open(&dir, src_path, pool));

      subpool = svn_pool_create(pool);

      /* Read src_path's entries one by one. */
      while (1)
        {
          const char *src_fullpath;

          svn_pool_clear(subpool);

          err = svn_io_dir_read(&this_entry, flags, dir, subpool);

          if (err)
            {
              /* Check if we're done reading the dir's entries. */
              if (APR_STATUS_IS_ENOENT(err->apr_err))
                {
                  apr_status_t apr_err;

                  svn_error_clear(err);
                  apr_err = apr_dir_close(dir);
                  if (apr_err)
                    return svn_error_wrap_apr(apr_err,
                                              _("Can't close "
                                                "directory '%s'"),
                                              svn_path_local_style(src_path,
                                                                   subpool));
                  break;
                }
              else
                {
                  return svn_error_createf(err->apr_err, err,
                                           _("Error during recursive copy "
                                             "of '%s'"),
                                           svn_path_local_style(src_path,
                                                            subpool));
                }
            }

          /* Skip entries for this dir and its parent.  */
          if (this_entry.name[0] == '.'
              && (this_entry.name[1] == '\0'
                  || (this_entry.name[1] == '.'
                      && this_entry.name[2] == '\0')))
            continue;

          /* Check cancellation so you can cancel during an
           * add of a directory with lots of files. */
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          /* Skip over SVN admin directories. */
          if (svn_wc_is_adm_dir(this_entry.name, subpool))
            continue;

          /* Construct the full path of the entry. */
          src_fullpath = svn_path_join(src_path, this_entry.name, subpool);

          SVN_ERR(svn_wc_entry(&entry, src_fullpath, src_child_dir_access,
                               TRUE, subpool));

          /* Recurse on directories; add files; ignore the rest. */
          if (this_entry.filetype == APR_DIR)
            {
              SVN_ERR(copy_added_dir_administratively(src_fullpath,
                                                      entry ? TRUE : FALSE,
                                                      dst_child_dir_access,
                                                      src_child_dir_access,
                                                      this_entry.name,
                                                      cancel_func,
                                                      cancel_baton,
                                                      notify_func,
                                                      notify_baton,
                                                      subpool));
            }
          else if (this_entry.filetype != APR_UNKFILE)
            {
              SVN_ERR(copy_added_file_administratively(src_fullpath,
                                                       entry ? TRUE : FALSE,
                                                       dst_child_dir_access,
                                                       this_entry.name,
                                                       cancel_func,
                                                       cancel_baton,
                                                       notify_func,
                                                       notify_baton,
                                                       subpool));
            }

        } /* End while(1) loop */

    svn_pool_destroy(subpool);

  } /* End else src_is_added. */

  return SVN_NO_ERROR;
}


/* Helper function for copy_file_administratively() and
   copy_dir_administratively().  Determines the COPYFROM_URL and
   COPYFROM_REV of a file or directory SRC_PATH which is the descendant
   of an explicitly moved or copied directory that has not been committed.
*/
static svn_error_t *
get_copyfrom_url_rev_via_parent(const char *src_path,
                                const char **copyfrom_url,
                                svn_revnum_t *copyfrom_rev,
                                svn_wc_adm_access_t *src_access,
                                apr_pool_t *pool)
{
  const char *parent_path = svn_path_dirname(src_path, pool);
  const char *rest = svn_path_basename(src_path, pool);
  *copyfrom_url = NULL;

  while (! *copyfrom_url)
    {
      svn_wc_adm_access_t *parent_access;
      const svn_wc_entry_t *entry;

      /* Don't look for parent_path in src_access if it can't be
         there... */
      if (svn_path_is_ancestor(svn_wc_adm_access_path(src_access),
                               parent_path))
        {
          SVN_ERR(svn_wc_adm_retrieve(&parent_access, src_access,
                                      parent_path, pool));
          SVN_ERR(svn_wc__entry_versioned(&entry, parent_path, parent_access,
                                         FALSE, pool));
        }
      else /* ...get access for parent_path instead. */
        {
          SVN_ERR(svn_wc_adm_probe_open3(&parent_access, NULL,
                                         parent_path, FALSE, -1,
                                         NULL, NULL, pool));
          SVN_ERR(svn_wc__entry_versioned(&entry, parent_path, parent_access,
                                         FALSE, pool));
          SVN_ERR(svn_wc_adm_close(parent_access));
        }

      if (entry->copyfrom_url)
        {
          *copyfrom_url = svn_path_join(entry->copyfrom_url, rest,
                                        pool);
          *copyfrom_rev = entry->copyfrom_rev;
        }
      else
        {
          rest = svn_path_join(svn_path_basename(parent_path, pool),
                               rest, pool);
          parent_path = svn_path_dirname(parent_path, pool);
        }
    }

  return SVN_NO_ERROR;
}

/* A helper for copy_file_administratively() which sets *COPYFROM_URL
   and *COPYFROM_REV appropriately (possibly to NULL/SVN_INVALID_REVNUM).
   DST_ENTRY may be NULL. */
static APR_INLINE svn_error_t *
determine_copyfrom_info(const char **copyfrom_url, svn_revnum_t *copyfrom_rev,
                        const char *src_path, svn_wc_adm_access_t *src_access,
                        const svn_wc_entry_t *src_entry,
                        const svn_wc_entry_t *dst_entry, apr_pool_t *pool)
{
  const char *url;
  svn_revnum_t rev;

  if (src_entry->copyfrom_url)
    {
      /* When copying/moving a file that was already explicitly
         copied/moved then we know the URL it was copied from... */
      url = src_entry->copyfrom_url;
      rev = src_entry->copyfrom_rev;
    }
  else
    {
      /* ...But if this file is merely the descendant of an explicitly
         copied/moved directory, we need to do a bit more work to
         determine copyfrom_url and copyfrom_rev. */
      SVN_ERR(get_copyfrom_url_rev_via_parent(src_path, &url, &rev,
                                              src_access, pool));
    }

  if (dst_entry && rev == dst_entry->revision &&
      strcmp(url, dst_entry->url) == 0)
    {
      /* Suppress copyfrom info when the copy source is the same as
         for the destination. */
      url = NULL;
      rev = SVN_INVALID_REVNUM;
    }
  else if (src_entry->copyfrom_url)
    {
      /* As the URL was allocated for src_entry, make a copy. */
      url = apr_pstrdup(pool, url);
    }

  *copyfrom_url = url;
  *copyfrom_rev = rev;
  return SVN_NO_ERROR;
}

/* This function effectively creates and schedules a file for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a file under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied file in dst_parent
 */
static svn_error_t *
copy_file_administratively(const char *src_path,
                           svn_wc_adm_access_t *src_access,
                           svn_wc_adm_access_t *dst_parent,
                           const char *dst_basename,
                           svn_wc_notify_func2_t notify_copied,
                           void *notify_baton,
                           apr_pool_t *pool)
{
  svn_node_kind_t dst_kind;
  const svn_wc_entry_t *src_entry, *dst_entry;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  const char *dst_path
    = svn_path_join(svn_wc_adm_access_path(dst_parent), dst_basename, pool);

  /* Discover the paths to the two text-base files */
  const char *src_txtb = svn_wc__text_base_path(src_path, FALSE, pool);
  const char *tmp_txtb = svn_wc__text_base_path(dst_path, TRUE, pool);

  /* Sanity check:  if dst file exists already, don't allow overwrite. */
  SVN_ERR(svn_io_check_path(dst_path, &dst_kind, pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                             _("'%s' already exists and is in the way"),
                             svn_path_local_style(dst_path, pool));

  /* Even if DST_PATH doesn't exist it may still be a versioned file; it
     may be scheduled for deletion, or the user may simply have removed the
     working copy.  Since we are going to write to DST_PATH text-base and
     prop-base we need to detect such cases and abort. */
  SVN_ERR(svn_wc_entry(&dst_entry, dst_path, dst_parent, FALSE, pool));
  if (dst_entry && dst_entry->kind == svn_node_file)
    {
      if (dst_entry->schedule != svn_wc_schedule_delete)
        return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                                 _("There is already a versioned item '%s'"),
                                 svn_path_local_style(dst_path, pool));
    }

  /* Sanity check 1: You cannot make a copy of something that's not
     under version control. */
  SVN_ERR(svn_wc__entry_versioned(&src_entry, src_path, src_access, FALSE,
                                 pool));

  /* Sanity check 2: You cannot make a copy of something that's not
     in the repository unless it's a copy of an uncommitted copy. */
  if ((src_entry->schedule == svn_wc_schedule_add && (! src_entry->copied))
      || (! src_entry->url))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot copy or move '%s': it is not in the repository yet; "
         "try committing first"),
       svn_path_local_style(src_path, pool));


  /* Schedule the new file for addition in its parent, WITH HISTORY. */
  {
    const char *copyfrom_url;
    const char *tmp_wc_text;
    svn_revnum_t copyfrom_rev;
    apr_hash_t *props, *base_props;

    /* Are we moving or copying a file that is already moved or copied
       but not committed? */
    if (src_entry->copied)
      {
        SVN_ERR(determine_copyfrom_info(&copyfrom_url, &copyfrom_rev, src_path,
                                        src_access, src_entry, dst_entry,
                                        pool));
      }
    else
      {
        /* Grrr.  Why isn't the first arg to svn_wc_get_ancestry const? */
        char *tmp;

        SVN_ERR(svn_wc_get_ancestry(&tmp, &copyfrom_rev, src_path, src_access,
                                    pool));

        copyfrom_url = tmp;
      }

    /* Load source base and working props. */
    SVN_ERR(svn_wc__load_props(&base_props, &props, NULL, src_access,
                               src_path, pool));

    /* Copy pristine text-base to temporary location. */
    SVN_ERR(svn_io_copy_file(src_txtb, tmp_txtb, TRUE, pool));

    /* Copy working copy file to temporary location */
    {
      svn_boolean_t special;

      SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_wc_text,
                                      svn_wc_adm_access_path(dst_parent),
                                      svn_io_file_del_none, pool));

      SVN_ERR(svn_wc__get_special(&special, src_path, src_access, pool));
      if (special)
        {
          SVN_ERR(svn_subst_copy_and_translate3(src_path, tmp_wc_text,
                                                NULL, FALSE, NULL,
                                                FALSE, special, pool));
        }
      else
        SVN_ERR(svn_io_copy_file(src_path, tmp_wc_text, TRUE, pool));
    }

    SVN_ERR(svn_wc_add_repos_file2(dst_path, dst_parent,
                                   tmp_txtb, tmp_wc_text,
                                   base_props, props,
                                   copyfrom_url, copyfrom_rev, pool));
  }

  /* Report the addition to the caller. */
  if (notify_copied != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(dst_path,
                                                     svn_wc_notify_add,
                                                     pool);
      notify->kind = svn_node_file;
      (*notify_copied)(notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* Recursively crawl over a directory PATH and do a number of things:
     - Remove lock tokens
     - Remove WC props
     - Convert deleted items to schedule-delete items
     - Set .svn directories to be hidden
*/
static svn_error_t *
post_copy_cleanup(svn_wc_adm_access_t *adm_access,
                  apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_entry_t *entry;
  const char *path = svn_wc_adm_access_path(adm_access);

  /* Remove wcprops. */
  SVN_ERR(svn_wc__props_delete(path, svn_wc__props_wcprop, adm_access, pool));

  /* Read this directory's entries file. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, pool));

  /* Because svn_io_copy_dir_recursively() doesn't copy directory
     permissions, we'll patch up our tree's .svn subdirs to be
     hidden. */
#ifdef APR_FILE_ATTR_HIDDEN
  {
    const char *adm_dir = svn_wc__adm_path(path, FALSE, pool, NULL);
    const char *path_apr;
    apr_status_t status;
    SVN_ERR(svn_path_cstring_from_utf8(&path_apr, adm_dir, pool));
    status = apr_file_attrs_set(path_apr,
                                APR_FILE_ATTR_HIDDEN,
                                APR_FILE_ATTR_HIDDEN,
                                pool);
    if (status)
      return svn_error_wrap_apr(status, _("Can't hide directory '%s'"),
                                svn_path_local_style(adm_dir, pool));
  }
#endif

  /* Loop over all children, removing lock tokens and recursing into
     directories. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_node_kind_t kind;
      svn_boolean_t deleted = FALSE;
      apr_uint64_t flags = SVN_WC__ENTRY_MODIFY_FORCE;

      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &val);
      entry = val;
      kind = entry->kind;
      deleted = entry->deleted;

      /* Convert deleted="true" into schedule="delete" for all
         children (and grandchildren, if RECURSE is set) of the path
         represented by ADM_ACCESS.  The result of this is that when
         the copy is committed the items in question get deleted and
         the result is a directory in the repository that matches the
         original source directory for copy.  If this were not done
         the deleted="true" items would simply vanish from the entries
         file as the copy is added to the working copy.  The new
         schedule="delete" files do not have a text-base and so their
         scheduled deletion cannot be reverted.  For directories a
         placeholder with an svn_node_kind_t of svn_node_file and
         schedule="delete" is used to avoid the problems associated
         with creating a directory.  See Issue #2101 for details. */
      if (entry->deleted)
        {
          entry->schedule = svn_wc_schedule_delete;
          flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;

          entry->deleted = FALSE;
          flags |= SVN_WC__ENTRY_MODIFY_DELETED;

          if (entry->kind == svn_node_dir)
            {
              /* ### WARNING: Very dodgy stuff here! ###

              Directories are a problem since a schedule delete directory
              needs an admin directory to be present.  It's possible to
              create a dummy admin directory and that sort of works, it's
              good enough if the user commits the copy.  Where it falls
              down is if the user *reverts* the dummy directory since the
              now schedule normal, copied, directory doesn't have the
              correct contents.

              The dodgy solution is to cheat and use a schedule delete file
              as a placeholder!  This is sufficient to provide a delete
              when the copy is committed.  Attempts to revert any such
              "fake" files will fail due to a missing text-base. This
              effectively means that the schedule deletes have to remain
              schedule delete until the copy is committed, when they become
              state deleted and everything works! */
              entry->kind = svn_node_file;
              flags |= SVN_WC__ENTRY_MODIFY_KIND;
            }
        }

      /* Remove lock stuffs. */
      if (entry->lock_token)
        {
          entry->lock_token = NULL;
          entry->lock_owner = NULL;
          entry->lock_comment = NULL;
          entry->lock_creation_date = 0;
          flags |= (SVN_WC__ENTRY_MODIFY_LOCK_TOKEN
                    | SVN_WC__ENTRY_MODIFY_LOCK_OWNER
                    | SVN_WC__ENTRY_MODIFY_LOCK_COMMENT
                    | SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE);
        }

      /* If we meaningfully modified the flags, we must be wanting to
         change the entry. */
      if (flags != SVN_WC__ENTRY_MODIFY_FORCE)
        SVN_ERR(svn_wc__entry_modify(adm_access, key, entry,
                                     flags, TRUE, subpool));

      /* If a dir, not deleted, and not "this dir", recurse. */
      if ((! deleted)
          && (kind == svn_node_dir)
          && (strcmp(key, SVN_WC_ENTRY_THIS_DIR) != 0))
        {
          svn_wc_adm_access_t *child_access;
          const char *child_path;
          child_path = svn_path_join
            (svn_wc_adm_access_path(adm_access), key, subpool);
          SVN_ERR(svn_wc_adm_retrieve(&child_access, adm_access,
                                      child_path, subpool));
          SVN_ERR(post_copy_cleanup(child_access, subpool));
        }
    }

  /* Cleanup */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* This function effectively creates and schedules a dir for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_path points to a dir under version control
     - dst_parent points to a dir under version control, in the same
                  working copy.
     - dst_basename will be the 'new' name of the copied dir in dst_parent
 */
static svn_error_t *
copy_dir_administratively(const char *src_path,
                          svn_wc_adm_access_t *src_access,
                          svn_wc_adm_access_t *dst_parent,
                          const char *dst_basename,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_copied,
                          void *notify_baton,
                          apr_pool_t *pool)
{
  const svn_wc_entry_t *src_entry;
  svn_wc_adm_access_t *adm_access;

  /* The 'dst_path' is simply dst_parent/dst_basename */
  const char *dst_path = svn_path_join(svn_wc_adm_access_path(dst_parent),
                                       dst_basename, pool);

  /* Sanity check 1: You cannot make a copy of something that's not
     under version control. */
  SVN_ERR(svn_wc__entry_versioned(&src_entry, src_path, src_access, FALSE,
                                 pool));

  /* Sanity check 2: You cannot make a copy of something that's not
     in the repository unless it's a copy of an uncommitted copy. */
  if ((src_entry->schedule == svn_wc_schedule_add && (! src_entry->copied))
      || (! src_entry->url))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot copy or move '%s': it is not in the repository yet; "
         "try committing first"),
       svn_path_local_style(src_path, pool));

  /* Recursively copy the whole directory over.  This gets us all
     text-base, props, base-props, as well as entries, local mods,
     schedulings, existences, etc.

      ### Should we be copying unversioned items within the directory? */
  SVN_ERR(svn_io_copy_dir_recursively(src_path,
                                      svn_wc_adm_access_path(dst_parent),
                                      dst_basename,
                                      TRUE,
                                      cancel_func, cancel_baton,
                                      pool));

  /* If this is part of a move, the copied directory will be locked,
     because the source directory was locked.  Running cleanup will remove
     the locks, even though this directory has not yet been added to the
     parent. */
  SVN_ERR(svn_wc_cleanup2(dst_path, NULL, cancel_func, cancel_baton, pool));

  /* We've got some post-copy cleanup to do now. */
  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, dst_path, TRUE, -1,
                           cancel_func, cancel_baton, pool));
  SVN_ERR(post_copy_cleanup(adm_access, pool));

  /* Schedule the directory for addition in both its parent and itself
     (this_dir) -- WITH HISTORY.  This function should leave the
     existing administrative dir untouched.  */
  {
    const char *copyfrom_url;
    svn_revnum_t copyfrom_rev;
    svn_wc_entry_t tmp_entry;

    /* Are we copying a dir that is already copied but not committed? */
    if (src_entry->copied)
      {
        const svn_wc_entry_t *dst_entry;
        SVN_ERR(svn_wc_entry(&dst_entry, dst_path, dst_parent, FALSE, pool));
        SVN_ERR(determine_copyfrom_info(&copyfrom_url, &copyfrom_rev, src_path,
                                        src_access, src_entry, dst_entry,
                                        pool));

        /* The URL for a copied dir won't exist in the repository, which
           will cause  svn_wc_add2() below to fail.  Set the URL to the
           URL of the first copy for now to prevent this. */
        tmp_entry.url = apr_pstrdup(pool, copyfrom_url);
        SVN_ERR(svn_wc__entry_modify(adm_access, NULL, /* This Dir */
                                     &tmp_entry,
                                     SVN_WC__ENTRY_MODIFY_URL, TRUE,
                                     pool));
      }
    else
      {
        /* Grrr.  Why isn't the first arg to svn_wc_get_ancestry const? */
        char *tmp;

        SVN_ERR(svn_wc_get_ancestry(&tmp, &copyfrom_rev, src_path, src_access,
                                    pool));

        copyfrom_url = tmp;
      }

    SVN_ERR(svn_wc_adm_close(adm_access));

    SVN_ERR(svn_wc_add3(dst_path, dst_parent, svn_depth_infinity,
                        copyfrom_url, copyfrom_rev,
                        cancel_func, cancel_baton,
                        notify_copied, notify_baton, pool));
  }

  return SVN_NO_ERROR;
}



/* Public Interface */

svn_error_t *
svn_wc_copy2(const char *src_path,
             svn_wc_adm_access_t *dst_parent,
             const char *dst_basename,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_node_kind_t src_kind;
  const char *dst_path;
  const svn_wc_entry_t *dst_entry, *src_entry;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, src_path, FALSE, -1,
                                 cancel_func, cancel_baton, pool));

  dst_path =  svn_wc_adm_access_path(dst_parent);
  SVN_ERR(svn_wc__entry_versioned(&dst_entry, dst_path, dst_parent, FALSE,
                                 pool));
  SVN_ERR(svn_wc__entry_versioned(&src_entry, src_path, adm_access, FALSE,
                                 pool));

  if ((src_entry->repos != NULL && dst_entry->repos != NULL) &&
      strcmp(src_entry->repos, dst_entry->repos) != 0)
    return svn_error_createf
      (SVN_ERR_WC_INVALID_SCHEDULE, NULL,
       _("Cannot copy to '%s', as it is not from repository '%s'; "
         "it is from '%s'"),
       svn_path_local_style(svn_wc_adm_access_path(dst_parent), pool),
       src_entry->repos, dst_entry->repos);
  if (dst_entry->schedule == svn_wc_schedule_delete)
    return svn_error_createf
      (SVN_ERR_WC_INVALID_SCHEDULE, NULL,
       _("Cannot copy to '%s' as it is scheduled for deletion"),
       svn_path_local_style(svn_wc_adm_access_path(dst_parent), pool));

  SVN_ERR(svn_io_check_path(src_path, &src_kind, pool));

  if (src_kind == svn_node_file)
    {
      /* Check if we are copying a file scheduled for addition,
         these require special handling. */
      if (src_entry->schedule == svn_wc_schedule_add
          && (! src_entry->copied))
        {
          SVN_ERR(copy_added_file_administratively(src_path, TRUE,
                                                   dst_parent, dst_basename,
                                                   cancel_func, cancel_baton,
                                                   notify_func, notify_baton,
                                                   pool));
        }
      else
        {
          SVN_ERR(copy_file_administratively(src_path, adm_access,
                                             dst_parent, dst_basename,
                                             notify_func, notify_baton, pool));
        }
    }
  else if (src_kind == svn_node_dir)
    {
      /* Check if we are copying a directory scheduled for addition,
         these require special handling. */
      if (src_entry->schedule == svn_wc_schedule_add
          && (! src_entry->copied))
        {
          SVN_ERR(copy_added_dir_administratively(src_path, TRUE,
                                                  dst_parent, adm_access,
                                                  dst_basename, cancel_func,
                                                  cancel_baton, notify_func,
                                                  notify_baton, pool));
        }
      else
        {
          SVN_ERR(copy_dir_administratively(src_path, adm_access,
                                            dst_parent, dst_basename,
                                            cancel_func, cancel_baton,
                                            notify_func, notify_baton, pool));
        }
    }

  SVN_ERR(svn_wc_adm_close(adm_access));


  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_copy(const char *src_path,
            svn_wc_adm_access_t *dst_parent,
            const char *dst_basename,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_copy2(src_path, dst_parent, dst_basename, cancel_func,
                      cancel_baton, svn_wc__compat_call_notify_func,
                      &nb, pool);
}


