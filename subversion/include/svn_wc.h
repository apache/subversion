/*
 * svn_wc.h :  public interface for the Subversion Working Copy Library
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

#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_ra.h"    /* for svn_ra_reporter_t type */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Locking/Opening/Closing ***/

/* Baton for access to working copy administrative area. One day all such
   access will require a baton, we're not there yet.

   Access batons can be grouped into sets, by passing an existing open
   baton when opening a new baton.  Given one baton in a set, other batons
   may be retrieved.  This allows an entire hierarchy to be locked, and
   then the set of batons can be passed around by passing a single baton.
 */
typedef struct svn_wc_adm_access_t svn_wc_adm_access_t;

/* Return, in *ADM_ACCESS, a pointer to a new access baton for the working
   copy administrative area associated with the directory PATH.  If
   WRITE_LOCK is true the baton will include a write lock, otherwise the
   baton can only be used for read access.  If PATH refers to a directory
   that is already locked then the error SVN_ERR_WC_LOCKED will be
   returned.

   If ASSOCIATED is an open access baton then ADM_ACCESS will be added to
   the set containing ASSOCIATED.  ASSOCIATED can be NULL, in which case
   ADM_ACCESS is the start of a new set.

   If TREE_LOCK is TRUE then the working copy directory hierarchy under
   PATH will be locked.  All the access batons will become part of the set
   containing ADM_ACCESS.  This is an all-or-nothing option, if it is not
   possible to lock the entire tree then an error will be returned and
   ADM_ACCESS will be invalid, with the exception that sudirectories of
   PATH that are missing from the physical filesystem will not be locked
   and will not cause an error.

   POOL will be used to allocate memory for the baton and any subsequently
   cached items.  If ADM_ACCESS has not been closed when the pool is
   cleared, it will be closed automatically at that point, and removed from
   its set.  A baton closed in this way will not remove physical locks from
   the working copy if cleanup is required.

   The first baton in a set, with ASSOCIATED passed as NULL, must have the
   longest lifetime of all the batons in the set.  This implies it must be
   the root of the hierarchy. */
svn_error_t *svn_wc_adm_open (svn_wc_adm_access_t **adm_access,
                              svn_wc_adm_access_t *associated,
                              const char *path,
                              svn_boolean_t write_lock,
                              svn_boolean_t tree_lock,
                              apr_pool_t *pool);

/* Checks the working copy to determine the node type of PATH.  If PATH is
   a versioned directory then the behaviour is like that of
   svn_wc_adm_open, otherwise, if PATH is a file, an unversioned directory,
   or does not exist, then the behaviour is like that of svn_wc_adm_open
   with PATH replaced by the parent directory of PATH. */
svn_error_t *svn_wc_adm_probe_open (svn_wc_adm_access_t **adm_access,
                                    svn_wc_adm_access_t *associated,
                                    const char *path,
                                    svn_boolean_t write_lock,
                                    svn_boolean_t tree_lock,
                                    apr_pool_t *pool);

/* Return, in *ADM_ACCESS, a pointer to an existing access baton associated
   with PATH.  PATH must be a directory that is locked as part of the set
   containing the ASSOCIATED access baton.

   POOL is used only for local processing it is not used for the batons. */
svn_error_t *svn_wc_adm_retrieve (svn_wc_adm_access_t **adm_access,
                                  svn_wc_adm_access_t *associated,
                                  const char *path,
                                  apr_pool_t *pool);

/* Checks the working copy to determine the node type of PATH.  If PATH is
   a versioned directory then the behaviour is like that of
   svn_wc_adm_retrieve, otherwise, if PATH is a file, an unversioned
   directory, or does not exist, then the behaviour is like that of
   svn_wc_adm_retrieve with PATH replaced by the parent directory of
   PATH. */
svn_error_t *svn_wc_adm_probe_retrieve (svn_wc_adm_access_t **adm_access,
                                        svn_wc_adm_access_t *associated,
                                        const char *path,
                                        apr_pool_t *pool);

/* Give up the access baton ADM_ACCESS, and its lock if any. This will
   recursively close any batons in the same set that are direct
   subdirectories of ADM_ACCESS.  Any physical locks will be removed from
   the working copy.  Lock removal is unconditional, there is no check to
   determine if cleanup is required. */
svn_error_t *svn_wc_adm_close (svn_wc_adm_access_t *adm_access);

/* Return the path used to open the access baton ADM_ACCESS */
const char *svn_wc_adm_access_path (svn_wc_adm_access_t *adm_access);

/* Return the pool used by access baton ADM_ACCESS */
apr_pool_t *svn_wc_adm_access_pool (svn_wc_adm_access_t *adm_access);

/* Ensure ADM_ACCESS has a write lock, and that it is still valid. Returns
   SVN_ERR_WC_NOT_LOCKED if this is not the case. */
svn_error_t *svn_wc_adm_write_check (svn_wc_adm_access_t *adm_access);


/* Set *LOCKED to non-zero if PATH is locked, else set it to zero. */
svn_error_t *svn_wc_locked (svn_boolean_t *locked, 
                            const char *path,
                            apr_pool_t *pool);



/*** Notification/callback handling. ***/

/* In many cases, the WC library will scan a working copy and making
   changes. The caller usually wants to know when each of these changes
   have been made, so that it can display some kind of notification to
   the user.

   These notifications have a standard callback function type, which
   takes the path of the file that was affected, and a caller-
   supplied baton.

   Note that the callback is a 'void' return -- this is a simple
   reporting mechanism, rather than an opportunity for the caller to
   alter the operation of the WC library.
*/
typedef enum svn_wc_notify_action_t
{
  svn_wc_notify_add = 0,
  svn_wc_notify_copy,
  svn_wc_notify_delete,
  svn_wc_notify_restore,
  svn_wc_notify_revert,
  svn_wc_notify_resolve,
  svn_wc_notify_status,

  /* The update actions are also used for checkouts, switches, and merges. */
  svn_wc_notify_update_delete,     /* Got a delete in an update. */
  svn_wc_notify_update_add,        /* Got an add in an update. */
  svn_wc_notify_update_update,     /* Got any other action in an update. */
  svn_wc_notify_update_completed,  /* The last notification in an update */
  svn_wc_notify_update_external,   /* About to update an external module;
                                      use for checkouts and switches too,
                                      end with svn_wc_update_completed. */

  svn_wc_notify_commit_modified,
  svn_wc_notify_commit_added,
  svn_wc_notify_commit_deleted,
  svn_wc_notify_commit_replaced,
  svn_wc_notify_commit_postfix_txdelta
} svn_wc_notify_action_t;


typedef enum svn_wc_notify_state_t
{
  svn_wc_notify_state_inapplicable = 0,
  svn_wc_notify_state_unknown,     /* Notifier doesn't know or isn't saying. */
  svn_wc_notify_state_unchanged,   /* The state did not change. */
  svn_wc_notify_state_changed,     /* Pristine state was modified. */
  svn_wc_notify_state_merged,      /* Modified state had mods merged in. */
  svn_wc_notify_state_conflicted   /* Modified state got conflicting mods. */
} svn_wc_notify_state_t;


