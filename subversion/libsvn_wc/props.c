/*
 * props.c :  routines dealing with properties in the working copy
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



#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_general.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_mergeinfo.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "svn_diff.h"

#include "private/svn_wc_private.h"
#include "private/svn_mergeinfo_private.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "entries.h"
#include "props.h"
#include "translate.h"
#include "questions.h"
#include "lock.h"

#include "svn_private_config.h"

/*---------------------------------------------------------------------*/

/*** Deducing local changes to properties ***/

/*---------------------------------------------------------------------*/

/*** Reading/writing property hashes from disk ***/

/* ### HKW/WC-NG: This is a summary of the my efforts to localize accesses to
   svn_wc__prop_path(), in preparation to moving to the wc_db API.  The
   general idea here is to combine all calls to svn_wc__prop_path() into a
   single one, which will make conversion to the new db API easier.  The catch
   is our currently loggy implementation, which uses the properties file path
   all over the place.  Rather than attempt to refactor all of those right
   now, I'm going to punt and leave properties loggy-ness to the SQLite
   transaction mechanism.

   Also, several of these could be funnelled through the load_props()
   interface, but for the fact that it may complicate handling of propcaching.
   I'm happy to do that, but I'm wary of killing performance right before
   branching 1.6, so those will happen after we branch, and trunk is once
   again a free-for-all.

   The following functions currently call this API:
     load_props(): The current "gateway" function through we all access to
       properties should be funneled.
     svn_wc__working_props_committed(): Moves WORKING props to BASE props,
       sync'ing to disk and clearing appropriate caches.
     empty_props_p(): Used to determine if a path has properties or not.
     svn_wc__props_last_modified(): Reads mtime of the props file.  Used
       externally to answer the question about when a node changed.  We can
       answer this question other ways in the SQLite schema, so this function
       may disappear.
     svn_wc_props_modified_p(): Used to shortcut property differences by
       checking property filesize differences.
     install_props_file(): Used with loggy.
     svn_wc__install_props(): Used with loggy.
     svn_wc__loggy_props_delete(): Used with loggy.
     svn_wc__loggy_revert_props_create(): Used with loggy.
     svn_wc__loggy_revert_props_restore(): Used with loggy.
 */

/* The real functionality here is part of libsvn_subr, in hashdump.c.
   But these are convenience routines for use in libsvn_wc. */

/* Get PATH's properies of PROPS_KIND, and put them into *HASH.
   PATH should be of kind NODE_KIND. */
static svn_error_t *
load_props(apr_hash_t **hash,
           const char *path,
           svn_node_kind_t node_kind,
           svn_wc__props_kind_t props_kind,
           apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stream_t *stream;
  apr_finfo_t finfo;
  const char *prop_path;

  SVN_ERR(svn_wc__prop_path(&prop_path, path, node_kind, props_kind, pool));

  *hash = apr_hash_make(pool);
  /* We shouldn't be calling load_prop_file() with an empty file, but
     we do.  This check makes sure that we don't call svn_hash_read2()
     on an empty stream.  Ugly, hacky and crude. */
  err = svn_io_stat(&finfo, prop_path, APR_FINFO_SIZE, pool);
  if (err)
    {
      if (APR_STATUS_IS_ENOENT(err->apr_err)
            || APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      else
        return err;
    }
  if (finfo.size == 0)
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_open_readonly(&stream, prop_path, pool, pool));

  SVN_ERR(svn_hash_read2(*hash, stream, SVN_HASH_TERMINATOR, pool));

  return svn_stream_close(stream);
}



/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH.  If WRITE_EMPTY is TRUE then writing
   an emtpy property hash will result in an actual empty property
   file on disk, otherwise an empty hash will result in no file
   being written at all. */
static svn_error_t *
save_prop_tmp_file(const char **tmp_file_path,
                   apr_hash_t *hash,
                   const char *tmp_base_dir,
                   svn_boolean_t write_empty,
                   apr_pool_t *pool)
{
  svn_stream_t *stream;

  SVN_ERR(svn_stream_open_unique(&stream, tmp_file_path, tmp_base_dir,
                                 svn_io_file_del_none, pool, pool));

  if (apr_hash_count(hash) != 0 || write_empty)
    SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, pool));

  return svn_stream_close(stream);
}


/*---------------------------------------------------------------------*/

/*** Misc ***/

/* Opens reject temporary stream for FULL_PATH in the appropriate tmp space. */
static svn_error_t *
open_reject_tmp_stream(svn_stream_t **stream, const char **reject_tmp_path,
                       const char *full_path,
                       svn_boolean_t is_dir, apr_pool_t *pool)
{
  const char *tmp_base_path;

  if (is_dir)
    tmp_base_path = svn_wc__adm_child(full_path, SVN_WC__ADM_TMP, pool);
  else
    tmp_base_path = svn_wc__adm_child(svn_path_dirname(full_path, pool),
                                      SVN_WC__ADM_TMP, pool);

  return svn_stream_open_unique(stream, reject_tmp_path, tmp_base_path,
                                svn_io_file_del_none, pool, pool);
}


/* Write CONFLICT_DESCRIPTION to STREAM, plus a trailing EOL sequence. */
static svn_error_t *
append_prop_conflict(svn_stream_t *stream,
                     const svn_string_t *conflict_description,
                     apr_pool_t *pool)
{
  /* TODO:  someday, perhaps prefix each conflict_description with a
     timestamp or something? */
  apr_size_t len;
  const char *native_text =
    svn_utf_cstring_from_utf8_fuzzy(conflict_description->data, pool);

  len = strlen(native_text);
  SVN_ERR(svn_stream_write(stream, native_text, &len));

  native_text = svn_utf_cstring_from_utf8_fuzzy(APR_EOL_STR, pool);
  len = strlen(native_text);
  return svn_stream_write(stream, native_text, &len);
}


/* Look up the entry for PATH within ADM_ACCESS and see if it has a `current'
   reject file describing a state of conflict.  Set *REJECT_FILE to the
   name of that file, or to NULL if no such file exists. */
static svn_error_t *
get_existing_prop_reject_file(const char **reject_file,
                              svn_wc_adm_access_t *adm_access,
                              const char *path,
                              apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  *reject_file = entry->prejfile
    ? svn_path_join(svn_wc_adm_access_path(adm_access), entry->prejfile, pool)
    : NULL;
  return SVN_NO_ERROR;
}

/*---------------------------------------------------------------------*/


/* Build a space separated list of properties that are contained in
   the hash PROPS and which we want to cache.
   The string is allocated in POOL. */
static const char *
build_present_props(apr_hash_t *props, apr_pool_t *pool)
{
  svn_stringbuf_t *present_props;

  if (apr_hash_count(props) == 0)
    return "";

  present_props = svn_stringbuf_create("", pool);

  if (apr_hash_get(props, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING) != NULL)
    svn_stringbuf_appendcstr(present_props, SVN_PROP_SPECIAL " ");
  if (apr_hash_get(props, SVN_PROP_EXTERNALS, APR_HASH_KEY_STRING) != NULL)
    svn_stringbuf_appendcstr(present_props, SVN_PROP_EXTERNALS " ");
  if (apr_hash_get(props, SVN_PROP_NEEDS_LOCK, APR_HASH_KEY_STRING) != NULL)
    svn_stringbuf_appendcstr(present_props, SVN_PROP_NEEDS_LOCK " ");

  /* Avoid returning a string with a trailing space. */
  svn_stringbuf_chop(present_props, 1);

  return present_props->data;
}

/*** Loading regular properties. ***/
svn_error_t *
svn_wc__load_props(apr_hash_t **base_props_p,
                   apr_hash_t **props_p,
                   apr_hash_t **revert_props_p,
                   svn_wc_adm_access_t *adm_access,
                   const char *path,
                   apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_boolean_t has_propcaching =
    svn_wc__adm_wc_format(adm_access) > SVN_WC__NO_PROPCACHING_VERSION;
  const svn_wc_entry_t *entry;
  apr_hash_t *base_props = NULL; /* Silence uninitialized warning. */

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  /* If there is no entry, we just return empty hashes, since the
     property merging can use this function when there is no entry. */
  if (! entry)
    {
      if (base_props_p)
        *base_props_p = apr_hash_make(pool);
      if (props_p)
        *props_p = apr_hash_make(pool);
      if (revert_props_p)
        *revert_props_p = apr_hash_make(pool);
      return SVN_NO_ERROR;
    }

  kind = entry->kind;
  /* We will need the base props if the user requested them, OR,
     our WC has prop caching, the user requested working props and there are no
     prop mods. */
  if (base_props_p
      || (has_propcaching && ! entry->has_prop_mods && entry->has_props))
    {
      SVN_ERR(load_props(&base_props, path, kind, svn_wc__props_base, pool));

      if (base_props_p)
        *base_props_p = base_props;
    }

  if (props_p)
    {
      if (has_propcaching && ! entry->has_prop_mods && entry->has_props)
        *props_p = apr_hash_copy(pool, base_props);
      else if (! has_propcaching || entry->has_props)
        SVN_ERR(load_props(props_p, path, kind, svn_wc__props_working, pool));
      else
        *props_p = apr_hash_make(pool);
    }

  if (revert_props_p)
    {
      if (entry->schedule == svn_wc_schedule_replace)
        SVN_ERR(load_props(revert_props_p, path, kind, svn_wc__props_revert,
                           pool));
      else
        *revert_props_p = apr_hash_make(pool);

    }

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------------*/

/*** Installing new properties. ***/

/* Extend LOG_ACCUM with log commands to write the properties PROPS into
 * the admin file specified by WC_PROP_KIND. ADM_ACCESS and PATH specify
 * the WC item with which this file should be associated. */
static svn_error_t *
install_props_file(svn_stringbuf_t **log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *path,
                   apr_hash_t *props,
                   svn_wc__props_kind_t wc_prop_kind,
                   apr_pool_t *pool)
{
  svn_node_kind_t node_kind;
  const char *propfile_path;
  const char *propfile_tmp_path;

  if (! svn_path_is_child(svn_wc_adm_access_path(adm_access), path, NULL))
    node_kind = svn_node_dir;
  else
    node_kind = svn_node_file;

  SVN_ERR(svn_wc__prop_path(&propfile_path, path,
                            node_kind, wc_prop_kind, pool));

  /* Write the property hash into a temporary file. */
  SVN_ERR(save_prop_tmp_file(&propfile_tmp_path, props,
                             svn_path_dirname(propfile_path, pool),
                             FALSE, pool));

  /* Write a log entry to move tmp file to real file. */
  SVN_ERR(svn_wc__loggy_move(log_accum, adm_access,
                             propfile_tmp_path,
                             propfile_path,
                             pool));

  /* Make the props file read-only */
  return svn_wc__loggy_set_readonly(log_accum, adm_access,
                                    propfile_path, pool);
}

svn_error_t *
svn_wc__install_props(svn_stringbuf_t **log_accum,
                      svn_wc_adm_access_t *adm_access,
                      const char *path,
                      apr_hash_t *base_props,
                      apr_hash_t *working_props,
                      svn_boolean_t write_base_props,
                      apr_pool_t *pool)
{
  apr_array_header_t *prop_diffs;
  const svn_wc_entry_t *entry;
  svn_wc_entry_t tmp_entry;
  svn_node_kind_t kind;
  svn_boolean_t has_propcaching =
    svn_wc__adm_wc_format(adm_access) > SVN_WC__NO_PROPCACHING_VERSION;

  if (! svn_path_is_child(svn_wc_adm_access_path(adm_access), path, NULL))
    kind = svn_node_dir;
  else
    kind = svn_node_file;

  /* Check if the props are modified, and update the entry. */
  SVN_ERR(svn_prop_diffs(&prop_diffs, working_props, base_props, pool));
  tmp_entry.has_prop_mods = (prop_diffs->nelts > 0);
  tmp_entry.has_props = (apr_hash_count(working_props) > 0);
  tmp_entry.present_props = build_present_props(working_props, pool);

  SVN_ERR(svn_wc__loggy_entry_modify(log_accum, adm_access,
                                     path, &tmp_entry,
                                     SVN_WC__ENTRY_MODIFY_HAS_PROPS
                                     | SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS
                                     | SVN_WC__ENTRY_MODIFY_PRESENT_PROPS,
                                     pool));

  if (has_propcaching)
    SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  else
    entry = NULL;

  /* Save the working properties file if it differs from base. */
  if (tmp_entry.has_prop_mods)
    {
      SVN_ERR(install_props_file(log_accum, adm_access, path, working_props,
                                 svn_wc__props_working, pool));
    }
  else
    {
      /* No property modifications, remove the file instead. */
      const char *working_propfile_path;

      SVN_ERR(svn_wc__prop_path(&working_propfile_path, path,
                                kind, svn_wc__props_working, pool));

      if (! has_propcaching || (entry && entry->has_prop_mods))
        SVN_ERR(svn_wc__loggy_remove(log_accum, adm_access,
                                     working_propfile_path, pool));
    }

  /* Repeat the above steps for the base properties if required. */
  if (write_base_props)
    {
      if (apr_hash_count(base_props) > 0)
        {
          SVN_ERR(install_props_file(log_accum, adm_access, path, base_props,
                                     svn_wc__props_base, pool));
        }
      else
        {
          const char *base_propfile_path;

          SVN_ERR(svn_wc__prop_path(&base_propfile_path, path,
                                    kind, svn_wc__props_base, pool));

          if (! has_propcaching || (entry && entry->has_props))
            SVN_ERR(svn_wc__loggy_remove(log_accum, adm_access,
                                         base_propfile_path, pool));
        }
    }

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__working_props_committed(const char *path,
                                svn_wc_adm_access_t *adm_access,
                                svn_boolean_t sync_entries,
                                apr_pool_t *pool)
{
  const char *working;
  const char *base;
  const svn_wc_entry_t *entry;
  svn_wc_entry_t mod_entry;
  svn_wc_adm_access_t *mod_access;


  /* The path is ensured not an excluded path. */
  /* TODO(#2843) It seems that there is no need to
     reveal hidden entry here? */
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  SVN_ERR(svn_wc__prop_path(&working, path, entry->kind,
                            svn_wc__props_working, pool));
  SVN_ERR(svn_wc__prop_path(&base, path, entry->kind,
                            svn_wc__props_base, pool));

  /* svn_io_file_rename() retains a read-only bit, so there's no
     need to explicitly set it. */
  SVN_ERR(svn_io_file_rename(working, base, pool));

  SVN_ERR(svn_wc_adm_probe_retrieve(&mod_access, adm_access, path, pool));
  mod_entry.has_prop_mods = FALSE;
  return svn_wc__entry_modify(mod_access, entry->name, &mod_entry,
                              SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS,
                              sync_entries, pool);
}


svn_error_t *
svn_wc__props_last_modified(apr_time_t *mod_time,
                            const char *path,
                            svn_wc__props_kind_t props_kind,
                            svn_wc_adm_access_t *adm_access,
                            apr_pool_t *pool)
{
  svn_error_t *err;
  const char *props_file;
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, TRUE, pool));
  SVN_ERR(svn_wc__prop_path(&props_file, path, entry->kind, props_kind, pool));

  err = svn_io_file_affected_time(mod_time, props_file, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      *mod_time = 0;
    }
  else
    SVN_ERR_W(err,
              apr_psprintf(pool,
                           _("Error getting 'affected time' on '%s'"),
                           svn_path_local_style(props_file, pool)));

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_file_if_present(const char *file, apr_pool_t *pool)
{
  svn_error_t *err;

  /* Try to remove the file. */
  err = svn_io_remove_file(file, pool);

  /* Ignore file not found error. */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }

  return err;
}


