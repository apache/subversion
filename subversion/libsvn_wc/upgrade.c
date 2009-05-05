/*
 * upgrade.c:  routines for upgrading a working copy
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "wc.h"
#include "adm_files.h"
#include "lock.h"
#include "log.h"
#include "entries.h"
#include "wc_db.h"

#include "svn_private_config.h"


/* For wcprops stored in a single file in this working copy, read that
   file and return it in *ALL_WCPROPS, allocated in RESULT_POOL.   Use
   SCRATCH_POOL for temporary allocations. */
static svn_error_t *
read_wcprops(apr_hash_t **all_wcprops,
             svn_wc__db_t *db,
             const char *dir_abspath,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  apr_hash_t *proplist;
  svn_stream_t *stream;
  svn_error_t *err;

  *all_wcprops = apr_hash_make(result_pool);

  err = svn_wc__open_adm_stream(&stream, dir_abspath,
                                SVN_WC__ADM_ALL_WCPROPS,
                                scratch_pool, scratch_pool);

  /* A non-existent file means there are no props. */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* Read the proplist for THIS_DIR. */
  proplist = apr_hash_make(result_pool);
  SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, result_pool));
  apr_hash_set(*all_wcprops, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING,
               proplist);

  /* And now, the children. */
  while (1729)
    {
      svn_stringbuf_t *line;
      svn_boolean_t eof;

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
      if (eof)
        {
          if (line->len > 0)
            return svn_error_createf
              (SVN_ERR_WC_CORRUPT, NULL,
               _("Missing end of line in wcprops file for '%s'"),
               svn_path_local_style(dir_abspath, scratch_pool));
          break;
        }
      proplist = apr_hash_make(result_pool);
      SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR,
                             result_pool));
      apr_hash_set(*all_wcprops, line->data, APR_HASH_KEY_STRING, proplist);
    }

  return svn_stream_close(stream);
}

/* Helper for converting wcprops from the one-file-per-file model.
   This implements svn_io_walk_func_t(). */
static svn_error_t *
convert_wcprops_walker(void *baton,
                       const char *path,
                       const apr_finfo_t *finfo,
                       apr_pool_t *pool)
{
  svn_wc__db_t *db = baton;
  apr_hash_t *proplist;
  svn_stream_t *stream;
  const char *local_abspath;
  int len;

  /* Skip the directory. */
  if (finfo->filetype == APR_DIR)
    return SVN_NO_ERROR;

  proplist = apr_hash_make(pool);
  SVN_ERR(svn_stream_open_readonly(&stream, path, pool, pool));
  SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  /* The filename will be something like foo.c.svn-work.  From it, determine
     the local_abspath of the node.  The magic number of 9 below is basically
     strlen(".svn-work"). */
  len = strlen(path);
  local_abspath = apr_pstrndup(pool, local_abspath, len - 9);

  return svn_error_return(svn_wc__db_base_set_dav_cache(db, local_abspath,
                                                        proplist, pool));
}

/* Convert a WC that has wcprops in files to use the wc-ng database.
   Do this for ADM_ACCESS and its file children, using POOL for temporary
   allocations. */