/* Notify the world that ACTION has happened to PATH.  PATH is either
 * absolute or relative to cwd (i.e., not relative to an anchor).
 *
 * KIND, CONTENT_STATE and PROP_STATE are from after ACTION, not before.
 *
 * If MIME_TYPE is non-null, it indicates the mime-type of PATH.  It
 * is always NULL for directories.
 *
 * REVISION is SVN_INVALID_REVNUM, except when ACTION is
 * svn_wc_notify_update_completed, in which case REVISION is the
 * target revision of the update if available, else it is still
 * SVN_INVALID_REVNUM.
 *
 * Note that if ACTION is svn_wc_notify_update, then PATH has already
 * been installed, so it is legitimate for an implementation of
 * svn_wc_notify_func_t to examine PATH in the working copy.
 *
 * ### Design Notes:
 *
 * The purpose of the KIND, MIME_TYPE, CONTENT_STATE, and PROP_STATE
 * fields is to provide "for free" information that this function is
 * likely to want, and which it would otherwise be forced to deduce
 * via expensive operations such as reading entries and properties.
 * However, if the caller does not have this information, it will
 * simply pass the corresponding `*_unknown' values, and it is up to
 * the implementation how to handle that (i.e., whether or not to
 * attempt deduction, or just to punt and give a less informative
 * notification).
 *
 * Recommendation: callers of svn_wc_notify_func_t should avoid
 * invoking it multiple times on the same PATH within a given
 * operation, and implementations should not bother checking for such
 * duplicate calls.  For example, in an update, the caller should not
 * invoke the notify func on receiving a prop change and then again
 * on receiving a text change.  Instead, wait until all changes have
 * been received, and then invoke the notify func once (from within
 * an svn_delta_editor_t's close_file(), for example), passing the
 * appropriate content_state and prop_state flags.
 */
typedef void (*svn_wc_notify_func_t) (void *baton,
                                      const char *path,
                                      svn_wc_notify_action_t action,
                                      svn_node_kind_t kind,
                                      const char *mime_type,
                                      svn_wc_notify_state_t content_state,
                                      svn_wc_notify_state_t prop_state,
                                      svn_revnum_t revision);



/* A callback vtable invoked by our diff-editors, as they receive
   diffs from the server.  'svn diff' and 'svn merge' both implement
   their own versions of this table. */
typedef struct svn_wc_diff_callbacks_t
{
  /* A file PATH has changed.  The changes can be seen by comparing
     TMPFILE1 and TMPFILE2, which represent REV1 and REV2 of the file,
     respectively.

     ADM_ACCESS will be an access baton for the directory containing PATH,
     or NULL if the diff editor is not using access batons.

     If STATE is non-null, set *STATE to the state of the file
     contents after the operation has been performed.  (In practice,
     this is only useful with merge, not diff; diff callbacks will
     probably set *STATE to svn_wc_notify_state_unknown, since they do
     not change the state and therefore do not bother to know the
     state after the operation.) */
  svn_error_t *(*file_changed) (svn_wc_adm_access_t *adm_access,
                                svn_wc_notify_state_t *state,
                                const char *path,
                                const char *tmpfile1,
                                const char *tmpfile2,
                                svn_revnum_t rev1,
                                svn_revnum_t rev2,
                                void *diff_baton);

  /* A file PATH was added.  The contents can be seen by comparing
     TMPFILE1 and TMPFILE2.

     ADM_ACCESS will be an access baton for the directory containing PATH,
     or NULL if the diff editor is not using access batons. */
  svn_error_t *(*file_added) (svn_wc_adm_access_t *adm_access,
                              const char *path,
                              const char *tmpfile1,
                              const char *tmpfile2,
                              void *diff_baton);
  
  /* A file PATH was deleted.  The [loss of] contents can be seen by
     comparing TMPFILE1 and TMPFILE2.

     ADM_ACCESS will be an access baton for the directory containing PATH,
     or NULL if the diff editor is not using access batons. */
  svn_error_t *(*file_deleted) (svn_wc_adm_access_t *adm_access,
                                const char *path,
                                const char *tmpfile1,
                                const char *tmpfile2,
                                void *diff_baton);
  
  /* A directory PATH was added.

     ADM_ACCESS will be an access baton for the directory containing PATH,
     or NULL if the diff editor is not using access batons. */
  svn_error_t *(*dir_added) (svn_wc_adm_access_t *adm_access,
                             const char *path,
                             void *diff_baton);
  
  /* A directory PATH was deleted.

     ADM_ACCESS will be an access baton for the directory containing PATH,
     or NULL if the diff editor is not using access batons. */
  svn_error_t *(*dir_deleted) (svn_wc_adm_access_t *adm_access,
                               const char *path,
                               void *diff_baton);
  
  /* A list of property changes (PROPCHANGES) was applied to PATH.
     The array is a list of (svn_prop_t) structures. 

     The original list of properties is provided in ORIGINAL_PROPS,
     which is a hash of svn_string_t values, keyed on the property
     name.

     ADM_ACCESS will be an access baton for the directory containing PATH,
     or NULL if the diff editor is not using access batons.

     If STATE is non-null, set *STATE to the state of the properties
     after the operation has been performed.  (In practice,
     this is only useful with merge, not diff; diff callbacks will
     probably set *STATE to svn_wc_notify_state_unknown, since they do
     not change the state and therefore do not bother to know the
     state after the operation.)  */
  svn_error_t *(*props_changed) (svn_wc_adm_access_t *adm_access,
                                 svn_wc_notify_state_t *state,
                                 const char *path,
                                 const apr_array_header_t *propchanges,
                                 apr_hash_t *original_props,
                                 void *diff_baton);

} svn_wc_diff_callbacks_t;



/*** Asking questions about a working copy. ***/

/* Set *IS_WC to PATH's working copy format version number if PATH
   is a valid working copy directory, else set it to 0.  Return error
   APR_ENOENT if PATH does not exist at all. */
svn_error_t *svn_wc_check_wc (const char *path,
                              int *wc_format,
                              apr_pool_t *pool);


/* Set *HAS_BINARY_PROP to TRUE iff PATH has been marked with a
   property indicating that it is non-text (i.e. binary.) */
svn_error_t *svn_wc_has_binary_prop (svn_boolean_t *has_binary_prop,
                                     const char *path,
                                     apr_pool_t *pool);


/*** Detecting modification. ***/

/* Set *MODIFIED_P to non-zero if FILENAME's text is modified
   w.r.t. the base revision, else set *MODIFIED_P to zero.
   FILENAME is a path to the file, not just a basename. ADM_ACCESS
   must be an access baton for FILENAME.

   If FILENAME does not exist, consider it unmodified.  If it exists
   but is not under revision control (not even scheduled for
   addition), return the error SVN_ERR_ENTRY_NOT_FOUND.
*/
svn_error_t *svn_wc_text_modified_p (svn_boolean_t *modified_p,
                                     const char *filename,
                                     svn_wc_adm_access_t *adm_access,
                                     apr_pool_t *pool);


/* Set *MODIFIED_P to non-zero if PATH's properties are modified
   w.r.t. the base revision, else set MODIFIED_P to zero. ADM_ACCESS
   must be an access baton for PATH. */
svn_error_t *svn_wc_props_modified_p (svn_boolean_t *modified_p,
                                      const char *path,
                                      svn_wc_adm_access_t *adm_access,
                                      apr_pool_t *pool);




/*** Administrative subdir. ***/

/* Ideally, this would be completely private to wc internals (in fact,
   it used to be that adm_files.c:adm_subdir() was the only function
   who knew the adm subdir's name).  However, import wants to protect
   against importing administrative subdirs, so now the name is a
   matter of public record. */

#define SVN_WC_ADM_DIR_NAME   ".svn"



/*** Entries and status. ***/

typedef enum svn_wc_schedule_t
{
  svn_wc_schedule_normal,       /* Nothing special here */
  svn_wc_schedule_add,          /* Slated for addition */
  svn_wc_schedule_delete,       /* Slated for deletion */
  svn_wc_schedule_replace       /* Slated for replacement (delete + add) */

} svn_wc_schedule_t;