/* If wcprops are stored in a single file in this working copy, read that file
   and store it in the cache of ADM_ACCESS.   Use POOL for temporary
   allocations. */
static svn_error_t *
read_wcprops(svn_wc_adm_access_t *adm_access, apr_pool_t *pool)
{
  apr_pool_t *cache_pool = svn_wc_adm_access_pool(adm_access);
  apr_hash_t *all_wcprops;
  apr_hash_t *proplist;
  svn_stream_t *stream;
  svn_error_t *err;

  /* If the WC format is too old, there is nothing to cache. */
  if (svn_wc__adm_wc_format(adm_access) <= SVN_WC__WCPROPS_MANY_FILES_VERSION)
    return SVN_NO_ERROR;

  all_wcprops = apr_hash_make(cache_pool);

  err = svn_wc__open_adm_stream(&stream, svn_wc_adm_access_path(adm_access),
                                SVN_WC__ADM_ALL_WCPROPS, pool, pool);

  /* A non-existent file means there are no props. */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      svn_wc__adm_access_set_wcprops(adm_access, all_wcprops);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* Read the proplist for THIS_DIR. */
  proplist = apr_hash_make(cache_pool);
  SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, cache_pool));
  apr_hash_set(all_wcprops, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING,
               proplist);

  /* And now, the children. */
  while (1729)
    {
      svn_stringbuf_t *line;
      svn_boolean_t eof;

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, cache_pool));
      if (eof)
        {
          if (line->len > 0)
            return svn_error_createf
              (SVN_ERR_WC_CORRUPT, NULL,
               _("Missing end of line in wcprops file for '%s'"),
               svn_path_local_style(svn_wc_adm_access_path(adm_access), pool));
          break;
        }
      proplist = apr_hash_make(cache_pool);
      SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR,
                             cache_pool));
      apr_hash_set(all_wcprops, line->data, APR_HASH_KEY_STRING, proplist);
    }

  svn_wc__adm_access_set_wcprops(adm_access, all_wcprops);

  return svn_stream_close(stream);
}

