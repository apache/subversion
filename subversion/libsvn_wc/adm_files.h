/*
 * adm_files.h :  handles locations inside the wc adm area
 *                (This should be the only code that actually knows
 *                *where* things are in .svn/.  If you can't get to
 *                something via these interfaces, something's wrong.)
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#ifndef SVN_LIBSVN_WC_ADM_FILES_H
#define SVN_LIBSVN_WC_ADM_FILES_H

#include <apr_pools.h>
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Return a path to something in PATH's administrative area.
 * Return path to the thing in the tmp area if TMP is non-zero.
 * Varargs are (const char *)'s, the final one must be NULL.
 */
const char * svn_wc__adm_path(const char *path,
                              svn_boolean_t tmp,
                              apr_pool_t *pool,
                              ...);


/* Return TRUE if a thing in the administrative area exists, FALSE
   otherwise. */
svn_boolean_t svn_wc__adm_path_exists(const char *path,
                                      svn_boolean_t tmp,
                                      apr_pool_t *pool,
                                      ...);


/* Make `PATH/<adminstrative_subdir>/THING'. */
svn_error_t *svn_wc__make_adm_thing(svn_wc_adm_access_t *adm_access,
                                    const char *thing,
                                    svn_node_kind_t type,
                                    apr_fileperms_t perms,
                                    svn_boolean_t tmp,
                                    apr_pool_t *pool);

/* Atomically rename a temporary text-base file to its canonical
   location.  The tmp file should be closed already. */
svn_error_t *
svn_wc__sync_text_base(const char *path, apr_pool_t *pool);


/* Return a path to PATH's text-base file.
   If TMP is set, return a path to the tmp text-base file. */
const char *svn_wc__text_base_path(const char *path,
                                   svn_boolean_t tmp,
                                   apr_pool_t *pool);


/* Return a path to PATH's revert file.
   If TMP is set, return a path to the tmp revert file. */
const char *
svn_wc__text_revert_path(const char *path,
                         svn_boolean_t tmp,
                         apr_pool_t *pool);


/* Return a path to the 'wcprop' file for PATH, possibly in TMP area.
   Valid values for KIND are svn_node_dir and svn_node_file. */
svn_error_t *svn_wc__wcprop_path(const char **wcprop_path,
                                 const char *path,
                                 svn_node_kind_t kind,
                                 svn_boolean_t tmp,
                                 apr_pool_t *pool);


/* Set *PROP_PATH to PATH's working properties file.
   If TMP is set, return a path to the tmp working property file. 
   PATH can be a directory or file, and even have changed w.r.t. the
   working copy's adm knowledge. Valid values for KIND are svn_node_dir
   and svn_node_file. */
svn_error_t *svn_wc__prop_path(const char **prop_path,
                               const char *path,
                               svn_node_kind_t kind,
                               svn_boolean_t tmp,
                               apr_pool_t *pool);


/* Set *PROP_PATH to PATH's `pristine' properties file.
   If TMP is set, return a path to the tmp working property file. 
   PATH can be a directory or file, and even have changed w.r.t. the
   working copy's adm knowledge. Valid values for KIND are svn_node_dir
   and svn_node_file. */
svn_error_t *svn_wc__prop_base_path(const char **prop_path,
                                    const char *path,
                                    svn_node_kind_t kind,
                                    svn_boolean_t tmp,
                                    apr_pool_t *pool);


/* Set *PROP_PATH to PATH's revert properties file.
   If TMP is set, return a path to the tmp working property file. 
   PATH can be a directory or file, and even have changed w.r.t. the
   working copy's adm knowledge. Valid values for KIND are svn_node_dir
   and svn_node_file. */
svn_error_t *svn_wc__prop_revert_path(const char **prop_path,
                                      const char *path,
                                      svn_node_kind_t kind,
                                      svn_boolean_t tmp,
                                      apr_pool_t *pool);


/*** Opening all kinds of adm files ***/

/* Yo, read this if you open and close files in the adm area:
 *
 * When you open a file for writing with svn_wc__open_foo(), the file
 * is actually opened in the corresponding location in the tmp/
 * directory.  Opening with APR_APPEND is not supported.  You are
 * guaranteed to be the owner of the new file.
 *
 * Somehow, this tmp file must eventually get renamed to its real
 * destination in the adm area.  You can do it either by passing the
 * SYNC flag to svn_wc__close_foo(), or by calling
 * svn_wc__sync_foo() (though of course you should still have
 * called svn_wc__close_foo() first, just without the SYNC flag).
 *
 * In other words, the adm area is only capable of modifying files
 * atomically, but you get some control over when the rename happens.
 */

