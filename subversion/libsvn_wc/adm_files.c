/*
 * adm_files.c: helper routines for handling files & dirs in the
 *              working copy administrative area (creating,
 *              deleting, opening, and closing).  This is the only
 *              code that actually knows where administrative
 *              information is kept.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <stdarg.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"
#include "lock.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** File names in the adm area. ***/

/* The default name of the WC admin directory. This name is always
   checked by svn_wc_is_adm_dir. */
static const char default_adm_dir_name[] = ".svn";

/* The name that is actually used for the WC admin directory.  The
   commonest case where this won't be the default is in Windows
   ASP.NET development environments, which choke on ".svn". */
static const char *adm_dir_name = default_adm_dir_name;


svn_boolean_t
svn_wc_is_adm_dir(const char *name, apr_pool_t *pool)
{
  return (0 == strcmp(name, adm_dir_name)
          || 0 == strcmp(name, default_adm_dir_name));
}


const char *
svn_wc_get_adm_dir(apr_pool_t *pool)
{
  return adm_dir_name;
}


svn_error_t *
svn_wc_set_adm_dir(const char *name, apr_pool_t *pool)
{
  /* This is the canonical list of administrative directory names.

     FIXME:
     An identical list is used in
       libsvn_subr/opt.c:svn_opt__args_to_target_array(),
     but that function can't use this list, because that use would
     create a circular dependency between libsvn_wc and libsvn_subr.
     Make sure changes to the lists are always synchronized! */
  static const char *valid_dir_names[] = {
    default_adm_dir_name,
    "_svn",
    NULL
  };

  const char **dir_name;
  for (dir_name = valid_dir_names; *dir_name; ++dir_name)
    if (0 == strcmp(name, *dir_name))
      {
        /* Use the pointer to the statically allocated string
           constant, to avoid potential pool lifetime issues. */
        adm_dir_name = *dir_name;
        return SVN_NO_ERROR;
      }
  return svn_error_createf
    (SVN_ERR_BAD_FILENAME, NULL,
     _("'%s' is not a valid administrative directory name"),
     svn_dirent_local_style(name, pool));
}


/* Return the path to something in PATH's administrative area.
 *
 * First, the adm subdir is appended to PATH as a component, then the
 * "tmp" directory is added iff USE_TMP is set, then each of the
 * varargs in AP (char *'s) is appended as a path component.  The list
 * must be terminated with a NULL argument.
 *
 * Adding an empty component results in no effect (i.e., the separator
 * char is not doubled).
 *
 * If EXTENSION is non-null, it will be appended to the final string
 * without a separator character.
 */
static const char *
v_extend_with_adm_name(const char *path,
                       const char *extension,
                       svn_boolean_t use_tmp,
                       apr_pool_t *pool,
                       va_list ap)
{
  const char *this;

  /* Tack on the administrative subdirectory. */
  path = svn_dirent_join(path, adm_dir_name, pool);

  /* If this is a tmp file, name it into the tmp area. */
  if (use_tmp)
    path = svn_dirent_join(path, SVN_WC__ADM_TMP, pool);

  /* Tack on everything else. */
  while ((this = va_arg(ap, const char *)) != NULL)
    {
      if (this[0] == '\0')
        continue;

      path = svn_dirent_join(path, this, pool);
    }

  if (extension)
    path = apr_pstrcat(pool, path, extension, NULL);

  return path;
}


/* See v_extend_with_adm_name() for details. */
static const char *
extend_with_adm_name(const char *path,
                     const char *extension,
                     svn_boolean_t use_tmp,
                     apr_pool_t *pool,
                     ...)
{
  va_list ap;

  va_start(ap, pool);
  path = v_extend_with_adm_name(path, extension, use_tmp, pool, ap);
  va_end(ap);

  return path;
}


const char *svn_wc__adm_child(const char *path,
                              const char *child,
                              apr_pool_t *result_pool)
{
  return extend_with_adm_name(path, NULL, FALSE, result_pool, child, NULL);
}


svn_boolean_t
svn_wc__adm_area_exists(const svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  const char *path = svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                                       NULL, pool);
  svn_node_kind_t kind;
  svn_error_t *err;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      /* Return early, since kind is undefined in this case. */
      return FALSE;
    }

  return kind != svn_node_none;
}



