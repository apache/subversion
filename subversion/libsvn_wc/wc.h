/*
 * wc.h :  shared stuff internal to the svn_wc library.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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


#include <apr_pools.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_wc.h"



#define SVN_WC__DIFF_EXT      ".diff"
#define SVN_WC__TMP_EXT       ".tmp"
#define SVN_WC__TEXT_REJ_EXT  ".rej"
#define SVN_WC__PROP_REJ_EXT  ".prej"
#define SVN_WC__BASE_EXT      ".svn-base"




/** File comparisons **/

/* Set *SAME to non-zero if file1 and file2 have the same contents,
   else set it to zero. 

   Note: This probably belongs in the svn_io library, however, it
   shares some private helper functions with other wc-specific
   routines.  Moving it to svn_io would not be impossible, merely
   non-trivial.  So far, it hasn't been worth it. */
svn_error_t *svn_wc__files_contents_same_p (svn_boolean_t *same,
                                            svn_stringbuf_t *file1,
                                            svn_stringbuf_t *file2,
                                            apr_pool_t *pool);


/* Set *MODIFIED_P to true if VERSIONED_FILE is modified with respect
 * to BASE_FILE, or false if it is not.  The comparison compensates
 * for VERSIONED_FILE's eol and keyword properties, but leaves
 * BASE_FILE alone (as though BASE_FILE were a text-base file, which
 * it usually is, only sometimes we're calling this on incoming
 * temporary text-bases).
 * 
 * If an error is returned, the effect on *MODIFIED_P is undefined.
 * 
 * Use POOL for temporary allocation.
 */
svn_error_t *svn_wc__versioned_file_modcheck (svn_boolean_t *modified_p,
                                              svn_stringbuf_t *versioned_file,
                                              svn_stringbuf_t *base_file,
                                              apr_pool_t *pool);


/* A special timestamp value which means "use the timestamp from the
   working copy".  This is sometimes used in a log entry like:
   
   <modify-entry name="foo.c" revision="5" timestamp="working"/>

 */
#define SVN_WC_TIMESTAMP_WC   "working"



/*** Names and file/dir operations in the administrative area. ***/

/* Create DIR as a working copy directory. */
/* ### This function hasn't been defined nor completely documented
   yet, so I'm not sure whether the "ancestor" arguments are really
   meant to be urls and should be changed to "url_*".  -kff */ 
svn_error_t *svn_wc__set_up_new_dir (svn_stringbuf_t *path,
                                     svn_stringbuf_t *ancestor_path,
                                     svn_revnum_t ancestor_revnum,
                                     apr_pool_t *pool);


/* kff todo: namespace-protecting these #defines so we never have to
   worry about them conflicting with future all-caps symbols that may
   be defined in svn_wc.h. */

/** The files within the administrative subdir. **/
#define SVN_WC__ADM_FORMAT              "format"
#define SVN_WC__ADM_README              "README"
#define SVN_WC__ADM_ENTRIES             "entries"
#define SVN_WC__ADM_LOCK                "lock"
#define SVN_WC__ADM_TMP                 "tmp"
#define SVN_WC__ADM_TEXT_BASE           "text-base"
#define SVN_WC__ADM_PROPS               "props"
#define SVN_WC__ADM_PROP_BASE           "prop-base"
#define SVN_WC__ADM_DIR_PROPS           "dir-props"
#define SVN_WC__ADM_DIR_PROP_BASE       "dir-prop-base"
#define SVN_WC__ADM_WCPROPS             "wcprops"
#define SVN_WC__ADM_DIR_WCPROPS         "dir-wcprops"
#define SVN_WC__ADM_LOG                 "log"
#define SVN_WC__ADM_KILLME              "KILLME"
#define SVN_WC__ADM_AUTH_DIR            "auth"
#define SVN_WC__ADM_EMPTY_FILE          "empty-file"


/* The basename of the ".prej" file, if a directory ever has property
   conflicts.  This .prej file will appear *within* the conflicted
   directory.  */