static svn_error_t *
write_wcprops(svn_wc_adm_access_t *adm_access, apr_pool_t *pool)
{
  apr_hash_t *wcprops = svn_wc__adm_access_wcprops(adm_access);
  svn_stream_t *stream;
  apr_hash_t *proplist;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_boolean_t any_props = FALSE;
  const char *temp_file_path;

  /* If there are no cached wcprops, there is nothing to do. */
  if (! wcprops)
    return SVN_NO_ERROR;

  /* Check if there are any properties at all. */
  for (hi = apr_hash_first(pool, wcprops); hi && ! any_props;
       hi = apr_hash_next(hi))
    {
      void *val;

      apr_hash_this(hi, NULL, NULL, &val);
      proplist = val;
      if (apr_hash_count(proplist) > 0)
        any_props = TRUE;
    }

  /* If there are no props, remove the file. */
  if (! any_props)
    {
      svn_error_t *err;

      err = svn_wc__remove_adm_file(adm_access, SVN_WC__ADM_ALL_WCPROPS,
                                    subpool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      else
        return err;
    }

  SVN_ERR(svn_wc__open_adm_writable(&stream, &temp_file_path,
                                    svn_wc_adm_access_path(adm_access),
                                    SVN_WC__ADM_ALL_WCPROPS, pool, subpool));

  /* First, the props for this_dir. */
  proplist = apr_hash_get(wcprops, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
  if (! proplist)
    proplist = apr_hash_make(subpool);
  SVN_ERR(svn_hash_write2(proplist, stream, SVN_HASH_TERMINATOR, subpool));

  /* Write children. */
  for (hi = apr_hash_first(pool, wcprops); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *name;

      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      proplist = val;

      /* We already wrote this_dir, and writing empty hashes makes me
         feel silly... */
      if (strcmp(SVN_WC_ENTRY_THIS_DIR, name) == 0
          || apr_hash_count(proplist) == 0)
        continue;

      svn_pool_clear(subpool);

      SVN_ERR(svn_stream_printf(stream, subpool, "%s\n", name));
      SVN_ERR(svn_hash_write2(proplist, stream, SVN_HASH_TERMINATOR, subpool));
    }

  return svn_wc__close_adm_stream(stream, temp_file_path,
                                  svn_wc_adm_access_path(adm_access),
                                  SVN_WC__ADM_ALL_WCPROPS, pool);
}


svn_error_t *
svn_wc__wcprops_flush(svn_wc_adm_access_t *adm_access,
                      apr_pool_t *scratch_pool)
{
  return write_wcprops(adm_access, scratch_pool);
}


svn_error_t *
svn_wc__loggy_props_delete(svn_stringbuf_t **log_accum,
                           const char *path,
                           svn_wc__props_kind_t props_kind,
                           svn_wc_adm_access_t *adm_access,
                           apr_pool_t *pool)
{
  const char *props_file;

  if (props_kind == svn_wc__props_wcprop)
    {
      /* We use 1 file for all wcprops in a directory,
         use a helper to remove them from that file */
      apr_hash_t *props;
      apr_pool_t *iterpool = svn_pool_create(pool);
      apr_hash_index_t *hi;

      SVN_ERR(svn_wc__wcprop_list(&props, path, adm_access, pool));
      /* ### TODO: There's no log command to delete all wcprops
         from a file at once. Removing all props should do it though. */

      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          const char *name;

          svn_pool_clear(iterpool);

          apr_hash_this(hi, &key, NULL, NULL);
          name = key;

          SVN_ERR(svn_wc__loggy_modify_wcprop(log_accum,
                                              adm_access, path,
                                              name, NULL, iterpool));
        }

      svn_pool_destroy(iterpool);
    }
  else
    {
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, TRUE, pool));
      SVN_ERR(svn_wc__prop_path(&props_file, path, entry->kind, props_kind,
                                pool));
      SVN_ERR(svn_wc__loggy_remove(log_accum, adm_access, props_file, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__props_delete(const char *path,
                     svn_wc__props_kind_t props_kind,
                     svn_wc_adm_access_t *adm_access,
                     apr_pool_t *pool)
{
  const char *props_file;

  if (props_kind == svn_wc__props_wcprop)
    {
      /* We use 1 file for all wcprops in a directory,
         use a helper to remove them from that file */

      svn_wc_adm_access_t *path_access;
      const char *filename;
      svn_boolean_t write_needed = FALSE;
      apr_hash_t *all_wcprops;

      SVN_ERR(svn_wc_adm_probe_retrieve(&path_access, adm_access,
                                        path, pool));

      all_wcprops = svn_wc__adm_access_wcprops(path_access);

      /* If PATH is a directory, then FILENAME will be NULL.
         If PATH is a file, then FILENAME will be the BASE_NAME of PATH. */
      filename = svn_path_is_child(svn_wc_adm_access_path(path_access),
                                   path, NULL);
      if (! filename)
        {
          /* There is no point in reading the props just to determine if we
             need to rewrite them:-), so assume a write is needed if the props
             aren't already cached. */
          if (! all_wcprops || apr_hash_count(all_wcprops) > 0)
            {
              apr_pool_t *adm_pool = svn_wc_adm_access_pool(path_access);

              svn_wc__adm_access_set_wcprops(path_access,
                                             apr_hash_make(adm_pool));
              write_needed = TRUE;
            }
        }
      else
        {
          apr_hash_t *wcprops;

          if (! all_wcprops)
            {
              SVN_ERR(read_wcprops(path_access, pool));
              all_wcprops = svn_wc__adm_access_wcprops(path_access);
            }
          if (all_wcprops)
            wcprops = apr_hash_get(all_wcprops, filename, APR_HASH_KEY_STRING);
          else
            wcprops = NULL;
          if (wcprops && apr_hash_count(wcprops) > 0)
            {
              apr_hash_set(all_wcprops, filename, APR_HASH_KEY_STRING, NULL);
              write_needed = TRUE;
            }
        }
      if (write_needed)
        SVN_ERR(write_wcprops(path_access, pool));
    }
  else
    {
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, TRUE, pool));
      SVN_ERR(svn_wc__prop_path(&props_file, path, entry->kind, props_kind,
                                pool));
      SVN_ERR(remove_file_if_present(props_file, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_revert_props_create(svn_stringbuf_t **log_accum,
                                  const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_boolean_t destroy_baseprops,
                                  apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  const char *dst_rprop;
  const char *dst_bprop;
  const char *tmp_rprop;
  svn_node_kind_t kind;

  /* TODO(#2843) The current caller ensures that PATH will not be an excluded
     item. But do we really need show_hidden = TRUE here? */
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  SVN_ERR(svn_wc__prop_path(&dst_rprop, path, entry->kind, svn_wc__props_revert,
                            pool));
  if (entry->kind == svn_node_dir)
    SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_rprop, path,
                                    svn_io_file_del_none, pool));
  else
    SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_rprop,
                                    svn_path_dirname(path, pool),
                                    svn_io_file_del_none, pool));
  SVN_ERR(svn_wc__prop_path(&dst_bprop, path, entry->kind, svn_wc__props_base,
                            pool));

  /* If prop base exist, copy it to revert base. */
  SVN_ERR(svn_io_check_path(dst_bprop, &kind, pool));
  if (kind == svn_node_file)
    {
      if (destroy_baseprops)
        SVN_ERR(svn_wc__loggy_move(log_accum,
                                   adm_access, dst_bprop, dst_rprop,
                                   pool));
      else
        {
          SVN_ERR(svn_io_copy_file(dst_bprop, tmp_rprop, TRUE, pool));
          SVN_ERR(svn_wc__loggy_move(log_accum, adm_access,
                                     tmp_rprop, dst_rprop, pool));
        }
    }
  else if (kind == svn_node_none)
    {
      /* If there wasn't any prop base we still need an empty revert
         propfile, otherwise a revert won't know that a change to the
         props needs to be made (it'll just see no file, and do nothing).
         So manufacture an empty propfile and force it to be written out. */

      SVN_ERR(save_prop_tmp_file(&dst_bprop, apr_hash_make(pool),
                                 svn_path_dirname(dst_bprop, pool),
                                 TRUE, pool));

      SVN_ERR(svn_wc__loggy_move(log_accum,
                                 adm_access, dst_bprop, dst_rprop,
                                 pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_revert_props_restore(svn_stringbuf_t **log_accum,
                                   const char *path,
                                   svn_wc_adm_access_t *adm_access,
                                   apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  const char *revert_file, *base_file;

  /* TODO(#2843) The current caller ensures that PATH will not be an excluded
     item. But do we really need show_hidden = TRUE here? */
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  SVN_ERR(svn_wc__prop_path(&base_file, path, entry->kind, svn_wc__props_base,
                            pool));
  SVN_ERR(svn_wc__prop_path(&revert_file, path, entry->kind,
                            svn_wc__props_revert, pool));

  return svn_wc__loggy_move(log_accum, adm_access,
                            revert_file, base_file, pool);
}


/*---------------------------------------------------------------------*/

/*** Merging propchanges into the working copy ***/


/* Parse FROM_PROP_VAL and TO_PROP_VAL into mergeinfo hashes, and
   calculate the deltas between them. */
static svn_error_t *
diff_mergeinfo_props(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                     const svn_string_t *from_prop_val,
                     const svn_string_t *to_prop_val, apr_pool_t *pool)
{
  if (svn_string_compare(from_prop_val, to_prop_val))
    {
      /* Don't bothering parsing identical mergeinfo. */
      *deleted = apr_hash_make(pool);
      *added = apr_hash_make(pool);
    }
  else
    {
      svn_mergeinfo_t from, to;
      SVN_ERR(svn_mergeinfo_parse(&from, from_prop_val->data, pool));
      SVN_ERR(svn_mergeinfo_parse(&to, to_prop_val->data, pool));
      SVN_ERR(svn_mergeinfo_diff(deleted, added, from, to,
                                 TRUE, pool));
    }
  return SVN_NO_ERROR;
}

/* Parse the mergeinfo from PROP_VAL1 and PROP_VAL2, combine it, then
   reconstitute it into *OUTPUT.  Call when the WC's mergeinfo has
   been modified to combine it with incoming mergeinfo from the
   repos. */
static svn_error_t *
combine_mergeinfo_props(const svn_string_t **output,
                        const svn_string_t *prop_val1,
                        const svn_string_t *prop_val2,
                        apr_pool_t *pool)
{
  svn_mergeinfo_t mergeinfo1, mergeinfo2;
  SVN_ERR(svn_mergeinfo_parse(&mergeinfo1, prop_val1->data, pool));
  SVN_ERR(svn_mergeinfo_parse(&mergeinfo2, prop_val2->data, pool));
  SVN_ERR(svn_mergeinfo_merge(mergeinfo1, mergeinfo2, pool));
  return svn_mergeinfo_to_string((svn_string_t **)output, mergeinfo1, pool);
}

/* Perform a 3-way merge operation on mergeinfo.  FROM_PROP_VAL is
   the "base" property value, WORKING_PROP_VAL is the current value,
   and TO_PROP_VAL is the new value. */
static svn_error_t *
combine_forked_mergeinfo_props(const svn_string_t **output,
                               const svn_string_t *from_prop_val,
                               const svn_string_t *working_prop_val,
                               const svn_string_t *to_prop_val,
                               apr_pool_t *pool)
{
  svn_mergeinfo_t from_mergeinfo, l_deleted, l_added, r_deleted, r_added;

  /* ### OPTIMIZE: Use from_mergeinfo when diff'ing. */
  SVN_ERR(diff_mergeinfo_props(&l_deleted, &l_added, from_prop_val,
                               working_prop_val, pool));
  SVN_ERR(diff_mergeinfo_props(&r_deleted, &r_added, from_prop_val,
                               to_prop_val, pool));
  SVN_ERR(svn_mergeinfo_merge(l_deleted, r_deleted, pool));
  SVN_ERR(svn_mergeinfo_merge(l_added, r_added, pool));

  /* Apply the combined deltas to the base. */
  SVN_ERR(svn_mergeinfo_parse(&from_mergeinfo, from_prop_val->data, pool));
  SVN_ERR(svn_mergeinfo_merge(from_mergeinfo, l_added, pool));

  SVN_ERR(svn_mergeinfo_remove(&from_mergeinfo, l_deleted,
                               from_mergeinfo, pool));

  return svn_mergeinfo_to_string((svn_string_t **)output, from_mergeinfo, pool);
}


svn_error_t *
svn_wc_merge_props(svn_wc_notify_state_t *state,
                   const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_hash_t *baseprops,
                   const apr_array_header_t *propchanges,
                   svn_boolean_t base_merge,
                   svn_boolean_t dry_run,
                   apr_pool_t *pool)
{
  return svn_wc_merge_props2(state, path, adm_access, baseprops, propchanges,
                             base_merge, dry_run, NULL, NULL, pool);
}


svn_error_t *
svn_wc_merge_props2(svn_wc_notify_state_t *state,
                    const char *path,
                    svn_wc_adm_access_t *adm_access,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  svn_stringbuf_t *log_accum;

  /* IMPORTANT: svn_wc_merge_prop_diffs relies on the fact that baseprops
     may be NULL. */

  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  /* Notice that we're not using svn_path_split_if_file(), because
     that looks at the actual working file.  Its existence shouldn't
     matter, so we're looking at entry->kind instead. */
  switch (entry->kind)
    {
    case svn_node_dir:
    case svn_node_file:
      break;
    default:
      return SVN_NO_ERROR; /* ### svn_node_none or svn_node_unknown */
    }

  if (! dry_run)
    log_accum = svn_stringbuf_create("", pool);

  /* Note that while this routine does the "real" work, it's only
     prepping tempfiles and writing log commands.  */
  SVN_ERR(svn_wc__merge_props(state, adm_access, path, baseprops, NULL, NULL,
                              propchanges, base_merge, dry_run,
                              conflict_func, conflict_baton, pool, &log_accum));

  if (! dry_run)
    {
      SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));
      SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));
    }

  return SVN_NO_ERROR;
}



/* Set the value of *STATE to NEW_VALUE if STATE is not NULL
 * and NEW_VALUE is a higer order value than *STATE's current value
 * using this ordering (lower order first):
 *
 * - unknown, unchanged, inapplicable
 * - changed
 * - merged
 * - missing
 * - obstructed
 * - conflicted
 *
 */
static void
set_prop_merge_state(svn_wc_notify_state_t *state,
                     svn_wc_notify_state_t new_value)
{
  static char ordering[] =
    { svn_wc_notify_state_unknown,
      svn_wc_notify_state_unchanged,
      svn_wc_notify_state_inapplicable,
      svn_wc_notify_state_changed,
      svn_wc_notify_state_merged,
      svn_wc_notify_state_obstructed,
      svn_wc_notify_state_conflicted };
  int state_pos = 0, i;

  if (! state)
    return;

  /* Find *STATE in our ordering */
  for (i = 0; i < sizeof(ordering); i++)
    {
      if (*state == ordering[i])
        {
          state_pos = i;
          break;
        }
    }

  /* Find NEW_VALUE in our ordering
   * We don't need to look further than where we found *STATE though:
   * If we find our value, it's order is too low.
   * If we don't find it, we'll want to set it, no matter its order.
   */

  for (i = 0; i <= state_pos; i++)
    {
      if (new_value == ordering[i])
        return;
    }

  *state = new_value;
}

/* Helper function for the three apply_* functions below, used when
 * merging properties together.
 *
 * Given property PROPNAME on PATH, and four possible property values,
 * generate four tmpfiles and pass them to CONFLICT_FUNC callback.
 * This gives the client an opportunity to interactively resolve the
 * property conflict.  (ADM_ACCESS provides the ability to examine
 * PATH's entries.)
 *
 * BASE_VAL/WORKING_VAL represent the current state of the working
 * copy, and OLD_VAL/NEW_VAL represents the incoming propchange.  Any
 * of these values might be NULL, indicating either non-existence or
 * intent-to-delete.
 *
 * If the callback isn't available, or if it responds with
 * 'choose_postpone', then set *CONFLICT_REMAINS to true and return.
 *
 * If the callback responds with a choice of 'base', 'theirs', 'mine',
 * or 'merged', then install the proper value into WORKING_PROPS and
 * set *CONFLICT_REMAINS to false.
 *
 */
static svn_error_t *
maybe_generate_propconflict(svn_boolean_t *conflict_remains,
                            const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_boolean_t is_dir,
                            const char *propname,
                            apr_hash_t *working_props,
                            const svn_string_t *old_val,
                            const svn_string_t *new_val,
                            const svn_string_t *base_val,
                            const svn_string_t *working_val,
                            svn_wc_conflict_resolver_func_t conflict_func,
                            void *conflict_baton,
                            apr_pool_t *pool)
{
  svn_wc_conflict_result_t *result = NULL;
  svn_string_t *mime_propval = NULL;
  apr_pool_t *filepool = svn_pool_create(pool);
  svn_wc_conflict_description_t *cdesc;
  const char *dirpath = svn_path_dirname(path, filepool);

  if (! conflict_func)
    {
      /* Just postpone the conflict. */
      *conflict_remains = TRUE;
      return SVN_NO_ERROR;
    }

  cdesc = svn_wc_conflict_description_create_prop(
    path, adm_access, is_dir ? svn_node_dir : svn_node_file, propname, pool);

  /* Create a tmpfile for each of the string_t's we've got.  */
  if (working_val)
    SVN_ERR(svn_io_write_unique(&cdesc->my_file, dirpath, working_val->data,
                                working_val->len,
                                svn_io_file_del_on_pool_cleanup, filepool));

  if (new_val)
    SVN_ERR(svn_io_write_unique(&cdesc->their_file, dirpath, new_val->data,
                                new_val->len, svn_io_file_del_on_pool_cleanup,
                                filepool));

  if (!base_val && !old_val)
    {
      /* If base and old are both NULL, then that's fine, we just let
         base_file stay NULL as-is.  Both agents are attempting to add a
         new property.  */
    }

  else if ((base_val && !old_val)
           || (!base_val && old_val))
    {
      /* If only one of base and old are defined, then we've got a
         situation where one agent is attempting to add the property
         for the first time, and the other agent is changing a
         property it thinks already exists.  In this case, we return
         whichever older-value happens to be defined, so that the
         conflict-callback can still attempt a 3-way merge. */

      const svn_string_t *the_val = base_val ? base_val : old_val;

      SVN_ERR(svn_io_write_unique(&cdesc->base_file, dirpath, the_val->data,
                                  the_val->len, svn_io_file_del_on_pool_cleanup,
                                  filepool));
    }

  else  /* base and old are both non-NULL */
    {
      const svn_string_t *the_val;

      if (! svn_string_compare(base_val, old_val))
        {
          /* What happens if 'base' and 'old' don't match up?  In an
             ideal situation, they would.  But if they don't, this is
             a classic example of a patch 'hunk' failing to apply due
             to a lack of context.  For example: imagine that the user
             is busy changing the property from a value of "cat" to
             "dog", but the incoming propchange wants to change the
             same property value from "red" to "green".  Total context
             mismatch.

             HOWEVER: we can still pass one of the two base values as
             'base_file' to the callback anyway.  It's still useful to
             present the working and new values to the user to
             compare. */

          if (working_val && svn_string_compare(base_val, working_val))
            the_val = old_val;
          else
            the_val = base_val;
        }
      else
        {
          the_val = base_val;
        }

      SVN_ERR(svn_io_write_unique(&cdesc->base_file, dirpath, the_val->data,
                                  the_val->len, svn_io_file_del_on_pool_cleanup,
                                  filepool));

      if (working_val && new_val)
        {
          svn_stream_t *mergestream;
          svn_diff_t *diff;
          svn_diff_file_options_t *options =
            svn_diff_file_options_create(filepool);

          SVN_ERR(svn_stream_open_unique(&mergestream, &cdesc->merged_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         filepool, pool));
          SVN_ERR(svn_diff_mem_string_diff3(&diff, the_val, working_val,
                                            new_val, options, filepool));
          SVN_ERR(svn_diff_mem_string_output_merge2
                  (mergestream, diff, the_val, working_val, new_val,
                   NULL, NULL, NULL, NULL,
                   svn_diff_conflict_display_modified_latest, filepool));
          svn_stream_close(mergestream);
        }
    }

  /* Build the rest of the description object: */
  if (!is_dir && working_props)
    mime_propval = apr_hash_get(working_props, SVN_PROP_MIME_TYPE,
                                APR_HASH_KEY_STRING);
  cdesc->mime_type = mime_propval ? mime_propval->data : NULL;
  cdesc->is_binary = mime_propval ?
      svn_mime_type_is_binary(mime_propval->data) : FALSE;

  if (!old_val && new_val)
    cdesc->action = svn_wc_conflict_action_add;
  else if (old_val && !new_val)
    cdesc->action = svn_wc_conflict_action_delete;
  else
    cdesc->action = svn_wc_conflict_action_edit;

  if (base_val && !working_val)
    cdesc->reason = svn_wc_conflict_reason_deleted;
  else if (!base_val && working_val)
    cdesc->reason = svn_wc_conflict_reason_obstructed;
  else
    cdesc->reason = svn_wc_conflict_reason_edited;

  /* Invoke the interactive conflict callback. */
  SVN_ERR(conflict_func(&result, cdesc, conflict_baton, pool));
  if (result == NULL)
    {
      *conflict_remains = TRUE;
      return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                              NULL, _("Conflict callback violated API:"
                                      " returned no results."));
    }

  switch (result->choice)
    {
      default:
      case svn_wc_conflict_choose_postpone:
        {
          *conflict_remains = TRUE;
          break;
        }
      case svn_wc_conflict_choose_mine_full:
        {
          /* No need to change working_props; it already contains working_val */
          *conflict_remains = FALSE;
          break;
        }
      /* I think _mine_full and _theirs_full are appropriate for prop
         behavior as well as the text behavior.  There should even be
         analogous behaviors for _mine and _theirs when those are
         ready, namely: fold in all non-conflicting prop changes, and
         then choose _mine side or _theirs side for conflicting ones. */
      case svn_wc_conflict_choose_theirs_full:
        {
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);
          *conflict_remains = FALSE;
          break;
        }
      case svn_wc_conflict_choose_base:
        {
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, base_val);
          *conflict_remains = FALSE;
          break;
        }
      case svn_wc_conflict_choose_merged:
        {
          if (!cdesc->merged_file && !result->merged_file)
            return svn_error_create
                (SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                 NULL, _("Conflict callback violated API:"
                         " returned no merged file."));
          else
            {
              svn_stringbuf_t *merged_stringbuf;
              svn_string_t *merged_string;

              SVN_ERR(svn_stringbuf_from_file2(&merged_stringbuf,
                                               result->merged_file ?
                                                    result->merged_file :
                                                    cdesc->merged_file,
                                               pool));
              merged_string = svn_string_create_from_buf(merged_stringbuf, pool);
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, merged_string);
              *conflict_remains = FALSE;
            }
          break;
        }
    }

  /* Delete any tmpfiles we made. */
  svn_pool_destroy(filepool);

  return SVN_NO_ERROR;
}