/* A working copy entry -- that is, revision control information about
   one versioned entity. */
typedef struct svn_wc_entry_t
{
  /* General Attributes */
  const char *name;              /* entry's name */
  svn_revnum_t revision;         /* base revision */
  const char *url;               /* url in repository */
  const char *repos;             /* canonical repository url */
  svn_node_kind_t kind;          /* node kind (file, dir, ...) */

  /* State information */
  svn_wc_schedule_t schedule;    /* scheduling (add, delete, replace ...) */
  svn_boolean_t copied;          /* in a copied state */
  svn_boolean_t deleted;         /* deleted, but parent rev lags behind */
  const char *copyfrom_url;      /* copyfrom location */
  svn_revnum_t copyfrom_rev;     /* copyfrom revision */
  const char *conflict_old;      /* old version of conflicted file */
  const char *conflict_new;      /* new version of conflicted file */
  const char *conflict_wrk;      /* wroking version of conflicted file */
  const char *prejfile;          /* property reject file */

  /* Timestamps (0 means no information available) */
  apr_time_t text_time;          /* last up-to-date time for text contents */
  apr_time_t prop_time;          /* last up-to-date time for properties */

  /* Checksum.  Optional; can be NULL for backwards compatibility. */
  const char *checksum;          /* base64-encoded checksum for the
                                    untranslated text base file. */

  /* "Entry props" */
  svn_revnum_t cmt_rev;          /* last revision this was changed */
  apr_time_t cmt_date;           /* last date this was changed */
  const char *cmt_author;        /* last commit author of this item */
  
} svn_wc_entry_t;


/* How an entries file's owner dir is named in the entries file. */
#define SVN_WC_ENTRY_THIS_DIR  "svn:this_dir"


/* Set *ENTRY to an entry for PATH, allocated in the access baton pool.
 * If SHOW_DELETED is true, return the entry even if it's in 'deleted'
 * state.  If PATH is not under revision control, or if entry is
 * 'deleted', not scheduled for re-addition, and SHOW_DELETED is
 * false, then set *ENTRY to NULL.
 *
 * *ENTRY should not be modified, since doing so modifies the entries cache
 * in ADM_ACCESS without changing the entries file on disk.
 *
 * If PATH is not a directory then ADM_ACCESS must be an access baton for
 * the parent directory of PATH.  To avoid needing to know whether PATH is
 * a directory or not, if PATH is a directory ADM_ACCESS can still be be an
 * access baton for the parent of PATH so long as the access baton for PATH
 * itself is in the same access baton set.
 *
 * Note that it is possible for PATH to be absent from disk but still
 * under revision control; and conversely, it is possible for PATH to
 * be present, but not under revision control.
 */
svn_error_t *svn_wc_entry (const svn_wc_entry_t **entry,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           svn_boolean_t show_deleted,
                           apr_pool_t *pool);


/* Parse the `entries' file for ADM_ACCESS and return a hash ENTRIES, whose
   keys are (const char *) entry names and values are (svn_wc_entry_t *).
   Allocate ENTRIES, and its keys and values, in POOL.
   
   Entries that are in a 'deleted' state (and not scheduled for
   re-addition) are not returned in the hash, unless SHOW_DELETED is true.

   Important note: the ENTRIES hash is the entries cache in ADM_ACCESS and
   so usually the hash itself, the keys and the values should be treated as
   read-only.  If any of these are modified then it is the callers
   resposibility to ensure that the entries file on disk is updated.  Treat
   the hash values as type (const svn_wc_entry_t *) if you wish to avoid
   accidental modification.

   Important note: only the entry structures representing files and
   SVN_WC_ENTRY_THIS_DIR contain complete information.  The entry
   structures representing subdirs have only the `kind' and `state'
   fields filled in.  If you want info on a subdir, you must use this
   routine to open its PATH and read the SVN_WC_ENTRY_THIS_DIR
   structure, or call svn_wc_get_entry on its PATH. */
svn_error_t *svn_wc_entries_read (apr_hash_t **entries,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_boolean_t show_deleted,
                                  apr_pool_t *pool);


/* Return a duplicate of ENTRY, allocated in POOL.  No part of the new
   entry will be shared with ENTRY. */
svn_wc_entry_t *svn_wc_entry_dup (const svn_wc_entry_t *entry,
                                  apr_pool_t *pool);


/* Given a DIR_PATH under version control, decide if one of its
   entries (ENTRY) is in state of conflict; return the answers in
   TEXT_CONFLICTED_P and PROP_CONFLICTED_P.  

   (If the entry mentions that a .rej or .prej exist, but they are
   both removed, assume the conflict has been resolved by the user.)  */
svn_error_t *svn_wc_conflicted_p (svn_boolean_t *text_conflicted_p,
                                  svn_boolean_t *prop_conflicted_p,
                                  const char *dir_path,
                                  const svn_wc_entry_t *entry,
                                  apr_pool_t *pool);

/* Set *URL and *REV to the ancestor url and revision for PATH,
   allocating in POOL.  ADM_ACCESS must ba an access baton for PATH. */
svn_error_t *svn_wc_get_ancestry (char **url,
                                  svn_revnum_t *rev,
                                  const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool);


/* A callback vtable invoked by the generic entry-walker function. */
typedef struct svn_wc_entry_callbacks_t
{
  /* ### TODO: these callbacks should take pool args, so
     svn_wc_walk_entries() itself can do pool management.  Think
     editors.  (And then go adjust callers, such as
     invalidate_wcprop_for_entry and friends in ra.c.) */

  /* An ENTRY was found at PATH.  */
  svn_error_t *(*found_entry) (const char *path,
                               const svn_wc_entry_t *entry,
                               void *walk_baton);

  /* ### add more callbacks as new callers need them. */

} svn_wc_entry_callbacks_t;


/* A generic entry-walker.

   Do a recursive depth-first entry-walk beginning on PATH, which can
   be a file or dir.  Call callbacks in WALK_CALLBACKS, passing
   WALK_BATON to each.  Use POOL for looping, recursion, and to
   allocate all entries returned.  ADM_ACCESS must be an access baton
   for PATH.

   Like our other entries interfaces, entries that are in a 'deleted'
   state (and not scheduled for re-addition) are not discovered,
   unless SHOW_DELETED is true.

   When a new directory is entered, SVN_WC_ENTRY_THIS_DIR will always
   be returned first.

   [Note:  callers should be aware that each directory will be
   returned *twice*:  first as an entry within its parent, and
   subsequently as the '.' entry within itself.  The two calls can be
   distinguished by looking for SVN_WC_ENTRY_THIS_DIR in the 'name'
   field of the entry.]   */
