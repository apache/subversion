/*
 * svn_wc.h :  public interface for the Subversion Working Copy Library
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



/* ==================================================================== */

/* 
 * Requires:  
 *            A working copy
 * 
 * Provides: 
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:   
 *            Clients.
 */

#ifndef SVN_WC_H
#define SVN_WC_H

#include <apr_tables.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_error.h"



/*** Detecting modification. ***/

/* Set *MODIFIED_P to non-zero if FILENAME's text is modified
   w.r.t. the base revision, else set MODIFIED_P to zero.
   FILENAME is a path to the file, not just a basename. */
svn_error_t *svn_wc_text_modified_p (svn_boolean_t *modified_p,
                                     svn_string_t *filename,
                                     apr_pool_t *pool);


/* Set *MODIFIED_P to non-zero if PATH's properties are modified
   w.r.t. the base revision, else set MODIFIED_P to zero. */
svn_error_t *svn_wc_props_modified_p (svn_boolean_t *modified_p,
                                      svn_string_t *path,
                                      apr_pool_t *pool);




/*** Entries and status. ***/

/* A working copy entry -- that is, revision control information about
   one versioned entity. */
typedef struct svn_wc_entry_t
{
  /* Note that the entry's name does not get its own field here,
     because it is usually the key for which this is the value.  If
     you really need it, look in the attributes. */

  svn_revnum_t revision;       /* Base revision.  (Required) */
  svn_string_t *ancestor;      /* Base path.  (Required) */
  enum svn_node_kind kind;     /* Is it a file, a dir, or... ? (Required) */

  int state;                   /* Bitmasks.  Entry modified?  conflicted?.. */

  apr_time_t text_time;        /* When the file's text was last
                                  up-to-date.  (Zero means not
                                  available) */

  apr_time_t prop_time;        /* When the file's properties were last
                                  up-to-date.  (Zero means not
                                  available) */

  apr_hash_t *attributes;      /* All XML attributes, both those
                                  duplicated above and any others.
                                  (Required) */
} svn_wc_entry_t;


#define SVN_WC_ENTRY_ATTR_NAME      "name"
#define SVN_WC_ENTRY_ATTR_REVISION  "revision"
#define SVN_WC_ENTRY_ATTR_KIND      "kind"
#define SVN_WC_ENTRY_ATTR_TEXT_TIME "text-time"
#define SVN_WC_ENTRY_ATTR_PROP_TIME "prop-time"
#define SVN_WC_ENTRY_ATTR_CHECKSUM  "checksum"
#define SVN_WC_ENTRY_ATTR_ADD       "add"
#define SVN_WC_ENTRY_ATTR_DELETE    "delete"
#define SVN_WC_ENTRY_ATTR_MERGED    "merged"
#define SVN_WC_ENTRY_ATTR_CONFLICT  "conflict"
#define SVN_WC_ENTRY_ATTR_ANCESTOR  "ancestor"
#define SVN_WC_ENTRY_ATTR_REJFILE   "text-reject-file"
#define SVN_WC_ENTRY_ATTR_PREJFILE  "prop-reject-file"


/* Bitmasks for `svn_wc_entry_t.state'.
   REMINDER: if you add a new mask here, make sure to update
   sync_entry() in entries.c. */
#define SVN_WC_ENTRY_ADDED         1  /* entry marked for addition */
#define SVN_WC_ENTRY_DELETED       2  /* entry marked for deletion */
#define SVN_WC_ENTRY_MERGED        4  /* wfile merged as of timestamp */
#define SVN_WC_ENTRY_CONFLICTED    8  /* wfile conflicted as of timestamp */
#define SVN_WC_ENTRY_CLEAR_NAMED  16  /* action: clear mentioned flags */
#define SVN_WC_ENTRY_CLEAR_ALL    32  /* action: clear all flags */

/* How an entries file's owner dir is named in the entries file. */
#define SVN_WC_ENTRY_THIS_DIR  ""


/* Set *ENTRY according to PATH. */
svn_error_t *svn_wc_entry (svn_wc_entry_t **entry,
                           svn_string_t *path,
                           apr_pool_t *pool);


/* Given a DIR_PATH under version control, decide if one of its
   entries (ENTRY) is in state of conflict; return the answers in
   TEXT_CONFLICTED_P and PROP_CONFLICTED_P.  

   (If the entry mentions that a .rej or .prej exist, but they are
   both removed, assume the conflict has been resolved by the user.)  */