/* Add the property with name PROPNAME to the set of WORKING_PROPS on
 * PATH, setting *STATE or *CONFLICT according to merge outcomes.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_merge_prop_state().
 *
 * BASE_VAL contains the working copy base property value
 *
 * NEW_VAL contains the value to be set.
 *
 * CONFLICT_FUNC/BATON is a callback to be called before declaring a
 * property conflict;  it gives the client a chance to resolve the
 * conflict interactively.  It uses ADM_ACCESS to possibly examine
 * PATH's entries.
 */
static svn_error_t *
apply_single_prop_add(svn_wc_notify_state_t *state,
                      const char *path,
                      svn_boolean_t is_dir,
                      apr_hash_t *working_props,
                      svn_string_t **conflict,
                      const char *propname,
                      const svn_string_t *base_val,
                      const svn_string_t *new_val,
                      svn_wc_conflict_resolver_func_t conflict_func,
                      void *conflict_baton,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool)

{
  svn_boolean_t got_conflict = FALSE;
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  if (working_val)
    {
      /* the property already exists in working_props... */

      if (svn_string_compare(working_val, new_val))
        /* The value we want is already there, so it's a merge. */
        set_prop_merge_state(state, svn_wc_notify_state_merged);

      else
        {
          /* The WC difference doesn't match the new value.
           We only merge mergeinfo;  other props conflict */
          if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
            {
              SVN_ERR(combine_mergeinfo_props(&new_val, working_val,
                                              new_val, pool));
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, new_val);
              set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
          else
            {
              SVN_ERR(maybe_generate_propconflict(&got_conflict, path,
                                                  adm_access, is_dir,
                                                  propname, working_props,
                                                  NULL, new_val,
                                                  base_val, working_val,
                                                  conflict_func, conflict_baton,
                                                  pool));
              if (got_conflict)
                *conflict = svn_string_createf
                    (pool,
                     _("Trying to add new property '%s' with value "
                       "'%s',\nbut property already exists with value '%s'."),
                     propname, new_val->data, working_val->data);
            }
        }
    }
  else if (base_val)
    {
      SVN_ERR(maybe_generate_propconflict(&got_conflict, path, adm_access,
                                          is_dir, propname,
                                          working_props, NULL, new_val,
                                          base_val, NULL,
                                          conflict_func, conflict_baton, pool));
      if (got_conflict)
        *conflict = svn_string_createf
            (pool, _("Trying to create property '%s' with value '%s',\n"
                     "but it has been locally deleted."),
             propname, new_val->data);
    }
  else  /* property doesn't yet exist in working_props...  */
    /* so just set it */
    apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);

  return SVN_NO_ERROR;
}


/* Delete the property with name PROPNAME from the set of
 * WORKING_PROPS on PATH, setting *STATE or *CONFLICT according to
 * merge outcomes.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_merge_prop_state().
 *
 * BASE_VAL contains the working copy base property value
 *
 * OLD_VAL contains the value the of the property the server
 * thinks it's deleting.
 *
 * CONFLICT_FUNC/BATON is a callback to be called before declaring a
 * property conflict;  it gives the client a chance to resolve the
 * conflict interactively.  It uses ADM_ACCESS to possibly examine
 * PATH's entries.
 */
static svn_error_t *
apply_single_prop_delete(svn_wc_notify_state_t *state,
                         const char *path,
                         svn_boolean_t is_dir,
                         apr_hash_t *working_props,
                         svn_string_t **conflict,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         svn_wc_conflict_resolver_func_t conflict_func,
                         void *conflict_baton,
                         svn_wc_adm_access_t *adm_access,
                         apr_pool_t *pool)
{
  svn_boolean_t got_conflict = FALSE;
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  if (! base_val)
    {
      apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, NULL);
      if (old_val)
        /* This is a merge, merging a delete into non-existent */
        set_prop_merge_state(state, svn_wc_notify_state_merged);
    }

  else if (svn_string_compare(base_val, old_val))
    {
       if (working_val)
         {
           if (svn_string_compare(working_val, old_val))
             /* they have the same values, so it's an update */
             apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, NULL);
           else
             {
               SVN_ERR(maybe_generate_propconflict(&got_conflict, path,
                                                   adm_access, is_dir,
                                                   propname, working_props,
                                                   old_val, NULL,
                                                   base_val, working_val,
                                                   conflict_func, conflict_baton,
                                                   pool));
               if (got_conflict)
                 *conflict = svn_string_createf
                     (pool,
                      _("Trying to delete property '%s' with value '%s'\n"
                        "but it has been modified from '%s' to '%s'."),
                      propname, old_val->data,
                      base_val->data, working_val->data);
             }
         }
       else
         /* The property is locally deleted, so it's a merge */
         set_prop_merge_state(state, svn_wc_notify_state_merged);
    }

  else
    {
      SVN_ERR(maybe_generate_propconflict(&got_conflict, path, adm_access,
                                          is_dir, propname,
                                          working_props, old_val, NULL,
                                          base_val, working_val,
                                          conflict_func, conflict_baton, pool));
      if (got_conflict)
        *conflict = svn_string_createf
            (pool,
             _("Trying to delete property '%s' with value '%s'\n"
               "but the local value is '%s'."),
             propname, base_val->data, working_val->data);
    }

  return SVN_NO_ERROR;
}


/* Merge a change to the mergeinfo property. The same as
   apply_single_prop_change(), except that the PROPNAME is always
   SVN_PROP_MERGEINFO. */
/* ### This function is extracted straight from the previous all-in-one
   version of apply_single_prop_change() by removing the code paths that
   were not followed for this property, but with no attempt to rationalize
   the remainder. */
