/*
 * props.c :  routines dealing with properties in the working copy
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
#include "svn_dirent_uri.h"
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
#include "lock.h"
#include "workqueue.h"

#include "svn_private_config.h"

/* #define TEST_DB_PROPS */

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
           svn_wc__db_t *db,
           const char *local_abspath,
           svn_wc__props_kind_t props_kind,
           apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stream_t *stream;
  apr_finfo_t finfo;
  const char *prop_path;
  svn_wc__db_kind_t kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, pool));
  SVN_ERR(svn_wc__prop_path(&prop_path, local_abspath, kind, props_kind,
                            pool));

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

          /* NOTE: we need to signal that this file is NOT PRESENT, as
             opposed to merely devoid of properties. */
          if (props_kind == svn_wc__props_working)
            *hash = NULL;
          else
            *hash = apr_hash_make(pool);

          return SVN_NO_ERROR;
        }
      else
        return err;
    }

  *hash = apr_hash_make(pool);

  if (finfo.size == 0)
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_open_readonly(&stream, prop_path, pool, pool));

  SVN_ERR(svn_hash_read2(*hash, stream, SVN_HASH_TERMINATOR, pool));

  return svn_stream_close(stream);
}


static svn_error_t *
loggy_write_properties(svn_stringbuf_t **log_accum,
                       apr_hash_t *properties,
                       const char *dest_abspath,
                       const char *adm_abspath,
                       apr_pool_t *scratch_pool)
{
  const char *prop_tmp_abspath;
  svn_stream_t *stream;

  /* Write the property hash into a temporary file. */
  SVN_ERR(svn_stream_open_unique(&stream, &prop_tmp_abspath,
                                 svn_dirent_dirname(dest_abspath,
                                                    scratch_pool),
                                 svn_io_file_del_none,
                                 scratch_pool, scratch_pool));
  if (apr_hash_count(properties) != 0)
    SVN_ERR(svn_hash_write2(properties, stream, SVN_HASH_TERMINATOR,
                            scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  /* Write a log entry to move tmp file to the destination.  */
  SVN_ERR(svn_wc__loggy_move(log_accum, adm_abspath,
                             prop_tmp_abspath, dest_abspath,
                             scratch_pool, scratch_pool));

  /* And make the destination read-only.  */
  SVN_ERR(svn_wc__loggy_set_readonly(log_accum, adm_abspath,
                                     dest_abspath,
                                     scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------------*/

/*** Misc ***/

/* Opens reject temporary stream for FULL_PATH in the appropriate tmp space. */
static svn_error_t *
open_reject_tmp_stream(svn_stream_t **stream,
                       const char **reject_tmp_path,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const char *tmp_base_abspath;

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmp_base_abspath, db, local_abspath,
                                         scratch_pool, scratch_pool));

  return svn_stream_open_unique(stream, reject_tmp_path, tmp_base_abspath,
                                svn_io_file_del_none, result_pool,
                                scratch_pool);
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


/* Get the reject file for LOCAL_ABSPATH in DB.  Set *REJECT_FILE to the
   name of that file, or to NULL if no such file exists. */
static svn_error_t *
get_existing_prop_reject_file(const char **reject_file,
                              svn_wc__db_t *db,
                              const char *adm_abspath,
                              const char *local_abspath,
                              apr_pool_t *pool)
{
  const apr_array_header_t *conflicts;
  int i;

  *reject_file = NULL;

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    pool, pool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *cd;
      cd = APR_ARRAY_IDX(conflicts, i, const svn_wc_conflict_description2_t *);

      if (cd->kind == svn_wc_conflict_kind_property)
        *reject_file = svn_dirent_join(adm_abspath, cd->their_file, pool);
    }

  return SVN_NO_ERROR;
}

/*---------------------------------------------------------------------*/


/* Temporary helper for determining where to store pristine properties.
   All calls will eventually be replaced by direct wc_db operations
   of the right type. */
svn_error_t *
svn_wc__prop_pristine_is_working(svn_boolean_t *working,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_boolean_t base_shadowed;
  *working = TRUE;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               &base_shadowed, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  switch (status)
    {
      case svn_wc__db_status_normal:
        *working = FALSE;
        break;
      case svn_wc__db_status_incomplete:
        *working = base_shadowed;
        break;
      case svn_wc__db_status_deleted:
        /* ### This call fails in some update_editor scenarios, because
               the parent directory can be incomplete. In this specific
               case a caller MUST provide the right location itself.
               (Which (in this case) is always the BASE_NODE table)*/
        SVN_ERR(svn_wc__db_scan_deletion(NULL, working, NULL, NULL, db,
                                         local_abspath, scratch_pool,
                                         scratch_pool));
        break;
      case svn_wc__db_status_added:
        break;
      case svn_wc__db_status_not_present:
      case svn_wc__db_status_absent:
      case svn_wc__db_status_excluded:
        SVN_ERR_ASSERT(0 && "Node not here");
      case svn_wc__db_status_obstructed:
      case svn_wc__db_status_obstructed_add:
      case svn_wc__db_status_obstructed_delete:
        SVN_ERR_ASSERT(0 && "Node misses property information");
      default:
        SVN_ERR_ASSERT(0 && "Unhandled status");
    }

  return SVN_NO_ERROR;
}

/*** Loading regular properties. ***/
svn_error_t *
svn_wc__load_props(apr_hash_t **base_props_p,
                   apr_hash_t **props_p,
                   svn_wc__db_t *db,
                   const char *local_abspath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_hash_t *base_props = NULL; /* Silence uninitialized warning. */

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* We will need the base props if the user requested them, or we need
     them if no (working) prop mods have occurred. */
  if (base_props_p != NULL || props_p != NULL)
    {
      SVN_ERR(load_props(&base_props, db, local_abspath, svn_wc__props_base,
                         result_pool));

      if (base_props_p)
        *base_props_p = base_props;

#ifdef TEST_DB_PROPS
      {
        apr_hash_t *db_base_props;
        SVN_ERR(svn_wc__db_read_pristine_props(&db_base_props, db,
                                               local_abspath,
                                               scratch_pool, scratch_pool));

        if (base_props != NULL)
          {
            if (apr_hash_count(base_props) > 0)
              {
                apr_array_header_t *diffs;

                SVN_ERR_ASSERT(db_base_props != NULL);

                SVN_ERR(svn_prop_diffs(&diffs, base_props, db_base_props,
                                       scratch_pool));

                SVN_ERR_ASSERT(diffs->nelts == 0);
              }
            else
              SVN_ERR_ASSERT(db_base_props == NULL ||
                             (apr_hash_count(db_base_props) == 0));
          }
        else
          SVN_ERR_ASSERT(db_base_props == NULL);
      }
#endif
    }

  if (props_p)
    {
      SVN_ERR(load_props(props_p, db, local_abspath, svn_wc__props_working,
                         result_pool));

      /* If the WORKING props are not present, then no modifications have
         occurred. Simply return a copy of the BASE props.

         Note that the WORKING props might be present, but simply empty,
         signifying that all BASE props have been deleted. */
      if (*props_p == NULL)
        *props_p = apr_hash_copy(result_pool, base_props);

#ifdef TEST_DB_PROPS
      {
        apr_hash_t *db_props;
        SVN_ERR(svn_wc__db_read_props(&db_props, db, local_abspath,
                                      scratch_pool, scratch_pool));

        if (*props_p != NULL)
          {
            if (apr_hash_count(*props_p) > 0)
              {
                apr_array_header_t *diffs;

                SVN_ERR_ASSERT(db_props != NULL);

                SVN_ERR(svn_prop_diffs(&diffs, *props_p, db_props,
                                       scratch_pool));

                SVN_ERR_ASSERT(diffs->nelts == 0);
              }
            else
              SVN_ERR_ASSERT(db_props == NULL ||
                             (apr_hash_count(db_props) == 0));
          }
        else
          SVN_ERR_ASSERT(db_props == NULL);
      }
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__load_revert_props(apr_hash_t **revert_props_p,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  if (revert_props_p)
    {
      svn_boolean_t replaced;

      SVN_ERR(svn_wc__internal_is_replaced(&replaced, db, local_abspath,
                                           scratch_pool));
      if (replaced)
        SVN_ERR(load_props(revert_props_p, db, local_abspath,
                           svn_wc__props_revert, result_pool));
      else
        *revert_props_p = apr_hash_make(result_pool);
    }

  #ifdef TEST_DB_PROPS
  {
    apr_hash_t *db_props;
    SVN_ERR(svn_wc__db_base_get_props(&db_props, db, local_abspath,
                                      scratch_pool, scratch_pool));

    if (apr_hash_count(*revert_props_p) > 0)
      {
        apr_array_header_t *diffs;
        SVN_ERR_ASSERT(db_props != NULL);

        SVN_ERR(svn_prop_diffs(&diffs, *revert_props_p, db_props,
                               scratch_pool));

        SVN_ERR_ASSERT(diffs->nelts == 0);
      }
    else
      SVN_ERR_ASSERT(db_props == NULL ||
                     (apr_hash_count(db_props) == 0));
  }
#endif

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__install_props(svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_hash_t *pristine_props,
                      apr_hash_t *props,
                      svn_boolean_t install_pristine_props,
                      svn_boolean_t force_base_install,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_diffs;

  /* Check if the props are modified. */
  SVN_ERR(svn_prop_diffs(&prop_diffs, props, pristine_props, scratch_pool));

  /* Save the actual properties file if it differs from base. */
  if (prop_diffs->nelts == 0)
    props = NULL; /* Remove actual properties*/

  if (!install_pristine_props)
    pristine_props = NULL; /* Don't change the pristine properties */

  SVN_ERR(svn_wc__wq_add_install_properties(db,
                                            local_abspath,
                                            pristine_props,
                                            props,
                                            force_base_install,
                                            scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
immediate_install_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        svn_wc__db_kind_t kind,
                        apr_hash_t *base_props,
                        apr_hash_t *working_props,
                        apr_pool_t *scratch_pool)
{
  const char *propfile_abspath;
  apr_array_header_t *prop_diffs;

  SVN_ERR(svn_wc__prop_path(&propfile_abspath, local_abspath, kind,
                            svn_wc__props_working, scratch_pool));

  /* Check if the props are modified. */
  SVN_ERR(svn_prop_diffs(&prop_diffs, working_props, base_props,
                         scratch_pool));

  /* Save the working properties file if it differs from base. */
  if (prop_diffs->nelts > 0)
    {
      /* Write out the properties (synchronously).  */
      svn_stream_t *stream;

      SVN_ERR(svn_io_remove_file2(propfile_abspath, TRUE, scratch_pool));
      SVN_ERR(svn_stream_open_writable(&stream, propfile_abspath, scratch_pool,
                                       scratch_pool));
      if (apr_hash_count(working_props) != 0)
        SVN_ERR(svn_hash_write2(working_props, stream, SVN_HASH_TERMINATOR,
                                scratch_pool));
      SVN_ERR(svn_stream_close(stream));

      SVN_ERR(svn_io_set_file_read_only(propfile_abspath, FALSE,
                                        scratch_pool));
    }
  else
    {
      /* No property modifications, remove the file instead. */
      SVN_ERR(svn_io_remove_file2(propfile_abspath, TRUE, scratch_pool));
    }

  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath,
                                  (prop_diffs->nelts > 0) ? working_props
                                                          : NULL,
                                  scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__working_props_committed(svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_kind_t kind;
  const char *working;
  const char *base;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE,
                               scratch_pool));

  /* The path is ensured not an excluded path. */
  /* TODO(#2843) It seems that there is no need to
     reveal hidden entry here? */

  SVN_ERR(svn_wc__prop_path(&working, local_abspath, kind,
                            svn_wc__props_working, scratch_pool));
  SVN_ERR(svn_wc__prop_path(&base, local_abspath, kind,
                            svn_wc__props_base, scratch_pool));

  /* svn_io_file_rename() retains a read-only bit, so there's no
     need to explicitly set it. */
  return svn_error_return(svn_io_file_rename(working, base, scratch_pool));
}


svn_error_t *
svn_wc__loggy_props_delete(svn_stringbuf_t **log_accum,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           const char *adm_abspath,
                           svn_wc__props_kind_t props_kind,
                           apr_pool_t *pool)
{
  svn_wc__db_kind_t kind;
  const char *props_file;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, pool));
  SVN_ERR(svn_wc__prop_path(&props_file, local_abspath, kind, props_kind,
                            pool));
  return svn_error_return(
    svn_wc__loggy_remove(log_accum, adm_abspath, props_file, pool, pool));
}


svn_error_t *
svn_wc__props_delete(svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_wc__props_kind_t props_kind,
                     apr_pool_t *pool)
{
  const char *props_file;
  svn_wc__db_kind_t kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, pool));
  SVN_ERR(svn_wc__prop_path(&props_file, local_abspath, kind, props_kind,
                            pool));
  return svn_error_return(svn_io_remove_file2(props_file, TRUE, pool));
}

svn_error_t *
svn_wc__loggy_revert_props_create(svn_stringbuf_t **log_accum,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const char *adm_abspath,
                                  apr_pool_t *pool)
{
  svn_wc__db_kind_t kind;
  const char *revert_prop_abspath;
  const char *base_prop_abspath;
  svn_node_kind_t on_disk;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, pool));

  /* TODO(#2843) The current caller ensures that PATH will not be an excluded
     item. But do we really need show_hidden = TRUE here? */

  SVN_ERR(svn_wc__prop_path(&revert_prop_abspath, local_abspath, kind,
                            svn_wc__props_revert, pool));
  SVN_ERR(svn_wc__prop_path(&base_prop_abspath, local_abspath, kind,
                            svn_wc__props_base, pool));

  /* If prop base exist, copy it to revert base. */
  SVN_ERR(svn_io_check_path(base_prop_abspath, &on_disk, pool));
  if (on_disk == svn_node_file)
    {
      SVN_ERR(svn_wc__loggy_move(log_accum, adm_abspath,
                                 base_prop_abspath, revert_prop_abspath,
                                 pool, pool));
    }
  else if (on_disk == svn_node_none)
    {
      /* If there wasn't any prop base we still need an empty revert
         propfile, otherwise a revert won't know that a change to the
         props needs to be made (it'll just see no file, and do nothing).
         So (loggily) write out an empty revert propfile.  */

      SVN_ERR(loggy_write_properties(log_accum, apr_hash_make(pool),
                                     revert_prop_abspath,
                                     adm_abspath, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__loggy_revert_props_restore(svn_stringbuf_t **log_accum,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *adm_abspath,
                                   apr_pool_t *pool)
{
  svn_wc__db_kind_t kind;
  const char *revert_file;
  const char *base_file;

  /* TODO(#2843) The current caller ensures that PATH will not be an excluded
     item. But do we really need show_hidden = TRUE here? */

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, pool));

  SVN_ERR(svn_wc__prop_path(&base_file, local_abspath, kind,
                            svn_wc__props_base, pool));
  SVN_ERR(svn_wc__prop_path(&revert_file, local_abspath, kind,
                            svn_wc__props_revert, pool));

  return svn_error_return(
    svn_wc__loggy_move(log_accum, adm_abspath, revert_file, base_file,
                       pool, pool));
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
                                 FALSE, pool));
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
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t mergeinfo1, mergeinfo2;
  SVN_ERR(svn_mergeinfo_parse(&mergeinfo1, prop_val1->data, scratch_pool));
  SVN_ERR(svn_mergeinfo_parse(&mergeinfo2, prop_val2->data, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge(mergeinfo1, mergeinfo2, scratch_pool));
  return svn_mergeinfo_to_string((svn_string_t **)output, mergeinfo1,
                                 result_pool);
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

  SVN_ERR(svn_mergeinfo_remove2(&from_mergeinfo, l_deleted,
                                from_mergeinfo, TRUE, pool, pool));

  return svn_mergeinfo_to_string((svn_string_t **)output, from_mergeinfo, pool);
}


svn_error_t *
svn_wc_merge_props3(svn_wc_notify_state_t *state,
                    svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *pool /* scratch_pool */)
{
  svn_wc__db_kind_t kind;
  svn_boolean_t hidden;
  svn_stringbuf_t *log_accum;
  apr_hash_t *new_base_props;
  apr_hash_t *new_actual_props;

  /* IMPORTANT: svn_wc_merge_prop_diffs relies on the fact that baseprops
     may be NULL. */

  /* Checks whether the node exists and returns the hidden flag */
  SVN_ERR(svn_wc__db_node_hidden(&hidden, wc_ctx->db, local_abspath, pool));

  if (hidden)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath, pool));

  if (! dry_run)
    log_accum = svn_stringbuf_create("", pool);
  else
    log_accum = NULL; /* Provide NULL to __merge_props */

  /* Note that while this routine does the "real" work, it's only
     prepping tempfiles and writing log commands.  */
  SVN_ERR(svn_wc__merge_props(&log_accum, state,
                              &new_base_props, &new_actual_props,
                              wc_ctx->db, local_abspath,
                              left_version, right_version,
                              baseprops, NULL, NULL,
                              propchanges, base_merge, dry_run,
                              conflict_func, conflict_baton,
                              cancel_func, cancel_baton,
                              pool, pool));

  if (!dry_run)
    SVN_ERR(svn_wc__install_props(wc_ctx->db, local_abspath,
                                  new_base_props, new_actual_props,
                                  base_merge, FALSE, pool));


  if (! dry_run)
    {
      const char *dir_abspath;

      SVN_ERR(svn_wc__db_read_kind(&kind, wc_ctx->db, local_abspath,
                                   FALSE, pool));

      switch (kind)
        {
        case svn_wc__db_kind_dir:
          dir_abspath = local_abspath;
          break;
        default:
          dir_abspath = svn_dirent_dirname(local_abspath, pool);
          break;
        }

      if (! svn_stringbuf_isempty(log_accum))
          SVN_ERR(svn_wc__wq_add_loggy(wc_ctx->db, dir_abspath, log_accum,
                                       pool));

      SVN_ERR(svn_wc__run_log2(wc_ctx->db, dir_abspath, pool));
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
 * Given property PROPNAME on LOCAL_ABSPATH, and four possible property
 * values, generate four tmpfiles and pass them to CONFLICT_FUNC callback.
 * This gives the client an opportunity to interactively resolve the
 * property conflict.
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
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            const svn_wc_conflict_version_t *left_version,
                            const svn_wc_conflict_version_t *right_version,
                            svn_boolean_t is_dir,
                            const char *propname,
                            apr_hash_t *working_props,
                            const svn_string_t *old_val,
                            const svn_string_t *new_val,
                            const svn_string_t *base_val,
                            const svn_string_t *working_val,
                            svn_wc_conflict_resolver_func_t conflict_func,
                            void *conflict_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_boolean_t dry_run,
                            apr_pool_t *scratch_pool)
{
  svn_wc_conflict_result_t *result = NULL;
  svn_string_t *mime_propval = NULL;
  apr_pool_t *filepool = svn_pool_create(scratch_pool);
  svn_wc_conflict_description2_t *cdesc;
  const char *dirpath = svn_dirent_dirname(local_abspath, filepool);

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (! conflict_func || dry_run)
    {
      /* Just postpone the conflict. */
      *conflict_remains = TRUE;
      return SVN_NO_ERROR;
    }

  cdesc = svn_wc_conflict_description_create_prop2(
    local_abspath,
    is_dir ? svn_node_dir : svn_node_file, propname, scratch_pool);

  cdesc->src_left_version = left_version;
  cdesc->src_right_version = right_version;

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
                                         filepool, scratch_pool));
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
  {
    svn_wc_conflict_description_t *cd = svn_wc__cd2_to_cd(cdesc, scratch_pool);
    SVN_ERR(conflict_func(&result, cd, conflict_baton, scratch_pool));
  }
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
                                               scratch_pool));
              merged_string = svn_string_create_from_buf(merged_stringbuf,
                                                         scratch_pool);
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
 * conflict interactively.
 */