svn_error_t *svn_wc_conflicted_p (svn_boolean_t *text_conflicted_p,
                                  svn_boolean_t *prop_conflicted_p,
                                  svn_string_t *dir_path,
                                  svn_wc_entry_t *entry,
                                  apr_pool_t *pool);



/*** Status. ***/

enum svn_wc_status_kind
{
    svn_wc_status_none = 1,  /* Among other things, indicates not under vc. */
    svn_wc_status_added,
    svn_wc_status_deleted,
    svn_wc_status_modified,
    svn_wc_status_merged,
    svn_wc_status_conflicted
};

/* Structure for holding the "status" of a working copy item. 
   The item's entry data is in ENTRY, augmented and possibly shadowed
   by the other fields.  ENTRY is null if this item is not under
   version control. */
typedef struct svn_wc_status_t
{
  svn_wc_entry_t *entry;     /* Can be NULL if not under vc. */
  svn_revnum_t repos_rev;    /* Likewise, can be SVN_INVALID_REVNUM */
  
  /* Mutually exclusive states. One of these will always be set for
     the "textual" component and one will be set for the "property"
     component.  */
  enum svn_wc_status_kind text_status;
  enum svn_wc_status_kind prop_status;

} svn_wc_status_t;


/* Fill *STATUS for PATH, allocating in POOL, with the exception of
   the repos_rev field, which is normally filled in by the caller. */
svn_error_t *svn_wc_status (svn_wc_status_t **status,
                            svn_string_t *path,
                            apr_pool_t *pool);

/* Under PATH, fill STATUSHASH to map paths to svn_wc_status_t
   structures.  For each struct, all fields will be filled in except
   for repos_rev; this would presumably be filled in by the caller. */
svn_error_t *svn_wc_statuses (apr_hash_t *statushash,
                              svn_string_t *path,
                              apr_pool_t *pool);



/* Where you see an argument like
 * 
 *   apr_array_header_t *paths
 *
 * it means an array of (svn_string_t *) types, each one of which is
 * a file or directory path.  This is so we can do atomic operations
 * on any random set of files and directories.
 */

/* kff todo: these do nothing and return SVN_NO_ERROR right now. */
svn_error_t *svn_wc_rename (svn_string_t *src,
                            svn_string_t *dst,
                            apr_pool_t *pool);

svn_error_t *svn_wc_copy (svn_string_t *src,
                          svn_string_t *dst,
                          apr_pool_t *pool);

svn_error_t *svn_wc_delete_file (svn_string_t *file,
                                 apr_pool_t *pool);

/* Add an entry for FILE.  Does not check that FILE exists on disk;
   caller should take care of that, if it cares. */
svn_error_t *svn_wc_add_file (svn_string_t *file,
                              apr_pool_t *pool);


/*** Commits. ***/

/* Update working copy PATH with NEW_REVISION after a commit has succeeded.
 * TARGETS is a hash of files/dirs that actually got committed --
 * these are the only ones who we can write log items for, and whose
 * revision numbers will get set.  todo: eventually this hash will be
 * of the sort used by svn_wc__compose_paths(), as with all entries
 * recursers.
 */
svn_error_t *
svn_wc_close_commit (svn_string_t *path,
                     svn_revnum_t new_revision,
                     apr_hash_t *targets,
                     apr_pool_t *pool);


/* Do a depth-first crawl of the local changes in a working copy,
   beginning at ROOT_DIRECTORY (absolute path).  Communicate all local
   changes (both textual and tree) to the supplied EDIT_FNS object
   (coupled with the supplied EDIT_BATON).

   (Presumably, the client library will someday grab EDIT_FNS and
   EDIT_BATON from libsvn_ra, and then pass it to this routine.  This
   is how local changes in the working copy are ultimately translated
   into network requests.)  

   A function and baton for completing this commit must be set in
   *CLOSE_COMMIT_FN and *CLOSE_COMMIT_BATON, respectively.  These are
   not so much for the caller's sake as for close_edit() in the
   editor, and they should be set before close_edit() is called.  See
   svn_ra_get_commit_editor() for an example of how they might be
   obtained.

   Any items that were found to be modified, and were therefore
   committed, are stored in targets as full paths, so caller can clean
   up appropriately.
*/
svn_error_t *
svn_wc_crawl_local_mods (apr_hash_t **targets,
                         svn_string_t *root_directory,
                         const svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         apr_pool_t *pool);