static svn_error_t *
apply_single_mergeinfo_prop_change(svn_wc_notify_state_t *state,
                                   const char *path,
                                   svn_boolean_t is_dir,
                                   apr_hash_t *working_props,
                                   svn_string_t **conflict,
                                   const char *propname,
                                   const svn_string_t *base_val,
                                   const svn_string_t *old_val,
                                   const svn_string_t *new_val,
                                   svn_wc_conflict_resolver_func_t conflict_func,
                                   void *conflict_baton,
                                   svn_wc_adm_access_t *adm_access,
                                   apr_pool_t *pool)
{
  svn_boolean_t got_conflict = FALSE;
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  if ((working_val && ! base_val)
      || (! working_val && base_val)
      || (working_val && base_val
          && !svn_string_compare(working_val, base_val)))
    {
      /* Locally changed property */
      if (working_val)
        {
          if (svn_string_compare(working_val, new_val))
            /* The new value equals the changed value: a no-op merge */
            set_prop_merge_state(state, svn_wc_notify_state_merged);
          else
            {
                  /* We have base, WC, and new values.  Discover
                     deltas between base <-> WC, and base <->
                     incoming.  Combine those deltas, and apply
                     them to base to get the new value. */
                  SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                         working_val,
                                                         new_val, pool));
                  apr_hash_set(working_props, propname,
                               APR_HASH_KEY_STRING, new_val);
                  set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
        }

      else
        {
          /* There is a base_val but no working_val */
          SVN_ERR(maybe_generate_propconflict(&got_conflict, path, adm_access,
                                              is_dir, propname, working_props,
                                              old_val, new_val,
                                              base_val, working_val,
                                              conflict_func, conflict_baton,
                                              pool));
          if (got_conflict)
            *conflict = svn_string_createf
                (pool,
                 _("Trying to change property '%s' from '%s' to '%s',\n"
                   "but it has been locally deleted."),
                 propname, old_val->data, new_val->data);
        }
    }

  else if (! working_val) /* means !working_val && !base_val due
                             to conditions above: no prop at all */
    {
          /* Discover any mergeinfo additions in the
             incoming value relative to the base, and
             "combine" those with the empty WC value. */
          svn_mergeinfo_t deleted_mergeinfo, added_mergeinfo;
          SVN_ERR(diff_mergeinfo_props(&deleted_mergeinfo,
                                       &added_mergeinfo,
                                       old_val, new_val, pool));
          SVN_ERR(svn_mergeinfo_to_string((svn_string_t **)&new_val,
                                          added_mergeinfo, pool));
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);
    }

  else /* means working && base && svn_string_compare(working, base) */
    {
      if (svn_string_compare(old_val, base_val))
        apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);

      else
        {
              /* We have base, WC, and new values.  Discover
                 deltas between base <-> WC, and base <->
                 incoming.  Combine those deltas, and apply
                 them to base to get the new value. */
              SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                     working_val,
                                                     new_val, pool));
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, new_val);
              set_prop_merge_state(state, svn_wc_notify_state_merged);
        }
    }

  return SVN_NO_ERROR;
}

/* Merge a change to a property, using the rule that if the working value
   is the same as OLD_VAL then apply the change as a simple update
   (replacement), otherwise invoke maybe_generate_propconflict().
   The definition of the arguments and behaviour is the same as
   apply_single_prop_change(). */
static svn_error_t *
apply_single_generic_prop_change(svn_wc_notify_state_t *state,
                                 const char *path,
                                 svn_boolean_t is_dir,
                                 apr_hash_t *working_props,
                                 svn_string_t **conflict,
                                 const char *propname,
                                 const svn_string_t *base_val,
                                 const svn_string_t *old_val,
                                 const svn_string_t *new_val,
                                 svn_wc_conflict_resolver_func_t conflict_func,
                                 void *conflict_baton,
                                 svn_wc_adm_access_t *adm_access,
                                 apr_pool_t *pool)
{
  svn_boolean_t got_conflict = FALSE;
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  /* If working_val is the same as old_val... */
  if ((!working_val && !old_val)
      || (working_val && old_val
          && svn_string_compare(working_val, old_val)))
    {
      /* A trivial update: change it to new_val. */
      apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);
    }
  else
    {
      /* Merge the change. */
      SVN_ERR(maybe_generate_propconflict(&got_conflict, path, adm_access,
                                          is_dir, propname, working_props,
                                          old_val, new_val,
                                          base_val, working_val,
                                          conflict_func, conflict_baton,
                                          pool));
      if (got_conflict)
        {
          /* Describe the conflict, referring to base_val as well as
             working_val for the user's convenience. */
          if (working_val && base_val
              && svn_string_compare(working_val, base_val))
            *conflict = svn_string_createf
              (pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but property already exists with value '%s'."),
               propname, old_val->data, new_val->data, working_val->data);
          else if (working_val && base_val)
            *conflict = svn_string_createf
              (pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but the property has been locally changed from '%s' to "
                 "'%s'."),
               propname, old_val->data, new_val->data,
               base_val->data, working_val->data);
          else if (working_val)
            *conflict = svn_string_createf
              (pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but property has been locally added with value "
                 "'%s'."),
               propname, old_val->data, new_val->data, working_val->data);
          else if (base_val)
            *conflict = svn_string_createf
              (pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but it has been locally deleted."),
               propname, old_val->data, new_val->data);
          else
            *conflict = svn_string_createf
              (pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but the property does not exist."),
               propname, old_val->data, new_val->data);
        }
    }

  return SVN_NO_ERROR;
}

/* Change the property with name PROPNAME in the set of WORKING_PROPS
 * on PATH, setting *STATE or *CONFLICT according to the merge outcome.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_prop_merge_state(). (May be null.).
 *
 * BASE_VAL contains the working copy base property value. (May be null.)
 *
 * OLD_VAL contains the value of the property the server
 * thinks it's overwriting. (Not null.)
 *
 * NEW_VAL contains the value to be set. (Not null.)
 *
 * CONFLICT_FUNC/BATON is a callback to be called before declaring a
 * property conflict;  it gives the client a chance to resolve the
 * conflict interactively.  It uses ADM_ACCESS to possibly examine the
 * path's entries.
 */
static svn_error_t *
apply_single_prop_change(svn_wc_notify_state_t *state,
                         const char *path,
                         svn_boolean_t is_dir,
                         apr_hash_t *working_props,
                         svn_string_t **conflict,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         const svn_string_t *new_val,
                         svn_wc_conflict_resolver_func_t conflict_func,
                         void *conflict_baton,
                         svn_wc_adm_access_t *adm_access,
                         apr_pool_t *pool)
{
  /* Note: The purpose is to apply the change (old_val -> new_val) onto
     (working_val). There is no need for base_val to be involved in the
     process except as a bit of context to help the user understand and
     resolve any conflict. */

  /* Decide how to merge, based on whether we know anything special about
     the property. */
  if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      /* We know how to merge any mergeinfo property change. */

      SVN_ERR(apply_single_mergeinfo_prop_change(state, path, is_dir,
                                                 working_props, conflict,
                                                 propname, base_val, old_val,
                                                 new_val, conflict_func,
                                                 conflict_baton, adm_access,
                                                 pool));
    }
  else
    {
      /* The standard method: perform a simple update automatically, but
         pass any other kind of merge to maybe_generate_propconflict(). */

      SVN_ERR(apply_single_generic_prop_change(state, path, is_dir,
                                               working_props, conflict,
                                               propname, base_val, old_val,
                                               new_val, conflict_func,
                                               conflict_baton, adm_access,
                                               pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__merge_props(svn_wc_notify_state_t *state,
                    svn_wc_adm_access_t *adm_access,
                    const char *path,
                    apr_hash_t *server_baseprops,
                    apr_hash_t *base_props,
                    apr_hash_t *working_props,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    apr_pool_t *pool,
                    svn_stringbuf_t **entry_accum)
{
  int i;
  svn_boolean_t is_dir;

  const char *reject_path = NULL;
  svn_stream_t *reject_tmp_stream = NULL;  /* the temporary conflicts stream */
  const char *reject_tmp_path = NULL;

  if (! svn_path_is_child(svn_wc_adm_access_path(adm_access), path, NULL))
    is_dir = TRUE;
  else
    is_dir = FALSE;

  /* If not provided, load the base & working property files into hashes */
  if (! base_props || ! working_props)
    SVN_ERR(svn_wc__load_props(base_props ? NULL : &base_props,
                               working_props ? NULL : &working_props,
                               NULL, adm_access, path, pool));
  if (!server_baseprops)
    server_baseprops = base_props;

  if (state)
    {
      /* Start out assuming no changes or conflicts.  Don't bother to
         examine propchanges->nelts yet; even if we knew there were
         propchanges, we wouldn't yet know if they are "normal" props,
         as opposed wc or entry props.  */
      *state = svn_wc_notify_state_unchanged;
    }

  /* Looping over the array of incoming propchanges we want to apply: */
  for (i = 0; i < propchanges->nelts; i++)
    {
      const char *propname;
      svn_string_t *conflict = NULL;
      const svn_prop_t *incoming_change;
      const svn_string_t *from_val, *to_val, *working_val, *base_val;
      svn_boolean_t is_normal;

      /* For the incoming propchange, figure out the TO and FROM values. */
      incoming_change = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);
      propname = incoming_change->name;
      is_normal = svn_wc_is_normal_prop(propname);
      to_val = incoming_change->value
        ? svn_string_dup(incoming_change->value, pool) : NULL;
      from_val = apr_hash_get(server_baseprops, propname, APR_HASH_KEY_STRING);

      working_val = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);
      base_val = apr_hash_get(base_props, propname, APR_HASH_KEY_STRING);

      if (base_merge)
        apr_hash_set(base_props, propname, APR_HASH_KEY_STRING, to_val);

      /* We already know that state is at least `changed', so mark
         that, but remember that we may later upgrade to `merged' or
         even `conflicted'. */
      if (is_normal)
        set_prop_merge_state(state, svn_wc_notify_state_changed);

      if (! from_val)  /* adding a new property */
        SVN_ERR(apply_single_prop_add(is_normal ? state : NULL, path, is_dir,
                                      working_props, &conflict,
                                      propname, base_val, to_val,
                                      conflict_func, conflict_baton,
                                      adm_access, pool));

      else if (! to_val) /* delete an existing property */
        SVN_ERR(apply_single_prop_delete(is_normal ? state : NULL, path, is_dir,
                                         working_props, &conflict,
                                         propname, base_val, from_val,
                                         conflict_func, conflict_baton,
                                         adm_access, pool));

      else  /* changing an existing property */
        SVN_ERR(apply_single_prop_change(is_normal ? state : NULL, path, is_dir,
                                         working_props, &conflict,
                                         propname, base_val, from_val, to_val,
                                         conflict_func, conflict_baton,
                                         adm_access, pool));


      /* merging logic complete, now we need to possibly log conflict
         data to tmpfiles.  */

      if (conflict)
        {
          if (is_normal)
            set_prop_merge_state(state, svn_wc_notify_state_conflicted);

          if (dry_run)
            continue;   /* skip to next incoming change */

          if (! reject_tmp_stream)
            /* This is the very first prop conflict found on this item. */
            SVN_ERR(open_reject_tmp_stream(&reject_tmp_stream, &reject_tmp_path,
                                           path, is_dir, pool));

          /* Append the conflict to the open tmp/PROPS/---.prej file */
          SVN_ERR(append_prop_conflict(reject_tmp_stream, conflict, pool));
        }

    }  /* foreach propchange ... */

  /* Finished applying all incoming propchanges to our hashes! */

  if (dry_run)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__install_props(entry_accum, adm_access, path,
                                base_props, working_props, base_merge,
                                pool));

  if (reject_tmp_stream)
    {
      /* There's a temporary reject file sitting in .svn/tmp/ somewhere.  Deal
         with the conflicts.  */

      /* First, _close_ this temporary conflicts file.  We've been
         appending to it all along. */
      SVN_ERR(svn_stream_close(reject_tmp_stream));

      /* Now try to get the name of a pre-existing .prej file from the
         entries file */
      SVN_ERR(get_existing_prop_reject_file(&reject_path,
                                            adm_access, path, pool));

      if (! reject_path)
        {
          /* Reserve a new .prej file *above* the .svn/ directory by
             opening and closing it. */
          const char *reject_dirpath;
          const char *reject_filename;

          if (is_dir)
            {
              reject_dirpath = path;
              reject_filename = SVN_WC__THIS_DIR_PREJ;
            }
          else
            svn_path_split(path, &reject_dirpath, &reject_filename, pool);

          SVN_ERR(svn_io_open_uniquely_named(NULL, &reject_path,
                                             reject_dirpath,
                                             reject_filename,
                                             SVN_WC__PROP_REJ_EXT,
                                             svn_io_file_del_none,
                                             pool, pool));

          /* This file will be overwritten when the log is run; that's
             ok, because at least now we have a reservation on
             disk. */
        }

      /* We've now guaranteed that some kind of .prej file exists
         above the .svn/ dir.  We write log entries to append our
         conflicts to it. */
      SVN_ERR(svn_wc__loggy_append(entry_accum, adm_access,
                                   reject_tmp_path, reject_path, pool));

      /* And of course, delete the temporary reject file. */
      SVN_ERR(svn_wc__loggy_remove(entry_accum, adm_access,
                                   reject_tmp_path, pool));

      /* Mark entry as "conflicted" with a particular .prej file. */
      {
        svn_wc_entry_t entry;

        entry.prejfile = svn_path_is_child(svn_wc_adm_access_path(adm_access),
                                           reject_path, NULL);
        SVN_ERR(svn_wc__loggy_entry_modify(entry_accum,
                                           adm_access,
                                           path,
                                           &entry,
                                           SVN_WC__ENTRY_MODIFY_PREJFILE,
                                           pool));
      }

    } /* if (reject_tmp_fp) */

  return SVN_NO_ERROR;
}