static svn_error_t *
convert_wcprops(svn_wc_adm_access_t *adm_access,
                int old_format,
                apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);

  if (old_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
    {
      svn_stream_t *stream;
      svn_error_t *err;
      const char *dir_abspath;
      apr_hash_t *proplist;

      /* First, look at dir-wcprops. */
      SVN_ERR(svn_dirent_get_absolute(&dir_abspath,
                                      svn_wc_adm_access_path(adm_access),
                                      scratch_pool));
      err = svn_wc__open_adm_stream(&stream, dir_abspath,
                                    SVN_WC__ADM_DIR_WCPROPS,
                                    scratch_pool, scratch_pool);
      if (err)
        {
          /* If the file doesn't exist, it means no wcprops. */
          if (APR_STATUS_IS_ENOENT(err->apr_err))
            svn_error_clear(err);
          else
            return svn_error_return(err);
        }
      else
        {
          proplist = apr_hash_make(scratch_pool);
          SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR,
                                 scratch_pool));
          SVN_ERR(svn_wc__db_base_set_dav_cache(db, dir_abspath, proplist,
                                                scratch_pool));
        }

      /* Now walk the wcprops directory. */
      SVN_ERR(svn_io_dir_walk(svn_wc__adm_child(svn_wc_adm_access_path(
                                                                adm_access),
                                                SVN_WC__ADM_WCPROPS,
                                                scratch_pool),
                              0 /* wanted */,
                              convert_wcprops_walker,
                              db, scratch_pool));
    }
  else
    {
      apr_hash_t *allprops;
      apr_hash_index_t *hi;
      apr_pool_t *iterpool;

      /* Read the all-wcprops file. */
      SVN_ERR(read_wcprops(&allprops, db, svn_wc_adm_access_path(adm_access),
                           scratch_pool, scratch_pool));

      /* Iterate over all the wcprops, writing each one to the wc_db. */
      iterpool = svn_pool_create(scratch_pool);
      for (hi = apr_hash_first(scratch_pool, allprops); hi;
            hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *name;
          apr_hash_t *proplist;
          const char *local_abspath;

          svn_pool_clear(iterpool);

          apr_hash_this(hi, &key, NULL, &val);
          name = key;
          proplist = val;

          SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                          svn_wc_adm_access_path(adm_access),
                                          iterpool));
          local_abspath = svn_dirent_join(local_abspath, name, iterpool);
          SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, proplist,
                                                iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__upgrade_format(svn_wc_adm_access_t *adm_access,
                       apr_pool_t *scratch_pool)
{
  int wc_format;
  svn_boolean_t cleanup_required;
  svn_stringbuf_t *log_accum;
  const svn_wc_entry_t *this_dir;

  SVN_ERR(svn_wc__adm_wc_format(&wc_format, adm_access, scratch_pool));

  if (wc_format > SVN_WC__VERSION)
    {
      return svn_error_createf
        (SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
         _("This client is too old to work with working copy '%s'.  You need\n"
           "to get a newer Subversion client, or to downgrade this working "
           "copy.\n"
           "See "
           "http://subversion.tigris.org/faq.html#working-copy-format-change\n"
           "for details."
           ),
         svn_path_local_style(svn_wc_adm_access_path(adm_access),
                              scratch_pool));
    }

  /* We can upgrade all formats that are accepted by check_format(). */
  if (wc_format >= SVN_WC__VERSION)
    return SVN_NO_ERROR;

  /* Don't try to mess with the WC if there are old log files left. */
  SVN_ERR(svn_wc__adm_is_cleanup_required(&cleanup_required,
                                          adm_access, scratch_pool));

  if (cleanup_required)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Cannot upgrade with existing logs; please "
                              "run 'svn cleanup' with Subversion 1.6"));

  /* What's going on here?
   *
   * We're attempting to upgrade an older working copy to the new wc-ng format.
   * The sematics and storage mechanisms between the two are vastly different,
   * so it's going to be a bit painful.  Here's a plan for the operation:
   *
   * 1) The 'entries' file needs to be moved to the new format.  Ideally, we'd
   *    read it using svn_wc__entries_read_old(), and then translate the
   *    current state of the file into a series of wc_db commands to duplicate
   *    that state in WC-NG.  We're not quite there yet, so we just use
   *    the same loggy process as we always have, relying on the lower layers
   *    to take care of the translation, and remembering to remove the old
   *    entries file when were're done.  ### This isn't a long-term solution.
   *
   * 2) Convert wcprop to the wc-ng format
   *
   * ### (fill in other bits as they are implemented)
   */

  /***** ENTRIES *****/
  /* Create an empty sqlite database for this directory. */
  SVN_ERR(svn_wc_entry(&this_dir, svn_wc_adm_access_path(adm_access),
                       adm_access, FALSE, scratch_pool));
  SVN_ERR(svn_wc__entries_init(svn_wc_adm_access_path(adm_access),
                               this_dir->uuid, this_dir->url,
                               this_dir->repos, this_dir->revision,
                               this_dir->depth, scratch_pool));

  /* Migrate the entries over to the new database.
     ### We need to think about atomicity here. */
  SVN_ERR(svn_wc__entries_upgrade(adm_access, SVN_WC__VERSION, scratch_pool));
  svn_error_clear(svn_io_remove_file(svn_wc__adm_child(svn_wc_adm_access_path(
                                                                adm_access),
                                                       SVN_WC__ADM_FORMAT,
                                                       scratch_pool),
                                     scratch_pool));
  SVN_ERR(svn_io_remove_file(svn_wc__adm_child(svn_wc_adm_access_path(
                                                                adm_access),
                                               SVN_WC__ADM_ENTRIES,
                                               scratch_pool),
                             scratch_pool));

  /* ### Note that lots of this content is cribbed from the old format updater.
     ### The following code will change as the wc-ng format changes and more
     ### stuff gets migrated to the sqlite format. */

  log_accum = svn_stringbuf_create("", scratch_pool);

  /***** WC PROPS *****/
  SVN_ERR(convert_wcprops(adm_access, wc_format, scratch_pool));

  SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, scratch_pool));

  if (wc_format <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
    {
      /* Remove wcprops directory, dir-props, README.txt and empty-file
         files.
         We just silently ignore errors, because keeping these files is
         not catastrophic. */

      svn_error_clear(svn_io_remove_dir2(
          svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                            SVN_WC__ADM_WCPROPS, scratch_pool),
          FALSE, NULL, NULL, scratch_pool));
      svn_error_clear(svn_io_remove_file(
          svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                            SVN_WC__ADM_DIR_WCPROPS, scratch_pool),
          scratch_pool));
      svn_error_clear(svn_io_remove_file(
          svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                            SVN_WC__ADM_EMPTY_FILE, scratch_pool),
          scratch_pool));
      svn_error_clear(svn_io_remove_file(
          svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                            SVN_WC__ADM_README, scratch_pool),
          scratch_pool));
    }
  else
    {
      svn_error_clear(svn_io_remove_file(
          svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                            SVN_WC__ADM_ALL_WCPROPS, scratch_pool),
          scratch_pool));
    }

  return svn_wc__run_log(adm_access, NULL, scratch_pool);
}
