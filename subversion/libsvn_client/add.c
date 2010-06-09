/*
 * add.c:  wrappers around wc add/mkdir functionality.
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

#include "svn_private_config.h"



/*** Code. ***/

/* This structure is used as baton for enumerating the config entries
   in the auto-props section.
*/
typedef struct
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
  int i;

  while (apr_isspace(*str))
    str++;
  *pstr = str;
  i = strlen(str);
  while ((i > 0) && apr_isspace(str[i-1]))
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
  if (strlen(value) == 0)
    return TRUE;

  /* check if filename matches and return if it doesn't */
  if (apr_fnmatch(name, autoprops->filename, APR_FNM_CASE_BLIND) == APR_FNM_NOMATCH)
    return TRUE;

  split_props(&props, value, autoprops->pool);

  for (i = 0; i < props->nelts; i++)
    {
      int len;
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
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  svn_config_t *cfg;
  svn_boolean_t use_autoprops;
  auto_props_baton_t autoprops;

  /* initialisation */
  autoprops.properties = apr_hash_make(pool);
  autoprops.filename = svn_path_basename(path, pool);
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
                     svn_string_create("", pool));
    }

  *mimetype = autoprops.mimetype;
  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         svn_client_ctx_t *ctx,
         svn_wc_adm_access_t *adm_access,
         apr_pool_t *pool)
{
  apr_hash_t* properties;
  apr_hash_index_t *hi;
  const char *mimetype;
  svn_node_kind_t kind;
  svn_boolean_t is_special;

  /* Check to see if this is a special file. */
  SVN_ERR(svn_io_check_special_path(path, &kind, &is_special, pool));

  if (is_special)
    mimetype = NULL;
  else
    /* Get automatic properties */
    /* This may fail on write-only files:
       we open them to estimate file type.
       That's why we postpone the add until after this step. */
    SVN_ERR(svn_client__get_auto_props(&properties, &mimetype, path, ctx,
                                       pool));

  /* Add the file */
  SVN_ERR(svn_wc_add3(path, adm_access, svn_depth_infinity, NULL,
                      SVN_INVALID_REVNUM, ctx->cancel_func, ctx->cancel_baton,
                      NULL, NULL, pool));

  if (is_special)
    /* This must be a special file. */
    SVN_ERR(svn_wc_prop_set3
            (SVN_PROP_SPECIAL,
             svn_string_create(SVN_PROP_BOOLEAN_TRUE, pool),
             path, adm_access, FALSE, NULL, NULL, pool));
  else if (properties)
    {
      /* loop through the hashtable and add the properties */
      for (hi = apr_hash_first(pool, properties);
           hi != NULL; hi = apr_hash_next(hi))
        {
          const void *pname;
          void *pval;
          svn_error_t *err;

          apr_hash_this(hi, &pname, NULL, &pval);
          /* It's probably best to pass 0 for force, so that if
             the autoprops say to set some weird combination,
             we just error and let the user sort it out. */
          err = svn_wc_prop_set3(pname, pval, path, adm_access, FALSE,
                                 NULL, NULL, pool);
          if (err)
            {
              /* Don't leave the job half-done. If we fail to set a property,
               * (try to) un-add the file. */
              svn_error_clear(svn_wc_revert3(path, adm_access,
                                             svn_depth_empty,
                                             FALSE /* use_commit_times */,
                                             NULL /* changelists */,
                                             NULL, NULL, NULL, NULL,
                                             pool));
              return err;
            }
        }
    }

  /* Report the addition to the caller. */
  if (ctx->notify_func2 != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(path, svn_wc_notify_add,
                                                     pool);
      notify->kind = svn_node_file;
      notify->mime_type = mimetype;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* Schedule directory DIRNAME, and some of the tree under it, for
 * addition with access baton ADM_ACCESS.  DEPTH is the depth at this
 * point in the descent (it may be changed for recursive calls).
 *
 * If DIRNAME (or any item below directory DIRNAME) is already scheduled for
 * addition, add will fail and return an error unless FORCE is TRUE.
 *
 * Files and directories that match ignore patterns will not be added unless
 * NO_IGNORE is TRUE.
 *
 * If CTX->CANCEL_FUNC is non-null, call it with CTX->CANCEL_BATON to allow
 * the user to cancel the operation
 */
static svn_error_t *
add_dir_recursive(const char *dirname,
                  svn_wc_adm_access_t *adm_access,
                  svn_depth_t depth,
                  svn_boolean_t force,
                  svn_boolean_t no_ignore,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  apr_dir_t *dir;
  apr_finfo_t this_entry;
  svn_error_t *err;
  apr_pool_t *subpool;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;
  svn_wc_adm_access_t *dir_access;
  apr_array_header_t *ignores;

  /* Check cancellation; note that this catches recursive calls too. */
  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  /* Add this directory to revision control. */
  err = svn_wc_add3(dirname, adm_access, svn_depth_infinity, NULL,
                    SVN_INVALID_REVNUM, ctx->cancel_func, ctx->cancel_baton,
                    ctx->notify_func2, ctx->notify_baton2, pool);
  if (err && err->apr_err == SVN_ERR_ENTRY_EXISTS && force)
    svn_error_clear(err);
  else if (err)
    return err;

  SVN_ERR(svn_wc_adm_retrieve(&dir_access, adm_access, dirname, pool));

  if (!no_ignore)
    SVN_ERR(svn_wc_get_ignores(&ignores, ctx->config, dir_access, pool));

  subpool = svn_pool_create(pool);

  SVN_ERR(svn_io_dir_open(&dir, dirname, pool));

  /* Read the directory entries one by one and add those things to
     version control. */
  while (1)
    {
      const char *fullpath;

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
                return svn_error_wrap_apr
                  (apr_err, _("Can't close directory '%s'"),
                   svn_path_local_style(dirname, subpool));
              break;
            }
          else
            {
              return svn_error_createf
                (err->apr_err, err,
                 _("Error during add of '%s'"),
                 svn_path_local_style(dirname, subpool));
            }
        }

      /* Skip entries for this dir and its parent.  */
      if (this_entry.name[0] == '.'
          && (this_entry.name[1] == '\0'
              || (this_entry.name[1] == '.' && this_entry.name[2] == '\0')))
        continue;

      /* Check cancellation so you can cancel during an
       * add of a directory with lots of files. */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Skip over SVN admin directories. */
      if (svn_wc_is_adm_dir(this_entry.name, subpool))
        continue;

      if ((!no_ignore) && svn_wc_match_ignore_list(this_entry.name,
                                                   ignores, subpool))
        continue;

      /* Construct the full path of the entry. */
      fullpath = svn_path_join(dirname, this_entry.name, subpool);

      /* Recurse on directories; add files; ignore the rest. */
      if (this_entry.filetype == APR_DIR && depth >= svn_depth_immediates)
        {
          svn_depth_t depth_below_here = depth;
          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(add_dir_recursive(fullpath, dir_access, depth_below_here,
                                    force, no_ignore, ctx, subpool));
        }
      else if (this_entry.filetype != APR_UNKFILE
               && this_entry.filetype != APR_DIR
               && depth >= svn_depth_files)
        {
          err = add_file(fullpath, ctx, dir_access, subpool);
          if (err && err->apr_err == SVN_ERR_ENTRY_EXISTS && force)
            svn_error_clear(err);
          else if (err)
            return err;
        }
    }

  /* Opened by svn_wc_add */
  SVN_ERR(svn_wc_adm_close2(dir_access, subpool));

  /* Destroy the per-iteration pool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* The main logic of the public svn_client_add4;  the only difference
   is that this function uses an existing access baton.
   (svn_client_add4 just generates an access baton and calls this func.) */
static svn_error_t *
add(const char *path,
    svn_depth_t depth,
    svn_boolean_t force,
    svn_boolean_t no_ignore,
    svn_wc_adm_access_t *adm_access,
    svn_client_ctx_t *ctx,
    apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    {
      /* We use add_dir_recursive for all directory targets
         and pass depth along no matter what it is, so that the
         target's depth will be set correctly. */
      err = add_dir_recursive(path, adm_access, depth,
                              force, no_ignore, ctx, pool);
    }
  else if (kind == svn_node_file)
    err = add_file(path, ctx, adm_access, pool);
  else
    err = svn_wc_add3(path, adm_access, depth, NULL, SVN_INVALID_REVNUM,
                      ctx->cancel_func, ctx->cancel_baton,
                      ctx->notify_func2, ctx->notify_baton2, pool);

  /* Ignore SVN_ERR_ENTRY_EXISTS when FORCE is set.  */
  if (err && err->apr_err == SVN_ERR_ENTRY_EXISTS && force)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  return err;
}


/* Go up the directory tree, looking for a versioned directory.  If found,
   add all the intermediate directories.  Otherwise, return
   SVN_ERR_CLIENT_NO_VERSIONED_PARENT. */
static svn_error_t *
add_parent_dirs(const char *path,
                svn_wc_adm_access_t **parent_access,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_error_t *err;

  err = svn_wc_adm_open3(&adm_access, NULL, path, TRUE, 0,
                         ctx->cancel_func, ctx->cancel_baton, pool);

  if (err && err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
    {
      svn_error_clear(err);
      if (svn_dirent_is_root(path, strlen(path)))
        {
          return svn_error_create
            (SVN_ERR_CLIENT_NO_VERSIONED_PARENT, NULL, NULL);
        }
      else if (svn_wc_is_adm_dir(svn_path_basename(path, pool), pool))
        {
          return svn_error_createf
            (SVN_ERR_RESERVED_FILENAME_SPECIFIED, NULL,
             _("'%s' ends in a reserved name"),
             svn_path_local_style(path, pool));
        }
      else
        {
          const char *parent_path = svn_path_dirname(path, pool);

          SVN_ERR(add_parent_dirs(parent_path, &adm_access, ctx, pool));
          SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, parent_path,
                                      pool));
          SVN_ERR(svn_wc_add3(path, adm_access, svn_depth_infinity,
                              NULL, SVN_INVALID_REVNUM,
                              ctx->cancel_func, ctx->cancel_baton,
                              ctx->notify_func2, ctx->notify_baton2, pool));
        }
    }
  else if (err)
    {
      return err;
    }

  if (parent_access)
    *parent_access = adm_access;

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
  svn_error_t *err, *err2;
  svn_wc_adm_access_t *adm_access;
  const char *parent_dir;

  if (add_parents)
    {
      apr_pool_t *subpool;

      SVN_ERR(svn_path_get_absolute(&path, path, pool));
      parent_dir = svn_path_dirname(path, pool);

      subpool = svn_pool_create(pool);
      SVN_ERR(add_parent_dirs(parent_dir, &adm_access, ctx, subpool));
      SVN_ERR(svn_wc_adm_close2(adm_access, subpool));
      svn_pool_destroy(subpool);
    }
  else
    {
      parent_dir = svn_path_dirname(path, pool);
    }

  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, parent_dir,
                           TRUE, 0, ctx->cancel_func, ctx->cancel_baton,
                           pool));

  err = add(path, depth, force, no_ignore, adm_access, ctx, pool);

  err2 = svn_wc_adm_close2(adm_access, pool);
  if (err2)
    {
      if (err)
        svn_error_clear(err2);
      else
        err = err2;
    }

  return err;
}