/*** Updates. ***/

/*
 * Return an editor for updating a working copy.
 * 
 * DEST is the local path to the working copy.
 *
 * TARGET_REVISION is the repository revision that results from this set
 * of changes.
 *
 * EDITOR, EDIT_BATON, and DIR_BATON are all returned by reference,
 * and the latter two should be used as parameters to editor
 * functions.
 */
svn_error_t *svn_wc_get_update_editor (svn_string_t *dest,
                                       svn_revnum_t target_revision,
                                       const svn_delta_edit_fns_t **editor,
                                       void **edit_baton,
                                       apr_pool_t *pool);


/* Like svn_wc_get_update_editor(), except that:
 *
 * DEST will be created as a working copy, if it does not exist
 * already.  It is not an error for it to exist; if it does, checkout
 * just behaves like update.
 *
 * It is the caller's job to make sure that DEST is not some other
 * working copy, or that if it is, it will not be damaged by the
 * application of this delta.  The wc library tries to detect
 * such a case and do as little damage as possible, but makes no
 * promises.
 *
 * REPOS is the repository string to be recorded in this working
 * copy.
 *
 * kff todo: Actually, REPOS is one of several possible non-delta-ish
 * things that may be needed by a editor when creating new
 * administrative subdirs.  Other things might be username and/or auth
 * info, which aren't necessarily included in the repository string.
 * Thinking more on this question...
 */
svn_error_t *svn_wc_get_checkout_editor (svn_string_t *dest,
                                         svn_string_t *repos,
                                         svn_string_t *ancestor_path,
                                         svn_revnum_t target_revision,
                                         const svn_delta_edit_fns_t **editor,
                                         void **edit_baton,
                                         apr_pool_t *pool);


#if 0
/* kff: Will have to think about the interface here a bit more. */

/* GJS: the function will look something like this:
 *
 * svn_wc_commit(source, commit_editor, commit_edit_baton, dir_baton, pool)
 *
 * The Client Library will fetch the commit_editor (& baton) from RA.
 * Source is something that describes the files/dirs (and recursion) to
 * commit. Internally, WC will edit the local dirs and push changes into
 * the commit editor.
 */

svn_error_t *svn_wc_make_skelta (void *delta_src,
                                 svn_delta_write_fn_t *delta_stream_writer,
                                 apr_array_header_t *paths);


svn_error_t *svn_wc_make_delta (void *delta_src,
                                svn_delta_write_fn_t *delta_stream_writer,
                                apr_array_header_t *paths);
#endif /* 0 */


/* A word about the implementation of working copy property storage:
 *
 * Since properties are key/val pairs, you'd think we store them in
 * some sort of Berkeley DB-ish format, and even store pending changes
 * to them that way too.
 *
 * However, we already have libsvn_subr/hashdump.c working, and it
 * uses a human-readable format.  That will be very handy when we're
 * debugging, and presumably we will not be dealing with any huge
 * properties or property lists initially.  Therefore, we will
 * continue to use hashdump as the internal mechanism for storing and
 * reading from property lists, but note that the interface here is
 * _not_ dependent on that.  We can swap in a DB-based implementation
 * at any time and users of this library will never know the
 * difference.
 */

/* Given a PATH to a node in the working copy, return all of its
   properties in PROPS.  (If the node has no properties, an empty hash
   is returned.) */
svn_error_t *svn_wc_prop_list (apr_hash_t **props,
                               svn_string_t *path,
                               apr_pool_t *pool);


/* Return local VALUE of property NAME for the file or directory PATH.
   If property name doesn't exist, VALUE is returned as NULL.  */
svn_error_t *svn_wc_prop_get (svn_string_t **value,
                              svn_string_t *name,
                              svn_string_t *path,
                              apr_pool_t *pool);

/* Set a local value of property NAME to VALUE for the file or
   directory PATH. */
svn_error_t *svn_wc_prop_set (svn_string_t *name,
                              svn_string_t *value,
                              svn_string_t *path,
                              apr_pool_t *pool);


#endif  /* SVN_WC_H */

/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
