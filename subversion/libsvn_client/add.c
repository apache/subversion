/*
 * add.c:  wrappers around wc add/mkdir functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include <apr_lib.h>
#include <apr_fnmatch.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "client.h"
#include "svn_ctype.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_magic.h"

#include "svn_private_config.h"



/*** Code. ***/

/* This structure is used as baton for enumerating the config entries
   in the auto-props section.
*/
typedef struct auto_props_baton_t
{
  /* the file name for which properties are searched */
  const char *filename;

  /* when this flag is set the hash contains svn:executable */
  svn_boolean_t have_executable;

  /* when mimetype is not NULL is set the hash contains svn:mime-type */
  const char *mimetype;

  /* the hash table for storing the property name/value pairs */
  apr_hash_t *properties;

  /* a pool used for allocating memory */
  apr_pool_t *pool;
} auto_props_baton_t;

/* Remove leading and trailing white space from a C string, in place. */
static void
trim_string(char **pstr)
{
  char *str = *pstr;
  size_t i;

  while (svn_ctype_isspace(*str))
    str++;
  *pstr = str;
  i = strlen(str);
  while ((i > 0) && svn_ctype_isspace(str[i-1]))
    i--;
  str[i] = '\0';
}

/* Remove leading and trailing single- or double quotes from a C string,
 * in place. */
static void
unquote_string(char **pstr)
{
  char *str = *pstr;
  size_t i = strlen(str);

  if (i > 0 && ((*str == '"' && str[i - 1] == '"') ||
                (*str == '\'' && str[i - 1] == '\'')))
    {
      str[i - 1] = '\0';
      str++;
    }
  *pstr = str;
}

/* Split PROPERTY and store each individual value in PROPS.
   Allocates from POOL. */
static void
split_props(apr_array_header_t **props,
            const char *property,
            apr_pool_t *pool)
{
  apr_array_header_t *temp_props;
  char *new_prop;
  int i = 0;
  int j = 0;

  temp_props = apr_array_make(pool, 4, sizeof(char *));
  new_prop = apr_palloc(pool, strlen(property)+1);

  for (i = 0; property[i] != '\0'; i++)
    {
      if (property[i] != ';')
        {
          new_prop[j] = property[i];
          j++;
        }
      else if (property[i] == ';')
        {
          /* ";;" becomes ";" */
          if (property[i+1] == ';')
            {
              new_prop[j] = ';';
              j++;
              i++;
            }
          else
            {
              new_prop[j] = '\0';
              APR_ARRAY_PUSH(temp_props, char *) = new_prop;
              new_prop += j + 1;
              j = 0;
            }
        }
    }
  new_prop[j] = '\0';
  APR_ARRAY_PUSH(temp_props, char *) = new_prop;
  *props = temp_props;
}

/* For one auto-props config entry (NAME, VALUE), if the filename pattern
   NAME matches BATON->filename case insensitively then add the properties
   listed in VALUE into BATON->properties.
   BATON must point to an auto_props_baton_t.
*/
static svn_boolean_t
auto_props_enumerator(const char *name,
                      const char *value,
                      void *baton,
                      apr_pool_t *pool)
{
  int i;
  auto_props_baton_t *autoprops = baton;
  apr_array_header_t *props;

  /* nothing to do here without a value */
  if (*value == 0)
    return TRUE;

  /* check if filename matches and return if it doesn't */
  if (apr_fnmatch(name, autoprops->filename, APR_FNM_CASE_BLIND) == APR_FNM_NOMATCH)
    return TRUE;

  split_props(&props, value, autoprops->pool);

  for (i = 0; i < props->nelts; i++)
    {
      size_t len;
      const char *this_value;
      char *property = APR_ARRAY_IDX(props, i, char *);
      char *equal_sign = strchr(property, '=');

      if (equal_sign)
        {
          *equal_sign = '\0';
          equal_sign++;
          trim_string(&equal_sign);
          unquote_string(&equal_sign);
          this_value = equal_sign;
        }
      else
        {
          this_value = "";
        }
      trim_string(&property);
      len = strlen(property);

      if (len > 0)
        {
          svn_string_t *propval = apr_palloc(autoprops->pool,
                                             sizeof(*propval));
          propval->data = this_value;
          propval->len = strlen(this_value);

          apr_hash_set(autoprops->properties, property, len, propval);
          if (strcmp(property, SVN_PROP_MIME_TYPE) == 0)
            autoprops->mimetype = this_value;
          else if (strcmp(property, SVN_PROP_EXECUTABLE) == 0)
            autoprops->have_executable = TRUE;
        }
    }
  return TRUE;
}

