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
#include "svn_xml.h"
#include "svn_wc.h"



/*** Asking questions about a working copy. ***/

/* Return an error unless PATH is a valid working copy.
   kff todo: make it compare repository too. */
svn_error_t *svn_wc__check_wc (svn_string_t *path, apr_pool_t *pool);


/* Set *APR_TIME to the later of PATH's (a regular file) mtime or ctime.
 *
 * Unix traditionally distinguishes between "mod time", which is when
 * someone last modified the contents of the file, and "change time",
 * when someone changed something else about the file (such as
 * permissions).
 *
 * Since Subversion versions both kinds of information, our timestamp
 * comparisons have to notice either kind of change.  That's why this
 * function gives the time of whichever kind came later.  APR will
 * hopefully make sure that both ctime and mtime always have useful
 * values, even on OS's that do things differently. (?)
 */
svn_error_t *svn_wc__file_affected_time (apr_time_t *apr_time,
                                         svn_string_t *path,
                                         apr_pool_t *pool);


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
#define SVN_WC__ADM_ENTRIES             "entries"
#define SVN_WC__ADM_PROPERTIES          "properties"
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


/* Ensure that PATH is a working copy directory.
 *
 * REPOSITORY is a repository string for initializing the adm area.
 *
 * ANCESTOR_PATH and ANCESTOR_VERSION are ancestry information for the
 * directory.
 *
 * If REQUIRE_NEW is non-zero, then it will be an error for this
 * directory to already be a working copy or non-empty.
 */