/*** Making and using files in the adm area. ***/


/* */
static svn_error_t *
make_adm_subdir(const char *path,
                const char *subdir,
                svn_boolean_t tmp,
                apr_pool_t *pool)
{
  const char *fullpath;

  fullpath = extend_with_adm_name(path, NULL, tmp, pool, subdir, NULL);

  return svn_io_dir_make(fullpath, APR_OS_DEFAULT, pool);
}


svn_error_t *
svn_wc__make_killme(svn_wc_adm_access_t *adm_access,
                    svn_boolean_t adm_only,
                    apr_pool_t *pool)
{
  const char *path;

  SVN_ERR(svn_wc__adm_write_check(adm_access, pool));

  path = svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                           SVN_WC__ADM_KILLME, pool);

  return svn_io_file_create(path, adm_only ? SVN_WC__KILL_ADM_ONLY : "", pool);
}

svn_error_t *
svn_wc__check_killme(svn_wc_adm_access_t *adm_access,
                     svn_boolean_t *exists,
                     svn_boolean_t *kill_adm_only,
                     apr_pool_t *pool)
{
  const char *path;
  svn_error_t *err;
  svn_stringbuf_t *contents;

  path = svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                           SVN_WC__ADM_KILLME, pool);

  err = svn_stringbuf_from_file2(&contents, path, pool);
  if (err)
    {
      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* Killme file doesn't exist. */
          *exists = FALSE;
          svn_error_clear(err);
          err = SVN_NO_ERROR;
        }

      return err;
    }

  *exists = TRUE;

  /* If the killme file contains the string 'adm-only' then only the
     administrative area should be removed. */
  *kill_adm_only = strcmp(contents->data, SVN_WC__KILL_ADM_ONLY) == 0;

  return SVN_NO_ERROR;
}


/*** Syncing files in the adm area. ***/


/* Rename a tmp text-base file to its real text-base name.
   The file had better already be closed. */
svn_error_t *
svn_wc__sync_text_base(const char *path, apr_pool_t *pool)
{
  const char *parent_path;
  const char *base_name;
  const char *tmp_path;
  const char *base_path;

  svn_dirent_split(path, &parent_path, &base_name, pool);

  /* Extend tmp name. */
  tmp_path = extend_with_adm_name(parent_path, SVN_WC__BASE_EXT, TRUE, pool,
                                  SVN_WC__ADM_TEXT_BASE, base_name, NULL);

  /* Extend real name. */
  base_path = extend_with_adm_name(parent_path, SVN_WC__BASE_EXT, FALSE, pool,
                                   SVN_WC__ADM_TEXT_BASE, base_name, NULL);

  /* Rename. */
  SVN_ERR(svn_io_file_rename(tmp_path, base_path, pool));
  return svn_io_set_file_read_only(base_path, FALSE, pool);
}

const char *
svn_wc__text_base_path(const char *path,
                       svn_boolean_t tmp,
                       apr_pool_t *pool)
{
  const char *newpath, *base_name;

  svn_dirent_split(path, &newpath, &base_name, pool);
  return extend_with_adm_name(newpath,
                              SVN_WC__BASE_EXT,
                              tmp,
                              pool,
                              SVN_WC__ADM_TEXT_BASE,
                              base_name,
                              NULL);
}

const char *
svn_wc__text_revert_path(const char *path,
                         apr_pool_t *pool)
{
  const char *newpath, *base_name;

  svn_dirent_split(path, &newpath, &base_name, pool);
  return extend_with_adm_name(newpath,
                              SVN_WC__REVERT_EXT,
                              FALSE,
                              pool,
                              SVN_WC__ADM_TEXT_BASE,
                              base_name,
                              NULL);
}


svn_error_t *
svn_wc__get_revert_contents(svn_stream_t **contents,
                            const char *path,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *revert_base = svn_wc__text_revert_path(path, scratch_pool);

  if (revert_base == NULL)
    {
      *contents = NULL;
      return SVN_NO_ERROR;
    }

  return svn_stream_open_readonly(contents, revert_base, result_pool,
                                  scratch_pool);
}


