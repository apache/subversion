/*
 * wc.h :  shared stuff internal to the svn_wc library.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */


#include <apr_pools.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_wc.h"




/*** Asking questions about a working copy. ***/

/* Return an error unless PATH is a valid working copy.
   kff todo: make it compare repository too. */
svn_error_t *svn_wc__check_wc (svn_string_t *path, apr_pool_t *pool);


/* Set *MODIFIED_P to non-zero if FILENAME has been locally modified,
   else set to zero. */
svn_error_t *svn_wc__file_modified_p (svn_boolean_t *modified_p,
                                      svn_string_t *filename,
                                      apr_pool_t *pool);


/*** Locking. ***/

/* Lock the working copy administrative area.
   Wait for WAIT seconds if encounter another lock, trying again every
   second, then return 0 if success or an SVN_ERR_WC_LOCKED error if
   failed to obtain the lock. */
svn_error_t *svn_wc__lock (svn_string_t *path, int wait, apr_pool_t *pool);

/* Unlock PATH, or error if can't. */
svn_error_t *svn_wc__unlock (svn_string_t *path, apr_pool_t *pool);



/*** Names and file/dir operations in the administrative area. ***/

/* Create DIR as a working copy directory. */
svn_error_t *svn_wc__set_up_new_dir (svn_string_t *path,
                                     svn_string_t *ancestor_path,
                                     svn_vernum_t ancestor_vernum,
                                     apr_pool_t *pool);


/* kff todo: namespace-protecting these #defines so we never have to
   worry about them conflicting with future all-caps symbols that may
   be defined in svn_wc.h. */

/** The files within the administrative subdir. **/
#define SVN_WC__ADM_FORMAT              "format"
#define SVN_WC__ADM_README              "README"
#define SVN_WC__ADM_REPOSITORY          "repository"
#define SVN_WC__ADM_ANCESTOR            "ancestor"
#define SVN_WC__ADM_VERSIONS            "versions"
#define SVN_WC__ADM_PROPERTIES          "properties"
#define SVN_WC__ADM_DELTA_HERE          "delta-here"
#define SVN_WC__ADM_LOCK                "lock"
#define SVN_WC__ADM_TMP                 "tmp"
#define SVN_WC__ADM_TEXT_BASE           "text-base"
#define SVN_WC__ADM_PROP_BASE           "prop-base"
#define SVN_WC__ADM_DPROP_BASE          "dprop-base"
#define SVN_WC__ADM_LOG                 "log"

/* Return a string containing the admin subdir name. */
svn_string_t *svn_wc__adm_subdir (apr_pool_t *pool);


/* Make `PATH/<adminstrative_subdir>/THING'. */
svn_error_t *svn_wc__make_adm_thing (svn_string_t *path,
                                     const char *thing,
                                     int type,
                                     svn_boolean_t tmp,
                                     apr_pool_t *pool);



/*** Opening all kinds of adm files ***/

/* Yo, read this if you open and close files in the adm area:
 *
 * When you open a file for writing with svn_wc__open_foo(), the file
 * is actually opened in the corresponding location in the tmp/
 * directory (and if you're appending as well, then the tmp file
 * starts out as a copy of the original file). 
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
svn_error_t *svn_wc__open_adm_file (apr_file_t **handle,
                                    svn_string_t *path,
                                    const char *fname,
                                    apr_int32_t flags,
                                    apr_pool_t *pool);


/* Close `PATH/<adminstrative_subdir>/FNAME'. */
svn_error_t *svn_wc__close_adm_file (apr_file_t *fp,
                                     svn_string_t *path,
                                     const char *fname,
                                     int sync,
                                     apr_pool_t *pool);

/* Append an entry ENTRY to administrative file FP.  Arguments
 * (attributes) follow in pairs: name, value, name, value, and so on,
 * terminated by a single null.  Each name is a char *, but values are
 * svn_string_t *.
 *
 * Since adm entries share a common format (XML), this abstracts the
 * content from the format.
 */