/* This is DEPRECATED, use svn_wc_merge_props2() instead. */
svn_error_t *
svn_wc_merge_prop_diffs(svn_wc_notify_state_t *state,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const apr_array_header_t *propchanges,
                        svn_boolean_t base_merge,
                        svn_boolean_t dry_run,
                        apr_pool_t *pool)
{
  /* NOTE: Here, we use implementation knowledge.  The public
     svn_wc_merge_props2 doesn't allow NULL as baseprops argument, but we know
     that it works. */
  return svn_wc_merge_props2(state, path, adm_access, NULL, propchanges,
                             base_merge, dry_run, NULL, NULL, pool);
}



/*------------------------------------------------------------------*/

/*** Private 'wc prop' functions ***/


svn_error_t *
svn_wc__wcprop_list(apr_hash_t **wcprops,
                    const char *entryname,
                    svn_wc_adm_access_t *adm_access,
                    apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  apr_hash_t *all_wcprops;
  apr_pool_t *cache_pool = svn_wc_adm_access_pool(adm_access);
  const char *path = svn_path_join(svn_wc_adm_access_path(adm_access),
                                   entryname, pool);

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  if (! entry)
    {
      /* No entry exists, therefore no wcprop-file can exist */
      *wcprops = apr_hash_make(pool);
      return SVN_NO_ERROR;
    }

  /* Try the cache first. */
  all_wcprops = svn_wc__adm_access_wcprops(adm_access);
  if (! all_wcprops)
    {
      SVN_ERR(read_wcprops(adm_access, pool));
      all_wcprops = svn_wc__adm_access_wcprops(adm_access);
    }
  if (all_wcprops)
    {
      *wcprops = apr_hash_get(all_wcprops, entryname, APR_HASH_KEY_STRING);
      /* The cache contains no hash tables for empty proplist, so we just
         create one here if that's the case. */
      if (! *wcprops)
        {
          *wcprops = apr_hash_make(cache_pool);
          entryname = apr_pstrdup(cache_pool, entryname);
          apr_hash_set(all_wcprops, entryname, APR_HASH_KEY_STRING, *wcprops);
        }
      return SVN_NO_ERROR;
    }

  /* Fall back on individual files for backwards compatibility. */
  return load_props(wcprops, path, entry->kind, svn_wc__props_wcprop, pool);
}


/* Get a single 'wcprop' NAME for versioned object PATH, return in
   *VALUE.  ADM_ACCESS is an access baton set that contains PATH. */
static svn_error_t *
wcprop_get(const svn_string_t **value,
                   const char *name,
                   const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  apr_hash_t *prophash;
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  if (! entry)
    {
      *value = NULL;
      return SVN_NO_ERROR;
    }
  if (entry->kind == svn_node_dir)
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, path, pool));
  else
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access,
                                svn_path_dirname(path, pool), pool));

  SVN_ERR_W(svn_wc__wcprop_list(&prophash, entry->name, adm_access, pool),
            _("Failed to load properties from disk"));

  *value = apr_hash_get(prophash, name, APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wcprop_set(const char *name,
                   const svn_string_t *value,
                   const char *path,
                   svn_wc_adm_access_t *adm_access,
                   svn_boolean_t force_write,
                   apr_pool_t *pool)
{
  apr_hash_t *prophash;
  apr_pool_t *cache_pool = svn_wc_adm_access_pool(adm_access);
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  if (entry->kind == svn_node_dir)
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, path, pool));
  else
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access,
                                svn_path_dirname(path, pool), pool));
  SVN_ERR_W(svn_wc__wcprop_list(&prophash, entry->name, adm_access, pool),
            _("Failed to load properties from disk"));

  /* Now we have all the properties in our hash.  Simply merge the new
     property into it. */
  name = apr_pstrdup(cache_pool, name);
  if (value)
    value = svn_string_dup(value, cache_pool);
  apr_hash_set(prophash, name, APR_HASH_KEY_STRING, value);

  if (svn_wc__adm_wc_format(adm_access) > SVN_WC__WCPROPS_MANY_FILES_VERSION)
    {
      if (force_write)
        SVN_ERR(write_wcprops(adm_access, pool));
    }
  else
    {
      SVN_ERR(svn_wc__write_old_wcprops(path, prophash, entry->kind, pool));
    }

  return SVN_NO_ERROR;
}

/*------------------------------------------------------------------*/


/*** Public Functions ***/


svn_error_t *
svn_wc_prop_list(apr_hash_t **props,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  /* if there is no entry, 'path' is not under version control and
     therefore has no props. */
  if (! entry)
    {
      *props = apr_hash_make(pool);
      return SVN_NO_ERROR;
    }

  if (entry->kind == svn_node_dir)
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, path, pool));
  else
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access,
                                svn_path_dirname(path, pool), pool));

  return svn_wc__load_props(NULL, props, NULL, adm_access, path, pool);
}

/* Determine if PROPNAME is contained in the list of space separated
   values STRING.  */

static svn_boolean_t
string_contains_prop(const char *string, const char *propname)
{
  const char *place = strstr(string, propname);
  int proplen = strlen(propname);

  if (!place)
    return FALSE;

  while (place)
    {
      if (place[proplen] == ' ' || place[proplen] == 0)
        return TRUE;
      place = strstr(place + 1, propname);
    }
  return FALSE;
}

svn_error_t *
svn_wc_prop_get(const svn_string_t **value,
                const char *name,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  apr_hash_t *prophash;
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  if (entry == NULL)
    {
      *value = NULL;
      return SVN_NO_ERROR;
    }

  if (entry->cachable_props
      && string_contains_prop(entry->cachable_props, name))
    {
      /* We separate these two cases so that we can return the correct
         value for booleans if they exist in the string.  */
      if (!entry->present_props
          || !string_contains_prop(entry->present_props, name))
        {
          *value = NULL;
          return SVN_NO_ERROR;
        }
      if (svn_prop_is_boolean(name))
        {
          *value = svn_string_create(SVN_PROP_BOOLEAN_TRUE, pool);
          SVN_ERR_ASSERT(*value != NULL);
          return SVN_NO_ERROR;
        }
    }

  if (kind == svn_prop_wc_kind)
    {
      return wcprop_get(value, name, path, adm_access, pool);
    }
  if (kind == svn_prop_entry_kind)
    {
      return svn_error_createf   /* we don't do entry properties here */
        (SVN_ERR_BAD_PROP_KIND, NULL,
         _("Property '%s' is an entry property"), name);
    }
  else  /* regular prop */
    {
      SVN_ERR_W(svn_wc_prop_list(&prophash, path, adm_access, pool),
                _("Failed to load properties from disk"));

      *value = apr_hash_get(prophash, name, APR_HASH_KEY_STRING);

      return SVN_NO_ERROR;
    }
}


/* The special Subversion properties are not valid for all node kinds.
   Return an error if NAME is an invalid Subversion property for PATH which
   is of kind NODE_KIND. */
static svn_error_t *
validate_prop_against_node_kind(const char *name,
                                const char *path,
                                svn_node_kind_t node_kind,
                                apr_pool_t *pool)
{

  const char *file_prohibit[] = { SVN_PROP_IGNORE,
                                  SVN_PROP_EXTERNALS,
                                  NULL };
  const char *dir_prohibit[] = { SVN_PROP_EXECUTABLE,
                                 SVN_PROP_KEYWORDS,
                                 SVN_PROP_EOL_STYLE,
                                 SVN_PROP_MIME_TYPE,
                                 SVN_PROP_NEEDS_LOCK,
                                 NULL };
  const char **node_kind_prohibit;
  const char *path_display
    = svn_path_is_url(path) ? path : svn_path_local_style(path, pool);

  switch (node_kind)
    {
    case svn_node_dir:
      node_kind_prohibit = dir_prohibit;
      while (*node_kind_prohibit)
        if (strcmp(name, *node_kind_prohibit++) == 0)
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Cannot set '%s' on a directory ('%s')"),
                                   name, path_display);
      break;
    case svn_node_file:
      node_kind_prohibit = file_prohibit;
      while (*node_kind_prohibit)
        if (strcmp(name, *node_kind_prohibit++) == 0)
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Cannot set '%s' on a file ('%s')"),
                                   name,
                                   path_display);
      break;
    default:
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("'%s' is not a file or directory"),
                               path_display);
    }

  return SVN_NO_ERROR;
}


struct getter_baton {
  const char *path;
  svn_wc_adm_access_t *adm_access;
};