svn_error_t *
svn_wc__prop_path(const char **prop_path,
                  const char *path,
                  svn_node_kind_t node_kind,
                  svn_wc__props_kind_t props_kind,
                  apr_pool_t *pool)
{
  if (node_kind == svn_node_dir)  /* It's a working copy dir */
    {
      static const char * names[] = {
        SVN_WC__ADM_DIR_PROP_BASE,    /* svn_wc__props_base */
        SVN_WC__ADM_DIR_PROP_REVERT,  /* svn_wc__props_revert */
        SVN_WC__ADM_DIR_WCPROPS,      /* svn_wc__props_wcprop */
        SVN_WC__ADM_DIR_PROPS         /* svn_wc__props_working */
      };

      *prop_path = extend_with_adm_name
        (path,
         NULL,
         FALSE,
         pool,
         names[props_kind],
         NULL);
    }
  else  /* It's a file */
    {
      static const char * extensions[] = {
        SVN_WC__BASE_EXT,     /* svn_wc__props_base */
        SVN_WC__REVERT_EXT,   /* svn_wc__props_revert */
        SVN_WC__WORK_EXT,     /* svn_wc__props_wcprop */
        SVN_WC__WORK_EXT      /* svn_wc__props_working */
      };

      static const char * dirs[] = {
        SVN_WC__ADM_PROP_BASE,  /* svn_wc__props_base */
        SVN_WC__ADM_PROP_BASE,  /* svn_wc__props_revert */
        SVN_WC__ADM_WCPROPS,    /* svn_wc__props_wcprop */
        SVN_WC__ADM_PROPS       /* svn_wc__props_working */
      };

      const char *base_name;

      svn_dirent_split(path, prop_path, &base_name, pool);
      *prop_path = extend_with_adm_name
        (*prop_path,
         extensions[props_kind],
         FALSE,
         pool,
         dirs[props_kind],
         base_name,
         NULL);
    }

  return SVN_NO_ERROR;
}


/*** Opening and closing files in the adm area. ***/

/* Open a file somewhere in the adm area for directory PATH.
 * First, add the adm subdir as the next component of PATH, then add
 * each of the varargs (they are char *'s), then add EXTENSION if it
 * is non-null, then open the resulting file as *STREAM.
 *
 * If FLAGS indicates writing, open the file in the adm tmp area.
 * This means the file will probably need to be renamed from there,
 * either by passing the sync flag to close_adm_file() later, or with
 * an explicit call to sync_adm_file().
 */
static svn_error_t *
open_adm_file(svn_stream_t **stream,
              const char **selected_path,
              const char *path,
              const char *extension,
              svn_boolean_t for_writing,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool,
              ...)
{
  svn_error_t *err;
  va_list ap;

  /* If we're writing, always do it to a tmp file. */
  if (for_writing)
    {
      /* Extend with tmp name. */
      va_start(ap, scratch_pool);
      path = v_extend_with_adm_name(path, extension, TRUE, result_pool, ap);
      va_end(ap);

      err = svn_stream_open_writable(stream, path, result_pool, scratch_pool);
    }
  else
    {
      /* Extend with regular adm name. */
      va_start(ap, scratch_pool);
      path = v_extend_with_adm_name(path, extension, FALSE, result_pool, ap);
      va_end(ap);

      err = svn_stream_open_readonly(stream, path, result_pool, scratch_pool);
    }

  if (selected_path)
    *selected_path = path;  /* note: built in result_pool */

  if (for_writing && err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      /* Exclusive open failed, delete and retry */
      svn_error_clear(err);
      SVN_ERR(svn_io_remove_file2(path, FALSE, scratch_pool));
      err = svn_stream_open_writable(stream, path, result_pool, scratch_pool);
    }

  /* Examine the error from the first and/or second attempt at opening. */
  if (for_writing && err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* If we receive a failure to open a file in our temporary directory,
       * it may be because our temporary directories aren't created.
       * Older SVN clients did not create these directories.
       * 'svn cleanup' will fix this problem.
       */
      err = svn_error_quick_wrap(err,
                                 _("Your .svn/tmp directory may be missing or "
                                   "corrupt; run 'svn cleanup' and try again"));
    }

  return err;
}