svn_error_t *svn_wc__write_adm_entry (apr_file_t *fp,
                                      apr_pool_t *pool,
                                      const char *entry,
                                      ...);

/* Remove `PATH/<adminstrative_subdir>/THING'. */
svn_error_t *svn_wc__remove_adm_file (svn_string_t *path,
                                      apr_pool_t *pool,
                                      ...);

/* Set *EXISTS to true iff PATH exists, false otherwise.
 * If PATH's existence cannot be determined, an error will be
 * returned, and *EXISTS untouched.
 */
svn_error_t *svn_wc__file_exists_p (svn_boolean_t *exists,
                                    svn_string_t *path,
                                    apr_pool_t *pool);


/* Open the text-base for FILE.
 * FILE can be any kind of path ending with a filename.
 * Behaves like svn_wc__open_adm_file(), which see.
 */
svn_error_t *svn_wc__open_text_base (apr_file_t **handle,
                                     svn_string_t *file,
                                     apr_int32_t flags,
                                     apr_pool_t *pool);

/* Close the text-base for FILE.
 * FP was obtained from svn_wc__open_text_base().
 * Behaves like svn_wc__close_adm_file(), which see.
 */
svn_error_t *svn_wc__close_text_base (apr_file_t *fp,
                                      svn_string_t *file,
                                      int sync,
                                      apr_pool_t *pool);


/* Atomically rename a temporary text-base file to its canonical
   location.  The tmp file should be closed already. */
svn_error_t *
svn_wc__sync_text_base (svn_string_t *path, apr_pool_t *pool);


/* Return a path to PATH's text-base file.
   If TMP is set, return a path to the tmp text-base file. */
svn_string_t *svn_wc__text_base_path (svn_string_t *path,
                                      svn_boolean_t tmp,
                                      apr_pool_t *pool);


/* Ensure that PATH is a locked working copy directory.
 *
 * (In practice, this means creating an adm area if none exists, in
 * which case it is locked from birth, or else locking an adm area
 * that's already there.)
 * 
 * REPOSITORY is a repository string for initializing the adm area.
 *
 * VERSION is the version for this directory.  kff todo: ancestor_path?
 */
svn_error_t *svn_wc__ensure_wc (svn_string_t *path,
                                svn_string_t *repository,
                                svn_string_t *ancestor_path,
                                svn_vernum_t ancestor_version,
                                apr_pool_t *pool);


/* Ensure that an administrative area exists for PATH, so that PATH is
 * a working copy subdir.
 *
 * Use REPOSITORY for the wc's repository.
 *
 * Does not ensure existence of PATH itself; if PATH does not exist,
 * an error will result. 
 */
svn_error_t *svn_wc__ensure_adm (svn_string_t *path,
                                 svn_string_t *repository,
                                 svn_string_t *ancestor_path,
                                 svn_vernum_t ancestor_version,
                                 apr_pool_t *pool);


/*** The log file. ***/

/* Note: every entry in the logfile is either idempotent or atomic.
 * This allows us to remove the entire logfile when every entry in it
 * has been completed -- if you crash in the middle of running a
 * logfile, and then later are running over it again as part of the
 * recovery, a given entry is "safe" in the sense that you can either
 * tell it has already been done (in which case, ignore it) or you can
 * do it again without ill effect.
 */

/* Ops and attributes in the log file. */
#define SVN_WC__LOG_MERGE_TEXT          "merge-text"
#define SVN_WC__LOG_REPLACE_TEXT_BASE   "replace-text-base"
#define SVN_WC__LOG_MERGE_PROPS         "merge-props"
#define SVN_WC__LOG_REPLACE_PROP_BASE   "replace-prop-base"
#define SVN_WC__LOG_SET_VERSION         "set-version"
#define SVN_WC__LOG_ATTR_NAME           "name"
#define SVN_WC__LOG_ATTR_VERSION        "version"
#define SVN_WC__LOG_ATTR_SAVED_MODS     "saved-mods"