static svn_error_t *
apply_single_prop_add(svn_wc_notify_state_t *state,
                      svn_string_t **conflict,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      const svn_wc_conflict_version_t *left_version,
                      const svn_wc_conflict_version_t *right_version,
                      svn_boolean_t is_dir,
                      apr_hash_t *working_props,
                      const char *propname,
                      const svn_string_t *base_val,
                      const svn_string_t *new_val,
                      svn_wc_conflict_resolver_func_t conflict_func,
                      void *conflict_baton,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      svn_boolean_t dry_run,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)

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
              const svn_string_t *merged_val;

              SVN_ERR(combine_mergeinfo_props(&merged_val, working_val,
                                              new_val, result_pool,
                                              scratch_pool));
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, merged_val);
              set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
          else
            {
              SVN_ERR(maybe_generate_propconflict(&got_conflict, db,
                                                  local_abspath,
                                                  left_version, right_version,
                                                  is_dir,
                                                  propname, working_props,
                                                  NULL, new_val,
                                                  base_val, working_val,
                                                  conflict_func, conflict_baton,
                                                  cancel_func, cancel_baton,
                                                  dry_run, scratch_pool));
              if (got_conflict)
                *conflict = svn_string_createf
                    (result_pool,
                     _("Trying to add new property '%s' with value "
                       "'%s',\nbut property already exists with value '%s'."),
                     propname, new_val->data, working_val->data);
            }
        }
    }
  else if (base_val)
    {
      SVN_ERR(maybe_generate_propconflict(&got_conflict, db, local_abspath,
                                          left_version, right_version,
                                          is_dir, propname,
                                          working_props, NULL, new_val,
                                          base_val, NULL,
                                          conflict_func, conflict_baton,
                                          cancel_func, cancel_baton,
                                          dry_run, scratch_pool));
      if (got_conflict)
        *conflict = svn_string_createf
            (result_pool,
             _("Trying to create property '%s' with value '%s',\n"
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
 * conflict interactively.
 */
static svn_error_t *
apply_single_prop_delete(svn_wc_notify_state_t *state,
                         svn_string_t **conflict,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const svn_wc_conflict_version_t *left_version,
                         const svn_wc_conflict_version_t *right_version,
                         svn_boolean_t is_dir,
                         apr_hash_t *working_props,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         svn_wc_conflict_resolver_func_t conflict_func,
                         void *conflict_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         svn_boolean_t dry_run,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
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
               SVN_ERR(maybe_generate_propconflict(&got_conflict, db,
                                                   local_abspath,
                                                   left_version, right_version,
                                                   is_dir,
                                                   propname, working_props,
                                                   old_val, NULL,
                                                   base_val, working_val,
                                                   conflict_func, conflict_baton,
                                                   cancel_func, cancel_baton,
                                                   dry_run, scratch_pool));
               if (got_conflict)
                 *conflict = svn_string_createf
                     (result_pool,
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
      SVN_ERR(maybe_generate_propconflict(&got_conflict, db, local_abspath,
                                          left_version, right_version,
                                          is_dir, propname,
                                          working_props, old_val, NULL,
                                          base_val, working_val,
                                          conflict_func, conflict_baton,
                                          cancel_func, cancel_baton,
                                          dry_run, scratch_pool));
      if (got_conflict)
        *conflict = svn_string_createf
            (result_pool,
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
                                   svn_string_t **conflict,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const svn_wc_conflict_version_t *left_version,
                                   const svn_wc_conflict_version_t *right_version,
                                   svn_boolean_t is_dir,
                                   apr_hash_t *working_props,
                                   const char *propname,
                                   const svn_string_t *base_val,
                                   const svn_string_t *old_val,
                                   const svn_string_t *new_val,
                                   svn_wc_conflict_resolver_func_t conflict_func,
                                   void *conflict_baton,
                                   svn_cancel_func_t cancel_func,
                                   void *cancel_baton,
                                   svn_boolean_t dry_run,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
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
                                                         new_val, result_pool));
                  apr_hash_set(working_props, propname,
                               APR_HASH_KEY_STRING, new_val);
                  set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
        }

      else
        {
          /* There is a base_val but no working_val */
          SVN_ERR(maybe_generate_propconflict(&got_conflict, db, local_abspath,
                                              left_version, right_version,
                                              is_dir, propname, working_props,
                                              old_val, new_val,
                                              base_val, working_val,
                                              conflict_func, conflict_baton,
                                              cancel_func, cancel_baton,
                                              dry_run, scratch_pool));
          if (got_conflict)
            *conflict = svn_string_createf
                (result_pool,
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
                                       old_val, new_val, scratch_pool));
          SVN_ERR(svn_mergeinfo_to_string((svn_string_t **)&new_val,
                                          added_mergeinfo, result_pool));
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
                                                     new_val, result_pool));
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
                                 svn_string_t **conflict,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 const svn_wc_conflict_version_t *left_version,
                                 const svn_wc_conflict_version_t *right_version,
                                 svn_boolean_t is_dir,
                                 apr_hash_t *working_props,
                                 const char *propname,
                                 const svn_string_t *base_val,
                                 const svn_string_t *old_val,
                                 const svn_string_t *new_val,
                                 svn_wc_conflict_resolver_func_t conflict_func,
                                 void *conflict_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 svn_boolean_t dry_run,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
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
      SVN_ERR(maybe_generate_propconflict(&got_conflict, db, local_abspath,
                                          left_version, right_version,
                                          is_dir, propname, working_props,
                                          old_val, new_val,
                                          base_val, working_val,
                                          conflict_func, conflict_baton,
                                          cancel_func, cancel_baton,
                                          dry_run, scratch_pool));
      if (got_conflict)
        {
          /* Describe the conflict, referring to base_val as well as
             working_val for the user's convenience. */
          if (working_val && base_val
              && svn_string_compare(working_val, base_val))
            *conflict = svn_string_createf
              (result_pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but property already exists with value '%s'."),
               propname, old_val->data, new_val->data, working_val->data);
          else if (working_val && base_val)
            *conflict = svn_string_createf
              (result_pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but the property has been locally changed from '%s' to "
                 "'%s'."),
               propname, old_val->data, new_val->data,
               base_val->data, working_val->data);
          else if (working_val)
            *conflict = svn_string_createf
              (result_pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but property has been locally added with value "
                 "'%s'."),
               propname, old_val->data, new_val->data, working_val->data);
          else if (base_val)
            *conflict = svn_string_createf
              (result_pool,
               _("Trying to change property '%s' from '%s' to '%s',\n"
                 "but it has been locally deleted."),
               propname, old_val->data, new_val->data);
          else
            *conflict = svn_string_createf
              (result_pool,
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
 * conflict interactively.
 */
static svn_error_t *
apply_single_prop_change(svn_wc_notify_state_t *state,
                         svn_string_t **conflict,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const svn_wc_conflict_version_t *left_version,
                         const svn_wc_conflict_version_t *right_version,
                         svn_boolean_t is_dir,
                         apr_hash_t *working_props,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         const svn_string_t *new_val,
                         svn_wc_conflict_resolver_func_t conflict_func,
                         void *conflict_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         svn_boolean_t dry_run,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
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

      SVN_ERR(apply_single_mergeinfo_prop_change(state, conflict, db,
                                                 local_abspath,
                                                 left_version, right_version,
                                                 is_dir,
                                                 working_props,
                                                 propname, base_val, old_val,
                                                 new_val,
                                                 conflict_func, conflict_baton,
                                                 cancel_func, cancel_baton,
                                                 dry_run, result_pool, scratch_pool));
    }
  else
    {
      /* The standard method: perform a simple update automatically, but
         pass any other kind of merge to maybe_generate_propconflict(). */

      SVN_ERR(apply_single_generic_prop_change(state, conflict, db,
                                               local_abspath,
                                               left_version, right_version,
                                               is_dir,
                                               working_props,
                                               propname, base_val, old_val,
                                               new_val,
                                               conflict_func, conflict_baton,
                                               cancel_func, cancel_baton,
                                               dry_run, result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__merge_props(svn_stringbuf_t **entry_accum,
                    svn_wc_notify_state_t *state,
                    apr_hash_t **new_base_props,
                    apr_hash_t **new_actual_props,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *server_baseprops,
                    apr_hash_t *base_props,
                    apr_hash_t *working_props,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  int i;
  svn_boolean_t is_dir;
  const char *reject_path = NULL;
  svn_stream_t *reject_tmp_stream = NULL;  /* the temporary conflicts stream */
  const char *reject_tmp_path = NULL;
  svn_wc__db_kind_t kind;
  const char *adm_abspath;

  *new_base_props = NULL;
  *new_actual_props = NULL;

  /* ### shouldn't ALLOW_MISSING be FALSE? how can we merge props into
     ### a node that doesn't exist?!  */
  /* ### BH: In some cases we allow merging into missing to create a new
             node. */
  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, TRUE, scratch_pool));

  if (kind == svn_wc__db_kind_dir)
    {
      is_dir = TRUE;
      adm_abspath = local_abspath;
    }
  else
    {
      is_dir = FALSE;
      adm_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
    }

  /* If not provided, load the base & working property files into hashes */
  if (! base_props || ! working_props)
    {
      if (kind == svn_wc__db_kind_unknown)
        {
          /* No entry... no props.  */
          if (base_props == NULL)
            base_props = apr_hash_make(result_pool);
          if (working_props == NULL)
            working_props = apr_hash_make(result_pool);
        }
      else
        {
          SVN_ERR(svn_wc__load_props(base_props ? NULL : &base_props,
                                     working_props ? NULL : &working_props,
                                     db, local_abspath,
                                     result_pool, scratch_pool));
        }
    }
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
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < propchanges->nelts; i++)
    {
      const char *propname;
      svn_string_t *conflict = NULL;
      const svn_prop_t *incoming_change;
      const svn_string_t *from_val, *to_val, *working_val, *base_val;
      svn_boolean_t is_normal;

      svn_pool_clear(iterpool);

      /* For the incoming propchange, figure out the TO and FROM values. */
      incoming_change = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);
      propname = incoming_change->name;
      is_normal = svn_wc_is_normal_prop(propname);
      to_val = incoming_change->value
        ? svn_string_dup(incoming_change->value, result_pool) : NULL;
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
        SVN_ERR(apply_single_prop_add(is_normal ? state : NULL, &conflict,
                                      db, local_abspath,
                                      left_version, right_version,
                                      is_dir, working_props,
                                      propname, base_val, to_val,
                                      conflict_func, conflict_baton,
                                      cancel_func, cancel_baton,
                                      dry_run, result_pool, iterpool));

      else if (! to_val) /* delete an existing property */
        SVN_ERR(apply_single_prop_delete(is_normal ? state : NULL, &conflict,
                                         db, local_abspath,
                                         left_version, right_version,
                                         is_dir,
                                         working_props,
                                         propname, base_val, from_val,
                                         conflict_func, conflict_baton,
                                         cancel_func, cancel_baton,
                                         dry_run, result_pool, iterpool));

      else  /* changing an existing property */
        SVN_ERR(apply_single_prop_change(is_normal ? state : NULL, &conflict,
                                         db, local_abspath,
                                         left_version, right_version,
                                         is_dir,
                                         working_props,
                                         propname, base_val, from_val, to_val,
                                         conflict_func, conflict_baton,
                                         cancel_func, cancel_baton,
                                         dry_run, result_pool, iterpool));


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
            SVN_ERR(open_reject_tmp_stream(&reject_tmp_stream,
                                           &reject_tmp_path, db,
                                           local_abspath,
                                           scratch_pool, iterpool));

          /* Append the conflict to the open tmp/PROPS/---.prej file */
          SVN_ERR(append_prop_conflict(reject_tmp_stream, conflict,
                                       iterpool));
        }

    }  /* foreach propchange ... */
  svn_pool_destroy(iterpool);

  /* Finished applying all incoming propchanges to our hashes! */

  if (dry_run)
    return SVN_NO_ERROR;

  *new_base_props = base_props;
  *new_actual_props = working_props;

  if (reject_tmp_stream)
    {
      /* There's a temporary reject file sitting in .svn/tmp/ somewhere.  Deal
         with the conflicts.  */

      /* First, _close_ this temporary conflicts file.  We've been
         appending to it all along. */
      SVN_ERR(svn_stream_close(reject_tmp_stream));

      /* Now try to get the name of a pre-existing .prej file from the
         entries file */
      SVN_ERR(get_existing_prop_reject_file(&reject_path, db, adm_abspath,
                                            local_abspath, scratch_pool));

      if (! reject_path)
        {
          /* Reserve a new .prej file *above* the .svn/ directory by
             opening and closing it. */
          const char *reject_dirpath;
          const char *reject_filename;

          if (is_dir)
            {
              reject_dirpath = local_abspath;
              reject_filename = SVN_WC__THIS_DIR_PREJ;
            }
          else
            svn_dirent_split(local_abspath, &reject_dirpath, &reject_filename,
                             scratch_pool);

          SVN_ERR(svn_io_open_uniquely_named(NULL, &reject_path,
                                             reject_dirpath,
                                             reject_filename,
                                             SVN_WC__PROP_REJ_EXT,
                                             svn_io_file_del_none,
                                             scratch_pool, scratch_pool));

          /* This file will be overwritten when the log is run; that's
             ok, because at least now we have a reservation on
             disk. */
        }

      /* We've now guaranteed that some kind of .prej file exists
         above the .svn/ dir.  We write log entries to append our
         conflicts to it. */
      SVN_WC__FLUSH_LOG_ACCUM(db, adm_abspath, *entry_accum, scratch_pool);
      SVN_ERR(svn_wc__loggy_append(db, adm_abspath, reject_tmp_path,
                                   reject_path, result_pool));

      /* And of course, delete the temporary reject file. */
      SVN_WC__FLUSH_LOG_ACCUM(db, adm_abspath, *entry_accum, scratch_pool);
      SVN_ERR(svn_wc__loggy_remove(entry_accum, adm_abspath,
                                   reject_tmp_path, result_pool,
                                   scratch_pool));

      /* Mark entry as "conflicted" with a particular .prej file. */
      {
        svn_wc_entry_t entry;

        entry.prejfile = svn_dirent_is_child(adm_abspath, reject_path, NULL);
        SVN_WC__FLUSH_LOG_ACCUM(db, adm_abspath, *entry_accum, scratch_pool);
        SVN_ERR(svn_wc__loggy_entry_modify(entry_accum, adm_abspath,
                                           local_abspath, &entry,
                                           SVN_WC__ENTRY_MODIFY_PREJFILE,
                                           result_pool, scratch_pool));
      }

    } /* if (reject_tmp_fp) */

  return SVN_NO_ERROR;
}