svn_error_t *
svn_wc__open_adm_writable(svn_stream_t **stream,
                          const char **temp_file_path,
                          const char *path,
                          const char *fname,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return open_adm_file(stream, temp_file_path, path, NULL /* extension */,
                       TRUE /* for_writing */,
                       result_pool, scratch_pool,
                       fname, NULL);
}


svn_error_t *
svn_wc__close_adm_stream(svn_stream_t *stream,
                         const char *temp_file_path,
                         const char *path,
                         const char *fname,
                         apr_pool_t *scratch_pool)
{
  const char *dst_path = extend_with_adm_name(path, NULL, FALSE, scratch_pool,
                                              fname, NULL);

  SVN_ERR(svn_stream_close(stream));

  /* Put the completed file into its intended location. */
  SVN_ERR(svn_io_file_rename(temp_file_path, dst_path, scratch_pool));
  return svn_error_return(svn_io_set_file_read_only(dst_path, FALSE,
                                                    scratch_pool));
}


svn_error_t *
svn_wc__remove_adm_file(const char *dir_path,
                        const char *filename,
                        apr_pool_t *scratch_pool)
{
  const char *path = svn_wc__adm_child(dir_path, filename, scratch_pool);

  return svn_error_return(svn_io_remove_file2(path, FALSE, scratch_pool));
}


svn_error_t *
svn_wc__open_adm_stream(svn_stream_t **stream,
                        const char *path,
                        const char *fname,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return open_adm_file(stream,
                       NULL /* selected_path */,
                       path,
                       NULL /* extension */,
                       FALSE /* for_writing */,
                       result_pool, scratch_pool,
                       fname,
                       NULL);
}


svn_error_t *
svn_wc__open_writable_base(svn_stream_t **stream,
                           const char **temp_base_path,
                           const char *path,
                           svn_boolean_t need_revert_base,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *parent_path;
  const char *base_name;

  svn_dirent_split(path, &parent_path, &base_name, scratch_pool);

  return open_adm_file(stream, temp_base_path,
                       parent_path,
                       need_revert_base
                         ? SVN_WC__REVERT_EXT
                         : SVN_WC__BASE_EXT,
                       TRUE /* for_writing */,
                       result_pool, scratch_pool,
                       SVN_WC__ADM_TEXT_BASE,
                       base_name,
                       NULL);
}



/*** Checking for and creating administrative subdirs. ***/