static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *path,
                    apr_pool_t *pool)
{
  const svn_delta_editor_t *editor = callback_baton;
  SVN_ERR(svn_path_check_valid(path, pool));
  return editor->add_directory(path, parent_baton, NULL,
                               SVN_INVALID_REVNUM, pool, dir_baton);
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
  const char *parent_url;

  svn_path_split(url, &parent_url, NULL, pool);

  SVN_ERR(svn_ra_reparent(ra_session, parent_url, temppool));
  SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                            temppool));

  if (kind == svn_node_none)
    SVN_ERR(add_url_parents(ra_session, parent_url, targets, temppool, pool));

  APR_ARRAY_PUSH(targets, const char *) = url;

  return SVN_NO_ERROR;
}

static svn_error_t *
mkdir_urls(svn_commit_info_t **commit_info_p,
           const apr_array_header_t *urls,
           svn_boolean_t make_parents,
           const apr_hash_t *revprop_table,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  svn_ra_session_t *ra_session = NULL;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *commit_baton;
  const char *log_msg;
  apr_array_header_t *targets;
  apr_hash_t *targets_hash;
  apr_hash_t *commit_revprops;
  svn_error_t *err;
  const char *common;
  int i;

  /* Find any non-existent parent directories */
  if (make_parents)
    {
      apr_array_header_t *all_urls = apr_array_make(pool, urls->nelts,
                                                    sizeof(const char *));
      const char *first_url = APR_ARRAY_IDX(urls, 0, const char *);
      apr_pool_t *iterpool = svn_pool_create(pool);

      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, first_url,
                                                   NULL, NULL, NULL, FALSE,
                                                   TRUE, ctx, pool));

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
  SVN_ERR(svn_path_condense_targets(&common, &targets, urls, FALSE, pool));
  SVN_ERR(svn_hash_from_cstring_keys(&targets_hash, targets, pool));
  SVN_ERR(svn_hash_keys(&targets, targets_hash, pool));

  if (! targets->nelts)
    {
      const char *bname;
      svn_path_split(common, &common, &bname, pool);
      APR_ARRAY_PUSH(targets, const char *) = bname;
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
          svn_path_split(common, &common, &bname, pool);
          for (i = 0; i < targets->nelts; i++)
            {
              const char *path = APR_ARRAY_IDX(targets, i, const char *);
              path = svn_path_join(bname, path, pool);
              APR_ARRAY_IDX(targets, i, const char *) = path;
            }
        }
    }
  qsort(targets->elts, targets->nelts, targets->elt_size,
        svn_sort_compare_paths);

  /* ### This reparent may be problematic in limited-authz-to-common-parent
     ### scenarios (compare issue #3242).  See also issue #3496. */
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
          item->url = svn_path_join(common, path, pool);
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
    SVN_ERR(svn_client__open_ra_session_internal(&ra_session, common, NULL,
                                                 NULL, NULL, FALSE, TRUE,
                                                 ctx, pool));

  /* URI-decode each target. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      path = svn_path_uri_decode(path, pool);
      APR_ARRAY_IDX(targets, i, const char *) = path;
    }

  /* Fetch RA commit editor */
  SVN_ERR(svn_client__commit_get_baton(&commit_baton, commit_info_p, pool));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    svn_client__commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  /* Call the path-based editor driver. */
  err = svn_delta_path_driver(editor, edit_baton, SVN_INVALID_REVNUM,
                              targets, path_driver_cb_func,
                              (void *)editor, pool);
  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear(editor->abort_edit(edit_baton, pool));
      return err;
    }

  /* Close the edit. */
  return editor->close_edit(edit_baton, pool);
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

  return err;
}


svn_error_t *
svn_client_mkdir3(svn_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_boolean_t make_parents,
                  const apr_hash_t *revprop_table,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  if (! paths->nelts)
    return SVN_NO_ERROR;

  if (svn_path_is_url(APR_ARRAY_IDX(paths, 0, const char *)))
    {
      SVN_ERR(mkdir_urls(commit_info_p, paths, make_parents,
                         revprop_table, ctx, pool));
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