svn_error_t *
svn_client__get_auto_props(apr_hash_t **properties,
                           const char **mimetype,
                           const char *path,
                           svn_magic__cookie_t *magic_cookie,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  svn_config_t *cfg;
  svn_boolean_t use_autoprops;
  auto_props_baton_t autoprops;

  /* initialisation */
  autoprops.properties = apr_hash_make(pool);
  autoprops.filename = svn_dirent_basename(path, pool);
  autoprops.pool = pool;
  autoprops.mimetype = NULL;
  autoprops.have_executable = FALSE;
  *properties = autoprops.properties;

  cfg = ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                                   APR_HASH_KEY_STRING) : NULL;

  /* check that auto props is enabled */
  SVN_ERR(svn_config_get_bool(cfg, &use_autoprops,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, FALSE));

  /* search for auto props */
  if (use_autoprops)
    svn_config_enumerate2(cfg, SVN_CONFIG_SECTION_AUTO_PROPS,
                          auto_props_enumerator, &autoprops, pool);

  /* if mimetype has not been set check the file */
  if (! autoprops.mimetype)
    {
      SVN_ERR(svn_io_detect_mimetype2(&autoprops.mimetype, path,
                                      ctx->mimetypes_map, pool));

      /* If we got no mime-type, or if it is "application/octet-stream",
       * try to get the mime-type from libmagic. */
      if (magic_cookie &&
          (!autoprops.mimetype ||
           strcmp(autoprops.mimetype, "application/octet-stream") == 0))
        {
          const char *magic_mimetype;

         /* Since libmagic usually treats UTF-16 files as "text/plain",
          * svn_magic__detect_binary_mimetype() will return NULL for such
          * files. This is fine for now since we currently don't support
          * UTF-16-encoded text files (issue #2194).
          * Once we do support UTF-16 this code path will fail to detect
          * them as text unless the svn_io_detect_mimetype2() call above
          * returns "text/plain" for them. */
          SVN_ERR(svn_magic__detect_binary_mimetype(&magic_mimetype,
                                                    path, magic_cookie,
                                                    pool, pool));
          if (magic_mimetype)
            autoprops.mimetype = magic_mimetype;
        }

      if (autoprops.mimetype)
        apr_hash_set(autoprops.properties, SVN_PROP_MIME_TYPE,
                     strlen(SVN_PROP_MIME_TYPE),
                     svn_string_create(autoprops.mimetype, pool));
    }

  /* if executable has not been set check the file */
  if (! autoprops.have_executable)
    {
      svn_boolean_t executable = FALSE;
      SVN_ERR(svn_io_is_file_executable(&executable, path, pool));
      if (executable)
        apr_hash_set(autoprops.properties, SVN_PROP_EXECUTABLE,
                     strlen(SVN_PROP_EXECUTABLE),
                     svn_string_create_empty(pool));
    }

  *mimetype = autoprops.mimetype;
  return SVN_NO_ERROR;
}