/* Process the instructions in the log file for PATH. */
svn_error_t *svn_wc__run_log (svn_string_t *path, apr_pool_t *pool);



/*** Handling the `versions' file. ***/

#define SVN_WC__VERSIONS_START   "wc-versions"
#define SVN_WC__VERSIONS_ENTRY   "entry"
#define SVN_WC__VERSIONS_END     "wc-versions"



/* Initialize contents of `versions' for a new adm area. */
svn_error_t *svn_wc__versions_init (svn_string_t *path, apr_pool_t *pool);


/* For a given ENTRYNAME in PATH, set its version to VERSION in the
   `versions' file.  Also set other XML attributes via varargs: name,
   value, name, value, etc. -- where names are char *'s and values are
   svn_string_t *'s.   Terminate list with NULL. 

   If no such ENTRYNAME exists, create it.
 */
svn_error_t *svn_wc__set_versions_entry (svn_string_t *path,
                                         apr_pool_t *pool,
                                         const char *entryname,
                                         svn_vernum_t version,
                                         ...);


/* For a given ENTRYNAME in PATH, read the `version's file and get its
   version into VERSION.  Also get other XML attributes via varargs:
   name, value, name, value, etc. -- where names are char *'s and
   values are svn_string_t *'s.  Terminate list with NULL.  */
svn_error_t *svn_wc__get_versions_entry (svn_string_t *path,
                                         apr_pool_t *pool,
                                         const char *entryname,
                                         svn_vernum_t *version,
                                         ...);


/* Remove ENTRYNAME from PATH's `versions' file. */
svn_error_t *svn_wc__remove_versions_entry (svn_string_t *path,
                                            apr_pool_t *pool,
                                            const char *entryname);




/*** General utilities that may get moved upstairs at some point. */
svn_error_t *svn_wc__ensure_directory (svn_string_t *path, apr_pool_t *pool);



/*** This will go into APR eventually. ***/

apr_status_t apr_copy_file (const char *src,
                            const char *dst,
                            apr_pool_t *pool);



/*** Diffing and merging ***/

/* Nota bene: here, diffing and merging is about discovering local changes
 * to a file and merging them back into an updated version of that
 * file, not about txdeltas.
 */

/* Get local changes to a working copy file.
 *
 * DIFF_FN stores its results in *RESULT, which will later be passed
 * to a matching patch function.  (Note that DIFF_FN will be invoked
 * on two filenames, a source and a target). 
 */
svn_error_t *svn_wc__get_local_changes (svn_wc_diff_fn_t *diff_fn,
                                        void **result,
                                        svn_string_t *path,
                                        apr_pool_t *pool);


/* An implementation of the `svn_wc_diff_fn_t' interface.
 * Store the diff between SRC and TARGET in *RESULT.  (What gets
 * stored isn't necessarily the actual diff data -- it might be the
 * name of a tmp file containing the data, for example.)
 */
svn_error_t *svn_wc__gnudiff_differ (void **result,
                                     svn_string_t *src,
                                     svn_string_t *target,
                                     apr_pool_t *pool);


/* Re-apply local changes to a working copy file that may have been
 * updated.
 *
 * PATCH_FN is a function, such as svn_wc__gnudiff_patcher, that can
 * use CHANGES to patch the file PATH (note that PATCH_FN will be
 * invoked on two filenames, a source and a target).
 */
svn_error_t *svn_wc__merge_local_changes (svn_wc_patch_fn_t *patch_fn,
                                          void *changes,
                                          svn_string_t *path,
                                          apr_pool_t *pool);



/* An implementation of the `svn_wc_patch_fn_t' interface.
 * Patch SRC with DIFF to yield TARGET.
 */
svn_error_t *svn_wc__gnudiff_patcher (void *diff,
                                      svn_string_t *src,
                                      svn_string_t *target,
                                      apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