static svn_error_t *
init_adm_tmp_area(const char *path, apr_pool_t *pool)
{
  /* SVN_WC__ADM_TMP */
  SVN_ERR(make_adm_subdir(path, SVN_WC__ADM_TMP, FALSE, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_TEXT_BASE */
  SVN_ERR(make_adm_subdir(path, SVN_WC__ADM_TEXT_BASE, TRUE, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROP_BASE */
  SVN_ERR(make_adm_subdir(path, SVN_WC__ADM_PROP_BASE, TRUE, pool));

  /* SVN_WC__ADM_TMP/SVN_WC__ADM_PROPS */
  return make_adm_subdir(path, SVN_WC__ADM_PROPS, TRUE, pool);
}


/* Set up a new adm area for PATH, with URL as the ancestor url, and
   INITIAL_REV as the starting revision.  The entries file starts out
   marked as 'incomplete.  The adm area starts out locked; remember to
   unlock it when done. */
static svn_error_t *
init_adm(const char *path,
         const char *uuid,
         const char *url,
         const char *repos,
         svn_revnum_t initial_rev,
         svn_depth_t depth,
         apr_pool_t *pool)
{
  /* First, make an empty administrative area. */
  SVN_ERR(svn_io_dir_make_hidden(svn_wc__adm_child(path, NULL, pool),
                                 APR_OS_DEFAULT, pool));

  /** Make subdirectories. ***/

  /* SVN_WC__ADM_TEXT_BASE */
  SVN_ERR(make_adm_subdir(path, SVN_WC__ADM_TEXT_BASE, FALSE, pool));

  /* SVN_WC__ADM_PROP_BASE */
  SVN_ERR(make_adm_subdir(path, SVN_WC__ADM_PROP_BASE, FALSE, pool));

  /* SVN_WC__ADM_PROPS */
  SVN_ERR(make_adm_subdir(path, SVN_WC__ADM_PROPS, FALSE, pool));

  /** Init the tmp area. ***/
  SVN_ERR(init_adm_tmp_area(path, pool));

  /** Initialize each administrative file. */

  /* SVN_WC__ADM_ENTRIES */
  /* THIS FILE MUST BE CREATED LAST:
     After this exists, the dir is considered complete. */
  SVN_ERR(svn_wc__entries_init(path, uuid, url, repos,
                               initial_rev, depth, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_ensure_adm3(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *local_abspath;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  int format;

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL /* ### config */, pool, pool));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc__internal_check_wc(&format, db, local_abspath, pool));

  /* ### we just created a DB. we should pass that into the other
     ### functions below.  */

  /* Early out: we know we're not dealing with an existing wc, so
     just create one. */
  if (format == 0)
    return init_adm(path, uuid, url, repos, revision, depth, pool);

  /* Now, get the existing url and repos for PATH. */
  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, FALSE, 0,
                           NULL, NULL, pool));
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));
  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  /* When the directory exists and is scheduled for deletion do not
   * check the revision or the URL.  The revision can be any
   * arbitrary revision and the URL may differ if the add is
   * being driven from a merge which will have a different URL. */
  if (entry->schedule != svn_wc_schedule_delete)
    {
      if (entry->revision != revision)
        return
          svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
           _("Revision %ld doesn't match existing revision %ld in '%s'"),
           revision, entry->revision, path);

      /* The caller gives us a URL which should match the entry. However,
         some callers compensate for an old problem in entry->url and pass
         the copyfrom_url instead. See ^/notes/api-errata/wc002.txt. As
         a result, we allow the passed URL to match copyfrom_url if it
         does match the entry's primary URL.  */
      /** ### comparing URLs, should they be canonicalized first? */
      if (strcmp(entry->url, url) != 0
          && (entry->copyfrom_url == NULL
              || strcmp(entry->copyfrom_url, url) != 0)
          && ((repos != NULL && !svn_uri_is_ancestor(repos, entry->url))
              || strcmp(entry->uuid, uuid) != 0))
        {
          return
            svn_error_createf
            (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
             _("URL '%s' doesn't match existing URL '%s' in '%s'"),
             url, entry->url, path);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__adm_destroy(svn_wc_adm_access_t *adm_access,
                    apr_pool_t *scratch_pool)
{
  const char *path;

  SVN_ERR(svn_wc__adm_write_check(adm_access, scratch_pool));

  /* Well, the coast is clear for blowing away the administrative
     directory, which also removes the lock file */
  path = svn_wc__adm_child(svn_wc_adm_access_path(adm_access), NULL,
                           scratch_pool);
  SVN_ERR(svn_wc_adm_close2(adm_access, scratch_pool));

  SVN_ERR(svn_io_remove_dir2(path, FALSE, NULL, NULL, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__adm_cleanup_tmp_area(const svn_wc_adm_access_t *adm_access,
                             apr_pool_t *scratch_pool)
{
  const char *path = svn_wc_adm_access_path(adm_access);
  const char *tmp_path;

  /* If the admin area doesn't even *exist*, then the temp area is
     definitely cleaned up. */
  if (!svn_wc__adm_area_exists(adm_access, scratch_pool))
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__adm_write_check(adm_access, scratch_pool));

  /* Get the path to the tmp area, and blow it away. */
  tmp_path = svn_wc__adm_child(path, SVN_WC__ADM_TMP, scratch_pool);

  SVN_ERR(svn_io_remove_dir2(tmp_path, TRUE, NULL, NULL, scratch_pool));

  /* Now, rebuild the tmp area. */
  return init_adm_tmp_area(path, scratch_pool);
}



svn_error_t *
svn_wc_create_tmp_file2(apr_file_t **fp,
                        const char **new_name,
                        const char *path,
                        svn_io_file_del_t delete_when,
                        apr_pool_t *pool)
{
  const char *temp_dir;
  apr_file_t *file;

  SVN_ERR_ASSERT(fp || new_name);

  temp_dir = svn_wc__adm_child(path, SVN_WC__ADM_TMP, pool);

  SVN_ERR(svn_io_open_unique_file3(&file, new_name, temp_dir,
                                   delete_when, pool, pool));

  if (fp)
    *fp = file;
  else
    SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}