svn_error_t *svn_wc__ensure_wc (svn_string_t *path,
                                svn_string_t *repository,
                                svn_string_t *ancestor_path,
                                svn_vernum_t ancestor_version,
                                svn_boolean_t require_new,
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
#define SVN_WC__LOG_SET_ENTRY           "set-entry"
#define SVN_WC__LOG_ATTR_NAME           "name"
#define SVN_WC__LOG_ATTR_VERSION        "version"
#define SVN_WC__LOG_ATTR_SAVED_MODS     "saved-mods"

/* Process the instructions in the log file for PATH. */
svn_error_t *svn_wc__run_log (svn_string_t *path, apr_pool_t *pool);



/*** Handling the `entries' file. ***/

#define SVN_WC__ENTRIES_START   "wc-entries"
#define SVN_WC__ENTRIES_ENTRY   "entry"
#define SVN_WC__ENTRIES_END     "wc-entries"

#define SVN_WC__ENTRIES_ATTR_NAME      "name"
#define SVN_WC__ENTRIES_ATTR_VERSION   "version"
#define SVN_WC__ENTRIES_ATTR_KIND      "kind"
#define SVN_WC__ENTRIES_ATTR_TIMESTAMP "timestamp"
#define SVN_WC__ENTRIES_ATTR_CHECKSUM  "checksum"
#define SVN_WC__ENTRIES_ATTR_ADD       "add"
#define SVN_WC__ENTRIES_ATTR_DELETE    "delete"
#define SVN_WC__ENTRIES_ATTR_ANCESTOR  "ancestor"

/* Initialize contents of `entries' for a new adm area. */
svn_error_t *svn_wc__entries_init (svn_string_t *path,
                                   svn_string_t *ancestor_path,
                                   apr_pool_t *pool);


/* For a given ENTRYNAME in PATH, set its version to VERSION in the
   `entries' file.  Set KIND to svn_file_kind or svn_dir_kind; also
   set other XML attributes via varargs: key, value, key, value,
   etc. -- where names are char *'s and values are svn_string_t *'s.
   Terminate list with NULL.

   ENTRYNAME is a string to match the value of the "name" attribute of
   some entry.  (This attribute is special attribute on entries,
   because no two entries can have the same name.  No other attribute
   of an entry is guaranteed to be unique.)

   If no such ENTRYNAME exists, create it.  If ENTRYNAME is NULL, then
   the entry for this dir will be set.

   The entries file must not be open for writing by anyone else when
   you call this, or badness will result.  */
svn_error_t *svn_wc__entry_set (svn_string_t *path,
                                svn_string_t *entryname,
                                svn_vernum_t version,
                                enum svn_node_kind kind,
                                apr_pool_t *pool,
                                ...);

/* Exactly like svn_wc__entry_set, except that changes are merged into
   an existing entry (by first fetching all entry attributes, *then*
   writing out.) */
svn_error_t *svn_wc__entry_merge (svn_string_t *path,
                                  svn_string_t *entryname,
                                  svn_vernum_t version,
                                  enum svn_node_kind kind,
                                  apr_pool_t *pool,
                                  ...);


/* For a given ENTRYNAME in PATH's entries file:
          get its version into *VERSION,
          get its file/dir kind into *KIND,
          and return all other xml attributes as a hash in *HASH.

   If any of the return-by-reference arguments is NULL, that argument
   will simply not be used.
*/
svn_error_t *svn_wc__entry_get (svn_string_t *path,
                                svn_string_t *entryname, 
                                svn_vernum_t *version,
                                enum svn_node_kind *kind,
                                apr_pool_t *pool,
                                apr_hash_t **hash);


/* Remove ENTRYNAME from PATH's `entries' file. */
svn_error_t *svn_wc__entry_remove (svn_string_t *path,
                                   svn_string_t *entryname,
                                   apr_pool_t *pool);



/* Contains info about an entry, used by our xml parser and by the crawler. */
typedef struct svn_wc__entry_baton_t
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;

  svn_boolean_t found_it;  /* Gets set to true iff we see a matching entry. */

  svn_boolean_t removing;        /* Set iff the task is to remove an entry. */
  svn_boolean_t allow_duplicate; /* Set iff should preserve previous entry. */

  apr_file_t *infile;      /* The entries file we're reading from. */
  apr_file_t *outfile;     /* If this is NULL, then we're GETTING
                              attributes; if this is non-NULL, then
                              we're SETTING attributes by writing a
                              new file.  */

  svn_string_t *entryname; /* The name of the entry we're looking for. */
  svn_vernum_t version;    /* The version we will get or set. */
  enum svn_node_kind kind; /* The kind we will get or set. */

  apr_hash_t *attributes;  /* The attribute list from XML, which will
                              be read from and written to. */

  /* Flag to indicate "looping" over an entries file.  Call this
     callback on each entry found */
  svn_boolean_t looping;
  svn_error_t *(*looper_callback) (void *callback_baton,
                                   struct svn_wc__entry_baton_t *entrybaton);
  void *callback_baton;

} svn_wc__entry_baton_t;


/* Take an entry baton BATON and parse the relevant `entries' file */
svn_error_t *do_parse (svn_wc__entry_baton_t *baton);


/* Set *ANCESTOR_VER and *ANCESTOR_PATH appropriately for the
   ENTRY in directory PATH.  If ENTRY is null, then PATH itself is
   meant. */
svn_error_t *svn_wc__entry_get_ancestry (svn_string_t *path,
                                         svn_string_t *entry,
                                         svn_string_t **ancestor_path,
                                         svn_vernum_t *ancestor_ver,
                                         apr_pool_t *pool);



/*** General utilities that may get moved upstairs at some point. */

/* Ensure that DIR exists. */
svn_error_t *svn_wc__ensure_directory (svn_string_t *path, apr_pool_t *pool);


/* Convert TIME to an svn string representation, which can be
   converted back by svn_wc__string_to_time(). */
svn_string_t *svn_wc__time_to_string (apr_time_t time, apr_pool_t *pool);


/* Convert TIMESTR to an apr_time_t.  TIMESTR should be of the form
   returned by svn_wc__time_to_string(). */
apr_time_t svn_wc__string_to_time (svn_string_t *timestr);



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