svn_error_t *svn_wc_walk_entries (const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  const svn_wc_entry_callbacks_t 
                                                     *walk_callbacks,
                                  void *walk_baton,
                                  svn_boolean_t show_deleted,
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
 *
 * NOTE:  Status structures returned by svn_wc_status() or found in
 * the hash created by svn_wc_statuses() may contain a NULL ->entry
 * field.  This indicates an item that is not versioned in the working
 * copy.
 */

enum svn_wc_status_kind
{
    svn_wc_status_none = 1,    /* does not exist */
    svn_wc_status_unversioned, /* is not a versioned thing in this wc */
    svn_wc_status_normal,      /* exists, but uninteresting. */
    svn_wc_status_added,       /* is scheduled for additon */
    svn_wc_status_absent,      /* under v.c., but is missing */
    svn_wc_status_deleted,     /* scheduled for deletion */
    svn_wc_status_replaced,    /* was deleted and then re-added */
    svn_wc_status_modified,    /* text or props have been modified */
    svn_wc_status_merged,      /* local mods received repos mods */
    svn_wc_status_conflicted,  /* local mods received conflicting repos mods */
    svn_wc_status_obstructed   /* an unversioned resource is in the way of
                                  the versioned resource */
};

/* Structure for holding the "status" of a working copy item. 
   The item's entry data is in ENTRY, augmented and possibly shadowed
   by the other fields.  ENTRY is null if this item is not under
   version control. */
typedef struct svn_wc_status_t
{
  svn_wc_entry_t *entry;     /* Can be NULL if not under vc. */
  
  /* Mutually exclusive states. One of these will always be set for
     the "textual" component and one will be set for the "property"
     component.  */
  enum svn_wc_status_kind text_status;
  enum svn_wc_status_kind prop_status;

  /* Booleans: a directory can be 'locked' if a working copy update
     was interrupted, a file or directory can be 'copied' if it's
     scheduled for addition-with-history (or part of a subtree that
     is scheduled as such.), and a file or directory can be
     'switched' if the switch command has been used. */
  svn_boolean_t locked;
  svn_boolean_t copied;
  svn_boolean_t switched;

  /* Fields that describe the status of the entry in the repository;
     in other words, these fields indicate whether text or props would
     be patched or deleted if we were to run 'svn up'. */
  enum svn_wc_status_kind repos_text_status;
  enum svn_wc_status_kind repos_prop_status;

} svn_wc_status_t;


/* Fill *STATUS for PATH, allocating in POOL, with the exception of
   the repos_rev field, which is normally filled in by the caller.
   ADM_ACCESS must be an access baton for PATH.

   Here are some things to note about the returned structure.  A quick
   examination of the STATUS->text_status after a successful return of
   this function can reveal the following things:

      svn_wc_status_none : PATH is not versioned, and is either not
                           present on disk, or is ignored by svn's
                           default ignore regular expressions or the
                           svn:ignore property setting for PATH's
                           parent directory.

      svn_wc_status_absent : PATH is versioned, but is missing from
                             the working copy.

      svn_wc_status_unversioned : PATH is not versioned, but is
                                  present on disk and not being
                                  ignored (see above).  

   The other available results for the text_status field more
   straightforward in their meanings.  See the comments on the
   svn_wc_status_kind structure above for some hints.  */
svn_error_t *svn_wc_status (svn_wc_status_t **status, 
                            const char *path, 
                            svn_wc_adm_access_t *adm_access,
                            apr_pool_t *pool);


/* Under PATH, fill STATUSHASH mapping paths to svn_wc_status_t
 * structures.  All fields in each struct will be filled in except for
 * repos_rev, which would presumably be filled in by the caller.
 * ADM_ACCESS must be an access baton for PATH.
 *
 * PATH will usually be a directory, since for a regular file, you would
 * have used svn_wc_status().  However, it is no error if PATH is not
 * a directory; its status will simply be stored in STATUSHASH like
 * any other.
 *
 * Assuming PATH is a directory, then:
 * 
 * If GET_ALL is false, then only locally-modified entries will be
 * returned.  If true, then all entries will be returned.
 *
 * If DESCEND is false, statushash will contain statuses for PATH and
 * its entries.  Else if DESCEND is true, statushash will contain
 * statuses for PATH and everything below it, including
 * subdirectories.  In other words, a full recursion. */
svn_error_t *svn_wc_statuses (apr_hash_t *statushash,
                              const char *path,
                              svn_wc_adm_access_t *adm_access,
                              svn_boolean_t descend,
                              svn_boolean_t get_all,
                              svn_boolean_t no_ignore,
                              svn_wc_notify_func_t notify_func,
                              void *notify_baton,
                              apr_pool_t *pool);


/* Set  *EDITOR and *EDIT_BATON to an editor that tweaks or adds
   svn_wc_status_t structures to STATUSHASH to reflect repository
   modifications that would be received on update, and that sets
   *YOUNGEST to the youngest revision in the repository (the editor
   also sets the repos_rev field in each svn_wc_status_t structure
   to the same value).  ADM_ACCESS must be an access baton for PATH.

   If DESCEND is zero, then only immediate children of PATH will be
   done, otherwise ADM_ACCESS should be part of an access baton set
   for the PATH hierarchy.

   Allocate the editor itself in POOL, but the editor does temporary
   allocations in a subpool of POOL.  */
svn_error_t *svn_wc_get_status_editor (const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *path,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_boolean_t descend,
                                       apr_hash_t *statushash,
                                       svn_revnum_t *youngest,
                                       apr_pool_t *pool);



/* Copy SRC to DST_BASENAME in DST_PARENT, and schedule DST_BASENAME
   for addition to the repository, remembering the copy history.

   SRC must be a file or directory under version control; DST_PARENT
   must be a directory under version control in the same working copy;
   DST_BASENAME will be the name of the copied item, and it must not
   exist already.

   For each file or directory copied, NOTIFY_FUNC will be called
   with its path and the NOTIFY_BATON. NOTIFY_FUNC may be NULL if
   you are not interested in this information.

   Important: this is a variant of svn_wc_add.  No changes will happen
   to the repository until a commit occurs.  This scheduling can be
   removed with svn_client_revert.  */
svn_error_t *svn_wc_copy (const char *src,
                          svn_wc_adm_access_t *dst_parent,
                          const char *dst_basename,
                          svn_wc_notify_func_t notify_func,
                          void *notify_baton,
                          apr_pool_t *pool);


/* Schedule PATH for deletion, it will be deleted from the repository on
   the next commit.  If PATH refers to a directory, then a recursive
   deletion will occur. ADM_ACCESS must hold a write lock for the parent of
   PATH.

   This function immediately deletes all files, modified and unmodified,
   versioned and unversioned from the working copy. It also immediately
   deletes unversioned directories and directories that are scheduled to be
   added.  Only versioned directories will remain in the working copy,
   these get deleted by the update following the commit.

   For each path marked for deletion, NOTIFY_FUNC will be called with
   the NOTIFY_BATON and that path. The NOTIFY_FUNC callback may be
   NULL if notification is not needed.  */
svn_error_t *svn_wc_delete (const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_func_t notify_func,
                            void *notify_baton,
                            apr_pool_t *pool);


/* Put PATH under version control by adding an entry in its parent,
   and, if PATH is a directory, adding an administrative area.  The
   new entry and anything under it is scheduled for addition to the
   repository.  PARENT_ACCESS should hold a write lock for the parent
   directory of PATH.  If PATH is a directory then an access baton for
   PATH will be added to the set containing PARENT_ACCESS.

   If PATH does not exist, return SVN_ERR_WC_PATH_NOT_FOUND.

   If COPYFROM_URL is non-null, it and COPYFROM_REV are used as
   `copyfrom' args.  This is for copy operations, where one wants
   to schedule PATH for addition with a particular history.

   When the PATH has been added, then NOTIFY_FUNC will be called
   (if it is not NULL) with the NOTIFY_BATON and the path.

   Return SVN_ERR_WC_NODE_KIND_CHANGE if PATH is both an unversioned
   directory and a file that is scheduled for deletion or in state deleted.

   ### This function currently does double duty -- it is also
   ### responsible for "switching" a working copy directory over to a
   ### new copyfrom ancestry and scheduling it for addition.  Here is
   ### the old doc string from Ben, lightly edited to bring it
   ### up-to-date, explaining the true, secret life of this function:

   Given a PATH within a working copy of type KIND, follow this algorithm:

      - if PATH is not under version control:
         - Place it under version control and schedule for addition; 
           if COPYFROM_URL is non-null, use it and COPYFROM_REV as
           'copyfrom' history

      - if PATH is already under version control:
            (This can only happen when a directory is copied, in which
             case ancestry must have been supplied as well.)

         -  Schedule the directory itself for addition with copyfrom history.
         -  Mark all its children with a 'copied' flag
         -  Rewrite all the URLs to what they will be after a commit.
         -  ### TODO:  remove old wcprops too, see the '###'below

   ### I think possibly the "switchover" functionality should be
   ### broken out into a separate function, but its all intertwined in
   ### the code right now.  Ben, thoughts?  Hard?  Easy?  Mauve? */
svn_error_t *svn_wc_add (const char *path,
                         svn_wc_adm_access_t *parent_access,
                         const char *copyfrom_url,
                         svn_revnum_t copyfrom_rev,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         apr_pool_t *pool);


/* Remove entry NAME in ADM_ACCESS from revision control.  NAME must be
   either a file or SVN_WC_ENTRY_THIS_DIR.  ADM_ACCESS must hold a write
   lock.

   If NAME is a file, all its info will be removed from ADM_ACCESS's
   administrative directory.  If NAME is SVN_WC_ENTRY_THIS_DIR, then
   ADM_ACCESS's entire administrative area will be deleted, along with
   *all* the administrative areas anywhere in the tree below ADM_ACCESS.

   Normally, only adminstrative data is removed.  However, if
   DESTROY_WF is true, then all working file(s) and dirs are deleted
   from disk as well.  When called with DESTROY_WF, any locally
   modified files will *not* be deleted, and the special error
   SVN_ERR_WC_LEFT_LOCAL_MOD might be returned.  (Callers only need to
   check for this special return value if DESTROY_WF is true.)

   WARNING:  This routine is exported for careful, measured use by
   libsvn_client.  Do *not* call this routine unless you really
   understand what the heck you're doing.  */
svn_error_t *
svn_wc_remove_from_revision_control (svn_wc_adm_access_t *adm_access,
                                     const char *name,
                                     svn_boolean_t destroy_wf,
                                     apr_pool_t *pool);


/* Assuming PATH is under version control and in a state of conflict, then
   take PATH *out* of this state.  If RESOLVE_TEXT is true then any text
   conflict is resolved, if RESOLVE_PROPS is true then any property
   conflicts are resolved.  If RECURSIVE is true, then search
   recursively for conflicts to resolve.

   Needless to say, this function doesn't touch conflict markers or
   anything of that sort -- only a human can semantically resolve a
   conflict.  Instead, this function simply marks a file as "having
   been resolved", clearing the way for a commit.  

   The implementation details are opaque, as our "conflicted" criteria
   might change over time.  (At the moment, this routine removes the
   three fulltext 'backup' files and any .prej file created in a conflict.)

   If PATH is not under version control, return SVN_ERR_ENTRY_NOT_FOUND.  
   If PATH isn't in a state of conflict to begin with, do nothing, and
   return SVN_NO_ERROR.

   If PATH was successfully taken out of a state of conflict, report this
   information to NOTIFY_FUNC (if non-NULL.)  If only text or only property
   conflict resolution was requested, and it was successful, then success
   gets reported.
 */
svn_error_t *svn_wc_resolve_conflict (const char *path,
                                      svn_boolean_t resolve_text,
                                      svn_boolean_t resolve_props,
                                      svn_boolean_t recursive,
                                      svn_wc_notify_func_t notify_func,
                                      void *notify_baton,
                                      apr_pool_t *pool);


/*** Commits. ***/

/* Bump a successfully committed absolute PATH to NEW_REVNUM after a
   commit succeeds.  REV_DATE and REV_AUTHOR are the (server-side)
   date and author of the new revision; one or both may be NULL.
   ADM_ACCESS must hold a write lock appropriate for PATH.

   If non-null, WCPROPS is an array of `svn_prop_t *' changes to wc
   properties; if an svn_prop_t->value is null, then that property is
   deleted.

   If RECURSE is true and PATH is a directory, then bump every
   versioned object at or under PATH.  This is usually done for
   copied trees.  */
svn_error_t *svn_wc_process_committed (const char *path,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_boolean_t recurse,
                                       svn_revnum_t new_revnum,
                                       const char *rev_date,
                                       const char *rev_author,
                                       apr_array_header_t *wcprop_changes,
                                       apr_pool_t *pool);




/*** Traversal info. ***/

/* Traversal information is information gathered by a working copy
 * crawl or update.  For example, the before and after values of the
 * svn:externals property are important after an update, and since
 * we're traversing the working tree anyway (a complete traversal
 * during the initial crawl, and a traversal of changed paths during
 * the checkout/update/switch), it makes sense to gather the
 * property's values then instead of making a second pass.
 */
typedef struct svn_wc_traversal_info_t svn_wc_traversal_info_t;


/* Return a new, empty traversal info object, allocated in POOL. */
svn_wc_traversal_info_t *svn_wc_init_traversal_info (apr_pool_t *pool);


/* Set *EXTERNALS_OLD and *EXTERNALS_NEW to hash tables representing
 * changes to values of the svn:externals property on directories
 * traversed by TRAVERSAL_INFO.
 *
 * TRAVERSAL_INFO is obtained from svn_wc_init_traversal_info, but is
 * only useful after it has been passed through another function, such
 * as svn_wc_crawl_revisions, svn_wc_get_update_editor,
 * svn_wc_get_checkout_editor, svn_wc_get_switch_editor, etc.
 *
 * Each hash maps `const char *' directory names onto `const char *'
 * values of the externals property for that directory.  The dir names
 * are full paths -- that is, anchor plus target, not target alone.
 * The values are not parsed, they are simply copied raw, and are
 * never null: directories that acquired or lost the property are
 * simply omitted from the appropriate table.  Directories whose value
 * of the property did not change show the same value in each hash.
 *
 * The hashes, keys, and values have the same lifetime as TRAVERSAL_INFO.
 */
void svn_wc_edited_externals (apr_hash_t **externals_old,
                              apr_hash_t **externals_new,
                              svn_wc_traversal_info_t *traversal_info);




/* Do a depth-first crawl in a working copy, beginning at PATH.
   Communicate the `state' of the working copy's revisions to
   REPORTER/REPORT_BATON.  Obviously, if PATH is a file instead of a
   directory, this depth-first crawl will be a short one.

   No locks are or logs are created, nor are any animals harmed in the
   process.  No cleanup is necessary.  ADM_ACCESS must be an access baton
   for the PATH hierarchy, it does not require a write lock.

   After all revisions are reported, REPORTER->finish_report() is
   called, which immediately causes the RA layer to update the working
   copy.  Thus the return value may very well reflect the result of
   the update!

   If RESTORE_FILES is true, then unexpectedly missing working files
   will be restored from the administrative directory's cache. For each
   file restored, the NOTIFY_FUNC function will be called with the
   NOTIFY_BATON and the path of the restored file. NOTIFY_FUNC may
   be NULL if this notification is not required.

   If TRAVERSAL_INFO is non-null, then record pre-update traversal
   state in it.  (Caller should obtain TRAVERSAL_INFO from
   svn_wc_init_traversal_info.)  */
svn_error_t *
svn_wc_crawl_revisions (const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_wc_notify_func_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool);


/*** Updates. ***/

/*
 * Set *WC_ROOT to TRUE if PATH represents a "working copy root",
 * FALSE otherwise.  Use POOL for any intermediate allocations.
 *
 * NOTE: Due to the way in which "WC-root-ness" is calculated, passing
 * a PATH of `.' to this function will always return TRUE.
 */
svn_error_t *svn_wc_is_wc_root (svn_boolean_t *wc_root,
                                const char *path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool);


/*
 * Conditionally split PATH into an ANCHOR and TARGET for the purpose
 * of updating and committing.
 *
 * ANCHOR is the directory at which the update or commit editor
 * should be rooted.
 *
 * TARGET is the actual subject (relative to the ANCHOR) of the
 * update/commit, or NULL if the ANCHOR itself is the subject.
 *
 * Allocate ANCHOR and TARGET in POOL.  
 */
svn_error_t *svn_wc_get_actual_target (const char *path,
                                       const char **anchor,
                                       const char **target,
                                       apr_pool_t *pool);



/*** Update and update-like functionality. ***/

/* Set *EDITOR and *EDIT_BATON to an editor and baton for updating a
 * working copy.
 *
 * If TI is non-null, record traversal info in TI, for use by
 * post-traversal accessors such as svn_wc_edited_externals().
 * 
 * ANCHOR is an access baton, with a write lock, for the local path to the
 * working copy which will be used as the root of our editor.  Further
 * locks will be acquired if the update creates new directories.  All
 * locks, both those in ANCHOR and newly acquired ones, will be released
 * when the editor driver calls close_edit.
 *
 * TARGET is the entry in ANCHOR that will actually be updated, or NULL if
 * all of ANCHOR should be updated.
 *
 * The editor invokes NOTIFY_FUNC with NOTIFY_BATON as the update
 * progresses, if NOTIFY_FUNC is non-null.
 *
 * TARGET_REVISION is the repository revision that results from this set
 * of changes.
 */
svn_error_t *svn_wc_get_update_editor (svn_wc_adm_access_t *anchor,
                                       const char *target,
                                       svn_revnum_t target_revision,
                                       svn_boolean_t recurse,
                                       svn_wc_notify_func_t notify_func,
                                       void *notify_baton,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       svn_wc_traversal_info_t *ti,
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
 * The editor invokes NOTIFY_FUNC with NOTIFY_BATON as the checkout
 * progresses, if NOTIFY_FUNC is non-null.
 *
 * ANCESTOR_URL is the repository string to be recorded in this
 * working copy.
 */
svn_error_t *svn_wc_get_checkout_editor (const char *dest,
                                         const char *ancestor_url,
                                         svn_revnum_t target_revision,
                                         svn_boolean_t recurse,
                                         svn_wc_notify_func_t notify_func,
                                         void *notify_baton,
                                         const svn_delta_editor_t **editor,
                                         void **edit_baton,
                                         svn_wc_traversal_info_t *ti,
                                         apr_pool_t *pool);


/* Another variant of svn_wc_get_update_editor(): 
 *
 * Set *EDITOR and *EDIT_BATON to an editor and baton for "switching"
 * a working copy to a new SWITCH_URL.  (Right now, this URL must be
 * within the same repository that the working copy already comes
 * from.)  SWITCH_URL must not be NULL.
 *
 * If TI is non-null, record traversal info in TI, for use by
 * post-traversal accessors such as svn_wc_edited_externals().
 * 
 * ANCHOR is an access baton, with a write lock, for the local path to the
 * working copy which will be used as the root of our editor.  Further
 * locks will be acquired if the switch creates new directories.  All
 * locks, both those in ANCHOR and newly acquired ones, will be released
 * when the editor driver calls close_edit.
 *
 * TARGET is the entry in ANCHOR that will actually be updated, or NULL if
 * all of ANCHOR should be updated.
 *
 * The editor invokes NOTIFY_FUNC with NOTIFY_BATON as the switch
 * progresses, if NOTIFY_FUNC is non-null.
 *
 * TARGET_REVISION is the repository revision that results from this set
 * of changes.
 */
svn_error_t *svn_wc_get_switch_editor (svn_wc_adm_access_t *anchor,
                                       const char *target,
                                       svn_revnum_t target_revision,
                                       const char *switch_url,
                                       svn_boolean_t recurse,
                                       svn_wc_notify_func_t notify_func,
                                       void *notify_baton,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       svn_wc_traversal_info_t *ti,
                                       apr_pool_t *pool);


/* Given a FILE_PATH already under version control, fully "install" a
   NEW_REVISION of the file.  ADM_ACCESS is an access baton with a write
   lock for the directory containing FILE_PATH.

   By "install", we mean: the working copy library creates a new
   text-base and prop-base, merges any textual and property changes
   into the working file, and finally updates all metadata so that the
   working copy believes it has a new working revision of the file.
   All of this work includes being sensitive to eol translation,
   keyword substitution, and performing all actions using a journaled
   logfile.

   The caller provides a NEW_TEXT_PATH which points to a temporary
   file containing the 'new' full text of the file at revision
   NEW_REVISION.  This function automatically removes NEW_TEXT_PATH
   upon successful completion.  If there is no new text, then caller
   must set NEW_TEXT_PATH to NULL.

   The caller also provides the new properties for the file in the
   PROPS array; if there are no new props, then caller must pass NULL
   instead.  This argument is an array of svn_prop_t structures, and
   can be interpreted in one of two ways:

      - if IS_FULL_PROPLIST is true, then the array represents the
        complete list of all properties for the file.  It is the new
        'pristine' proplist.

      - if IS_FULL_PROPLIST is false, then the array represents a set of
        *differences* against the file's existing pristine proplist.
        (A deletion is represented by setting an svn_prop_t's 'value'
        field to NULL.)  

   Note that the PROPS array is expected to contain all categories of
   props, not just 'regular' ones that the user sees.  (See 'enum
   svn_prop_kind').

   If CONTENT_STATE is non-null, set *CONTENT_STATE to the state of
   the file contents after the installation; if return error, the
   value of *CONTENT_STATE is undefined.

   If PROP_STATE is non-null, set *PROP_STATE to the state of the
   properties after the installation; if return error, the value of
   *PROP_STATE is undefined.

   If NEW_URL is non-NULL, then this URL will be attached to the file
   in the 'entries' file.  Otherwise, the file will simply "inherit"
   its URL from the parent dir.

   POOL is used for all bookkeeping work during the installation.
 */
svn_error_t *svn_wc_install_file (svn_wc_notify_state_t *content_state,
                                  svn_wc_notify_state_t *prop_state,
                                  svn_wc_adm_access_t *adm_access,
                                  const char *file_path,
                                  svn_revnum_t new_revision,
                                  const char *new_text_path,
                                  const apr_array_header_t *props,
                                  svn_boolean_t is_full_proplist,
                                  const char *new_URL,
                                  apr_pool_t *pool);



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

/* Set *PROPS to a hash table mapping char * names onto
   svn_string_t * values for all the wc properties of PATH.
   Allocate the table, names, and values in POOL.  If the node has no
   properties, an empty hash is returned. */
svn_error_t *svn_wc_prop_list (apr_hash_t **props,
                               const char *path,
                               apr_pool_t *pool);


/* Set *VALUE to the value of property NAME for PATH, allocating
   *VALUE in POOL.  If no such prop, set *VALUE to NULL.  NAME may be
   a regular or wc property; if it is an entry property, return the
   error SVN_ERR_BAD_PROP_KIND. */
svn_error_t *svn_wc_prop_get (const svn_string_t **value,
                              const char *name,
                              const char *path,
                              apr_pool_t *pool);

/* Set property NAME to VALUE for PATH.  Do any temporary
   allocation in POOL.  If NAME is not a valid property for PATH,
   return SVN_ERR_ILLEGAL_TARGET.  If VALUE is null, remove property
   NAME.  ADM_ACCESS must be an access baton with a write lock for PATH. 

   NAME may be a wc property or a regular property; but if it is an
   entry property, return the error SVN_ERR_BAD_PROP_KIND. */
svn_error_t *svn_wc_prop_set (const char *name,
                              const svn_string_t *value,
                              const char *path,
                              svn_wc_adm_access_t *adm_access,
                              apr_pool_t *pool);


/* Return true iff NAME is a 'normal' property name.  'Normal' is
   defined as a user-visible and user-tweakable property that shows up
   when you fetch a proplist.

   The function currently parses the namespace like so:

     'svn:wc:'  ==>  a wcprop, stored/accessed seperately via different API.

     'svn:entry:' ==> an "entry" prop, shunted into the 'entries' file.

   If these patterns aren't found, then the property is assumed to be
   Normal.  */
svn_boolean_t svn_wc_is_normal_prop (const char *name);



/* Return true iff NAME is a 'wc' property name.  (see above) */
svn_boolean_t svn_wc_is_wc_prop (const char *name);

/* Return true iff NAME is a 'entry' property name.  (see above) */
svn_boolean_t svn_wc_is_entry_prop (const char *name);




/*** Diffs ***/


/* Return an EDITOR/EDIT_BATON for diffing a working copy against the
 * repository.
 *
 * ANCHOR/TARGET represent the base of the hierarchy to be compared.
 *
 * CALLBACKS/CALLBACK_BATON is the callback table to use when two
 * files are to be compared.
 *
 * RECURSE determines whether to descend into subdirectories when TARGET
 * is a directory.  If RECURSE is TRUE then ANCHOR should be part of an
 * access baton set for the TARGET hierarchy.
 */
svn_error_t *svn_wc_get_diff_editor (svn_wc_adm_access_t *anchor,
                                     const char *target,
                                     const svn_wc_diff_callbacks_t *callbacks,
                                     void *callback_baton,
                                     svn_boolean_t recurse,
                                     const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     apr_pool_t *pool);


/* Compare working copy against the text-base.
 *
 * ANCHOR/TARGET represent the base of the hierarchy to be compared.
 *
 * CALLBACKS/CALLBACK_BATON is the callback table to use when two
 * files are to be compared.
 *
 * RECURSE determines whether to descend into subdirectories when TARGET
 * is a directory.  If RECURSE is TRUE then ANCHOR should be part of an
 * access baton set for the TARGET hierarchy.
 */
svn_error_t *svn_wc_diff (svn_wc_adm_access_t *anchor,
                          const char *target,
                          const svn_wc_diff_callbacks_t *callbacks,
                          void *callback_baton,
                          svn_boolean_t recurse,
                          apr_pool_t *pool);

/* Given a PATH to a file or directory under version control, discover
   any local changes made to properties and/or the set of 'pristine'
   properties.

   If PROPCHANGES is non-NULL, return these changes as an array of
   svn_prop_t structures stored in *PROPCHANGES.  The structures and
   array will be allocated in POOL.  If there are no local property
   modifications on PATH, then set *PROPCHANGES to NULL.

   If ORIGINAL_PROPS is non-NULL, then set *ORIGINAL_PROPS to
   hashtable (const char *name -> const svn_string_t *value) that
   represents the 'pristine' property list of PATH.  This hashtable is
   allocated in POOL, and can be used to compare old and new values of
   properties.
*/
svn_error_t *svn_wc_get_prop_diffs (apr_array_header_t **propchanges,
                                    apr_hash_t **original_props,
                                    const char *path,
                                    apr_pool_t *pool);



/* Given two property hashes (const char *name -> const svn_string_t
   *value), deduce the differences between them (from BASEPROPS ->
   LOCALPROPS).  Return these changes as a series of svn_prop_t
   structures stored in LOCAL_PROPCHANGES, allocated from POOL.
   
   For note, here's a quick little table describing the logic of this
   routine:

   basehash        localhash         event
   --------        ---------         -----
   value = foo     value = NULL      Deletion occurred.
   value = foo     value = bar       Set occurred (modification)
   value = NULL value = baz Set occurred (creation) */
svn_error_t *
svn_wc_get_local_propchanges (apr_array_header_t **local_propchanges,
                              apr_hash_t *localprops,
                              apr_hash_t *baseprops,
                              apr_pool_t *pool);



/* The outcome of a merge carried out (or tried as a dry-run) by
   svn_wc_merge */
typedef enum svn_wc_merge_outcome_t
{
   /* The working copy is (or would be) unchanged.  The changes to be
      merged were already present in the working copy */
   svn_wc_merge_unchanged,

   /* The working copy has been (or would be) changed. */
   svn_wc_merge_merged,

   /* The working copy has been (or would be) changed, but there was (or
      would be) a conflict */
   svn_wc_merge_conflict
} svn_wc_merge_outcome_t;

/* Given paths to three fulltexts, merge the differences between LEFT
   and RIGHT into MERGE_TARGET.  (It may help to know that LEFT,
   RIGHT, and MERGE_TARGET correspond to "OLDER", "YOURS", and "MINE",
   respectively, in the diff3 documentation.)  Use POOL for any
   temporary allocation.

   ADM_ACCESS is an access baton with a write lock for the directory
   containing MERGE_TARGET.

   This function assumes that LEFT and RIGHT are in repository-normal
   form (linefeeds, with keywords contracted); if necessary,
   MERGE_TARGET is temporarily converted to this form to receive the
   changes, then translated back again.

   MERGE_TARGET must be under version control; if it is not, return
   SVN_ERR_NO_SUCH_ENTRY.

   DRY_RUN determines whether the working copy is modified.  When it
   is FALSE the merge will cause MERGE_TARGET to be modified, when it
   is TRUE the merge will be carried out to determine the result but
   MERGE_TARGET will not be modified.

   The outcome of the merge is returned in *MERGE_OUTCOME. If there is
   a conflict and DRY_RUN is FALSE, then

     * Put conflict markers around the conflicting regions in
       MERGE_TARGET, labeled with LEFT_LABEL, RIGHT_LABEL, and
       TARGET_LABEL.  (If any of these labels are NULL, default values
       will be used.)
 
     * Copy LEFT, RIGHT, and the original MERGE_TARGET to unique names
       in the same directory as MERGE_TARGET, ending with the suffixes
       ".LEFT_LABEL", ".RIGHT_LABEL", and ".TARGET_LABEL"
       respectively.

     * Mark the entry for MERGE_TARGET as "conflicted", and track the
       abovementioned backup files in the entry as well.

   Binary case:

    If MERGE_TARGET is a binary file, then no merging is attempted,
    the merge is deemed to be a conflict.  If DRY_RUN is FALSE the
    working MERGE_TARGET is untouched, and copies of LEFT and RIGHT
    are created next to it using LEFT_LABEL and RIGHT_LABEL.
    MERGE_TARGET's entry is marked as "conflicted", and begins
    tracking the two backup files.  If DRY_RUN is TRUE no files are
    changed.  The outcome of the merge is returned in *MERGE_OUTCOME.

*/
svn_error_t *svn_wc_merge (const char *left,
                           const char *right,
                           const char *merge_target,
                           svn_wc_adm_access_t *adm_access,
                           const char *left_label,
                           const char *right_label,
                           const char *target_label,
                           svn_boolean_t dry_run,
                           enum svn_wc_merge_outcome_t *merge_outcome,
                           apr_pool_t *pool);


/* Given a PATH under version control, merge an array of PROPCHANGES
   into the path's existing properties.  PROPCHANGES is an array of
   svn_prop_t objects.  ADM_ACCESS is an access baton for the directory
   containing PATH.

   If BASE_MERGE is FALSE only the working properties will be changed,
   if it is TRUE both the base and working properties will be changed.

   If STATE is non-null, set *STATE to the state of the properties
   after the merge.

   If conflicts are found when merging working properties, they are
   described in a temporary .prej file (or appended to an already-existing
   .prej file), and the entry is marked "conflicted".  Base properties
   are changed unconditionally, if BASE_MERGE is TRUE, they never result
   in a conflict.
*/
svn_error_t *
svn_wc_merge_prop_diffs (svn_wc_notify_state_t *state,
                         const char *path,
                         svn_wc_adm_access_t *adm_access,
                         const apr_array_header_t *propchanges,
                         svn_boolean_t base_merge,
                         svn_boolean_t dry_run,
                         apr_pool_t *pool);



/* Given a PATH to a wc file, return a PRISTINE_PATH which points to a
   pristine version of the file.  This is needed so clients can do
   diffs.  If the WC has no text-base, return a NULL instead of a
   path. */
svn_error_t *svn_wc_get_pristine_copy_path (const char *path,
                                            const char **pristine_path,
                                            apr_pool_t *pool);


/* Recurse from PATH, cleaning up unfinished log business.  Perform
   necessary allocations in POOL.  Any working copy locks under PATH will
   be taken over and then cleared by this function.  WARNING: there is no
   mechanism that will protect locks that are still being used. */
svn_error_t *
svn_wc_cleanup (const char *path,
                svn_wc_adm_access_t *optional_adm_access,
                apr_pool_t *pool);


/* Revert changes to PATH (perhaps in a RECURSIVE fashion).  Perform
   necessary allocations in POOL.

   PARENT_ACCESS is an access baton for the directory containing PATH,
   unless PATH is a wc root, in which case PARENT_ACCESS refers to PATH
   itself.

   For each item reverted, NOTIFY_FUNC will be called with NOTIFY_BATON
   and the path of the reverted item. NOTIFY_FUNC may be NULL if this
   notification is not needed.  */
svn_error_t *
svn_wc_revert (const char *path, 
               svn_wc_adm_access_t *parent_access,
               svn_boolean_t recursive, 
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool);



/*** Authentication files ***/

/* Get the *CONTENTS of FILENAME in the authentcation area of PATH's
   administrative directory, allocated in POOL.  PATH must be a
   working copy directory. If file does not exist,
   SVN_ERR_WC_PATH_NOT_FOUND is returned.

   Note: CONTENTS is a stringbuf because maybe we'll need to fetch
   binary contents from an auth file.  If that's unlikely, then we
   should change it to const char *.  */
svn_error_t *
svn_wc_get_auth_file (const char *path,
                      const char *filename,
                      svn_stringbuf_t **contents,
                      apr_pool_t *pool);


/* Store a file named FILENAME with CONTENTS in the authentication
   area of ADM_ACCESS's administrative directory. If no such file
   exists, it will be created.  If the file exists already, it will
   be completely overwritten with the new contents.  If RECURSE is
   true, this file will be stored in every administrative area below
   ADM_ACCESS as well. 

   Note: CONTENTS is a stringbuf because maybe we'll need to store
   binary contents in an auth file.  If that's unlikely, then we
   should change it to const char *.  */
svn_error_t *
svn_wc_set_auth_file (svn_wc_adm_access_t *adm_access,
                      svn_boolean_t recurse,
                      const char *filename,
                      svn_stringbuf_t *contents,
                      apr_pool_t *pool);



/*** Tmp files ***/

/* Create a unique temporary file in administrative tmp/ area of
   directory PATH.  Return a handle in *FP.
   
   The flags will be APR_WRITE | APR_CREATE | APR_EXCL and
   optionally APR_DELONCLOSE (if the delete_on_close argument is set TRUE).

   This means that as soon as FP is closed, the tmp file will vanish.  */
svn_error_t *
svn_wc_create_tmp_file (apr_file_t **fp,
                        const char *path,
                        svn_boolean_t delete_on_close,
                        apr_pool_t *pool);



/*** Eol conversion and keyword expansion. ***/

/* Set *XLATED_P to a path to a possibly translated copy of versioned
 * file VFILE, or to VFILE itself if no translation is necessary.
 * That is, if VFILE's properties indicate newline conversion or
 * keyword expansion, point *XLATED_P to a copy of VFILE whose
 * newlines are unconverted and keywords contracted, in whatever
 * manner is indicated by VFILE's properties; otherwise, set *XLATED_P
 * to VFILE.
 *
 * If FORCE_REPAIR is set, the translated file will have any
 * inconsistent line endings repaired.  This should only be used when
 * the resultant file is being created for comparison against VFILE's
 * text base.
 *
 * Caller is responsible for detecting if they are different (pointer
 * comparison is sufficient), and for removing *XLATED_P if
 * necessary.
 *
 * This function is generally used to get a file that can be compared
 * meaningfully against VFILE's text base.
 *
 * If *XLATED_P is different from VFILE, then choose *XLATED_P's name
 * using svn_io_open_unique_file() with SVN_WC__TMP_EXT, and allocate
 * it in POOL.  Also use POOL for any temporary allocation.
 *
 * If an error is returned, the effect on *XLATED_P is undefined.  */
svn_error_t *svn_wc_translated_file (const char **xlated_p,
                                     const char *vfile,
                                     svn_wc_adm_access_t *adm_access,
                                     svn_boolean_t force_repair,
                                     apr_pool_t *pool);



/*** Text/Prop Deltas Using an Editor ***/


/* Send the local modifications for versioned file PATH (with
   matching FILE_BATON) through EDITOR, then close FILE_BATON
   afterwards.  Use POOL for any temporary allocation and
   ADM_ACCESS as an access baton for PATH.
  
   This process creates a copy of PATH with keywords and eol
   untranslated.  If TEMPFILE is non-null, set *TEMPFILE to the path
   to this copy.  Do not clean up the copy; caller can do that.  (The
   purpose of handing back the tmp copy is that it is usually about to
   become the new text base anyway, but the installation of the new
   text base is outside the scope of this function.)

   If FULLTEXT, send the untranslated copy of PATH through EDITOR as
   full-text; else send it as svndiff against the current text base.

   If sending a diff, and the recorded checksum for PATH's text-base
   does not match the current actual checksum, then remove the tmp
   copy (and set *TEMPFILE to null if appropriate), and return the
   error SVN_ERR_WC_CORRUPT_TEXT_BASE.

   Note: this is intended for use with both infix and postfix
   text-delta styled editor drivers.
*/
svn_error_t *svn_wc_transmit_text_deltas (const char *path,
                                          svn_wc_adm_access_t *adm_access,
                                          svn_boolean_t fulltext,
                                          const svn_delta_editor_t *editor,
                                          void *file_baton,
                                          const char **tempfile,
                                          apr_pool_t *pool);


/* Given a PATH with its accompanying ENTRY, transmit all local property
   modifications using the appropriate EDITOR method (in conjunction
   with BATON).  Use POOL for all allocations.

   If a temporary file remains after this function is finished, the
   path to that file is returned in *TEMPFILE (so the caller can clean
   this up if it wishes to do so).  */
svn_error_t *svn_wc_transmit_prop_deltas (const char *path,
                                          const svn_wc_entry_t *entry,
                                          const svn_delta_editor_t *editor,
                                          void *baton,
                                          const char **tempfile,
                                          apr_pool_t *pool);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_WC_H */