static svn_error_t *
get_file_for_validation(const svn_string_t **mime_type,
                        svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *pool)
{
  struct getter_baton *gb = baton;

  if (mime_type)
    SVN_ERR(svn_wc_prop_get(mime_type, SVN_PROP_MIME_TYPE,
                            gb->path, gb->adm_access, pool));

  if (stream)
    {
      svn_stream_t *read_stream;

      /* Open PATH. */
      SVN_ERR(svn_stream_open_readonly(&read_stream, gb->path,
                                       pool, pool));

      /* Copy from the file into the translating stream. */
      SVN_ERR(svn_stream_copy3(read_stream, svn_stream_disown(stream, pool),
                               NULL, NULL, pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
validate_eol_prop_against_file(const char *path,
                               svn_wc_canonicalize_svn_prop_get_file_t getter,
                               void *getter_baton,
                               apr_pool_t *pool)
{
  svn_stream_t *translating_stream;
  svn_error_t *err;
  const svn_string_t *mime_type;
  const char *path_display
    = svn_path_is_url(path) ? path : svn_path_local_style(path, pool);

  /* First just ask the "getter" for the MIME type. */
  SVN_ERR(getter(&mime_type, NULL, getter_baton, pool));

  /* See if this file has been determined to be binary. */
  if (mime_type && svn_mime_type_is_binary(mime_type->data))
    return svn_error_createf
      (SVN_ERR_ILLEGAL_TARGET, NULL,
       _("File '%s' has binary mime type property"),
       path_display);

  /* Now ask the getter for the contents of the file; this will do a
     newline translation.  All we really care about here is whether or
     not the function fails on inconsistent line endings.  The
     function is "translating" to an empty stream.  This is
     sneeeeeeeeeeeaky. */
  translating_stream = svn_subst_stream_translated(svn_stream_empty(pool),
                                                   "", FALSE, NULL, FALSE,
                                                   pool);

  err = getter(NULL, translating_stream, getter_baton, pool);

  if (!err)
    err = svn_stream_close(translating_stream);

  if (err && err->apr_err == SVN_ERR_IO_INCONSISTENT_EOL)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, err,
                             _("File '%s' has inconsistent newlines"),
                             path_display);
  else if (err)
    return err;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_prop_set3(const char *name,
                 const svn_string_t *value,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t skip_checks,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *pool)
{
  apr_hash_t *prophash, *base_prophash;
  enum svn_prop_kind prop_kind = svn_property_kind(NULL, name);
  svn_stringbuf_t *log_accum = svn_stringbuf_create("", pool);
  const svn_wc_entry_t *entry;
  svn_wc_notify_action_t notify_action;

  if (prop_kind == svn_prop_wc_kind)
    return svn_wc__wcprop_set(name, value, path, adm_access, TRUE, pool);
  else if (prop_kind == svn_prop_entry_kind)
    return svn_error_createf   /* we don't do entry properties here */
      (SVN_ERR_BAD_PROP_KIND, NULL,
       _("Property '%s' is an entry property"), name);

  /* Else, handle a regular property: */

  /* Get the entry and name for this path. */
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  /* Get the access baton for the entry's directory. */
  if (entry->kind == svn_node_dir)
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, path, pool));
  else
    SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access,
                                svn_path_dirname(path, pool), pool));

  /* Setting an inappropriate property is not allowed (unless
     overridden by 'skip_checks', in some circumstances).  Deleting an
     inappropriate property is allowed, however, since older clients
     allowed (and other clients possibly still allow) setting it in
     the first place. */
  if (value && svn_prop_is_svn_prop(name))
    {
      const svn_string_t *new_value;
      struct getter_baton *gb = apr_pcalloc(pool, sizeof(*gb));

      gb->path = path;
      gb->adm_access = adm_access;

      SVN_ERR(svn_wc_canonicalize_svn_prop(&new_value, name, value, path,
                                           entry->kind, skip_checks,
                                           get_file_for_validation, gb, pool));
      value = new_value;
    }

  if (entry->kind == svn_node_file && strcmp(name, SVN_PROP_EXECUTABLE) == 0)
    {
      /* If the svn:executable property was set, then chmod +x.
         If the svn:executable property was deleted (NULL value passed
         in), then chmod -x. */
      if (value == NULL)
        SVN_ERR(svn_io_set_file_executable(path, FALSE, TRUE, pool));
      else
        SVN_ERR(svn_io_set_file_executable(path, TRUE, TRUE, pool));
    }

  if (entry->kind == svn_node_file && strcmp(name, SVN_PROP_NEEDS_LOCK) == 0)
    {
      /* If the svn:needs-lock property was set to NULL, set the file
         to read-write */
      if (value == NULL)
        SVN_ERR(svn_io_set_file_read_write(path, FALSE, pool));

      /* If not, we'll set the file to read-only at commit time. */
    }

  SVN_ERR_W(svn_wc__load_props(&base_prophash, &prophash, NULL,
                               adm_access, path, pool),
            _("Failed to load properties from disk"));

  /* If we're changing this file's list of expanded keywords, then
   * we'll need to invalidate its text timestamp, since keyword
   * expansion affects the comparison of working file to text base.
   *
   * Here we retrieve the old list of expanded keywords; after the
   * property is set, we'll grab the new list and see if it differs
   * from the old one.
   */
  if (entry->kind == svn_node_file && strcmp(name, SVN_PROP_KEYWORDS) == 0)
    {
      svn_string_t *old_value = apr_hash_get(prophash, SVN_PROP_KEYWORDS,
                                             APR_HASH_KEY_STRING);
      apr_hash_t *old_keywords, *new_keywords;

      SVN_ERR(svn_wc__get_keywords(&old_keywords, path, adm_access,
                                   old_value ? old_value->data : "", pool));
      SVN_ERR(svn_wc__get_keywords(&new_keywords, path, adm_access,
                                   value ? value->data : "", pool));

      if (svn_subst_keywords_differ2(old_keywords, new_keywords, FALSE, pool))
        {
          svn_wc_entry_t tmp_entry;

          /* If we changed the keywords or newlines, void the entry
             timestamp for this file, so svn_wc_text_modified_p() does
             a real (albeit slow) check later on. */
          tmp_entry.kind = svn_node_file;
          tmp_entry.text_time = 0;
          SVN_ERR(svn_wc__loggy_entry_modify(&log_accum, adm_access,
                                             path, &tmp_entry,
                                             SVN_WC__ENTRY_MODIFY_TEXT_TIME,
                                             pool));
        }
    }

  /* Find out what type of property change we are doing: add, modify, or
     delete. */
  if (apr_hash_get(prophash, name, APR_HASH_KEY_STRING) == NULL)
    {
      if (value == NULL)
        /* Deleting a non-existent property. */
        notify_action = svn_wc_notify_property_deleted_nonexistent;
      else
        /* Adding a property. */
        notify_action = svn_wc_notify_property_added;
    }
  else
    {
      if (value == NULL)
        /* Deleting the property. */
        notify_action = svn_wc_notify_property_deleted;
      else
        /* Modifying property. */
        notify_action = svn_wc_notify_property_modified;
    }

  /* Now we have all the properties in our hash.  Simply merge the new
     property into it. */
  apr_hash_set(prophash, name, APR_HASH_KEY_STRING, value);

  SVN_ERR(svn_wc__install_props(&log_accum, adm_access, path,
                                base_prophash, prophash, FALSE, pool));
  SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));
  SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(path, notify_action, pool);
      notify->prop_name = name;

      (*notify_func)(notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_canonicalize_svn_prop(const svn_string_t **propval_p,
                             const char *propname,
                             const svn_string_t *propval,
                             const char *path,
                             svn_node_kind_t kind,
                             svn_boolean_t skip_some_checks,
                             svn_wc_canonicalize_svn_prop_get_file_t getter,
                             void *getter_baton,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *new_value = NULL;

  /* Keep this static, it may get stored (for read-only purposes) in a
     hash that outlives this function. */
  static const svn_string_t boolean_value =
    {
      SVN_PROP_BOOLEAN_TRUE,
      sizeof(SVN_PROP_BOOLEAN_TRUE) - 1
    };

  SVN_ERR(validate_prop_against_node_kind(propname, path, kind, pool));

  if (!skip_some_checks && (strcmp(propname, SVN_PROP_EOL_STYLE) == 0))
    {
      svn_subst_eol_style_t eol_style;
      const char *ignored_eol;
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
      svn_subst_eol_style_from_value(&eol_style, &ignored_eol, new_value->data);
      if (eol_style == svn_subst_eol_style_unknown)
        return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                 _("Unrecognized line ending style '%s' for '%s'"),
                                 new_value->data,
                                 svn_path_local_style(path, pool));
      SVN_ERR(validate_eol_prop_against_file(path, getter, getter_baton,
                                             pool));
    }
  else if (!skip_some_checks && (strcmp(propname, SVN_PROP_MIME_TYPE) == 0))
    {
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
      SVN_ERR(svn_mime_type_validate(new_value->data, pool));
    }
  else if (strcmp(propname, SVN_PROP_IGNORE) == 0
           || strcmp(propname, SVN_PROP_EXTERNALS) == 0)
    {
      /* Make sure that the last line ends in a newline */
      if (propval->data[propval->len - 1] != '\n')
        {
          new_value = svn_stringbuf_create_from_string(propval, pool);
          svn_stringbuf_appendbytes(new_value, "\n", 1);
        }

      /* Make sure this is a valid externals property.  Do not
         allow 'skip_some_checks' to override, as there is no circumstance in
         which this is proper (because there is no circumstance in
         which Subversion can handle it). */
      if (strcmp(propname, SVN_PROP_EXTERNALS) == 0)
        {
          /* We don't allow "." nor ".." as target directories in
             an svn:externals line.  As it happens, our parse code
             checks for this, so all we have to is invoke it --
             we're not interested in the parsed result, only in
             whether or the parsing errored. */
          SVN_ERR(svn_wc_parse_externals_description3
                  (NULL, path, propval->data, FALSE, pool));
        }
    }
  else if (strcmp(propname, SVN_PROP_KEYWORDS) == 0)
    {
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
    }
  else if (strcmp(propname, SVN_PROP_EXECUTABLE) == 0
        || strcmp(propname, SVN_PROP_NEEDS_LOCK) == 0)
    {
      new_value = svn_stringbuf_create_from_string(&boolean_value, pool);
    }
  else if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      apr_hash_t *mergeinfo;
      svn_string_t *new_value_str;

      SVN_ERR(svn_mergeinfo_parse(&mergeinfo, propval->data, pool));
      SVN_ERR(svn_mergeinfo_to_string(&new_value_str, mergeinfo, pool));
      new_value = svn_stringbuf_create_from_string(new_value_str, pool);
    }

  if (new_value)
    *propval_p = svn_string_create_from_buf(new_value, pool);
  else
    *propval_p = propval;

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc_is_normal_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  return (kind == svn_prop_regular_kind);
}


svn_boolean_t
svn_wc_is_wc_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  return (kind == svn_prop_wc_kind);
}


svn_boolean_t
svn_wc_is_entry_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  return (kind == svn_prop_entry_kind);
}


/* Helper to optimize svn_wc_props_modified_p().

   If PATH_TO_PROP_FILE is nonexistent, is empty, or is of size 4 bytes
   ("END\n"), then set EMPTY_P to true.   Otherwise set EMPTY_P to false,
   which means that the file must contain real properties.  */
static svn_error_t *
empty_props_p(svn_boolean_t *empty_p,
              const char *path,
              svn_node_kind_t node_kind,
              svn_wc__props_kind_t props_kind,
              apr_pool_t *pool)
{
  svn_error_t *err;
  apr_finfo_t finfo;
  const char *prop_path;

  SVN_ERR(svn_wc__prop_path(&prop_path, path, node_kind, props_kind, pool));

  err = svn_io_stat(&finfo, prop_path, APR_FINFO_MIN | APR_FINFO_TYPE, pool);
  if (err)
    {
      if (! APR_STATUS_IS_ENOENT(err->apr_err)
          && ! APR_STATUS_IS_ENOTDIR(err->apr_err))
        return err;

      /* nonexistent */
      svn_error_clear(err);
      *empty_p = TRUE;
      return SVN_NO_ERROR;
    }

  /* If we remove props from a propfile, eventually the file will
     be empty, or, for working copies written by pre-1.3 libraries, will
     contain nothing but "END\n" */
  if (finfo.filetype == APR_REG && (finfo.size == 4 || finfo.size == 0))
    *empty_p = TRUE;
  else
    *empty_p = FALSE;

  /* If the size is between 1 and 4, then something is corrupt.
     If the size is between 4 and 16, then something is corrupt,
     because 16 is the -smallest- the file can possibly be if it
     contained only one property.  So long as we say it is "not
     empty", we will discover such corruption later when we try
     to read the properties from the file. */

  return SVN_NO_ERROR;
}