/* Only call this if the on-disk node kind is a file. */
static svn_error_t *
add_file(const char *local_abspath,
         svn_magic__cookie_t *magic_cookie,
         svn_client_ctx_t *ctx,
         apr_pool_t *pool)
{
  apr_hash_t* properties;
  apr_hash_index_t *hi;
  const char *mimetype;
  svn_node_kind_t kind;
  svn_boolean_t is_special;

  /* Check to see if this is a special file. */
  SVN_ERR(svn_io_check_special_path(local_abspath, &kind, &is_special, pool));

  if (is_special)
    mimetype = NULL;
  else
    /* Get automatic properties */
    /* This may fail on write-only files:
       we open them to estimate file type.
       That's why we postpone the add until after this step. */
    SVN_ERR(svn_client__get_auto_props(&properties, &mimetype, local_abspath,
                                       magic_cookie, ctx, pool));

  /* Add the file */
  SVN_ERR(svn_wc_add_from_disk(ctx->wc_ctx, local_abspath,
                               NULL, NULL, pool));

  if (is_special)
    /* This must be a special file. */
    SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, local_abspath, SVN_PROP_SPECIAL,
                             svn_string_create(SVN_PROP_BOOLEAN_TRUE, pool),
                             svn_depth_empty, FALSE, NULL,
                             NULL, NULL /* cancellation */,
                             NULL, NULL /* notification */,
                             pool));
  else if (properties)
    {
      /* loop through the hashtable and add the properties */
      for (hi = apr_hash_first(pool, properties);
           hi != NULL; hi = apr_hash_next(hi))
        {
          const char *pname = svn__apr_hash_index_key(hi);
          const svn_string_t *pval = svn__apr_hash_index_val(hi);
          svn_error_t *err;

          /* It's probably best to pass 0 for force, so that if
             the autoprops say to set some weird combination,
             we just error and let the user sort it out. */
          err = svn_wc_prop_set4(ctx->wc_ctx, local_abspath, pname, pval,
                                 svn_depth_empty, FALSE, NULL,
                                 NULL, NULL /* cancellation */,
                                 NULL, NULL /* notification */,
                                 pool);
          if (err)
            {
              /* Don't leave the job half-done. If we fail to set a property,
               * (try to) un-add the file. */
              svn_error_clear(svn_wc_revert4(ctx->wc_ctx,
                                             local_abspath,
                                             svn_depth_empty,
                                             FALSE /* use_commit_times */,
                                             NULL /* changelists */,
                                             NULL, NULL, NULL, NULL,
                                             pool));
              return svn_error_trace(err);
            }
        }
    }

  /* Report the addition to the caller. */
  if (ctx->notify_func2 != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     svn_wc_notify_add, pool);
      notify->kind = svn_node_file;
      notify->mime_type = mimetype;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* Schedule directory DIR_ABSPATH, and some of the tree under it, for
 * addition.  DEPTH is the depth at this
 * point in the descent (it may be changed for recursive calls).
 *
 * If DIR_ABSPATH (or any item below DIR_ABSPATH) is already scheduled for
 * addition, add will fail and return an error unless FORCE is TRUE.
 *
 * Files and directories that match ignore patterns will not be added unless
 * NO_IGNORE is TRUE.
 *
 * Use MAGIC_COOKIE (which may be NULL) to detect the mime-type of files
 * if necessary.
 *
 * If CTX->CANCEL_FUNC is non-null, call it with CTX->CANCEL_BATON to allow
 * the user to cancel the operation
 */
static svn_error_t *
add_dir_recursive(const char *dir_abspath,
                  svn_depth_t depth,
                  svn_boolean_t force,
                  svn_boolean_t no_ignore,
                  svn_magic__cookie_t *magic_cookie,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  apr_pool_t *iterpool;
  apr_array_header_t *ignores;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  /* Check cancellation; note that this catches recursive calls too. */
  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  iterpool = svn_pool_create(scratch_pool);

  /* Add this directory to revision control. */
  err = svn_wc_add_from_disk(ctx->wc_ctx, dir_abspath,
                             ctx->notify_func2, ctx->notify_baton2,
                             iterpool);
  if (err && err->apr_err == SVN_ERR_ENTRY_EXISTS && force)
    svn_error_clear(err);
  else if (err)
    return svn_error_trace(err);

  if (!no_ignore)
    {
      SVN_ERR(svn_wc_get_ignores2(&ignores, ctx->wc_ctx, dir_abspath,
                                  ctx->config, scratch_pool, iterpool));
    }

  SVN_ERR(svn_io_get_dirents3(&dirents, dir_abspath, TRUE, scratch_pool,
                              iterpool));

  /* Read the directory entries one by one and add those things to
     version control. */
  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_io_dirent2_t *dirent = svn__apr_hash_index_val(hi);
      const char *abspath;

      svn_pool_clear(iterpool);

      /* Check cancellation so you can cancel during an
       * add of a directory with lots of files. */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Skip over SVN admin directories. */
      if (svn_wc_is_adm_dir(name, iterpool))
        continue;

      if ((!no_ignore) && svn_wc_match_ignore_list(name, ignores, iterpool))
        continue;

      /* Construct the full path of the entry. */
      abspath = svn_dirent_join(dir_abspath, name, iterpool);

      /* Recurse on directories; add files; ignore the rest. */
      if (dirent->kind == svn_node_dir && depth >= svn_depth_immediates)
        {
          svn_depth_t depth_below_here = depth;
          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(add_dir_recursive(abspath, depth_below_here,
                                    force, no_ignore, magic_cookie,
                                    ctx, iterpool));
        }
      else if ((dirent->kind == svn_node_file || dirent->special)
               && depth >= svn_depth_files)
        {
          err = add_file(abspath, magic_cookie, ctx, iterpool);
          if (err && err->apr_err == SVN_ERR_ENTRY_EXISTS && force)
            svn_error_clear(err);
          else
            SVN_ERR(err);
        }
    }

  /* Destroy the per-iteration pool. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* The main logic of the public svn_client_add4.
 *
 * EXISTING_PARENT_ABSPATH is the absolute path to the first existing
 * parent directory of local_abspath. If not NULL, all missing parents
 * of LOCAL_ABSPATH must be created before LOCAL_ABSPATH can be added. */
