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


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_WC_H
#define SVN_WC_H


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

#include <apr_tables.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_ra.h"    /* for svn_ra_reporter_t type */


/*** Asking questions about a working copy. ***/

/* Set *IS_WC to true iff PATH is a valid working copy directory, else
   set it to false.  PATH must exist, either as a file or directory,
   else an error will be returned. */
svn_error_t *svn_wc_check_wc (const svn_string_t *path,
                              svn_boolean_t *is_wc,
                              apr_pool_t *pool);



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


/* Get the ENTRY structure for PATH, allocating from POOL. */
svn_error_t *svn_wc_entry (svn_wc_entry_t **entry,
                           svn_string_t *path,
                           apr_pool_t *pool);


/* Parse the `entries' file for PATH and return a hash ENTRIES, whose
   keys are entry names and values are (svn_wc_entry_t *). 
   
   Important note: only the entry structures representing files and
   SVN_WC_ENTRY_THIS_DIR contain complete information.  The entry
   structures representing subdirs have only the `kind' field filled
   in.  If you want info on a subdir, you must use this routine to
   open its PATH and read the SVN_WC_ENTRY_THIS_DIR structure. */
svn_error_t *svn_wc_entries_read (apr_hash_t **entries,
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

/* We have two functions for getting working copy status: one function
 * for getting the status of exactly one thing, and another for
 * getting the statuses of (potentially) multiple things.
 * 
 * The WebDAV concept of "depth" may be useful in understanding the
 * motivation behind this.  Suppose we're getting the status of
 * directory D.  The three depth levels would mean
 * 
 *    depth 0:         D itself (just the named directory)
 *    depth 1:         D and its immediate children (D + its entries)
 *    depth Infinity:  D and all its descendants (full recursion)
 * 
 * To offer all three levels, we could have one unified function,
 * taking a `depth' parameter.  Unfortunately, because this function
 * would have to handle multiple return values as well as the single
 * return value case, getting the status of just one entity would
 * become cumbersome: you'd have to roll through a hash to find one
 * lone status.
 * 
 * So we have svn_wc_status() for depth 0, and svn_wc_statuses() for
 * depths 1 and 2, since the latter two involve multiple return
 * values.
 */

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


/* Under PATH, fill STATUSHASH mapping paths to svn_wc_status_t
 * structures.  All fields in each struct will be filled in except for
 * repos_rev, which would presumably be filled in by the caller.
 *
 * PATH is usually be a directory, since for a regular file, you would
 * have used svn_wc_status().  However, it is no error if PATH is not
 * a directory; its status will simply be stored in STATUSHASH like
 * any other.
 *
 * Assuming PATH is a directory, then:
 * 
 * If DESCEND is zero, statushash will contain paths for PATH and
 * its non-directory entries (subdirectories should be subjects of
 * separate status calls).  
 *
 * If DESCEND is non-zero, statushash will contain statuses for PATH
 * and everything below it, including subdirectories.  In other
 * words, a full recursion.
 */
svn_error_t *svn_wc_statuses (apr_hash_t *statushash,
                              svn_string_t *path,
                              svn_boolean_t descend,
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

/* Add an entry for DIR, and create an administrative directory for
   it.  Does not check that DIR exists on disk; caller should take
   care of that, if it cares. */
svn_error_t *svn_wc_add_directory (svn_string_t *dir,
                                   apr_pool_t *pool);

/* Add an entry for FILE.  Does not check that FILE exists on disk;
   caller should take care of that, if it cares. */
svn_error_t *svn_wc_add_file (svn_string_t *file,
                              apr_pool_t *pool);


/*** Commits. ***/

/* Publically declared, so libsvn_client can pass it off to the RA
   layer with svn_wc_bump_target(). */
struct svn_wc_close_commit_baton
{
  /* The "prefix" path that must be prepended to each target that
     comes in here.  It's the original path that the user specified to
     the `svn commit' command. */
  svn_string_t *prefix_path;

  /* Pool to use for all logging, running of logs, etc. */
  apr_pool_t *pool;
};

/* This is the "new" callback that the RA layer uses to bump each
   committed target, one-at-a-time.  It's a function of type
   svn_ra_close_commit_func_t.  

   Eventually, the "track" editor's close_edit() routine needs to call
   this too, for those times when it's specifically comingled with the
   XML-output editor.  (svn_wc_close_commit and its helpers will then
   be deprecated!) */

svn_error_t *svn_wc_set_revision (void *baton,
                                  svn_string_t *target,
                                  svn_revnum_t new_revnum);

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


/* Crawl a tree depth-first, to import new data or commit changes.

   Start the crawl at ROOT_DIRECTORY, communicate all local changes (both
   textual and tree) to EDIT_FNS and EDIT_BATON.

   Any items (files or dirs) that were found to be modified, and were
   therefore committed, are stored in TARGETS as full paths. */
svn_error_t *
svn_wc_crawl_local_mods (apr_hash_t **targets,
                         svn_string_t *root_directory,
                         const svn_delta_edit_fns_t *edit_fns,
                         void *edit_baton,
                         apr_pool_t *pool);


/* Do a depth-first crawl in a working copy, beginning at
   ROOT_DIRECTORY.  Communicate the `state' of the working copy's
   revisions to REPORTER/REPORT_BATON.  

   No locks are or logs are created, nor are any animals harmed in the
   process.  No cleanup is necessary.

   After all revisions are reported, REPORTER->finish_report() is
   called, which immediately causes the RA layer to update the working
   copy.  Thus the return value may very well reflect the result of
   the update!  */
svn_error_t *
svn_wc_crawl_revisions (svn_string_t *root_directory,
                        const svn_ra_reporter_t *reporter,
                        void *report_baton,
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



/*** Diffs ***/

/* Given a PATH to a wc file, return a PRISTINE_PATH which points to a
   pristine version of the file.  This is needed so clients can do
   diffs.  If the WC has no text-base, return a NULL instead of a
   path. */
svn_error_t *svn_wc_get_pristine_copy_path (svn_string_t *path,
                                            svn_string_t **pristine_path,
                                            apr_pool_t *pool);


/* Invoke PROGRAM with ARGS, using PATH as working directory.
 * Connect PROGRAM's stdin, stdout, and stderr to INFILE, OUTFILE, and
 * ERRFILE, except where they are null.
 *
 * ARGS is a list of (const char *)'s, terminated by NULL.
 * ARGS[0] is the name of the program, though it need not be the same
 * as CMD.
 */
svn_error_t *
svn_wc_run_cmd_in_directory (svn_string_t *path,
                             const char *cmd,
                             const char *const *args,
                             apr_file_t *infile,
                             apr_file_t *outfile,
                             apr_file_t *errfile,
                             apr_pool_t *pool);

#endif  /* SVN_WC_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