/* Open `PATH/<adminstrative_subdir>/FNAME'. */
svn_error_t *svn_wc__open_adm_file(apr_file_t **handle,
                                   const char *path,
                                   const char *fname,
                                   apr_int32_t flags,
                                   apr_pool_t *pool);


/* Close `PATH/<adminstrative_subdir>/FNAME'. */
svn_error_t *svn_wc__close_adm_file(apr_file_t *fp,
                                    const char *path,
                                    const char *fname,
                                    int sync,
                                    apr_pool_t *pool);

/* Remove `PATH/<adminstrative_subdir>/THING'. */
svn_error_t *svn_wc__remove_adm_file(const char *path,
                                     apr_pool_t *pool,
                                     ...);

/* Open the text-base for FILE.
 * FILE can be any kind of path ending with a filename.
 * Behaves like svn_wc__open_adm_file(), which see.
 */
svn_error_t *svn_wc__open_text_base(apr_file_t **handle,
                                    const char *file,
                                    apr_int32_t flags,
                                    apr_pool_t *pool);

/* Open the revert-base for FILE.
 * FILE can be any kind of path ending with a filename.
 * Behaves like svn_wc__open_adm_file(), which see.
 */
svn_error_t *svn_wc__open_revert_base(apr_file_t **handle,
                                      const char *file,
                                      apr_int32_t flags,
                                      apr_pool_t *pool);

/* Close the text-base for FILE.
 * FP was obtained from svn_wc__open_text_base().
 * Behaves like svn_wc__close_adm_file(), which see.
 */
svn_error_t *svn_wc__close_text_base(apr_file_t *fp,
                                     const char *file,
                                     int sync,
                                     apr_pool_t *pool);

/* Close the revert-base for FILE.
 * FP was obtained from svn_wc__open_revert_base().
 * Behaves like svn_wc__close_adm_file(), which see.
 */
svn_error_t *svn_wc__close_revert_base(apr_file_t *fp,
                                       const char *file,
                                       int sync,
                                       apr_pool_t *pool);

/* Open the property file for PATH.
 * PATH can be any kind of path, either file or dir.
 *
 * If BASE is set, then the "pristine" property file will be opened.
 * If WCPROPS is set, then the "wc" property file will be opened.
 *
 * (Don't set BASE and WCPROPS at the same time; this is meaningless.)
 */
svn_error_t *svn_wc__open_props(apr_file_t **handle,
                                const char *path,
                                apr_int32_t flags,
                                svn_boolean_t base,
                                svn_boolean_t wcprops,
                                apr_pool_t *pool);

/* Close the property file for PATH.
 * FP was obtained from svn_wc__open_props().
 *
 * The BASE and WCPROPS must have the same state used to open the file!
 *
 * Like svn_wc__close_adm_file(), SYNC indicates the file should be
 * atomically written.
 */
svn_error_t *svn_wc__close_props(apr_file_t *fp,
                                 const char *path,
                                 svn_boolean_t base,
                                 svn_boolean_t wcprops,
                                 int sync,
                                 apr_pool_t *pool);

/* Atomically rename a temporary property file to its canonical
   location.  The tmp file should be closed already. 

   Again, BASE and WCPROPS flags should be identical to those used to
   open the file. */
svn_error_t *svn_wc__sync_props(const char *path, 
                                svn_boolean_t base,
                                svn_boolean_t wcprops,
                                apr_pool_t *pool);

/* Blow away the admistrative directory associated with the access baton
   ADM_ACCESS. This closes ADM_ACCESS, but it is safe to close ADM_ACCESS
   again, after calling this function. */
svn_error_t *svn_wc__adm_destroy(svn_wc_adm_access_t *adm_access,
                                 apr_pool_t *pool);


/* Cleanup the temporary storage area of the administrative
   directory. */
svn_error_t *svn_wc__adm_cleanup_tmp_area(svn_wc_adm_access_t *adm_access,
                                          apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_ADM_FILES_H */