/* Simple wrapper around empty_props_p, and inversed. */
svn_error_t *
svn_wc__has_props(svn_boolean_t *has_props,
                  const char *path,
                  svn_wc_adm_access_t *adm_access,
                  apr_pool_t *pool)
{
  svn_boolean_t is_empty;
  const svn_wc_entry_t *entry;
  svn_boolean_t has_propcaching =
    svn_wc__adm_wc_format(adm_access) > SVN_WC__NO_PROPCACHING_VERSION;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  /*### Maybe assert (entry); calling svn_wc__has_props
    for an unversioned path is bogus */
  if (! entry)
    {
      *has_props = FALSE;
      return SVN_NO_ERROR;
    }

  /* Use the flag in the entry if the WC is recent enough. */
  if (has_propcaching)
    {
      *has_props = entry->has_props;
      return SVN_NO_ERROR;
    }

  /* The rest is for compatibility with WCs that don't have propcaching. */

  SVN_ERR(empty_props_p(&is_empty, path, entry->kind, svn_wc__props_working,
                        pool));

  *has_props = !is_empty;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_props_modified_p(svn_boolean_t *modified_p,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  int wc_format = svn_wc__adm_wc_format(adm_access);

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  /* If we have no entry, we can't have any prop mods. */
  if (! entry)
    {
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }

  /* For newer WCs, if there is an entry for the path, we have a fast
   * and nice way to retrieve the information from the entry. */
  if (wc_format > SVN_WC__NO_PROPCACHING_VERSION)
    {
      *modified_p = entry->has_prop_mods;
      return SVN_NO_ERROR;
    }

  /* So, we have a WC in an older format, we have some work to do... */

  {
    apr_array_header_t *local_propchanges;
    apr_hash_t *localprops;
    apr_hash_t *baseprops;

    SVN_ERR(load_props(&localprops, path, entry->kind, svn_wc__props_working,
                       pool));

    /* If something is scheduled for replacement, we do *not* want to
       pay attention to any base-props;  they might be residual from the
       old deleted file. */
    /* ### in modern WC formats, they should be the replaced file's
       ### base props. hard to know on old WCs tho? (given the above
       ### comment). just declare propmods if the node has any working
       ### properties. */
    if (entry->schedule == svn_wc_schedule_replace)
      {
        *modified_p = apr_hash_count(localprops) > 0;
        return SVN_NO_ERROR;
      }

    SVN_ERR(load_props(&baseprops, path, entry->kind, svn_wc__props_base,
                       pool));

    SVN_ERR(svn_prop_diffs(&local_propchanges, localprops, baseprops, pool));

    *modified_p = (local_propchanges->nelts > 0);
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_prop_diffs(apr_array_header_t **propchanges,
                      apr_hash_t **original_props,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  apr_hash_t *baseprops, *props;
  const char *entryname;

  /*### Maybe assert (entry); calling svn_wc_get_prop_diffs
    for an unversioned path is bogus */
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  if (! entry)
    {
      if (original_props)
        *original_props = apr_hash_make(pool);

      if (propchanges)
        *propchanges = apr_array_make(pool, 0, sizeof(svn_prop_t));

      return SVN_NO_ERROR;
    }

  if (entry->kind == svn_node_dir)
    {
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, path, pool));
      entryname = SVN_WC_ENTRY_THIS_DIR;
    }
  else
    {
      const char *dirname;
      svn_path_split(path, &dirname, &entryname, pool);
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, adm_access, dirname, pool));
    }

  SVN_ERR(svn_wc__load_props(&baseprops, propchanges ? &props : NULL, NULL,
                             adm_access, path, pool));

  if (original_props != NULL)
    *original_props = baseprops;

  if (propchanges != NULL)
    SVN_ERR(svn_prop_diffs(propchanges, props, baseprops, pool));

  return SVN_NO_ERROR;
}



/** Externals **/

/*
 * Look for either
 *
 *   -r N
 *   -rN
 *
 * in the LINE_PARTS array and update the revision field in ITEM with
 * the revision if the revision is found.  Set REV_IDX to the index in
 * LINE_PARTS where the revision specification starts.  Remove from
 * LINE_PARTS the element(s) that specify the revision.
 * PARENT_DIRECTORY_DISPLAY and LINE are given to return a nice error
 * string.
 *
 * If this function returns successfully, then LINE_PARTS will have
 * only two elements in it.
 */
static svn_error_t *
find_and_remove_externals_revision(int *rev_idx,
                                   const char **line_parts,
                                   int num_line_parts,
                                   svn_wc_external_item2_t *item,
                                   const char *parent_directory_display,
                                   const char *line,
                                   apr_pool_t *pool)
{
  int i;

  for (i = 0; i < 2; ++i)
    {
      const char *token = line_parts[i];

      if (token[0] == '-' && token[1] == 'r')
        {
          svn_opt_revision_t end_revision = { svn_opt_revision_unspecified };
          const char *digits_ptr;
          int shift_count;
          int j;

          *rev_idx = i;

          if (token[2] == '\0')
            {
              /* There must be a total of four elements in the line if
                 -r N is used. */
              if (num_line_parts != 4)
                goto parse_error;

              shift_count = 2;
              digits_ptr = line_parts[i+1];
            }
          else
            {
              /* There must be a total of three elements in the line
                 if -rN is used. */
              if (num_line_parts != 3)
                goto parse_error;

              shift_count = 1;
              digits_ptr = token+2;
            }

          if (svn_opt_parse_revision(&item->revision,
                                     &end_revision,
                                     digits_ptr, pool) != 0)
            goto parse_error;
          /* We want a single revision, not a range. */
          if (end_revision.kind != svn_opt_revision_unspecified)
            goto parse_error;
          /* Allow only numbers and dates, not keywords. */
          if (item->revision.kind != svn_opt_revision_number
              && item->revision.kind != svn_opt_revision_date)
            goto parse_error;

          /* Shift any line elements past the revision specification
             down over the revision specification. */
          for (j = i; j < num_line_parts-shift_count; ++j)
            line_parts[j] = line_parts[j+shift_count];
          line_parts[num_line_parts-shift_count] = NULL;

          /* Found the revision, so leave the function immediately, do
           * not continue looking for additional revisions. */
          return SVN_NO_ERROR;
        }
    }

  /* No revision was found, so there must be exactly two items in the
     line array. */
  if (num_line_parts == 2)
    return SVN_NO_ERROR;

 parse_error:
  return svn_error_createf
    (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
     _("Error parsing %s property on '%s': '%s'"),
     SVN_PROP_EXTERNALS,
     parent_directory_display,
     line);
}

svn_error_t *
svn_wc_parse_externals_description3(apr_array_header_t **externals_p,
                                    const char *parent_directory,
                                    const char *desc,
                                    svn_boolean_t canonicalize_url,
                                    apr_pool_t *pool)
{
  apr_array_header_t *lines = svn_cstring_split(desc, "\n\r", TRUE, pool);
  int i;
  const char *parent_directory_display = svn_path_is_url(parent_directory) ?
    parent_directory : svn_path_local_style(parent_directory, pool);

  if (externals_p)
    *externals_p = apr_array_make(pool, 1, sizeof(svn_wc_external_item2_t *));

  for (i = 0; i < lines->nelts; i++)
    {
      const char *line = APR_ARRAY_IDX(lines, i, const char *);
      apr_status_t status;
      char **line_parts;
      int num_line_parts;
      svn_wc_external_item2_t *item;
      const char *token0;
      const char *token1;
      svn_boolean_t token0_is_url;
      svn_boolean_t token1_is_url;

      /* Index into line_parts where the revision specification
         started. */
      int rev_idx = -1;

      if ((! line) || (line[0] == '#'))
        continue;

      /* else proceed */

      status = apr_tokenize_to_argv(line, &line_parts, pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't split line into components: '%s'"),
                                  line);
      /* Count the number of tokens. */
      for (num_line_parts = 0; line_parts[num_line_parts]; num_line_parts++)
        ;

      SVN_ERR(svn_wc_external_item_create
              ((const svn_wc_external_item2_t **) &item, pool));
      item->revision.kind = svn_opt_revision_unspecified;
      item->peg_revision.kind = svn_opt_revision_unspecified;

      /*
       * There are six different formats of externals:
       *
       * 1) DIR URL
       * 2) DIR -r N URL
       * 3) DIR -rN  URL
       * 4) URL DIR
       * 5) -r N URL DIR
       * 6) -rN URL DIR
       *
       * The last three allow peg revisions in the URL.
       *
       * With relative URLs and no '-rN' or '-r N', there is no way to
       * distinguish between 'DIR URL' and 'URL DIR' when URL is a
       * relative URL like /svn/repos/trunk, so this case is taken as
       * case 4).
       */
      if (num_line_parts < 2 || num_line_parts > 4)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Error parsing %s property on '%s': '%s'"),
           SVN_PROP_EXTERNALS,
           parent_directory_display,
           line);

      /* To make it easy to check for the forms, find and remove -r N
         or -rN from the line item array.  If it is found, rev_idx
         contains the index into line_parts where '-r' was found and
         set item->revision to the parsed revision. */
      /* ### ugh. stupid cast. */
      SVN_ERR(find_and_remove_externals_revision(&rev_idx,
                                                 (const char **)line_parts,
                                                 num_line_parts, item,
                                                 parent_directory_display,
                                                 line, pool));

      token0 = line_parts[0];
      token1 = line_parts[1];

      token0_is_url = svn_path_is_url(token0);
      token1_is_url = svn_path_is_url(token1);

      if (token0_is_url && token1_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use two absolute URLs ('%s' and '%s') in an external; "
             "one must be a path where an absolute or relative URL is "
             "checked out to"),
           SVN_PROP_EXTERNALS, parent_directory_display, token0, token1);

      if (0 == rev_idx && token1_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use a URL '%s' as the target directory for an external "
             "definition"),
           SVN_PROP_EXTERNALS, parent_directory_display, token1);

      if (1 == rev_idx && token0_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use a URL '%s' as the target directory for an external "
             "definition"),
           SVN_PROP_EXTERNALS, parent_directory_display, token0);

      /* The appearence of -r N or -rN forces the type of external.
         If -r is at the beginning of the line or the first token is
         an absolute URL or if the second token is not an absolute
         URL, then the URL supports peg revisions. */
      if (0 == rev_idx ||
          (-1 == rev_idx && (token0_is_url || ! token1_is_url)))
        {
          /* The URL is passed to svn_opt_parse_path in
             uncanonicalized form so that the scheme relative URL
             //hostname/foo is not collapsed to a server root relative
             URL /hostname/foo. */
          SVN_ERR(svn_opt_parse_path(&item->peg_revision, &item->url,
                                     token0, pool));
          item->target_dir = token1;
        }
      else
        {
          item->target_dir = token0;
          item->url = token1;
          item->peg_revision = item->revision;
        }

      SVN_ERR(svn_opt_resolve_revisions(&item->peg_revision,
                                        &item->revision, TRUE, FALSE,
                                        pool));

      item->target_dir = svn_path_canonicalize
        (svn_path_internal_style(item->target_dir, pool), pool);

      if (item->target_dir[0] == '\0' || item->target_dir[0] == '/'
          || svn_path_is_backpath_present(item->target_dir))
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "target '%s' is an absolute path or involves '..'"),
           SVN_PROP_EXTERNALS,
           parent_directory_display,
           item->target_dir);

      if (canonicalize_url)
          item->url = svn_path_canonicalize(item->url, pool);

      if (externals_p)
        APR_ARRAY_PUSH(*externals_p, svn_wc_external_item2_t *) = item;
    }

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_wc__has_special_property(apr_hash_t *props)
{
  return apr_hash_get(props, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING) != NULL;
}

svn_boolean_t
svn_wc__has_magic_property(const apr_array_header_t *properties)
{
  int i;

  for (i = 0; i < properties->nelts; i++)
    {
      const svn_prop_t *property = &APR_ARRAY_IDX(properties, i, svn_prop_t);

      if (strcmp(property->name, SVN_PROP_EXECUTABLE) == 0
          || strcmp(property->name, SVN_PROP_KEYWORDS) == 0
          || strcmp(property->name, SVN_PROP_EOL_STYLE) == 0
          || strcmp(property->name, SVN_PROP_SPECIAL) == 0
          || strcmp(property->name, SVN_PROP_NEEDS_LOCK) == 0)
        return TRUE;
    }
  return FALSE;
}