/*** Private 'wc prop' functions ***/


svn_error_t *
svn_wc__wcprop_set(svn_wc__db_t *db,
                   const char *local_abspath,
                   const char *name,
                   const svn_string_t *value,
                   apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                        scratch_pool, scratch_pool));

  if (prophash == NULL)
    prophash = apr_hash_make(scratch_pool);

  apr_hash_set(prophash, name, APR_HASH_KEY_STRING, value);
  return svn_error_return(svn_wc__db_base_set_dav_cache(db, local_abspath,
                                                        prophash,
                                                        scratch_pool));
}

/*------------------------------------------------------------------*/


/*** Public Functions ***/


svn_error_t *
svn_wc_prop_list2(apr_hash_t **props,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  return svn_error_return(
    svn_wc__load_props(NULL, props, wc_ctx->db, local_abspath,
                       result_pool, scratch_pool));
}

svn_error_t *
svn_wc_prop_get2(const svn_string_t **value,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (kind == svn_prop_entry_kind)
    {
      /* we don't do entry properties here */
      return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                               _("Property '%s' is an entry property"), name);
    }

  SVN_ERR(svn_wc__internal_propget(value, wc_ctx->db, local_abspath, name,
                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_propget(const svn_string_t **value,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash = NULL;
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  svn_wc__db_kind_t wc_kind;
  svn_boolean_t hidden;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(kind != svn_prop_entry_kind);

  SVN_ERR(svn_wc__db_read_kind(&wc_kind, db, local_abspath, TRUE, scratch_pool));

  if (wc_kind == svn_wc__db_kind_unknown)
    {
      /* The node is not present, or not really "here". Therefore, the
         property is not present.  */
      *value = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__db_node_hidden(&hidden, db, local_abspath, scratch_pool));
  if (hidden)
    {
      /* The node is not present, or not really "here". Therefore, the
         property is not present.  */
      *value = NULL;
      return SVN_NO_ERROR;
    }

  if (kind == svn_prop_wc_kind)
    {
      svn_error_t *err;
      /* If no dav cache can be found, just set VALUE to NULL (for
         compatibility with pre-WC-NG code). */
      err = svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                          result_pool, scratch_pool);
      if (err && (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND))
        {
          *value = NULL;
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      SVN_ERR_W(err, _("Failed to load properties from disk"));
    }
  else
    {
      /* regular prop */
      SVN_ERR_W(svn_wc__load_props(NULL, &prophash, db, local_abspath,
                                   result_pool, scratch_pool),
                _("Failed to load properties from disk"));
    }

  if (prophash)
    *value = apr_hash_get(prophash, name, APR_HASH_KEY_STRING);
  else
    *value = NULL;

  return SVN_NO_ERROR;
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
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

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
  const char *local_abspath;
  svn_wc__db_t *db;
};


static svn_error_t *
get_file_for_validation(const svn_string_t **mime_type,
                        svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *pool)
{
  struct getter_baton *gb = baton;

  if (mime_type)
    SVN_ERR(svn_wc__internal_propget(mime_type, gb->db, gb->local_abspath,
                                     SVN_PROP_MIME_TYPE, pool, pool));

  if (stream)
    {
      svn_stream_t *read_stream;

      /* Open PATH. */
      SVN_ERR(svn_stream_open_readonly(&read_stream, gb->local_abspath,
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
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

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
svn_wc__internal_propset(svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         const svn_string_t *value,
                         svn_boolean_t skip_checks,
                         svn_wc_notify_func2_t notify_func,
                         void *notify_baton,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash, *base_prophash;
  enum svn_prop_kind prop_kind = svn_property_kind(NULL, name);
  svn_wc_notify_action_t notify_action;
  svn_wc__db_kind_t kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (prop_kind == svn_prop_wc_kind)
    return svn_error_return(svn_wc__wcprop_set(db, local_abspath,
                                               name, value, scratch_pool));

  /* we don't do entry properties here */
  if (prop_kind == svn_prop_entry_kind)
    return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                             _("Property '%s' is an entry property"), name);

  /* Else, handle a regular property: */

  /* Get the node kind for this path. */
  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  /* Setting an inappropriate property is not allowed (unless
     overridden by 'skip_checks', in some circumstances).  Deleting an
     inappropriate property is allowed, however, since older clients
     allowed (and other clients possibly still allow) setting it in
     the first place. */
  if (value && svn_prop_is_svn_prop(name))
    {
      const svn_string_t *new_value;
      struct getter_baton gb;

      gb.local_abspath = local_abspath;
      gb.db = db;

      SVN_ERR(svn_wc_canonicalize_svn_prop(&new_value, name, value,
                                           local_abspath,
                                           kind == svn_wc__db_kind_dir ?
                                                    svn_node_dir :
                                                    svn_node_file,
                                           skip_checks,
                                           get_file_for_validation, &gb,
                                           scratch_pool));
      value = new_value;
    }

  if (kind == svn_wc__db_kind_file && strcmp(name, SVN_PROP_EXECUTABLE) == 0)
    {
      /* If the svn:executable property was set, then chmod +x.
         If the svn:executable property was deleted (NULL value passed
         in), then chmod -x. */
      if (value == NULL)
        SVN_ERR(svn_io_set_file_executable(local_abspath, FALSE, TRUE,
                                           scratch_pool));
      else
        SVN_ERR(svn_io_set_file_executable(local_abspath, TRUE, TRUE,
                                           scratch_pool));
    }

  if (kind == svn_wc__db_kind_file && strcmp(name, SVN_PROP_NEEDS_LOCK) == 0)
    {
      /* If the svn:needs-lock property was set to NULL, set the file
         to read-write */
      if (value == NULL)
        SVN_ERR(svn_io_set_file_read_write(local_abspath, FALSE,
                                           scratch_pool));

      /* If not, we'll set the file to read-only at commit time. */
    }

  SVN_ERR_W(svn_wc__load_props(&base_prophash, &prophash, db,
                               local_abspath, scratch_pool, scratch_pool),
            _("Failed to load properties from disk"));

  /* If we're changing this file's list of expanded keywords, then
   * we'll need to invalidate its text timestamp, since keyword
   * expansion affects the comparison of working file to text base.
   *
   * Here we retrieve the old list of expanded keywords; after the
   * property is set, we'll grab the new list and see if it differs
   * from the old one.
   */
  if (kind == svn_wc__db_kind_file && strcmp(name, SVN_PROP_KEYWORDS) == 0)
    {
      svn_string_t *old_value = apr_hash_get(prophash, SVN_PROP_KEYWORDS,
                                             APR_HASH_KEY_STRING);
      apr_hash_t *old_keywords, *new_keywords;

      SVN_ERR(svn_wc__get_keywords(&old_keywords, db, local_abspath,
                                   old_value ? old_value->data : "",
                                   scratch_pool, scratch_pool));
      SVN_ERR(svn_wc__get_keywords(&new_keywords, db, local_abspath,
                                   value ? value->data : "",
                                   scratch_pool, scratch_pool));

      if (svn_subst_keywords_differ2(old_keywords, new_keywords, FALSE,
                                     scratch_pool))
        {
          /* NOTE: this change is immediate. If the overall propset fails,
             then we end up with an un-cached text_time. Big whoop.  */

          /* If we changed the keywords or newlines, void the entry
             timestamp for this file, so svn_wc_text_modified_p() does
             a real (albeit slow) check later on. */
          /* Setting the last mod time to zero will effectively invalidate
             it's value. */
          SVN_ERR(svn_wc__db_op_set_last_mod_time(db, local_abspath, 0,
                                                  scratch_pool));
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

  /* Drop it right onto the disk. We don't need loggy since we aren't
     coordinating this change with anything else.  */
  SVN_ERR(immediate_install_props(db, local_abspath, kind,
                                  base_prophash, prophash, scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     notify_action,
                                                     scratch_pool);
      notify->prop_name = name;

      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_prop_set4(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 const svn_string_t *value,
                 svn_boolean_t skip_checks,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__internal_propset(wc_ctx->db, local_abspath,
                                   name, value, skip_checks, notify_func,
                                   notify_baton, scratch_pool));
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
                                 _("Unrecognized line ending style for '%s'"),
                                 svn_dirent_local_style(path, pool));
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


svn_error_t *
svn_wc__has_props(svn_boolean_t *has_props,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  apr_hash_t *base_props;
  apr_hash_t *working_props;

  SVN_ERR(svn_wc__load_props(&base_props, &working_props,
                             db, local_abspath, scratch_pool, scratch_pool));
  *has_props =
        ((apr_hash_count(base_props) + apr_hash_count(working_props)) > 0);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__props_modified(svn_boolean_t *modified_p,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  apr_array_header_t *local_propchanges;
  apr_hash_t *localprops;
  apr_hash_t *baseprops;
  svn_wc__db_status_t status;
  svn_error_t *err;
  svn_boolean_t replaced;

  err = svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL,
                             db, local_abspath,
                             scratch_pool, scratch_pool);

  /* If we have no entry, we can't have any prop mods. */
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      *modified_p = FALSE;
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  SVN_ERR(load_props(&localprops, db, local_abspath, svn_wc__props_working,
                     scratch_pool));

  /* If the WORKING props are not present, then no modifications have
     occurred. */
  if (localprops == NULL)
    {
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }

  /* If something is scheduled for replacement, we do *not* want to
     pay attention to any base-props;  they might be residual from the
     old deleted file. */
  /* ### in modern WC formats, they should be the replaced file's
     ### base props. hard to know on old WCs tho? (given the above
     ### comment). just declare propmods if the node has any working
     ### properties. */
  SVN_ERR(svn_wc__internal_is_replaced(&replaced, db, local_abspath,
                                       scratch_pool));
  if (replaced)
    {
      *modified_p = apr_hash_count(localprops) > 0;
      return SVN_NO_ERROR;
    }

  /* The WORKING props are present, so let's dig in and see what the
     differences are. On really old WCs, they might be the same. On
     newer WCs, the file would have been removed if there was no delta. */
  SVN_ERR(load_props(&baseprops, db, local_abspath, svn_wc__props_base,
                     scratch_pool));

  SVN_ERR(svn_prop_diffs(&local_propchanges, localprops, baseprops,
                         scratch_pool));

  *modified_p = (local_propchanges->nelts > 0);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__internal_propdiff(apr_array_header_t **propchanges,
                          apr_hash_t **original_props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_hash_t *baseprops, *props;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__load_props(&baseprops, propchanges ? &props : NULL,
                             db, local_abspath, result_pool, scratch_pool));

  if (original_props != NULL)
    *original_props = baseprops;

  if (propchanges != NULL)
    SVN_ERR(svn_prop_diffs(propchanges, props, baseprops, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_prop_diffs2(apr_array_header_t **propchanges,
                       apr_hash_t **original_props,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__internal_propdiff(propchanges,
                                    original_props, wc_ctx->db, local_abspath,
                                    result_pool, scratch_pool));
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
    parent_directory : svn_dirent_local_style(parent_directory, pool);

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

      item->target_dir = svn_dirent_internal_style(item->target_dir, pool);

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
          item->url = svn_uri_canonicalize(item->url, pool);

      if (externals_p)
        APR_ARRAY_PUSH(*externals_p, svn_wc_external_item2_t *) = item;
    }

  return SVN_NO_ERROR;
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
