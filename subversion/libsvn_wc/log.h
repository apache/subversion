/*
 * log.h :  interfaces for running .svn/log files.
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


#ifndef SVN_LIBSVN_WC_LOG_H
#define SVN_LIBSVN_WC_LOG_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Return the path to use for logfile number LOG_NUMBER.  The returned
   string will be allocated from POOL.

   For log number 0, this will just be SVN_WC__ADM_LOG to maintain
   compatibility with 1.0.x.  Higher numbers have the digits of the
   number appended to SVN_WC__ADM_LOG so that they look like "log.1",
   "log.2", etc. */
const char *svn_wc__logfile_path(int log_number,
                                 apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to append the contents
   of SRC to DST.
*/

svn_error_t *
svn_wc__loggy_append(svn_stringbuf_t **log_accum,
                     svn_wc_adm_access_t *adm_access,
                     const char *src, const char *dst,
                     apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to mark PATH as committed
   with revision REVNUM.
*/

svn_error_t *
svn_wc__loggy_committed(svn_stringbuf_t **log_accum,
                        svn_wc_adm_access_t *adm_access,
                        const char *path, svn_revnum_t revnum,
                        apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to copy the file SRC_PATH to
   DST_PATH, if it exists. If it doesn't and REMOVE_DST_IF_NO_SRC is TRUE
   the file at DST_PATH will be deleted if any.

   Sets *DST_MODIFIED, if either the copy or the remove have been carried out.
*/

typedef enum svn_wc__copy_t
{
  /* Normal copy, no translation */
  svn_wc__copy_normal = 0,

  /* Copy, translate using file properties */
  svn_wc__copy_translate,

  /* Copy, translate using only the svn:special property, if any */
  svn_wc__copy_translate_special_only,

  /* Copy, detranslate using file properties */
  svn_wc__copy_detranslate
} svn_wc__copy_t;


svn_error_t *
svn_wc__loggy_copy(svn_stringbuf_t **log_accum,
                   svn_boolean_t *dst_modified,
                   svn_wc_adm_access_t *adm_access,
                   svn_wc__copy_t copy_type,
                   const char *src_path, const char *dst_path,
                   svn_boolean_t remove_dst_if_no_src,
                   apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to generate a translated
   file from SRC to DST with translation settings from VERSIONED
   and flags specified in FLAGS.
*/

svn_error_t *
svn_wc__loggy_translated_file(svn_stringbuf_t **log_accum,
                              svn_wc_adm_access_t *adm_access,
                              const char *dst,
                              const char *src,
                              const char *versioned,
                              apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to delete the entry
   associated with PATH from the entries file.
*/
svn_error_t *
svn_wc__loggy_delete_entry(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to delete lock related
   fields from the entry belonging to PATH.
*/

svn_error_t *
svn_wc__loggy_delete_lock(svn_stringbuf_t **log_accum,
                          svn_wc_adm_access_t *adm_access,
                          const char *path,
                          apr_pool_t *pool);


/* Extend **LOG_ACCUM with commands to modify the entry associated with NAME
   according to the flags specified in MODIFY_FLAGS, based on the values
   supplied in *ENTRY.

   The flags in MODIFY_FLAGS are to be taken from the svn_wc__entry_modify
   parameter by the same name.
*/
svn_error_t *
svn_wc__loggy_entry_modify(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *name,
                           svn_wc_entry_t *entry,
                           apr_uint32_t modify_flags,
                           apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to modify wcprop PROPNAME
   for PATH, setting it to PROPVAL.
*/

svn_error_t *
svn_wc__loggy_modify_wcprop(svn_stringbuf_t **log_accum,
                            svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *propname,
                            const char *propval,
                            apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to merge changes between
   LEFT and RIGHT into TARGET, marking conflicts with the appropriate labels.
*/

svn_error_t *
svn_wc__loggy_merge(svn_stringbuf_t **log_accum,
                    svn_wc_adm_access_t *adm_access,
                    const char *target,
                    const char *left,
                    const char *right,
                    const char *left_label,
                    const char *right_label,
                    const char *target_label,
                    apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to move the file SRC_PATH to
   DST_PATH, if it exists. If it doesn't and REMOVE_DST_IF_NO_SRC is TRUE
   the file at DST_PATH will be deleted if any.

   Sets *DST_MODIFIED, if either the copy or the remove have been carried out.
*/

svn_error_t *
svn_wc__loggy_move(svn_stringbuf_t **log_accum,
                   svn_boolean_t *dst_modified,
                   svn_wc_adm_access_t *adm_access,
                   const char *src_path, const char *dst_path,
                   svn_boolean_t remove_dst_if_no_src,
                   apr_pool_t *pool);



/* Extend **LOG_ACCUM with log instructions to set permissions of PATH
   to 'executable' if it has the 'executable' property set.
*/

svn_error_t *
svn_wc__loggy_maybe_set_executable(svn_stringbuf_t **log_accum,
                                   svn_wc_adm_access_t *adm_access,
                                   const char *path,
                                   apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to set permissions of PATH
   to 'readonly' if it has the 'needs-lock' property set and there is
   no lock for the file in the working copy.
*/

svn_error_t *
svn_wc__loggy_maybe_set_readonly(svn_stringbuf_t **log_accum,
                                 svn_wc_adm_access_t *adm_access,
                                 const char *path,
                                 apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to set the timestamp of PATH
   in the entry field with name TIME_PROP.

   Use SVN_WC__ENTRY_ATTR_* values for TIME_PROP.
*/

svn_error_t *
svn_wc__loggy_set_entry_timestamp_from_wc(svn_stringbuf_t **log_accum,
                                          svn_wc_adm_access_t *adm_access,
                                          const char *path,
                                          const char *time_prop,
                                          apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to set permissions of PATH
   to 'readonly'.
*/

svn_error_t *
svn_wc__loggy_set_readonly(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to set the timestamp of PATH.
*/

svn_error_t *
svn_wc__loggy_set_timestamp(svn_stringbuf_t **log_accum,
                            svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *timestr,
                            apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to remove the file
   BASE_NAME, if it exists.
*/
svn_error_t *
svn_wc__loggy_remove(svn_stringbuf_t **log_accum,
                     svn_wc_adm_access_t *adm_access,
                     const char *base_name,
                     apr_pool_t *pool);

/* Extend **LOG_ACCUM with instructions to cause the working copy of ADM_ACCESS
   to be upgraded to FORMAT. */
svn_error_t *
svn_wc__loggy_upgrade_format(svn_stringbuf_t **log_accum,
                             svn_wc_adm_access_t *adm_access,
                             int format,
                             apr_pool_t *pool);


/* Create a log file with LOG_NUMBER. Write LOG_CONTENT to it and close-
   and-sync afterwards. ADM_ACCESS must point to a locked working copy.


   Helper to eliminate code duplication. */
svn_error_t *
svn_wc__write_log(svn_wc_adm_access_t *adm_access,
                  int log_number, svn_stringbuf_t *log_content,
                  apr_pool_t *pool);


/* Process the instructions in the log file for ADM_ACCESS. 
   DIFF3_CMD is the external differ used by the 'SVN_WC__LOG_MERGE'
   log entry.  It is always safe to pass null for this.

   If the log fails on its first command, return the error
   SVN_ERR_WC_BAD_ADM_LOG_START.  If it fails on some subsequent
   command, return SVN_ERR_WC_BAD_ADM_LOG. */
svn_error_t *svn_wc__run_log(svn_wc_adm_access_t *adm_access,
                             const char *diff3_cmd,
                             apr_pool_t *pool);

/* Similar to svn_wc__run_log except that it is assumed that the log
   file has been run before and so some of the log commands may
   already have been processed. */
svn_error_t *svn_wc__rerun_log(svn_wc_adm_access_t *adm_access,
                               const char *diff3_cmd,
                               apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_LOG_H */