#define SVN_WC__THIS_DIR_PREJ           "dir_conflicts"



/*** General utilities that may get moved upstairs at some point. */

/* Ensure that DIR exists. */
svn_error_t *svn_wc__ensure_directory (svn_stringbuf_t *path, apr_pool_t *pool);


/*** Routines that deal with properties ***/


/* If the working item at PATH has properties attached, set HAS_PROPS. */
svn_error_t *
svn_wc__has_props (svn_boolean_t *has_props,
                   svn_stringbuf_t *path,
                   apr_pool_t *pool);


/* Given two property hashes (working copy and `base'), deduce what
   propchanges the user has made since the last update.  Return these
   changes as a series of svn_prop_t structures stored in
   LOCAL_PROPCHANGES, allocated from POOL.  */
svn_error_t *
svn_wc__get_local_propchanges (apr_array_header_t **local_propchanges,
                               apr_hash_t *localprops,
                               apr_hash_t *baseprops,
                               apr_pool_t *pool);



/* Given two propchange objects, return TRUE iff they conflict.  If
   there's a conflict, DESCRIPTION will contain an english description
   of the problem. */

/* For note, here's the table being implemented:

              |  update set     |    update delete   |
  ------------|-----------------|--------------------|
  user set    | conflict iff    |      conflict      |
              |  vals differ    |                    |
  ------------|-----------------|--------------------|
  user delete |   conflict      |      merge         |
              |                 |    (no problem)    |
  ----------------------------------------------------

*/
svn_boolean_t
svn_wc__conflicting_propchanges_p (const svn_string_t **description,
                                   const svn_prop_t *local,
                                   const svn_prop_t *update,
                                   apr_pool_t *pool);

/* Look up the entry NAME within PATH and see if it has a `current'
   reject file describing a state of conflict.  If such a file exists,
   return the name of the file in REJECT_FILE.  If no such file exists,
   return (REJECT_FILE = NULL). */
svn_error_t *
svn_wc__get_existing_prop_reject_file (const svn_string_t **reject_file,
                                       const char *path,
                                       const char *name,
                                       apr_pool_t *pool);

/* If PROPFILE_PATH exists (and is a file), assume it's full of
   properties and load this file into HASH.  Otherwise, leave HASH
   untouched.  */
svn_error_t *
svn_wc__load_prop_file (const char *propfile_path,
                        apr_hash_t *hash,
                        apr_pool_t *pool);



/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH */
svn_error_t *
svn_wc__save_prop_file (const char *propfile_path,
                        apr_hash_t *hash,
                        apr_pool_t *pool);


/* Given PATH/NAME and an array of PROPCHANGES, merge the changes into
   the working copy.  Necessary log entries will be appended to
   ENTRY_ACCUM.

   If we are attempting to merge changes to a directory, simply pass
   the directory as PATH and NULL for NAME.

   If conflicts are found when merging, they are placed into a
   temporary .prej file within SVN. Log entries are then written to
   move this file into PATH, or to append the conflicts to the file's
   already-existing .prej file in PATH.

   Any conflicts are also returned in a hash that maps (const char *)
   propnames -> conflicting (const svn_prop_t *) ptrs from the PROPCHANGES
   array.  In this case, *CONFLICTS will be allocated in POOL.  If no
   conflicts occurred, then *CONFLICTS is simply allocated as an empty
   hash.
*/
svn_error_t *
svn_wc__merge_prop_diffs (const char *path,
                          const char *name,
                          const apr_array_header_t *propchanges,
                          apr_pool_t *pool,
                          svn_stringbuf_t **entry_accum,
                          apr_hash_t **conflicts);


/* Get a single 'wcprop' NAME for versioned object PATH, return in
   *VALUE. */
svn_error_t *svn_wc__wcprop_get (const svn_string_t **value,
                                 const char *name,
                                 const char *path,
                                 apr_pool_t *pool);