static svn_error_t *
add(const char *local_abspath,
    svn_depth_t depth,
    svn_boolean_t force,
    svn_boolean_t no_ignore,
    const char *existing_parent_abspath,
    svn_client_ctx_t *ctx,
    apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;
  svn_magic__cookie_t *magic_cookie;

  svn_magic__init(&magic_cookie, scratch_pool);

  if (existing_parent_abspath)
    {
      const char *parent_abspath;
      const char *child_relpath;
      apr_array_header_t *components;
      int i;
      apr_pool_t *iterpool;

      parent_abspath = existing_parent_abspath;
      child_relpath = svn_dirent_is_child(existing_parent_abspath,
                                          local_abspath, NULL);
      components = svn_path_decompose(child_relpath, scratch_pool);
      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < components->nelts - 1; i++)
        {
          const char *component;
          svn_node_kind_t disk_kind;

          svn_pool_clear(iterpool);

          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          component = APR_ARRAY_IDX(components, i, const char *);
          parent_abspath = svn_dirent_join(parent_abspath, component,
                                           scratch_pool);
          SVN_ERR(svn_io_check_path(parent_abspath, &disk_kind, iterpool));
          if (disk_kind != svn_node_none && disk_kind != svn_node_dir)
            return svn_error_createf(SVN_ERR_CLIENT_NO_VERSIONED_PARENT, NULL,
                                     _("'%s' prevents creating parent of '%s'"),
                                     parent_abspath, local_abspath);

          SVN_ERR(svn_io_make_dir_recursively(parent_abspath, scratch_pool));
          SVN_ERR(svn_wc_add_from_disk(ctx->wc_ctx, parent_abspath,
                                       ctx->notify_func2, ctx->notify_baton2,
                                       scratch_pool));
        }
      svn_pool_destroy(iterpool);
    }

  SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));
  if (kind == svn_node_dir)
    {
      /* We use add_dir_recursive for all directory targets
         and pass depth along no matter what it is, so that the
         target's depth will be set correctly. */
      err = add_dir_recursive(local_abspath, depth, force, no_ignore,
                              magic_cookie, ctx, scratch_pool);
    }
  else if (kind == svn_node_file)
    err = add_file(local_abspath, magic_cookie, ctx, scratch_pool);
  else if (kind == svn_node_none)
    {
      svn_boolean_t tree_conflicted;

      /* Provide a meaningful error message if the node does not exist
       * on disk but is a tree conflict victim. */
      err = svn_wc_conflicted_p3(NULL, NULL, &tree_conflicted,
                                 ctx->wc_ctx, local_abspath,
                                 scratch_pool);
      if (err)
        svn_error_clear(err);
      else if (tree_conflicted)
        return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                                 _("'%s' is an existing item in conflict; "
                                   "please mark the conflict as resolved "
                                   "before adding a new item here"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));

      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("'%s' not found"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }
  else
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Unsupported node kind for path '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Ignore SVN_ERR_ENTRY_EXISTS when FORCE is set.  */
  if (err && err->apr_err == SVN_ERR_ENTRY_EXISTS && force)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  return svn_error_trace(err);
}


/* Go up the directory tree from LOCAL_ABSPATH, looking for a versioned
 * directory.  If found, return its path in *EXISTING_PARENT_ABSPATH.
 * Otherwise, return SVN_ERR_CLIENT_NO_VERSIONED_PARENT. */
