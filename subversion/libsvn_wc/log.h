/*
 * log.h :  interfaces for running .svn/log files.
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


#ifndef SVN_LIBSVN_WC_LOG_H
#define SVN_LIBSVN_WC_LOG_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* OVERVIEW OF THE LOGGY API
 *
 * NOTES
 *
 *  * When a doc string says "Extend **LOG_ACCUM", it means: "if *LOG_ACCUM is
 *    NULL then set *LOG_ACCUM to a new stringbuf allocated in POOL, else
 *    append to the existing stringbuf there."
 */



/* Return the filename (with no path components) to use for logfile number
   LOG_NUMBER.  The returned string will be allocated from POOL.

   For log number 0, this will just be SVN_WC__ADM_LOG to maintain
   compatibility with 1.0.x.  Higher numbers have the digits of the
   number appended to SVN_WC__ADM_LOG so that they look like "log.1",
   "log.2", etc. */
const char *svn_wc__logfile_path(int log_number,
                                 apr_pool_t *pool);



/* The svn_wc__loggy_* functions in this section take path arguments
   with the same base as with which the adm_access was opened.

*/

/* Extend **LOG_ACCUM with log instructions to append the contents
   of SRC to DST.
   SRC and DST are relative to ADM_ACCESS.

   This command fails to be idempotent or atomic: there's no way to
   tell if you should re-run this!  This function is deprecated; new
   uses should not be added, and the single current use (constructing
   human-readable non-parsed property conflict files) should be
   rewritten.  See Issue #3015.
*/
SVN_DEPRECATED
svn_error_t *
svn_wc__loggy_append(svn_stringbuf_t **log_accum,
                     svn_wc_adm_access_t *adm_access,
                     const char *src, const char *dst,
                     apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to mark PATH as committed
   with revision REVNUM.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_committed(svn_stringbuf_t **log_accum,
                        svn_wc_adm_access_t *adm_access,
                        const char *path, svn_revnum_t revnum,
                        apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to copy (and translate!) the
   file SRC_PATH to DST_PATH, if it exists. If it doesn't and
   REMOVE_DST_IF_NO_SRC is TRUE the file at DST_PATH will be deleted if any.

   The test for existence is made during this call, not at log running time.

   SRC_PATH and DST_PATH are relative to ADM_ACCESS.
*/
svn_error_t *
svn_wc__loggy_copy(svn_stringbuf_t **log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *src_path, const char *dst_path,
                   apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to generate a translated
   file from SRC to DST with translation settings from VERSIONED.
   DST and SRC and VERSIONED are relative to ADM_ACCESS.
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
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_delete_entry(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to delete lock related
   fields from the entry belonging to PATH.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_delete_lock(svn_stringbuf_t **log_accum,
                          svn_wc_adm_access_t *adm_access,
                          const char *path,
                          apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to delete changelist
   from the entry belonging to PATH.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_delete_changelist(svn_stringbuf_t **log_accum,
                                svn_wc_adm_access_t *adm_access,
                                const char *path,
                                apr_pool_t *pool);

/* Extend **LOG_ACCUM with commands to modify the entry associated with PATH
   in ADM_ACCESS according to the flags specified in MODIFY_FLAGS, based on
   the values supplied in *ENTRY.

   The flags in MODIFY_FLAGS are to be taken from the svn_wc__entry_modify()
   parameter by the same name.
*/
svn_error_t *
svn_wc__loggy_entry_modify(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           svn_wc_entry_t *entry,
                           apr_uint64_t modify_flags,
                           apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to modify wcprop PROPNAME
   for PATH, setting it to PROPVAL (which may be NULL to delete the property).
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_modify_wcprop(svn_stringbuf_t **log_accum,
                            svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *propname,
                            const char *propval,
                            apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to move the file SRC_PATH to
   DST_PATH, if it exists. If it doesn't and REMOVE_DST_IF_NO_SRC is TRUE
   the file at DST_PATH will be deleted if any.

   The test for existence is made now, not at log run time.

   SRC_PATH and DST_PATH are relative to ADM_ACCESS.

   Set *DST_MODIFIED (if DST_MODIFIED isn't NULL) to indicate whether the
   destination path will have been modified after running the log: if either
   the move or the remove will have been carried out.
*/
svn_error_t *
svn_wc__loggy_move(svn_stringbuf_t **log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *src_path, const char *dst_path,
                   apr_pool_t *pool);



/* Extend **LOG_ACCUM with log instructions to set permissions of PATH
   to 'executable' if it has the 'executable' property set.
   The property is tested at log run time, within this log instruction.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_maybe_set_executable(svn_stringbuf_t **log_accum,
                                   svn_wc_adm_access_t *adm_access,
                                   const char *path,
                                   apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to set permissions of PATH
   to 'readonly' if it has the 'needs-lock' property set and there is
   no lock for the file in the working copy.
   The tests are made at log run time, within this log instruction.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_maybe_set_readonly(svn_stringbuf_t **log_accum,
                                 svn_wc_adm_access_t *adm_access,
                                 const char *path,
                                 apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to set the timestamp of PATH
   in the entry field with name TIME_PROP.

   Use one of the SVN_WC__ENTRY_ATTR_* values for TIME_PROP.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_set_entry_timestamp_from_wc(svn_stringbuf_t **log_accum,
                                          svn_wc_adm_access_t *adm_access,
                                          const char *path,
                                          apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to set the file size of PATH
   in the entries' WORKING_SIZE field.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_set_entry_working_size_from_wc(svn_stringbuf_t **log_accum,
                                             svn_wc_adm_access_t *adm_access,
                                             const char *path,
                                             apr_pool_t *pool);


/* Extend **LOG_ACCUM with log instructions to set permissions of PATH
   to 'readonly'.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_set_readonly(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to set the timestamp of PATH to
   the time TIMESTR.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_set_timestamp(svn_stringbuf_t **log_accum,
                            svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *timestr,
                            apr_pool_t *pool);

/* Extend **LOG_ACCUM with log instructions to remove the file
   PATH, if it exists.
   ADM_ACCESS is the access baton for PATH.
*/
svn_error_t *
svn_wc__loggy_remove(svn_stringbuf_t **log_accum,
                     svn_wc_adm_access_t *adm_access,
                     const char *path,
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
*/
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