/* Set a single 'wcprop' NAME to VALUE for versioned object PATH. */
svn_error_t * svn_wc__wcprop_set (const char *name,
                                  const svn_string_t *value,
                                  const char *path,
                                  apr_pool_t *pool);

/* Remove all wc properties under PATH, recursively.  Do any temporary
   allocation in POOL.  If PATH is not a directory, return the error
   SVN_ERR_WC_NOT_DIRECTORY. */
svn_error_t *svn_wc__remove_wcprops (svn_stringbuf_t *path, apr_pool_t *pool);


/* Strip SVN_PROP_ENTRY_PREFIX off the front of NAME.  Modifies NAME
   in-place.  If NAME is not an 'entry' property, then NAME is
   untouched. */
void svn_wc__strip_entry_prefix (svn_stringbuf_t *name);



/* Newline and keyword translation properties */

/* Valid states for 'svn:eol-style' property.  
   Property nonexistence is equivalent to 'none'. */
enum svn_wc__eol_style
{
  svn_wc__eol_style_unknown, /* An unrecognized style */
  svn_wc__eol_style_none,    /* EOL translation is "off" or ignored value */
  svn_wc__eol_style_native,  /* Translation is set to client's native style */
  svn_wc__eol_style_fixed    /* Translation is set to one of LF, CR, CRLF */
};

/* The text-base eol style for files using svn_wc__eol_style_native
   style.  */
#define SVN_WC__DEFAULT_EOL_MARKER "\n"


/* Query the SVN_PROP_EOL_STYLE property on a file at PATH, and set
   *STYLE to PATH's eol style (one of the three values: none, native,
   or fixed), and set *EOL to

      - NULL if *STYLE is svn_wc__eol_style_none, or

      - a null-terminated C string containing the native eol marker
        for this platform, if *STYLE is svn_wc__eol_style_native, or

      - a null-terminated C string containing the eol marker indicated
        by the property value, if *STYLE is svn_wc__eol_style_fixed.

   If non-null, *EOL is a static string, not allocated in POOL.

   Use POOL for temporary allocation.
*/
svn_error_t *svn_wc__get_eol_style (enum svn_wc__eol_style *style,
                                    const char **eol,
                                    const char *path,
                                    apr_pool_t *pool);


/* Variant of svn_wc__get_eol_style, but without the path argument.
   It assumes that you already have the property VALUE.  This is for
   more "abstract" callers that just want to know how values map to
   EOL styles. */
void svn_wc__eol_style_from_value (enum svn_wc__eol_style *style,
                                   const char **eol,
                                   const char *value);

/* Reverse parser.  Given a real EOL string ("\n", "\r", or "\r\n"),
   return an encoded *VALUE ("LF", "CR", "CRLF") that one might see in
   the property value. */
void svn_wc__eol_value_from_string (const char **value,
                                    const char *eol);

/* Expand keywords for the file at PATH, by parsing a
   whitespace-delimited list of keywords.  If any keywords are found
   in the list, allocate *KEYWORDS from POOL, and then populate its
   entries with the related keyword values (also allocated in POOL).
   If no keywords are found in the list, or if there is no list, set
   *KEYWORDS to NULL.

   If FORCE_LIST is non-null, use it as the list; else use the
   SVN_PROP_KEYWORDS property for PATH.  In either case, use PATH to
   expand keyword values.  If a keyword is in the list, but no
   corresponding value is available, set that element of *KEYWORDS to
   the empty string ("").
*/
svn_error_t *svn_wc__get_keywords (svn_wc_keywords_t **keywords,
                                   const char *path,
                                   const char *force_list,
                                   apr_pool_t *pool);



/* Return a new string, allocated in POOL, containing just the
 * human-friendly portion of DATE.  Subversion date strings typically
 * contain more information than humans want, for example
 *
 *   "Mon 28 Jan 2002 16:17:09.777994 (day 028, dst 0, gmt_off -21600)"
 *   
 * would be converted to
 *
 *   "Mon 28 Jan 2002 16:17:09"
 */
svn_string_t *svn_wc__friendly_date (const char *date, apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