static svn_error_t *
find_existing_parent(const char **existing_parent_abspath,
                     svn_client_ctx_t *ctx,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *parent_abspath;
  svn_wc_context_t *wc_ctx = ctx->wc_ctx;

  SVN_ERR(svn_wc_read_kind(&kind, wc_ctx, local_abspath, FALSE, scratch_pool));

  if (kind == svn_node_dir)
    {
      svn_boolean_t is_deleted;

      SVN_ERR(svn_wc__node_is_status_deleted(&is_deleted,
                                             wc_ctx, local_abspath,
                                             scratch_pool));
      if (!is_deleted)
        {
          *existing_parent_abspath = apr_pstrdup(result_pool, local_abspath);
          return SVN_NO_ERROR;
        }
    }

  if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
    return svn_error_create(SVN_ERR_CLIENT_NO_VERSIONED_PARENT, NULL, NULL);

  if (svn_wc_is_adm_dir(svn_dirent_basename(local_abspath, scratch_pool),
                        scratch_pool))
    return svn_error_createf(SVN_ERR_RESERVED_FILENAME_SPECIFIED, NULL,
                             _("'%s' ends in a reserved name"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  SVN_ERR(find_existing_parent(existing_parent_abspath, ctx, parent_abspath,
                               result_pool, scratch_pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_client_add4(const char *path,
                svn_depth_t depth,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_boolean_t add_parents,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  const char *parent_abspath;
  const char *local_abspath;
  const char *existing_parent_abspath;
  svn_boolean_t is_wc_root;
  svn_error_t *err;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* See if we're being asked to add a wc-root.  That's typically not
     okay, unless we're in "force" mode.  svn_wc__strictly_is_wc_root()
     will return TRUE even if LOCAL_ABSPATH is a *symlink* to a working
     copy root, which is a scenario we want to treat differently.  */
  err = svn_wc__strictly_is_wc_root(&is_wc_root, ctx->wc_ctx,
                                    local_abspath, pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND
          && err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
        {
          return svn_error_trace(err);
        }

      svn_error_clear(err);
      err = NULL; /* SVN_NO_ERROR */
      is_wc_root = FALSE;
    }
  if (is_wc_root)
    {
#ifdef HAVE_SYMLINK
      svn_node_kind_t disk_kind;
      svn_boolean_t is_special;

      SVN_ERR(svn_io_check_special_path(local_abspath, &disk_kind, &is_special,
                                        pool));

      /* A symlink can be an unversioned target and a wcroot. Lets try to add
         the symlink, which can't be a wcroot. */
      if (is_special)
        is_wc_root = FALSE;
      else
#endif
        {
          if (! force)
            return svn_error_createf(
                                 SVN_ERR_ENTRY_EXISTS, NULL,
                                 _("'%s' is already under version control"),
                                 svn_dirent_local_style(local_abspath, pool));
        }
    }

  if (is_wc_root)
    parent_abspath = local_abspath; /* We will only add children */
  else
    parent_abspath = svn_dirent_dirname(local_abspath, pool);

  existing_parent_abspath = NULL;
  if (add_parents && !is_wc_root)
    {
      apr_pool_t *subpool;
      const char *existing_parent_abspath2;

      subpool = svn_pool_create(pool);
      SVN_ERR(find_existing_parent(&existing_parent_abspath2, ctx,
                                   parent_abspath, pool, subpool));
      if (strcmp(existing_parent_abspath2, parent_abspath) != 0)
        existing_parent_abspath = existing_parent_abspath2;
      svn_pool_destroy(subpool);
    }

  SVN_WC__CALL_WITH_WRITE_LOCK(
    add(local_abspath, depth, force, no_ignore, existing_parent_abspath,
        ctx, pool),
    ctx->wc_ctx, (existing_parent_abspath ? existing_parent_abspath 
                                          : parent_abspath),
    FALSE /* lock_anchor */, pool);
  return SVN_NO_ERROR;
}


/* Append URL, and all it's non-existent parent directories, to TARGETS.
   Use TEMPPOOL for temporary allocations and POOL for any additions to
   TARGETS. */
static svn_error_t *
add_url_parents(svn_ra_session_t *ra_session,
                const char *url,
                apr_array_header_t *targets,
                apr_pool_t *temppool,
                apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *parent_url = svn_uri_dirname(url, pool);

  SVN_ERR(svn_ra_reparent(ra_session, parent_url, temppool));
  SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                            temppool));

  if (kind == svn_node_none)
    SVN_ERR(add_url_parents(ra_session, parent_url, targets, temppool, pool));

  APR_ARRAY_PUSH(targets, const char *) = url;

  return SVN_NO_ERROR;
}

static svn_error_t *
drive_editor(svn_editor_t *editor,
             apr_array_header_t *targets,
             apr_hash_t *children_hash,
             const char *base_relpath,
             apr_pool_t *scratch_pool)
{
  apr_hash_t *empty_props = apr_hash_make(scratch_pool);
  apr_pool_t *iterpool;
  svn_error_t *err;
  int i;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      apr_array_header_t *children;

      svn_pool_clear(iterpool);

      children = apr_hash_get(children_hash, path, APR_HASH_KEY_STRING);
      if (!children)
        children = apr_array_make(iterpool, 1, sizeof(const char *));

      SVN_ERR(svn_path_check_valid(path, iterpool));
      path = svn_relpath_join(base_relpath, path, iterpool);
      err = svn_editor_add_directory(editor, path, children, empty_props,
                                     SVN_INVALID_REVNUM);
      if (err)
        break;
    }
  svn_pool_destroy(iterpool);

  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear(svn_editor_abort(editor));
      return svn_error_trace(err);
    }

  /* Complete the edit. */
  return svn_error_trace(svn_editor_complete(editor));
}


static svn_error_t *
mkdir_urls(const apr_array_header_t *urls,
           svn_boolean_t make_parents,
           const apr_hash_t *revprop_table,
           svn_commit_callback2_t commit_callback,
           void *commit_baton,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  svn_ra_session_t *ra_session = NULL;
  svn_editor_t *editor;
  const char *log_msg;
  apr_array_header_t *targets;
  apr_hash_t *targets_hash;
  apr_hash_t *commit_revprops;
  apr_hash_t *children_hash;
  const char *repos_root;
  const char *base_relpath;
  const char *common;
  int i;

  /* Find any non-existent parent directories */
  if (make_parents)
    {
      apr_array_header_t *all_urls = apr_array_make(pool, urls->nelts,
                                                    sizeof(const char *));
      const char *first_url = APR_ARRAY_IDX(urls, 0, const char *);
      apr_pool_t *iterpool = svn_pool_create(pool);

      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL,
                                                   first_url, NULL, NULL,
                                                   FALSE, TRUE, ctx, pool));

      for (i = 0; i < urls->nelts; i++)
        {
          const char *url = APR_ARRAY_IDX(urls, i, const char *);

          svn_pool_clear(iterpool);
          SVN_ERR(add_url_parents(ra_session, url, all_urls, iterpool, pool));
        }

      svn_pool_destroy(iterpool);

      urls = all_urls;
    }

  /* Condense our list of mkdir targets. */
  SVN_ERR(svn_uri_condense_targets(&common, &targets, urls, FALSE,
                                   pool, pool));

  /*Remove duplicate targets introduced by make_parents with more targets. */
  SVN_ERR(svn_hash_from_cstring_keys(&targets_hash, targets, pool));
  SVN_ERR(svn_hash_keys(&targets, targets_hash, pool));

  if (! targets->nelts)
    {
      const char *bname;
      svn_uri_split(&common, &bname, common, pool);
      APR_ARRAY_PUSH(targets, const char *) = bname;

      if (*bname == '\0')
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("There is no valid uri above '%s'"),
                                 common);
    }
  else
    {
      svn_boolean_t resplit = FALSE;

      /* We can't "mkdir" the root of an editor drive, so if one of
         our targets is the empty string, we need to back everything
         up by a path component. */
      for (i = 0; i < targets->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(targets, i, const char *);
          if (! *path)
            {
              resplit = TRUE;
              break;
            }
        }
      if (resplit)
        {
          const char *bname;

          svn_uri_split(&common, &bname, common, pool);

          if (*bname == '\0')
             return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                      _("There is no valid uri above '%s'"),
                                      common);

          for (i = 0; i < targets->nelts; i++)
            {
              const char *path = APR_ARRAY_IDX(targets, i, const char *);
              path = svn_relpath_join(bname, path, pool);
              APR_ARRAY_IDX(targets, i, const char *) = path;
            }
        }
    }
  qsort(targets->elts, targets->nelts, targets->elt_size,
        svn_sort_compare_paths);

  /* ### This reparent may be problematic in limited-authz-to-common-parent
     ### scenarios (compare issue #3242).  See also issue #3649. */
  if (ra_session)
    SVN_ERR(svn_ra_reparent(ra_session, common, pool));

  /* Create new commit items and add them to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(pool, targets->nelts, sizeof(item));

      for (i = 0; i < targets->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(targets, i, const char *);

          item = svn_client_commit_item3_create(pool);
          item->url = svn_path_url_add_component2(common, path, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
        }

      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      ctx, pool));

      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, pool));

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  if (!ra_session)
    SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, common,
                                                 NULL, NULL, FALSE, TRUE,
                                                 ctx, pool));

  children_hash = apr_hash_make(pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      const char *parent;
      const char *base_name;
      apr_array_header_t *children;
      
      svn_relpath_split(&parent, &base_name, path, pool);

      children = apr_hash_get(children_hash, parent, APR_HASH_KEY_STRING);
      if (!children)
        {
          children = apr_array_make(pool, 1, sizeof(const char *));
          apr_hash_set(children_hash, parent, APR_HASH_KEY_STRING, children);
        }

      APR_ARRAY_PUSH(children, const char *) = base_name;
    }

  /* Calculate the base_relpath. */
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));
  base_relpath = svn_uri_skip_ancestor(repos_root, common, pool);

  /* Fetch RA commit editor */
  SVN_ERR(svn_ra__get_commit_ev2(&editor, ra_session,
                                 commit_revprops,
                                 commit_callback,
                                 commit_baton,
                                 NULL, TRUE, /* No lock tokens */
                                 NULL, NULL, NULL, NULL,
                                 pool, pool));

  return svn_error_trace(drive_editor(editor, targets, children_hash,
                                      base_relpath, pool));
}



