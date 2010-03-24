/*
 * adm_files.c: helper routines for handling files & dirs in the
 *              working copy administrative area (creating,
 *              deleting, opening, and closing).  This is the only
 *              code that actually knows where administrative
 *              information is kept.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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



/*** Syncing files in the adm area. ***/


svn_error_t *
svn_wc__sync_text_base(const char *local_abspath,
                       const char *tmp_text_base_abspath,
                       apr_pool_t *pool)
{
  const char *parent_path;
  const char *base_name;
  const char *base_path;

  svn_dirent_split(local_abspath, &parent_path, &base_name, pool);

  /* Extend real name. */
  base_path = extend_with_adm_name(parent_path, SVN_WC__BASE_EXT, FALSE, pool,
                                   SVN_WC__ADM_TEXT_BASE, base_name, NULL);

  /* Rename. */
  SVN_ERR(svn_io_file_rename(tmp_text_base_abspath, base_path, pool));
  return svn_io_set_file_read_only(base_path, FALSE, pool);
}

svn_error_t *
svn_wc__text_base_path(const char **result_abspath,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_boolean_t tmp,
                       apr_pool_t *pool)
{
  const char *newpath, *base_name;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  svn_dirent_split(local_abspath, &newpath, &base_name, pool);
  *result_abspath = extend_with_adm_name(newpath,
                                         SVN_WC__BASE_EXT,
                                         tmp,
                                         pool,
                                         SVN_WC__ADM_TEXT_BASE,
                                         base_name,
                                         NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__text_revert_path(const char **result_abspath,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *pool)
{
  const char *newpath, *base_name;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  svn_dirent_split(local_abspath, &newpath, &base_name, pool);
  *result_abspath = extend_with_adm_name(newpath,
                                         SVN_WC__REVERT_EXT,
                                         FALSE,
                                         pool,
                                         SVN_WC__ADM_TEXT_BASE,
                                         base_name,
                                         NULL);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_revert_contents(svn_stream_t **contents,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *revert_base;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__text_revert_path(&revert_base, db, local_abspath,
                                   scratch_pool));

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
                  svn_wc__db_kind_t node_kind,
                  svn_wc__props_kind_t props_kind,
                  apr_pool_t *pool)
{
  if (node_kind == svn_wc__db_kind_dir)
    {
      static const char * names[] = {
        SVN_WC__ADM_DIR_PROP_BASE,    /* svn_wc__props_base */
        SVN_WC__ADM_DIR_PROP_REVERT,  /* svn_wc__props_revert */
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
        SVN_WC__WORK_EXT      /* svn_wc__props_working */
      };

      static const char * dirs[] = {
        SVN_WC__ADM_PROP_BASE,  /* svn_wc__props_base */
        SVN_WC__ADM_PROP_BASE,  /* svn_wc__props_revert */
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

svn_error_t *
svn_wc__open_adm_stream(svn_stream_t **stream,
                        const char *dir_abspath,
                        const char *fname,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  local_abspath = svn_wc__adm_child(dir_abspath, fname, scratch_pool);
  return svn_error_return(svn_stream_open_readonly(stream, local_abspath,
                                                   result_pool, scratch_pool));
}


svn_error_t *
svn_wc__open_writable_base(svn_stream_t **stream,
                           const char **temp_base_abspath,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *temp_dir_abspath;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Select a directory in which to put a WC-1-style temp text-base file. */
  /* See update_editor.c:get_pristine_tee_stream() for the WC-NG way. */
  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir_abspath, db, local_abspath,
                                         scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_open_unique(stream,
                                 temp_base_abspath,
                                 temp_dir_abspath,
                                 svn_io_file_del_none,
                                 result_pool, scratch_pool));
  return SVN_NO_ERROR;
}



/*** Checking for and creating administrative subdirs. ***/


/* */
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
init_adm(svn_wc__db_t *db,
         const char *local_abspath,
         const char *url,
         const char *repos_root_url,
         const char *repos_uuid,
         svn_revnum_t initial_rev,
         svn_depth_t depth,
         apr_pool_t *pool)
{
  const char *repos_relpath;

  SVN_ERR_ASSERT(svn_uri_is_ancestor(repos_root_url, url));

  /* First, make an empty administrative area. */
  SVN_ERR(svn_io_dir_make_hidden(svn_wc__adm_child(local_abspath, NULL, pool),
                                 APR_OS_DEFAULT, pool));

  /** Make subdirectories. ***/

  /* SVN_WC__ADM_TEXT_BASE */
  SVN_ERR(make_adm_subdir(local_abspath, SVN_WC__ADM_TEXT_BASE, FALSE, pool));

  /* SVN_WC__ADM_PROP_BASE */
  SVN_ERR(make_adm_subdir(local_abspath, SVN_WC__ADM_PROP_BASE, FALSE, pool));

  /* SVN_WC__ADM_PROPS */
  SVN_ERR(make_adm_subdir(local_abspath, SVN_WC__ADM_PROPS, FALSE, pool));

  /* SVN_WC__ADM_PRISTINE */
  SVN_ERR(make_adm_subdir(local_abspath, SVN_WC__ADM_PRISTINE, FALSE, pool));

  /** Init the tmp area. ***/
  SVN_ERR(init_adm_tmp_area(local_abspath, pool));

  /* Lastly, create the SDB.  */
  repos_relpath = svn_uri_is_child(repos_root_url, url, pool);
  SVN_ERR(svn_wc__db_init(db, local_abspath,
                          repos_relpath == NULL
                            ? ""
                            : svn_path_uri_decode(repos_relpath, pool),
                          repos_root_url, repos_uuid, initial_rev, depth,
                          pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_ensure_adm(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *url,
                            const char *repos_root_url,
                            const char *repos_uuid,
                            svn_revnum_t revision,
                            svn_depth_t depth,
                            apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *entry;
  int format;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_root_url != NULL);
  SVN_ERR_ASSERT(repos_uuid != NULL);

  SVN_ERR(svn_wc__internal_check_wc(&format, db, local_abspath, scratch_pool));

  /* Early out: we know we're not dealing with an existing wc, so
     just create one. */
  if (format == 0)
    return init_adm(db, local_abspath, url, repos_root_url, repos_uuid,
                    revision, depth, scratch_pool);

  /* Now, get the existing url and repos for PATH. */
  SVN_ERR(svn_wc__get_entry(&entry, db, local_abspath, FALSE, svn_node_unknown,
                            FALSE, scratch_pool, scratch_pool));

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
           revision, entry->revision, local_abspath);

      /* The caller gives us a URL which should match the entry. However,
         some callers compensate for an old problem in entry->url and pass
         the copyfrom_url instead. See ^/notes/api-errata/wc002.txt. As
         a result, we allow the passed URL to match copyfrom_url if it
         does match the entry's primary URL.  */
      /** ### comparing URLs, should they be canonicalized first? */
      if (strcmp(entry->url, url) != 0
          && (entry->copyfrom_url == NULL
              || strcmp(entry->copyfrom_url, url) != 0)
          && (!svn_uri_is_ancestor(repos_root_url, entry->url)
              || strcmp(entry->uuid, repos_uuid) != 0))
        {
          return
            svn_error_createf
            (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
             _("URL '%s' doesn't match existing URL '%s' in '%s'"),
             url, entry->url, local_abspath);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_ensure_adm4(svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   const char *url,
                   const char *repos_root_url,
                   const char *repos_uuid,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   apr_pool_t *scratch_pool)
{
  return svn_error_return(
    svn_wc__internal_ensure_adm(wc_ctx->db, local_abspath, url, repos_root_url,
                                repos_uuid, revision, depth, scratch_pool));
}

svn_error_t *
svn_wc__adm_destroy(svn_wc__db_t *db,
                    const char *dir_abspath,
                    apr_pool_t *scratch_pool)
{
  const char *adm_abspath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(svn_wc__write_check(db, dir_abspath, scratch_pool));

  /* Well, the coast is clear for blowing away the administrative
     directory, which also removes the lock */
  SVN_ERR(svn_wc__db_temp_forget_directory(db, dir_abspath, scratch_pool));

  adm_abspath = svn_wc__adm_child(dir_abspath, NULL, scratch_pool);
  SVN_ERR(svn_io_remove_dir2(adm_abspath, FALSE, NULL, NULL, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__adm_cleanup_tmp_area(svn_wc__db_t *db,
                             const char *adm_abspath,
                             apr_pool_t *scratch_pool)
{
  const char *tmp_path;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(adm_abspath));

  SVN_ERR(svn_wc__write_check(db, adm_abspath, scratch_pool));

  /* Get the path to the tmp area, and blow it away. */
  tmp_path = svn_wc__adm_child(adm_abspath, SVN_WC__ADM_TMP, scratch_pool);

  SVN_ERR(svn_io_remove_dir2(tmp_path, TRUE, NULL, NULL, scratch_pool));

  /* Now, rebuild the tmp area. */
  return init_adm_tmp_area(adm_abspath, scratch_pool);
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