svn_error_t *
svn_client__make_local_parents(const char *path,
                               svn_boolean_t make_parents,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_node_kind_t orig_kind;
  SVN_ERR(svn_io_check_path(path, &orig_kind, pool));
  if (make_parents)
    SVN_ERR(svn_io_make_dir_recursively(path, pool));
  else
    SVN_ERR(svn_io_dir_make(path, APR_OS_DEFAULT, pool));

  /* Should no longer use svn_depth_empty to indicate that only the directory
     itself is added, since it not only constraints the operation depth, but
     also defines the depth of the target directory now. Moreover, the new
     directory will have no children at all.*/
  err = svn_client_add4(path, svn_depth_infinity, FALSE, FALSE,
                        make_parents, ctx, pool);

  /* If we created a new directory, but couldn't add it to version
     control, then delete it. */
  if (err && (orig_kind == svn_node_none))
    {
      /* ### If this returns an error, should we link it onto
         err instead, so that the user is warned that we just
         created an unversioned directory? */

      svn_error_clear(svn_io_remove_dir2(path, FALSE, NULL, NULL, pool));
    }

  return svn_error_trace(err);
}


svn_error_t *
svn_client_mkdir4(const apr_array_header_t *paths,
                  svn_boolean_t make_parents,
                  const apr_hash_t *revprop_table,
                  svn_commit_callback2_t commit_callback,
                  void *commit_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  if (! paths->nelts)
    return SVN_NO_ERROR;

  SVN_ERR(svn_client__assert_homogeneous_target_type(paths));

  if (svn_path_is_url(APR_ARRAY_IDX(paths, 0, const char *)))
    {
      SVN_ERR(mkdir_urls(paths, make_parents, revprop_table, commit_callback,
                         commit_baton, ctx, pool));
    }
  else
    {
      /* This is a regular "mkdir" + "svn add" */
      apr_pool_t *subpool = svn_pool_create(pool);
      int i;

      for (i = 0; i < paths->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(paths, i, const char *);

          svn_pool_clear(subpool);

          /* See if the user wants us to stop. */
          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          SVN_ERR(svn_client__make_local_parents(path, make_parents, ctx,
                                                 subpool));
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
