/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_wc.h
 * @brief public interface for the Subversion Working Copy Library
 *
 * Requires:  
 *            - A working copy
 * 
 * Provides: 
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:   
 *            - Clients.
 */

#ifndef SVN_WC_H
#define SVN_WC_H


#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_auth.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_ra.h"    /* for svn_ra_reporter_t type */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Locking/Opening/Closing */

/** Baton for access to a working copy administrative area.
 *
 * Baton for access to a working copy administrative area.
 *
 * One day all such access will require a baton, we're not there yet.
 *
 * Access batons can be grouped into sets, by passing an existing open
 * baton when opening a new baton.  Given one baton in a set, other batons
 * may be retrieved.  This allows an entire hierarchy to be locked, and
 * then the set of batons can be passed around by passing a single baton.
 */
typedef struct svn_wc_adm_access_t svn_wc_adm_access_t;

/** Return, in @a *adm_access, a pointer to a new access baton for the 
 * working copy administrative area associated with the directory @a path.
 *
 * Return, in @a *adm_access, a pointer to a new access baton for the working
 * copy administrative area associated with the directory @a path.  If
 * @a write_lock is true the baton will include a write lock, otherwise the
 * baton can only be used for read access.  If @a path refers to a directory
 * that is already write locked then the error @c SVN_ERR_WC_LOCKED will be
 * returned.  The error @c SVN_ERR_WC_NOT_DIRECTORY will be returned if
 * @a path is not a versioned directory.
 *
 * If @a associated is an open access baton then @a adm_access will be added 
 * to the set containing @a associated.  @a associated can be @c NULL, in 
 * which case @a adm_access is the start of a new set.
 *
 * If @a tree_lock is @c TRUE then the working copy directory hierarchy under
 * @a path will be locked.  All the access batons will become part of the set
 * containing @a adm_access.  This is an all-or-nothing option, if it is not
 * possible to lock the entire tree then an error will be returned and
 * @a adm_access will be invalid, with the exception that sudirectories of
 * @a path that are missing from the physical filesystem will not be locked
 * and will not cause an error.  The error @c SVN_ERR_WC_LOCKED will be
 * returned if a subdirectory of @a path is already write locked.
 *
 * @a pool will be used to allocate memory for the baton and any subsequently
 * cached items.  If @a adm_access has not been closed when the pool is
 * cleared, it will be closed automatically at that point, and removed from
 * its set.  A baton closed in this way will not remove physical locks from
 * the working copy if cleanup is required.
 *
 * The first baton in a set, with @a associated passed as @c NULL, must have 
 * the longest lifetime of all the batons in the set.  This implies it must be
 * the root of the hierarchy.
 */
svn_error_t *svn_wc_adm_open (svn_wc_adm_access_t **adm_access,
                              svn_wc_adm_access_t *associated,
                              const char *path,
                              svn_boolean_t write_lock,
                              svn_boolean_t tree_lock,
                              apr_pool_t *pool);

/** Checks the working copy to determine the node type of @a path.
 *
 * Checks the working copy to determine the node type of @a path.  If 
 * @a path is a versioned directory then the behaviour is like that of
 * @c svn_wc_adm_open, otherwise, if @a path is a file, an unversioned 
 * directory, or does not exist, then the behaviour is like that of 
 * @c svn_wc_adm_open with @a path replaced by the parent directory of 
 * @a path.
 */
svn_error_t *svn_wc_adm_probe_open (svn_wc_adm_access_t **adm_access,
                                    svn_wc_adm_access_t *associated,
                                    const char *path,
                                    svn_boolean_t write_lock,
                                    svn_boolean_t tree_lock,
                                    apr_pool_t *pool);

/** Return, in @a *adm_access, a pointer to an existing access baton 
 * associated with @a path.
 *
 * Return, in @a *adm_access, a pointer to an existing access baton associated
 * with @a path.  @a path must be a directory that is locked as part of the 
 * set containing the @a associated access baton.
 *
 * @a pool is used only for local processing it is not used for the batons.
 */
svn_error_t *svn_wc_adm_retrieve (svn_wc_adm_access_t **adm_access,
                                  svn_wc_adm_access_t *associated,
                                  const char *path,
                                  apr_pool_t *pool);

/** Checks the working copy to determine the node type of @a path.
 *
 * Checks the working copy to determine the node type of @a path.  If 
 * @a path is a versioned directory then the behaviour is like that of
 * @c svn_wc_adm_retrieve, otherwise, if @a path is a file, an unversioned
 * directory, or does not exist, then the behaviour is like that of
 * @c svn_wc_adm_retrieve with @a path replaced by the parent directory of
 * @a path.
 */
svn_error_t *svn_wc_adm_probe_retrieve (svn_wc_adm_access_t **adm_access,
                                        svn_wc_adm_access_t *associated,
                                        const char *path,
                                        apr_pool_t *pool);

/** Give up the access baton @a adm_access, and its lock if any.
 *
 * Give up the access baton @a adm_access, and its lock if any. This will
 * recursively close any batons in the same set that are direct
 * subdirectories of @a adm_access.  Any physical locks will be removed from
 * the working copy.  Lock removal is unconditional, there is no check to
 * determine if cleanup is required.
 */
svn_error_t *svn_wc_adm_close (svn_wc_adm_access_t *adm_access);

/** Return the path used to open the access baton @a adm_access */
const char *svn_wc_adm_access_path (svn_wc_adm_access_t *adm_access);

/** Return the pool used by access baton @a adm_access */
apr_pool_t *svn_wc_adm_access_pool (svn_wc_adm_access_t *adm_access);

/** Ensure @a adm_access has a write lock, and that it is still valid.
 *
 * Ensure @a adm_access has a write lock, and that it is still valid. Returns
 * @c SVN_ERR_WC_NOT_LOCKED if this is not the case.
 */
svn_error_t *svn_wc_adm_write_check (svn_wc_adm_access_t *adm_access);


/** Set @a *locked to non-zero if @a path is locked, else set it to zero. */
svn_error_t *svn_wc_locked (svn_boolean_t *locked, 
                            const char *path,
                            apr_pool_t *pool);



/* Notification/callback handling. */

/**
 * @defgroup svn_wc_notifications notification callback handling
 * @{
 *
 * In many cases, the WC library will scan a working copy and making
 * changes. The caller usually wants to know when each of these changes
 * have been made, so that it can display some kind of notification to
 * the user.
 *
 * These notifications have a standard callback function type, which
 * takes the path of the file that was affected, and a caller-
 * supplied baton.
 *
 * Note that the callback is a 'void' return -- this is a simple
 * reporting mechanism, rather than an opportunity for the caller to
 * alter the operation of the WC library.
 */

/** The type of action occuring. */
typedef enum svn_wc_notify_action_t
{
  svn_wc_notify_add = 0,
  svn_wc_notify_copy,
  svn_wc_notify_delete,
  svn_wc_notify_restore,
  svn_wc_notify_revert,
  svn_wc_notify_failed_revert,
  svn_wc_notify_resolve,
  svn_wc_notify_status,
  svn_wc_notify_skip,

  /* The update actions are also used for checkouts, switches, and merges. */

  /** Got a delete in an update. */
  svn_wc_notify_update_delete,

  /** Got an add in an update. */
  svn_wc_notify_update_add,

  /** Got any other action in an update. */
  svn_wc_notify_update_update,

  /** The last notification in an update */
  svn_wc_notify_update_completed,

  /** About to update an external module, use for checkouts and switches too,
   * end with @c svn_wc_update_completed.
   */
  svn_wc_notify_update_external,

  svn_wc_notify_commit_modified,
  svn_wc_notify_commit_added,
  svn_wc_notify_commit_deleted,
  svn_wc_notify_commit_replaced,
  svn_wc_notify_commit_postfix_txdelta
} svn_wc_notify_action_t;


/** The type of notification that is occuring. */
typedef enum svn_wc_notify_state_t
{
  svn_wc_notify_state_inapplicable = 0,

  /** Notifier doesn't know or isn't saying. */
  svn_wc_notify_state_unknown,

  /** The state did not change. */
  svn_wc_notify_state_unchanged,

  /** Pristine state was modified. */
  svn_wc_notify_state_changed,

  /** Modified state had mods merged in. */
  svn_wc_notify_state_merged,

  /** Modified state got conflicting mods. */
  svn_wc_notify_state_conflicted

} svn_wc_notify_state_t;


/** Notify the world that @a action has happened to @a path.
 *
 * Notify the world that @a action has happened to @a path.  @a path is 
 * either absolute or relative to cwd (i.e., not relative to an anchor).
 *
 * @a kind, @a content_state and @a prop_state are from after @a action, 
 * not before.
 *
 * If @a mime_type is non-null, it indicates the mime-type of @a path.  It
 * is always @c NULL for directories.
 *
 * @a revision is @c SVN_INVALID_REVNUM, except when @a action is
 * @c svn_wc_notify_update_completed, in which case @a revision is 
 * the target revision of the update if available, else it is still
 * @c SVN_INVALID_REVNUM.
 *
 * Note that if @a action is @c svn_wc_notify_update, then @a path has 
 * already been installed, so it is legitimate for an implementation of
 * @c svn_wc_notify_func_t to examine @a path in the working copy.
 *
 * ### Design Notes:
 *
 * The purpose of the @a kind, @a mime_type, @a content_state, and 
 * @a prop_state fields is to provide "for free" information that this 
 * function is likely to want, and which it would otherwise be forced 
 * to deduce via expensive operations such as reading entries and 
 * properties.  However, if the caller does not have this information, 
 * it will simply pass the corresponding `*_unknown' values, and it is 
 * up to the implementation how to handle that (i.e., whether or not to
 * attempt deduction, or just to punt and give a less informative
 * notification).
 *
 * Recommendation: callers of @c svn_wc_notify_func_t should avoid
 * invoking it multiple times on the same @a path within a given
 * operation, and implementations should not bother checking for such
 * duplicate calls.  For example, in an update, the caller should not
 * invoke the notify func on receiving a prop change and then again
 * on receiving a text change.  Instead, wait until all changes have
 * been received, and then invoke the notify func once (from within
 * an @c svn_delta_editor_t's @c close_file(), for example), passing 
 * the appropriate @a content_state and @a prop_state flags.
 */
typedef void (*svn_wc_notify_func_t) (void *baton,
                                      const char *path,
                                      svn_wc_notify_action_t action,
                                      svn_node_kind_t kind,
                                      const char *mime_type,
                                      svn_wc_notify_state_t content_state,
                                      svn_wc_notify_state_t prop_state,
                                      svn_revnum_t revision);

/** @} */



/** A callback vtable invoked by our diff-editors, as they receive
 * diffs from the server.
 *
 * A callback vtable invoked by our diff-editors, as they receive
 * diffs from the server.  'svn diff' and 'svn merge' both implement
 * their own versions of this table.
 */
typedef struct svn_wc_diff_callbacks_t
{
  /** A file @a path has changed.
   *
   * A file @a path has changed.  The changes can be seen by comparing
   * @a tmpfile1 and @a tmpfile2, which represent @a rev1 and @a rev2 of 
   * the file, respectively.
   *
   * If known, the @c svn:mime-type value of each file is passed into
   * @a mimetype1 and @a mimetype2;  either or both of the values can
   * be NULL.  The implementor can use this information to decide if
   * (or how) to generate differences.
   *
   * @a adm_access will be an access baton for the directory containing 
   * @a path, or @c NULL if the diff editor is not using access batons.
   *
   * If @a state is non-null, set @a *state to the state of the file
   * contents after the operation has been performed.  (In practice,
   * this is only useful with merge, not diff; diff callbacks will
   * probably set @a *state to @c svn_wc_notify_state_unknown, since 
   * they do not change the state and therefore do not bother to know 
   * the state after the operation.)
   */
  svn_error_t *(*file_changed) (svn_wc_adm_access_t *adm_access,
                                svn_wc_notify_state_t *state,
                                const char *path,
                                const char *tmpfile1,
                                const char *tmpfile2,
                                svn_revnum_t rev1,
                                svn_revnum_t rev2,
                                const char *mimetype1,
                                const char *mimetype2,
                                void *diff_baton);

  /** A file @a path was added.
   *
   * A file @a path was added.  The contents can be seen by comparing
   * @a tmpfile1 and @a tmpfile2.
   *
   * If known, the @c svn:mime-type value of each file is passed into
   * @a mimetype1 and @a mimetype2;  either or both of the values can
   * be NULL.  The implementor can use this information to decide if
   * (or how) to generate differences.
   *
   * @a adm_access will be an access baton for the directory containing 
   * @a path, or @c NULL if the diff editor is not using access batons.
   */
  svn_error_t *(*file_added) (svn_wc_adm_access_t *adm_access,
                              const char *path,
                              const char *tmpfile1,
                              const char *tmpfile2,
                              const char *mimetype1,
                              const char *mimetype2,
                              void *diff_baton);
  
  /** A file @a path was deleted.
   *
   * A file @a path was deleted.  The [loss of] contents can be seen by
   * comparing @a tmpfile1 and @a tmpfile2.
   *
   * If known, the @c svn:mime-type value of each file is passed into
   * @a mimetype1 and @a mimetype2;  either or both of the values can
   * be NULL.  The implementor can use this information to decide if
   * (or how) to generate differences.
   *
   * @a adm_access will be an access baton for the directory containing 
   * @a path, or @c NULL if the diff editor is not using access batons.
   */
  svn_error_t *(*file_deleted) (svn_wc_adm_access_t *adm_access,
                                const char *path,
                                const char *tmpfile1,
                                const char *tmpfile2,
                                const char *mimetype1,
                                const char *mimetype2,
                                void *diff_baton);
  
  /** A directory @a path was added.
   *
   * A directory @a path was added.
   *
   * @a adm_access will be an access baton for the directory containing 
   * @a path, or @c NULL if the diff editor is not using access batons.
   */
  svn_error_t *(*dir_added) (svn_wc_adm_access_t *adm_access,
                             const char *path,
                             void *diff_baton);
  
  /** A directory @a path was deleted.
   *
   * A directory @a path was deleted.
   *
   * @a adm_access will be an access baton for the directory containing 
   * @a path, or @c NULL if the diff editor is not using access batons.
   */
  svn_error_t *(*dir_deleted) (svn_wc_adm_access_t *adm_access,
                               const char *path,
                               void *diff_baton);
  
  /** A list of property changes (@a propchanges) was applied to @a path.
   *
   * A list of property changes (@a propchanges) was applied to @a path.
   * The array is a list of (@c svn_prop_t) structures. 
   *
   * The original list of properties is provided in @a original_props,
   * which is a hash of @c svn_string_t values, keyed on the property
   * name.
   *
   * @a adm_access will be an access baton for the directory containing 
   * @a path, or @c NULL if the diff editor is not using access batons.
   *
   * If @a state is non-null, set @a *state to the state of the properties
   * after the operation has been performed.  (In practice, this is only 
   * useful with merge, not diff; diff callbacks will probably set @a *state 
   * to @c svn_wc_notify_state_unknown, since they do not change the state 
   * and therefore do not bother to know the state after the operation.)
   */
  svn_error_t *(*props_changed) (svn_wc_adm_access_t *adm_access,
                                 svn_wc_notify_state_t *state,
                                 const char *path,
                                 const apr_array_header_t *propchanges,
                                 apr_hash_t *original_props,
                                 void *diff_baton);

} svn_wc_diff_callbacks_t;


/* Asking questions about a working copy. */

/** Set @a *wc_format to @a path's working copy format version number if 
 * @a path is a valid working copy directory, else set it to 0.
 *
 * Set @a *wc_format to @a path's working copy format version number if 
 * @a path is a valid working copy directory, else set it to 0.  
 * Return error @c APR_ENOENT if @a path does not exist at all.
 */
svn_error_t *svn_wc_check_wc (const char *path,
                              int *wc_format,
                              apr_pool_t *pool);


/** Set @a *has_binary_prop to @c TRUE iff @a path has been marked 
 * with a property indicating that it is non-text (in other words, binary).
 * @a adm_access is an access baton set that contains @path.
 */
svn_error_t *svn_wc_has_binary_prop (svn_boolean_t *has_binary_prop,
                                     const char *path,
                                     svn_wc_adm_access_t *adm_access,
                                     apr_pool_t *pool);


/* Detecting modification. */

/** Set @a *modified_p to non-zero if @a filename's text is modified
 * with regard to the base revision, else set @a *modified_p to zero.
 *
 * Set @a *modified_p to non-zero if @a filename's text is modified
 * with regard to the base revision, else set @a *modified_p to zero.
 * @a filename is a path to the file, not just a basename. @a adm_access
 * must be an access baton for @a filename.
 *
 * If @a force_comparison is @c TRUE, this function will not allow
 * early return mechanisms that avoid actual content comparison.
 *
 * If @a filename does not exist, consider it unmodified.  If it exists
 * but is not under revision control (not even scheduled for
 * addition), return the error @c SVN_ERR_ENTRY_NOT_FOUND.  */
svn_error_t *svn_wc_text_modified_p (svn_boolean_t *modified_p,
                                     const char *filename,
                                     svn_boolean_t force_comparison,
                                     svn_wc_adm_access_t *adm_access,
                                     apr_pool_t *pool);


/** Set @a *modified_p to non-zero if @a path's properties are modified
 * with regard to the base revision, else set @a modified_p to zero.
 *
 * Set @a *modified_p to non-zero if @a path's properties are modified
 * with regard to the base revision, else set @a modified_p to zero. 
 * @a adm_access must be an access baton for @a path.
 */
svn_error_t *svn_wc_props_modified_p (svn_boolean_t *modified_p,
                                      const char *path,
                                      svn_wc_adm_access_t *adm_access,
                                      apr_pool_t *pool);




/** Administrative subdir.
 *
 * Ideally, this would be completely private to wc internals (in fact,
 * it used to be that @c adm_subdir() in adm_files.c was the only function
 * who knew the adm subdir's name).  However, import wants to protect
 * against importing administrative subdirs, so now the name is a
 * matter of public record.
 */
#define SVN_WC_ADM_DIR_NAME   ".svn"



/* Entries and status. */

/** The schedule states an entry can be in. */
typedef enum svn_wc_schedule_t
{
  /** Nothing special here */
  svn_wc_schedule_normal,

  /** Slated for addition */
  svn_wc_schedule_add,

  /** Slated for deletion */
  svn_wc_schedule_delete,

  /** Slated for replacement (delete + add) */
  svn_wc_schedule_replace

} svn_wc_schedule_t;


/** A working copy entry -- that is, revision control information about
 * one versioned entity.
 */
typedef struct svn_wc_entry_t
{
  /* IMPORTANT: If you extend this structure, check svn_wc_entry_dup to see
     if you need to extend that as well. */

  /* General Attributes */

  /** entry's name */
  const char *name;

  /** base revision */
  svn_revnum_t revision;

  /** url in repository */
  const char *url;

  /** canonical repository url */
  const char *repos;

  /** repository uuid */
  const char *uuid;

  /** node kind (file, dir, ...) */
  svn_node_kind_t kind;

  /* State information */

  /** scheduling (add, delete, replace ...) */
  svn_wc_schedule_t schedule;

  /** in a copied state */
  svn_boolean_t copied;

  /** deleted, but parent rev lags behind */
  svn_boolean_t deleted;

  /** copyfrom location */
  const char *copyfrom_url;

  /** copyfrom revision */
  svn_revnum_t copyfrom_rev;

  /** old version of conflicted file */
  const char *conflict_old;

  /** new version of conflicted file */
  const char *conflict_new;

  /** working version of conflicted file */
  const char *conflict_wrk;

  /** property reject file */
  const char *prejfile;

  /** last up-to-date time for text contents (0 means no information available)
   */
  apr_time_t text_time;

  /** last up-to-date time for properties (0 means no information available) */
  apr_time_t prop_time;

  /** base64-encoded checksum for the untranslated text base file,
   * can be @c NULL for backwards compatibility.
   */
  const char *checksum;

  /* "Entry props" */

  /** last revision this was changed */
  svn_revnum_t cmt_rev;

  /** last date this was changed */
  apr_time_t cmt_date;

  /** last commit author of this item */
  const char *cmt_author;

  /* IMPORTANT: If you extend this structure, check svn_wc_entry_dup to see
     if you need to extend that as well. */
} svn_wc_entry_t;


/** How an entries file's owner dir is named in the entries file. */
#define SVN_WC_ENTRY_THIS_DIR  "svn:this_dir"


/** Set @a *entry to an entry for @a path, allocated in the access baton 
 * pool.
 *
 * Set @a *entry to an entry for @a path, allocated in the access baton 
 * pool.  If @a show_deleted is true, return the entry even if it's in 
 * 'deleted' state.  If @a path is not under revision control, or if entry 
 * is 'deleted', not scheduled for re-addition, and @a show_deleted is
 * @c FALSE, then set @a *entry to @c NULL.
 *
 * @a *entry should not be modified, since doing so modifies the entries 
 * cache in @a adm_access without changing the entries file on disk.
 *
 * If @a path is not a directory then @a adm_access must be an access baton 
 * for the parent directory of @a path.  To avoid needing to know whether 
 * @a path is a directory or not, if @a path is a directory @a adm_access 
 * can still be an access baton for the parent of @a path so long as the 
 * access baton for @a path itself is in the same access baton set.
 *
 * Note that it is possible for @a path to be absent from disk but still
 * under revision control; and conversely, it is possible for @a path to
 * be present, but not under revision control.
 */
svn_error_t *svn_wc_entry (const svn_wc_entry_t **entry,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           svn_boolean_t show_deleted,
                           apr_pool_t *pool);


/** Parse the `entries' file for @a adm_access and return a hash @a entries.
 *
 * Parse the `entries' file for @a adm_access and return a hash @a entries, 
 * whose keys are (<tt>const char *</tt>) entry names and values are 
 * (<tt>svn_wc_entry_t *</tt>).  Allocate @a entries, and its keys and 
 * values, in @a pool.
 *  
 * Entries that are in a 'deleted' state (and not scheduled for
 * re-addition) are not returned in the hash, unless @a show_deleted is true.
 *
 * Important note: the @a entries hash is the entries cache in @a adm_access 
 * and so usually the hash itself, the keys and the values should be treated 
 * as read-only.  If any of these are modified then it is the callers
 * resposibility to ensure that the entries file on disk is updated.  Treat
 * the hash values as type (<tt>const svn_wc_entry_t *</tt>) if you wish to 
 * avoid accidental modification.  Modifying the schedule member is a
 * particularly bad idea, as the entries writing process relies on having
 * access to the original schedule.  Use a duplicate entry to modify the
 * schedule.
 *
 * Important note: only the entry structures representing files and
 * @c SVN_WC_ENTRY_THIS_DIR contain complete information.  The entry
 * structures representing subdirs have only the `kind' and `state'
 * fields filled in.  If you want info on a subdir, you must use this
 * routine to open its @a path and read the @c SVN_WC_ENTRY_THIS_DIR 
 * structure, or call @c svn_wc_get_entry on its @a path.
 */
svn_error_t *svn_wc_entries_read (apr_hash_t **entries,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_boolean_t show_deleted,
                                  apr_pool_t *pool);


/** Return a duplicate of @a entry, allocated in @a pool.
 *
 * Return a duplicate of @a entry, allocated in @a pool.  No part of the new
 * entry will be shared with @a entry.
 */
svn_wc_entry_t *svn_wc_entry_dup (const svn_wc_entry_t *entry,
                                  apr_pool_t *pool);


/** Given a @a dir_path under version control, decide if one of its
 * entries (@a entry) is in state of conflict.
 *
 * Given a @a dir_path under version control, decide if one of its
 * entries (@a entry) is in state of conflict; return the answers in
 * @a text_conflicted_p and @a prop_conflicted_p.  
 *
 * (If the entry mentions that a .rej or .prej exist, but they are
 * both removed, assume the conflict has been resolved by the user.)
 */
svn_error_t *svn_wc_conflicted_p (svn_boolean_t *text_conflicted_p,
                                  svn_boolean_t *prop_conflicted_p,
                                  const char *dir_path,
                                  const svn_wc_entry_t *entry,
                                  apr_pool_t *pool);

/** Set @a *url and @a *rev to the ancestor url and revision for @a path,
 * allocating in @a pool.
 *
 * Set @a *url and @a *rev to the ancestor url and revision for @a path,
 * allocating in @a pool.  @a adm_access must be an access baton for @a path. 
 *
 * If @a url or @a rev is null, then ignore it (just don't return the
 * corresponding information).
 */
svn_error_t *svn_wc_get_ancestry (char **url,
                                  svn_revnum_t *rev,
                                  const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool);


/** A callback vtable invoked by the generic entry-walker function. */
typedef struct svn_wc_entry_callbacks_t
{
  /* ### TODO: these callbacks should take pool args, so
     svn_wc_walk_entries() itself can do pool management.  Think
     editors.  (And then go adjust callers, such as
     invalidate_wcprop_for_entry and friends in ra.c.) */

  /** An @a entry was found at @a path. */
  svn_error_t *(*found_entry) (const char *path,
                               const svn_wc_entry_t *entry,
                               void *walk_baton);

  /* ### add more callbacks as new callers need them. */

} svn_wc_entry_callbacks_t;


/** A generic entry-walker.
 *
 * Do a recursive depth-first entry-walk beginning on @a path, which can
 * be a file or dir.  Call callbacks in @a walk_callbacks, passing
 * @a walk_baton to each.  Use @a pool for looping, recursion, and to
 * allocate all entries returned.  @a adm_access must be an access baton
 * for @a path.
 *
 * Like our other entries interfaces, entries that are in a 'deleted'
 * state (and not scheduled for re-addition) are not discovered,
 * unless @a show_deleted is true.
 *
 * When a new directory is entered, @c SVN_WC_ENTRY_THIS_DIR will always
 * be returned first.
 *
 * Note:  callers should be aware that each directory will be
 * returned *twice*:  first as an entry within its parent, and
 * subsequently as the '.' entry within itself.  The two calls can be
 * distinguished by looking for @c SVN_WC_ENTRY_THIS_DIR in the 'name'
 * field of the entry.
 */
svn_error_t *svn_wc_walk_entries (const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  const svn_wc_entry_callbacks_t 
                                                     *walk_callbacks,
                                  void *walk_baton,
                                  svn_boolean_t show_deleted,
                                  apr_pool_t *pool);


/** Mark missing @a path as 'deleted' in its @a parent's list of entries.
 *
 * Mark missing @a path as 'deleted' in its @a parent's list of entries.
 * Return @c SVN_ERR_WC_PATH_FOUND if @a path isn't actually missing.
 */
svn_error_t *svn_wc_mark_missing_deleted (const char *path,
                                          svn_wc_adm_access_t *parent,
                                          apr_pool_t *pool);
                       


/** 
 * @defgroup svn_wc_status working copy status.
 * @{
 *
 * We have two functions for getting working copy status: one function
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
 * So we have @c svn_wc_status() for depth 0, and @c svn_wc_statuses() for
 * depths 1 and 2, since the latter two involve multiple return
 * values.
 *
 * NOTE:  Status structures returned by @c svn_wc_status() or found in
 * the hash created by @c svn_wc_statuses() may contain a @c NULL ->entry
 * field.  This indicates an item that is not versioned in the working
 * copy.
 */

enum svn_wc_status_kind
{
    /** does not exist */
    svn_wc_status_none = 1,

    /** is not a versioned thing in this wc */
    svn_wc_status_unversioned,

    /** exists, but uninteresting. */
    svn_wc_status_normal,

    /** is scheduled for additon */
    svn_wc_status_added,

    /** under v.c., but is missing */
    svn_wc_status_absent,

    /** scheduled for deletion */
    svn_wc_status_deleted,

    /** was deleted and then re-added */
    svn_wc_status_replaced,

    /** text or props have been modified */
    svn_wc_status_modified,

    /** local mods received repos mods */
    svn_wc_status_merged,

    /** local mods received conflicting repos mods */
    svn_wc_status_conflicted,

    /** an unversioned resource is in the way of the versioned resource */
    svn_wc_status_obstructed
};

/** Structure for holding the "status" of a working copy item. 
 *
 * Structure for holding the "status" of a working copy item. 
 * The item's entry data is in @a entry, augmented and possibly shadowed
 * by the other fields.  @a entry is @c NULL if this item is not under
 * version control.
 */
typedef struct svn_wc_status_t
{
  /** Can be @c NULL if not under version control. */
  svn_wc_entry_t *entry;
  
  /** The status of the entries text. */
  enum svn_wc_status_kind text_status;

  /** The status of the entries properties. */
  enum svn_wc_status_kind prop_status;

  /** a directory can be 'locked' if a working copy update was interrupted. */
  svn_boolean_t locked;

  /** a file or directory can be 'copied' if it's scheduled for 
   * addition-with-history (or part of a subtree that is scheduled as such.).
   */
  svn_boolean_t copied;

  /** a file or directory can be 'switched' if the switch command has been 
   * used.
   */
  svn_boolean_t switched;

  /** The entry's text status in the repository. */
  enum svn_wc_status_kind repos_text_status;

  /** The entry's property status in the repository. */
  enum svn_wc_status_kind repos_prop_status;

} svn_wc_status_t;


/** Fill @a *status for @a path, allocating in @a pool, with the exception 
 * of the repos_rev field, which is normally filled in by the caller.
 *
 * Fill @a *status for @a path, allocating in @a pool, with the exception 
 * of the @c repos_rev field, which is normally filled in by the caller.
 * @a adm_access must be an access baton for @a path.
 *
 * Here are some things to note about the returned structure.  A quick
 * examination of the @c status->text_status after a successful return of
 * this function can reveal the following things:
 *
 *    - @c svn_wc_status_none : @a path is not versioned, and is either not
 *                              present on disk, or is ignored by svn's
 *                              default ignore regular expressions or the
 *                              svn:ignore property setting for @a path's
 *                              parent directory.
 *
 *    - @c svn_wc_status_absent : @a path is versioned, but is missing from
 *                                the working copy.
 *
 *    - @c svn_wc_status_unversioned : @a path is not versioned, but is
 *                                     present on disk and not being
 *                                     ignored (see above).  
 *
 * The other available results for the @c text_status field more
 * straightforward in their meanings.  See the comments on the
 * @c svn_wc_status_kind structure above for some hints.
 */
svn_error_t *svn_wc_status (svn_wc_status_t **status, 
                            const char *path, 
                            svn_wc_adm_access_t *adm_access,
                            apr_pool_t *pool);


/** Under @a path, fill @a statushash mapping paths to @c svn_wc_status_t
 * structures.
 *
 * Under @a path, fill @a statushash mapping paths to @c svn_wc_status_t
 * structures.  All fields in each struct will be filled in except for
 * @c repos_rev, which would presumably be filled in by the caller.
 * @a adm_access must be an access baton for @a path.
 *
 * @a path will usually be a directory, since for a regular file, you would
 * have used @c svn_wc_status().  However, it is no error if @a path is not
 * a directory; its status will simply be stored in @a statushash like
 * any other.
 *
 * Assuming @a path is a directory, then:
 * 
 * If @a get_all is false, then only locally-modified entries will be
 * returned.  If true, then all entries will be returned.
 *
 * If @a descend is false, @a statushash will contain statuses for @a path 
 * and its entries.  Else if @a descend is true, @a statushash will contain
 * statuses for @a path and everything below it, including
 * subdirectories.  In other words, a full recursion.
 *
 * If @a cancel_func is non-null, call it with @a cancel_baton while building 
 * the @a statushash to determine if the client has cancelled the operation.
 *
 * @a config is a hash mapping @c SVN_CONFIG_CATEGORY's to @c svn_config_t's.
 */
svn_error_t *svn_wc_statuses (apr_hash_t *statushash,
                              const char *path,
                              svn_wc_adm_access_t *adm_access,
                              svn_boolean_t descend,
                              svn_boolean_t get_all,
                              svn_boolean_t no_ignore,
                              svn_wc_notify_func_t notify_func,
                              void *notify_baton,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_hash_t *config,
                              apr_pool_t *pool);


/** Get a status @a editor and @a edit_baton for determining the changes 
 * that would occur to @a path upon updating to @a youngest.
 *
 * Set  @a *editor and @a *edit_baton to an editor that tweaks or adds
 * @c svn_wc_status_t structures to @a statushash to reflect repository
 * modifications that would be received on update, and that sets
 * @a *youngest to the youngest revision in the repository (the editor
 * also sets the @c repos_rev field in each @c svn_wc_status_t structure
 * to the same value).  @a adm_access must be an access baton for @a path.
 *
 * If @a descend is zero, then only immediate children of @a path will be
 * done, otherwise @a adm_access should be part of an access baton set
 * for the @a path hierarchy.
 *
 * If @a cancel_func is non-null, call it with @a cancel_baton periodically 
 * during the editor's drive to see if the drive should continue.
 *
 * Allocate the editor itself in @a pool, but the editor does temporary
 * allocations in a subpool of @a pool.
 */
svn_error_t *svn_wc_get_status_editor (const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *path,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_boolean_t descend,
                                       apr_hash_t *statushash,
                                       svn_revnum_t *youngest,
                                       svn_cancel_func_t cancel_func,
                                       void *cancel_baton,
                                       apr_pool_t *pool);

/** @} */


/** Copy @a src to @a dst_basename in @a dst_parent, and schedule 
 * @a dst_basename for addition to the repository, remembering the copy 
 * history.
 *
 * Copy @a src to @a dst_basename in @a dst_parent, and schedule 
 * @a dst_basename for addition to the repository, remembering the copy 
 * history.
 *
 * @a src must be a file or directory under version control; @a dst_parent
 * must be a directory under version control in the same working copy;
 * @a dst_basename will be the name of the copied item, and it must not
 * exist already.
 *
 * If @a cancel_func is non-null, call it with @a cancel_baton at
 * various points during the deletion.  If it returns an error
 * (typically SVN_ERR_CANCELLED), return that error immediately.
 *
 * For each file or directory copied, @a notify_func will be called
 * with its path and the @a notify_baton.  @a notify_func may be @c NULL 
 * if you are not interested in this information.
 *
 * Important: this is a variant of @c svn_wc_add.  No changes will happen
 * to the repository until a commit occurs.  This scheduling can be
 * removed with @c svn_client_revert.
 */
svn_error_t *svn_wc_copy (const char *src,
                          svn_wc_adm_access_t *dst_parent,
                          const char *dst_basename,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func_t notify_func,
                          void *notify_baton,
                          apr_pool_t *pool);


/** Schedule @a path for deletion, it will be deleted from the repository on
 * the next commit.
 *
 * Schedule @a path for deletion, it will be deleted from the repository on
 * the next commit.  If @a path refers to a directory, then a recursive
 * deletion will occur.  @a adm_access must hold a write lock for the parent 
 * of @a path.
 *
 * This function immediately deletes all files, modified and unmodified,
 * versioned and unversioned from the working copy. It also immediately
 * deletes unversioned directories and directories that are scheduled to be
 * added.  Only versioned directories will remain in the working copy,
 * these get deleted by the update following the commit.
 *
 * If @a cancel_func is non-null, call it with @a cancel_baton at
 * various points during the deletion.  If it returns an error
 * (typically SVN_ERR_CANCELLED), return that error immediately.
 *
 * For each path marked for deletion, @a notify_func will be called with
 * the @a notify_baton and that path. The @a notify_func callback may be
 * @c NULL if notification is not needed.
 */
svn_error_t *svn_wc_delete (const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_wc_notify_func_t notify_func,
                            void *notify_baton,
                            apr_pool_t *pool);


/** Put @a path under version control by adding an entry in its parent,
 * and, if @a path is a directory, adding an administrative area.
 *
 * Put @a path under version control by adding an entry in its parent,
 * and, if @a path is a directory, adding an administrative area.  The
 * new entry and anything under it is scheduled for addition to the
 * repository.  @a parent_access should hold a write lock for the parent
 * directory of @a path.  If @a path is a directory then an access baton 
 * for @a path will be added to the set containing @a parent_access.
 *
 * If @a path does not exist, return @c SVN_ERR_WC_PATH_NOT_FOUND.
 *
 * If @a copyfrom_url is non-null, it and @a copyfrom_rev are used as
 * `copyfrom' args.  This is for copy operations, where one wants
 * to schedule @a path for addition with a particular history.
 *
 * If @a cancel_func is non-null, call it with @a cancel_baton at
 * various points during the deletion.  If it returns an error
 * (typically SVN_ERR_CANCELLED), return that error immediately.
 *
 * When the @a path has been added, then @a notify_func will be called
 * (if it is not @c NULL) with the @a notify_baton and the path.
 *
 * Return @c SVN_ERR_WC_NODE_KIND_CHANGE if @a path is both an unversioned
 * directory and a file that is scheduled for deletion or in state deleted.
 *
 *<pre> ### This function currently does double duty -- it is also
 * ### responsible for "switching" a working copy directory over to a
 * ### new copyfrom ancestry and scheduling it for addition.  Here is
 * ### the old doc string from Ben, lightly edited to bring it
 * ### up-to-date, explaining the true, secret life of this function:</pre>
 *
 * Given a @a path within a working copy of type KIND, follow this algorithm:
 *
 *    - if @a path is not under version control:
 *       - Place it under version control and schedule for addition; 
 *         if @a copyfrom_url is non-null, use it and @a copyfrom_rev as
 *         'copyfrom' history
 *
 *    - if @a path is already under version control:
 *          (This can only happen when a directory is copied, in which
 *           case ancestry must have been supplied as well.)
 *
 *       -  Schedule the directory itself for addition with copyfrom history.
 *       -  Mark all its children with a 'copied' flag
 *       -  Rewrite all the URLs to what they will be after a commit.
 *       -  ### TODO:  remove old wcprops too, see the '###'below
 *
 *<pre> ### I think possibly the "switchover" functionality should be
 * ### broken out into a separate function, but its all intertwined in
 * ### the code right now.  Ben, thoughts?  Hard?  Easy?  Mauve?</pre>
 */
svn_error_t *svn_wc_add (const char *path,
                         svn_wc_adm_access_t *parent_access,
                         const char *copyfrom_url,
                         svn_revnum_t copyfrom_rev,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         apr_pool_t *pool);


/** Remove entry @a name in @a adm_access from revision control.
 *
 * Remove entry @a name in @a adm_access from revision control.  @a name 
 * must be either a file or @c SVN_WC_ENTRY_THIS_DIR.  @a adm_access must 
 * hold a write lock.
 *
 * If @a name is a file, all its info will be removed from @a adm_access's
 * administrative directory.  If @a name is @c SVN_WC_ENTRY_THIS_DIR, then
 * @a adm_access's entire administrative area will be deleted, along with
 * *all* the administrative areas anywhere in the tree below @a adm_access.
 *
 * Normally, only administrative data is removed.  However, if
 * @a destroy_wf is true, then all working file(s) and dirs are deleted
 * from disk as well.  When called with @a destroy_wf, any locally
 * modified files will *not* be deleted, and the special error
 * @c SVN_ERR_WC_LEFT_LOCAL_MOD might be returned.  (Callers only need to
 * check for this special return value if @a destroy_wf is true.)
 *
 * If @a cancel_func is non-null, call it with @a cancel_baton at
 * various points during the removal.  If it returns an error
 * (typically SVN_ERR_CANCELLED), return that error immediately.
 *
 * WARNING:  This routine is exported for careful, measured use by
 * libsvn_client.  Do *not* call this routine unless you really
 * understand what the heck you're doing.
 */
svn_error_t *
svn_wc_remove_from_revision_control (svn_wc_adm_access_t *adm_access,
                                     const char *name,
                                     svn_boolean_t destroy_wf,
                                     svn_cancel_func_t cancel_func,
                                     void *cancel_baton,
                                     apr_pool_t *pool);


/** Assuming @a path is under version control and in a state of conflict, 
 * then take @a path *out* of this state.
 *
 * Assuming @a path is under version control and in a state of conflict, 
 * then take @a path *out* of this state.  If @a resolve_text is true then 
 * any text conflict is resolved, if @a resolve_props is true then any 
 * property conflicts are resolved.  If @a recursive is true, then search
 * recursively for conflicts to resolve.
 *
 * @a adm_access is an access baton, with a write lock, for @a path.
 *
 * Needless to say, this function doesn't touch conflict markers or
 * anything of that sort -- only a human can semantically resolve a
 * conflict.  Instead, this function simply marks a file as "having
 * been resolved", clearing the way for a commit.  
 *
 * The implementation details are opaque, as our "conflicted" criteria
 * might change over time.  (At the moment, this routine removes the
 * three fulltext 'backup' files and any .prej file created in a conflict,
 * and modifies @a path's entry.)
 *
 * If @a path is not under version control, return @c SVN_ERR_ENTRY_NOT_FOUND.  
 * If @a path isn't in a state of conflict to begin with, do nothing, and
 * return @c SVN_NO_ERROR.
 *
 * If @c path was successfully taken out of a state of conflict, report this
 * information to @c notify_func (if non-@c NULL.)  If only text or only 
 * property conflict resolution was requested, and it was successful, then 
 * success gets reported.
 */
svn_error_t *svn_wc_resolve_conflict (const char *path,
                                      svn_wc_adm_access_t *adm_access,
                                      svn_boolean_t resolve_text,
                                      svn_boolean_t resolve_props,
                                      svn_boolean_t recursive,
                                      svn_wc_notify_func_t notify_func,
                                      void *notify_baton,
                                      apr_pool_t *pool);


/* Commits. */

/** Bump a successfully committed absolute @a path to @a new_revnum after a
 * commit succeeds.
 *
 * Bump a successfully committed absolute @a path to @a new_revnum after a
 * commit succeeds.  @a rev_date and @a rev_author are the (server-side)
 * date and author of the new revision; one or both may be @c NULL.
 * @a adm_access must hold a write lock appropriate for @a path.
 *
 * If non-null, @a wcprops is an array of <tt>svn_prop_t *</tt> changes to 
 * wc properties; if an @c svn_prop_t->value is null, then that property is
 * deleted.
 *
 * If @a recurse is true and @a path is a directory, then bump every
 * versioned object at or under @a path.  This is usually done for
 * copied trees.
 */
svn_error_t *svn_wc_process_committed (const char *path,
                                       svn_wc_adm_access_t *adm_access,
                                       svn_boolean_t recurse,
                                       svn_revnum_t new_revnum,
                                       const char *rev_date,
                                       const char *rev_author,
                                       apr_array_header_t *wcprop_changes,
                                       apr_pool_t *pool);




/** Traversal information from a working copy crawl or update.
 *
 * Traversal information is information gathered by a working copy
 * crawl or update.  For example, the before and after values of the
 * svn:externals property are important after an update, and since
 * we're traversing the working tree anyway (a complete traversal
 * during the initial crawl, and a traversal of changed paths during
 * the checkout/update/switch), it makes sense to gather the
 * property's values then instead of making a second pass.
 */
typedef struct svn_wc_traversal_info_t svn_wc_traversal_info_t;


/** Return a new, empty traversal info object, allocated in @a pool. */
svn_wc_traversal_info_t *svn_wc_init_traversal_info (apr_pool_t *pool);


/** Set @a *externals_old and @a *externals_new to hash tables representing
 * changes to values of the svn:externals property on directories
 * traversed by @a traversal_info.
 *
 * Set @a *externals_old and @a *externals_new to hash tables representing
 * changes to values of the svn:externals property on directories
 * traversed by @a traversal_info.
 *
 * @a traversal_info is obtained from @c svn_wc_init_traversal_info, but is
 * only useful after it has been passed through another function, such
 * as @c svn_wc_crawl_revisions, @c svn_wc_get_update_editor,
 * @c svn_wc_get_checkout_editor, @c svn_wc_get_switch_editor, etc.
 *
 * Each hash maps <tt>const char *</tt> directory names onto 
 * <tt>const char *</tt> values of the externals property for that directory.  
 * The dir names are full paths -- that is, anchor plus target, not target 
 * alone. The values are not parsed, they are simply copied raw, and are
 * never null: directories that acquired or lost the property are
 * simply omitted from the appropriate table.  Directories whose value
 * of the property did not change show the same value in each hash.
 *
 * The hashes, keys, and values have the same lifetime as @a traversal_info.
 */
void svn_wc_edited_externals (apr_hash_t **externals_old,
                              apr_hash_t **externals_new,
                              svn_wc_traversal_info_t *traversal_info);




/** Do a depth-first crawl in a working copy, beginning at @a path.
 *
 * Do a depth-first crawl in a working copy, beginning at @a path.
 * Communicate the `state' of the working copy's revisions to
 * @a reporter/@a report_baton.  Obviously, if @a path is a file instead 
 * of a directory, this depth-first crawl will be a short one.
 *
 * No locks are or logs are created, nor are any animals harmed in the
 * process.  No cleanup is necessary.  @a adm_access must be an access 
 * baton for the @a path hierarchy, it does not require a write lock.
 *
 * After all revisions are reported, @a reporter->finish_report() is
 * called, which immediately causes the RA layer to update the working
 * copy.  Thus the return value may very well reflect the result of
 * the update!
 *
 * If @a restore_files is true, then unexpectedly missing working files
 * will be restored from the administrative directory's cache. For each
 * file restored, the @a notify_func function will be called with the
 * @a notify_baton and the path of the restored file. @a notify_func may
 * be @c NULL if this notification is not required.
 *
 * If @a traversal_info is non-null, then record pre-update traversal
 * state in it.  (Caller should obtain @a traversal_info from
 * @c svn_wc_init_traversal_info.)
 */
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


/* Updates. */

/** Set @a *wc_root to @c TRUE if @a path represents a "working copy root".
 *
 * Set @a *wc_root to @c TRUE if @a path represents a "working copy root",
 * @c FALSE otherwise.  Use @a pool for any intermediate allocations.
 *
 * NOTE: Due to the way in which "WC-root-ness" is calculated, passing
 * a @a path of `.' to this function will always return @c TRUE.
 */
svn_error_t *svn_wc_is_wc_root (svn_boolean_t *wc_root,
                                const char *path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool);


/** Conditionally split @a path into an @a anchor and @a target for the 
 * purpose of updating and committing.
 *
 * Conditionally split @a path into an @a anchor and @a target for the 
 * purpose of updating and committing.
 *
 * @a anchor is the directory at which the update or commit editor
 * should be rooted.
 *
 * @a target is the actual subject (relative to the @a anchor) of the
 * update/commit, or @c NULL if the @a anchor itself is the subject.
 *
 * Allocate @a anchor and @a target in @a pool.  
 */
svn_error_t *svn_wc_get_actual_target (const char *path,
                                       const char **anchor,
                                       const char **target,
                                       apr_pool_t *pool);



/* Update and update-like functionality. */

/** Set @a *editor and @a *edit_baton to an editor and baton for updating a
 * working copy.
 *
 * Set @a *editor and @a *edit_baton to an editor and baton for updating a
 * working copy.
 *
 * If @a ti is non-null, record traversal info in @a ti, for use by
 * post-traversal accessors such as @c svn_wc_edited_externals().
 * 
 * @a anchor is an access baton, with a write lock, for the local path to the
 * working copy which will be used as the root of our editor.  Further
 * locks will be acquired if the update creates new directories.  All
 * locks, both those in @a anchor and newly acquired ones, will be released
 * when the editor driver calls @c close_edit.
 *
 * @a target is the entry in @a anchor that will actually be updated, or 
 * @c NULL if all of @a anchor should be updated.
 *
 * The editor invokes @a notify_func with @a notify_baton as the update
 * progresses, if @a notify_func is non-null.
 *
 * If @a cancel_func is non-null, the editor will invoke @a cancel_func with 
 * @a cancel_baton as the update progresses to see if it should continue.
 *
 * If @a diff3_cmd is non-null, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.
 *
 * @a target_revision is the repository revision that results from this set
 * of changes.
 */
svn_error_t *svn_wc_get_update_editor (svn_wc_adm_access_t *anchor,
                                       const char *target,
                                       svn_revnum_t target_revision,
                                       svn_boolean_t recurse,
                                       svn_wc_notify_func_t notify_func,
                                       void *notify_baton,
                                       svn_cancel_func_t cancel_func,
                                       void *cancel_baton,
                                       const char *diff3_cmd,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       svn_wc_traversal_info_t *ti,
                                       apr_pool_t *pool);


/** Like @c svn_wc_get_update_editor(), except for checkouts.
 *
 * Like @c svn_wc_get_update_editor(), except that:
 *
 * @a dest will be created as a working copy, if it does not exist
 * already.  It is not an error for it to exist; if it does, checkout
 * just behaves like update.
 *
 * It is the caller's job to make sure that @a dest is not some other
 * working copy, or that if it is, it will not be damaged by the
 * application of this delta.  The wc library tries to detect
 * such a case and do as little damage as possible, but makes no
 * promises.
 *
 * The editor invokes @a notify_func with @a notify_baton as the checkout
 * progresses, if @a notify_func is non-null.
 *
 * If @a cancel_func is non-null, it gets called, with @a cancel_baton as 
 * the checkout progresses, to determine if it should continue.
 *
 * @a ancestor_url is the repository string to be recorded in this
 * working copy.
 */
svn_error_t *svn_wc_get_checkout_editor (const char *dest,
                                         const char *ancestor_url,
                                         svn_revnum_t target_revision,
                                         svn_boolean_t recurse,
                                         svn_wc_notify_func_t notify_func,
                                         void *notify_baton,
                                         svn_cancel_func_t cancel_func,
                                         void *cancel_baton,
                                         const svn_delta_editor_t **editor,
                                         void **edit_baton,
                                         svn_wc_traversal_info_t *ti,
                                         apr_pool_t *pool);


/** Another variant of @c svn_wc_get_update_editor().
 *
 * Set @a *editor and @a *edit_baton to an editor and baton for "switching"
 * a working copy to a new @a switch_url.  (Right now, this URL must be
 * within the same repository that the working copy already comes
 * from.)  @a switch_url must not be @c NULL.
 *
 * If @a ti is non-null, record traversal info in @a ti, for use by
 * post-traversal accessors such as @c svn_wc_edited_externals().
 * 
 * @a anchor is an access baton, with a write lock, for the local path to the
 * working copy which will be used as the root of our editor.  Further
 * locks will be acquired if the switch creates new directories.  All
 * locks, both those in @a anchor and newly acquired ones, will be released
 * when the editor driver calls @c close_edit.
 *
 * @a target is the entry in @a anchor that will actually be updated, or 
 * @c NULL if all of @a anchor should be updated.
 *
 * The editor invokes @a notify_func with @a notify_baton as the switch
 * progresses, if @a notify_func is non-null.
 *
 * If @a cancel_func is non-null, it will be called with @a cancel_baton as 
 * the switch progresses to determine if it should continue.
 *
 * If @a diff3_cmd is non-null, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.
 *
 * @a target_revision is the repository revision that results from this set
 * of changes.
 */
svn_error_t *svn_wc_get_switch_editor (svn_wc_adm_access_t *anchor,
                                       const char *target,
                                       svn_revnum_t target_revision,
                                       const char *switch_url,
                                       svn_boolean_t recurse,
                                       svn_wc_notify_func_t notify_func,
                                       void *notify_baton,
                                       svn_cancel_func_t cancel_func,
                                       void *cancel_baton,
                                       const char *diff3_cmd,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       svn_wc_traversal_info_t *ti,
                                       apr_pool_t *pool);


/** Given a @a file_path already under version control, fully "install" a
 * @a new_revision of the file.
 *
 * Given a @a file_path already under version control, fully "install" a
 * @a new_revision of the file.  @a adm_access is an access baton with a 
 * write lock for the directory containing @a file_path.
 *
 * By "install", we mean: the working copy library creates a new
 * text-base and prop-base, merges any textual and property changes
 * into the working file, and finally updates all metadata so that the
 * working copy believes it has a new working revision of the file.
 * All of this work includes being sensitive to eol translation,
 * keyword substitution, and performing all actions using a journaled
 * logfile.
 *
 * The caller provides a @a new_text_path which points to a temporary
 * file containing the 'new' full text of the file at revision
 * @a new_revision.  This function automatically removes @a new_text_path
 * upon successful completion.  If there is no new text, then caller
 * must set @a new_text_path to @c NULL.
 *
 * The caller also provides the new properties for the file in the
 * @a props array; if there are no new props, then caller must pass 
 * @c NULL instead.  This argument is an array of @c svn_prop_t structures, 
 * and can be interpreted in one of two ways:
 *
 *    - if @a is_full_proplist is true, then the array represents the
 *      complete list of all properties for the file.  It is the new
 *      'pristine' proplist.
 *
 *    - if @a is_full_proplist is false, then the array represents a set of
 *      *differences* against the file's existing pristine proplist.
 *      (A deletion is represented by setting an @c svn_prop_t's 'value'
 *      field to @c NULL.)  
 *
 * Note that the @a props array is expected to contain all categories of
 * props, not just 'regular' ones that the user sees.  (See <tt>enum
 * svn_prop_kind</tt>).
 *
 * If @a content_state is non-null, set @a *content_state to the state of
 * the file contents after the installation; if return error, the
 * value of @a *content_state is undefined.
 *
 * If @a prop_state is non-null, set @a *prop_state to the state of the
 * properties after the installation; if return error, the value of
 * @a *prop_state is undefined.
 *
 * If @a new_url is non-null, then this URL will be attached to the file
 * in the 'entries' file.  Otherwise, the file will simply "inherit"
 * its URL from the parent dir.
 *
 * If @a diff3_cmd is non-null, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.
 *
 * @a pool is used for all bookkeeping work during the installation.
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
                                  const char *diff3_cmd,
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

/** List the properties of @a path and put them in @a props.
 *
 * Set @a *props to a hash table mapping <tt>char *</tt> names onto
 * <tt>svn_string_t *</tt> values for all the regular properties of 
 * @a path.  Allocate the table, names, and values in @a POOL.  If 
 * the node has no properties, an empty hash is returned.  @a adm_access
 * is an access baton set that contains @a path.
 */
svn_error_t *svn_wc_prop_list (apr_hash_t **props,
                               const char *path,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool);


/** Get the @a value of property @a name for @a path.
 *
 * Set @a *value to the value of property @a name for @a path, allocating
 * @a *value in @a pool.  If no such prop, set @a *value to @c NULL.  
 * @a name may be a regular or wc property; if it is an entry property, 
 * return the error @c SVN_ERR_BAD_PROP_KIND.  @a adm_access is an access
 * baton set that contains @a path.
 */
svn_error_t *svn_wc_prop_get (const svn_string_t **value,
                              const char *name,
                              const char *path,
                              svn_wc_adm_access_t *adm_access,
                              apr_pool_t *pool);

/** Set property @a name to @a value for @a path.
 *
 * Set property @a name to @a value for @a path.  Do any temporary
 * allocation in @a pool.  If @a name is not a valid property for @a path,
 * return @c SVN_ERR_ILLEGAL_TARGET.  If @a value is null, remove property
 * @a name.  @a adm_access must be an access baton with a write lock for 
 * @a path. 
 *
 * @a name may be a wc property or a regular property; but if it is an
 * entry property, return the error @c SVN_ERR_BAD_PROP_KIND.
 */
svn_error_t *svn_wc_prop_set (const char *name,
                              const svn_string_t *value,
                              const char *path,
                              svn_wc_adm_access_t *adm_access,
                              apr_pool_t *pool);


/** Return true iff @a name is a 'normal' property name.
 *
 * Return true iff @a name is a 'normal' property name.  'Normal' is
 * defined as a user-visible and user-tweakable property that shows up
 * when you fetch a proplist.
 *
 * The function currently parses the namespace like so:
 *
 *   - 'svn:wc:'  ==>  a wcprop, stored/accessed seperately via different API.
 *
 *   - 'svn:entry:' ==> an "entry" prop, shunted into the 'entries' file.
 *
 * If these patterns aren't found, then the property is assumed to be
 * Normal.
 */
svn_boolean_t svn_wc_is_normal_prop (const char *name);



/** Return true iff @a name is a 'wc' property name. */
svn_boolean_t svn_wc_is_wc_prop (const char *name);

/** Return true iff @a name is a 'entry' property name. */
svn_boolean_t svn_wc_is_entry_prop (const char *name);




/* Diffs */


/** Return an @a editor/@a edit_baton for diffing a working copy against the
 * repository.
 *
 * Return an @a editor/@a edit_baton for diffing a working copy against the
 * repository.
 *
 * @a anchor/@a target represent the base of the hierarchy to be compared.
 *
 * @a callbacks/@a callback_baton is the callback table to use when two
 * files are to be compared.
 *
 * @a recurse determines whether to descend into subdirectories when @a target
 * is a directory.  If @a recurse is @c TRUE then @a anchor should be part of 
 * an access baton set for the @a target hierarchy.
 *
 * If @a cancel_func is non-null, it will be used along with @a cancel_baton 
 * to periodically check if the client has canceled the operation.
 */
svn_error_t *svn_wc_get_diff_editor (svn_wc_adm_access_t *anchor,
                                     const char *target,
                                     const svn_wc_diff_callbacks_t *callbacks,
                                     void *callback_baton,
                                     svn_boolean_t recurse,
                                     svn_cancel_func_t cancel_func,
                                     void *cancel_baton,
                                     const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     apr_pool_t *pool);


/** Compare working copy against the text-base.
 *
 * Compare working copy against the text-base.
 *
 * @a anchor/@a target represent the base of the hierarchy to be compared.
 *
 * @a callbacks/@a callback_baton is the callback table to use when two
 * files are to be compared.
 *
 * @a recurse determines whether to descend into subdirectories when @a target
 * is a directory.  If @a recurse is @c TRUE then @a anchor should be part of 
 * an access baton set for the @a target hierarchy.
 */
svn_error_t *svn_wc_diff (svn_wc_adm_access_t *anchor,
                          const char *target,
                          const svn_wc_diff_callbacks_t *callbacks,
                          void *callback_baton,
                          svn_boolean_t recurse,
                          apr_pool_t *pool);

/** Get any changes to the properties of @a path.
 *
 * Given a @a path to a file or directory under version control, discover
 * any local changes made to properties and/or the set of 'pristine'
 * properties.  @a adm_access is an access baton set for @a path.
 *
 * If @a propchanges is non-@c NULL, return these changes as an array of
 * @c svn_prop_t structures stored in @a *propchanges.  The structures and
 * array will be allocated in @a pool.  If there are no local property
 * modifications on @a path, then set @a *propchanges to @c NULL.
 *
 * If @a original_props is non-@c NULL, then set @a *original_props to
 * hashtable (<tt>const char *name</tt> -> <tt>const svn_string_t *value</tt>)
 * that represents the 'pristine' property list of @a path.  This hashtable is
 * allocated in @a pool, and can be used to compare old and new values of
 * properties.
 */
svn_error_t *svn_wc_get_prop_diffs (apr_array_header_t **propchanges,
                                    apr_hash_t **original_props,
                                    const char *path,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool);



/** Determine the difference between two property hashes.
 *
 * Given two property hashes (const char *name -> const svn_string_t
 * *value), deduce the differences between them (from BASEPROPS ->
 * LOCALPROPS).  Return these changes as a series of svn_prop_t
 * structures stored in LOCAL_PROPCHANGES, allocated from POOL.
 *
 * For note, here's a quick little table describing the logic of this
 * routine:
 *
 *<pre> basehash        localhash         event
 * --------        ---------         -----
 * value = foo     value = NULL      Deletion occurred.
 * value = foo     value = bar       Set occurred (modification)
 * value = NULL value = baz Set occurred (creation)</pre>
 */
svn_error_t *
svn_wc_get_local_propchanges (apr_array_header_t **local_propchanges,
                              apr_hash_t *localprops,
                              apr_hash_t *baseprops,
                              apr_pool_t *pool);



/** The outcome of a merge carried out (or tried as a dry-run) by 
 * @c svn_wc_merge
 */
typedef enum svn_wc_merge_outcome_t
{
   /** The working copy is (or would be) unchanged.
    *
    * The working copy is (or would be) unchanged.  The changes to be
    * merged were already present in the working copy
    */
   svn_wc_merge_unchanged,

   /** The working copy has been (or would be) changed. */
   svn_wc_merge_merged,

   /** The working copy has been (or would be) changed, but there was (or
    * would be) a conflict
    */
   svn_wc_merge_conflict,

   /** No merge was performed, probably because the target file was
    * either absent or not under version control.
    */
   svn_wc_merge_no_merge

} svn_wc_merge_outcome_t;

/** Given paths to three fulltexts, merge the differences between @a left
 * and @a right into @a merge_target.
 *
 * Given paths to three fulltexts, merge the differences between @a left
 * and @a right into @a merge_target.  (It may help to know that @a left,
 * @a right, and @a merge_target correspond to "OLDER", "YOURS", and "MINE",
 * respectively, in the diff3 documentation.)  Use @a pool for any
 * temporary allocation.
 *
 * @a adm_access is an access baton with a write lock for the directory
 * containing @a merge_target.
 *
 * This function assumes that @a left and @a right are in repository-normal
 * form (linefeeds, with keywords contracted); if necessary,
 * @a merge_target is temporarily converted to this form to receive the
 * changes, then translated back again.
 *
 * If @a merge_target is absent, or present but not under version
 * control, then set @a *merge_outcome to svn_wc_merge_no_merge and
 * return success without merging anything.  (The reasoning is that if
 * the file is not versioned, then it is probably unrelated to the
 * changes being considered, so they should not be merged into it.)
 *
 * @a dry_run determines whether the working copy is modified.  When it
 * is @c FALSE the merge will cause @a merge_target to be modified, when it
 * is @c TRUE the merge will be carried out to determine the result but
 * @a merge_target will not be modified.
 *
 * If @a diff3_cmd is non-null, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.
 *
 * The outcome of the merge is returned in @a *merge_outcome. If there is
 * a conflict and @a dry_run is @c FALSE, then
 *
 *   * Put conflict markers around the conflicting regions in
 *     @a merge_target, labeled with @a left_label, @a right_label, and
 *     @a target_label.  (If any of these labels are @c NULL, default 
 *     values will be used.)
 * 
 *   * Copy @a left, @a right, and the original @a merge_target to unique 
 *     names in the same directory as @a merge_target, ending with the 
 *     suffixes ".LEFT_LABEL", ".RIGHT_LABEL", and ".TARGET_LABEL"
 *     respectively.
 *
 *   * Mark the entry for @a merge_target as "conflicted", and track the
 *     abovementioned backup files in the entry as well.
 *
 * Binary case:
 *
 *  If @a merge_target is a binary file, then no merging is attempted,
 *  the merge is deemed to be a conflict.  If @a dry_run is @c FALSE the
 *  working @a merge_target is untouched, and copies of @a left and 
 *  @a right are created next to it using @a left_label and @a right_label.
 *  @a merge_target's entry is marked as "conflicted", and begins
 *  tracking the two backup files.  If @a dry_run is @c TRUE no files are
 *  changed.  The outcome of the merge is returned in @a *merge_outcome.
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
                           const char *diff3_cmd,
                           apr_pool_t *pool);


/** Given a @a path under version control, merge an array of @a propchanges
 * into the path's existing properties.
 *
 * Given a @a path under version control, merge an array of @a propchanges
 * into the path's existing properties.  @a propchanges is an array of
 * @c svn_prop_t objects.  @a adm_access is an access baton for the directory
 * containing @a path.
 *
 * If @a base_merge is @c FALSE only the working properties will be changed,
 * if it is @c TRUE both the base and working properties will be changed.
 *
 * If @a state is non-null, set @a *state to the state of the properties
 * after the merge.
 *
 * If conflicts are found when merging working properties, they are
 * described in a temporary .prej file (or appended to an already-existing
 * .prej file), and the entry is marked "conflicted".  Base properties
 * are changed unconditionally, if @a base_merge is @c TRUE, they never result
 * in a conflict.
 */
svn_error_t *
svn_wc_merge_prop_diffs (svn_wc_notify_state_t *state,
                         const char *path,
                         svn_wc_adm_access_t *adm_access,
                         const apr_array_header_t *propchanges,
                         svn_boolean_t base_merge,
                         svn_boolean_t dry_run,
                         apr_pool_t *pool);



/** Given a @a path to a wc file, return a @a pristine_path which points to a
 * pristine version of the file.
 *
 * Given a @a path to a wc file, return a @a pristine_path which points to a
 * pristine version of the file.  This is needed so clients can do
 * diffs.  If the WC has no text-base, return a @c NULL instead of a
 * path.
 */
svn_error_t *svn_wc_get_pristine_copy_path (const char *path,
                                            const char **pristine_path,
                                            apr_pool_t *pool);


/** Recurse from @a path, cleaning up unfinished log business.
 *
 * Recurse from @a path, cleaning up unfinished log business.  Perform
 * necessary allocations in @a pool.  Any working copy locks under @a path 
 * will be taken over and then cleared by this function.
 *
 * WARNING: there is no mechanism that will protect locks that are still 
 * being used.
 *
 * If @a cancel_func is non-null, invoke it with @a cancel_baton at
 * various points during the operation.  If it returns an error
 * (typically SVN_ERR_CANCELLED), return that error immediately.
 */
svn_error_t *
svn_wc_cleanup (const char *path,
                svn_wc_adm_access_t *optional_adm_access,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool);


/** Revert changes to @a path (perhaps in a @a recursive fashion).
 *
 * Revert changes to @a path (perhaps in a @a recursive fashion).  Perform
 * necessary allocations in @a pool.
 *
 * @a parent_access is an access baton for the directory containing @a path,
 * unless @a path is a wc root, in which case @a parent_access refers to 
 * @a path itself.
 *
 * If @cancel_func is non-null, call it with @a cancel_baton at
 * various points during the reversion process.  If it returns an
 * error (typically SVN_ERR_CANCELLED), return that error
 * immediately.
 *
 * For each item reverted, @a notify_func will be called with @a notify_baton
 * and the path of the reverted item. @a notify_func may be @c NULL if this
 * notification is not needed.
 */
svn_error_t *
svn_wc_revert (const char *path, 
               svn_wc_adm_access_t *parent_access,
               svn_boolean_t recursive, 
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func_t notify_func,
               void *notify_baton,
               apr_pool_t *pool);



/* Authentication files */

/** Get the @a *contents of @a filename in the authentcation area of @a path's
 * administrative directory, allocated in @a pool.
 *
 * Get the @a *contents of @a filename in the authentcation area of @a path's
 * administrative directory, allocated in @a pool.  @a path must be a
 * working copy directory. If file does not exist,
 * @c SVN_ERR_WC_PATH_NOT_FOUND is returned.
 *
 * Note: @a contents is a stringbuf because maybe we'll need to fetch
 * binary contents from an auth file.  If that's unlikely, then we
 * should change it to <tt>const char *</tt>.
 */
svn_error_t *
svn_wc_get_auth_file (const char *path,
                      const char *filename,
                      svn_stringbuf_t **contents,
                      apr_pool_t *pool);


/** Store a file named @a filename with @a contents in the authentication
 * area of @a adm_access's administrative directory.
 *
 * Store a file named @a filename with @a contents in the authentication
 * area of @a adm_access's administrative directory. If no such file
 * exists, it will be created.  If the file exists already, it will
 * be completely overwritten with the new contents.  If @a recurse is
 * true, this file will be stored in every administrative area below
 * @a adm_access as well. 
 *
 * Note: @a contents is a stringbuf because maybe we'll need to store
 * binary contents in an auth file.  If that's unlikely, then we
 * should change it to <tt>const char *</tt>.
 */
svn_error_t *
svn_wc_set_auth_file (svn_wc_adm_access_t *adm_access,
                      svn_boolean_t recurse,
                      const char *filename,
                      svn_stringbuf_t *contents,
                      apr_pool_t *pool);


/** Set @a *provider and @ *provider_baton to an authentication
 *  provider of type @c svn_auth_cred_simple_t that gets/sets
 *  information from a working copy directory. 
 *  
 *   This provider requires certain run-time parameters be present in
 *   the auth_baton:
 *
 *    - a working copy directory to read/write to.
 *             (@c SVN_AUTH_PARAM_SIMPLE_WC_WCDIR)
 *
 *    - if available, an access baton for the directory.
 *             (@c SVN_AUTH_PARAM_SIMPLE_WC_ACCESS)
 *
 *  Additionally, if a default username or password is available, this
 *  provider will honor them as well, and return them when @c
 *  svn_auth_first_credentials is called.  (see @c
 *  SVN_AUTH_PARAM_DEFAULT_USERNAME and @c SVN_AUTH_PARAM_DEFAULT_PASSWORD).
 */
void 
svn_wc_get_simple_provider (const svn_auth_provider_t **provider,
                            void **provider_baton,
                            apr_pool_t *pool);


/** Set @a *provider and @ *provider_baton to an authentication
 *  provider of type @c svn_auth_cred_username_t that gets/sets
 *  information from a working copy directory. 
 *  
 *   This provider requires certain run-time parameters be present in
 *   the auth_baton:
 *
 *    - a working copy directory to read/write to.
 *             (@c SVN_AUTH_PARAM_SIMPLE_WC_WCDIR)
 *
 *    - if available, an access baton for the directory.
 *             (@c SVN_AUTH_PARAM_SIMPLE_WC_ACCESS)
 *
 *  Additionally, if a default username is available, this provider
 *  will honor it, and return it when @c svn_auth_first_credentials is
 *  called.  (see @c SVN_AUTH_PARAM_DEFAULT_USERNAME). 
 */
void 
svn_wc_get_username_provider (const svn_auth_provider_t **provider,
                              void **provider_baton,
                              apr_pool_t *pool);


/** The specialized runtime parameters required by the simple wc
    provider; see svn_auth.h for more explanation. */
#define SVN_AUTH_SIMPLE_WC_PARAM_PREFIX SVN_AUTH_PARAM_PREFIX "wc:"
#define SVN_AUTH_PARAM_SIMPLE_WC_WCDIR SVN_AUTH_SIMPLE_WC_PARAM_PREFIX "wcdir"
#define SVN_AUTH_PARAM_SIMPLE_WC_ACCESS SVN_AUTH_SIMPLE_WC_PARAM_PREFIX "access"



/* Tmp files */

/** Create a unique temporary file in administrative tmp/ area of
 * directory @a path.
 *
 * Create a unique temporary file in administrative tmp/ area of
 * directory @a path.  Return a handle in @a *fp.
 *
 * The flags will be <tt>APR_WRITE | APR_CREATE | APR_EXCL</tt> and
 * optionally @c APR_DELONCLOSE (if the @a delete_on_close argument is 
 * set @c TRUE).
 *
 * This means that as soon as @a fp is closed, the tmp file will vanish.
 */
svn_error_t *
svn_wc_create_tmp_file (apr_file_t **fp,
                        const char *path,
                        svn_boolean_t delete_on_close,
                        apr_pool_t *pool);



/* Eol conversion and keyword expansion. */

/** Set @a *xlated_p to a path to a possibly translated copy of versioned
 * file @a vfile, or to @a vfile itself if no translation is necessary.
 *
 * Set @a *xlated_p to a path to a possibly translated copy of versioned
 * file @a vfile, or to @a vfile itself if no translation is necessary.
 * That is, if @a vfile's properties indicate newline conversion or
 * keyword expansion, point @a *xlated_p to a copy of @a vfile whose
 * newlines are unconverted and keywords contracted, in whatever
 * manner is indicated by @a vfile's properties; otherwise, set @a *xlated_p
 * to @a vfile.
 *
 * If @a force_repair is set, the translated file will have any
 * inconsistent line endings repaired.  This should only be used when
 * the resultant file is being created for comparison against @a vfile's
 * text base.
 *
 * Caller is responsible for detecting if they are different (pointer
 * comparison is sufficient), and for removing @a *xlated_p if
 * necessary.
 *
 * This function is generally used to get a file that can be compared
 * meaningfully against @a vfile's text base.
 *
 * If @a *xlated_p is different from @a vfile, then choose @a *xlated_p's 
 * name using @c svn_io_open_unique_file() with @c SVN_WC__TMP_EXT, and 
 * allocate it in @a pool.  Also use @a pool for any temporary allocation.
 *
 * If an error is returned, the effect on @a *xlated_p is undefined.
 */
svn_error_t *svn_wc_translated_file (const char **xlated_p,
                                     const char *vfile,
                                     svn_wc_adm_access_t *adm_access,
                                     svn_boolean_t force_repair,
                                     apr_pool_t *pool);



/* Text/Prop Deltas Using an Editor */


/** Send the local modifications for versioned file @a path (with
 * matching @a file_baton) through @a editor, then close @a file_baton
 * afterwards.
 *
 * Send the local modifications for versioned file @a path (with
 * matching @a file_baton) through @a editor, then close @a file_baton
 * afterwards.  Use @a pool for any temporary allocation and
 * @a adm_access as an access baton for @a path.
 * 
 * This process creates a copy of @a path with keywords and eol
 * untranslated.  If @a tempfile is non-null, set @a *tempfile to the 
 * path to this copy.  Do not clean up the copy; caller can do that.  
 * (The purpose of handing back the tmp copy is that it is usually about 
 * to become the new text base anyway, but the installation of the new
 * text base is outside the scope of this function.)
 *
 * If @a fulltext, send the untranslated copy of @a path through @a editor 
 * as full-text; else send it as svndiff against the current text base.
 *
 * If sending a diff, and the recorded checksum for @a path's text-base
 * does not match the current actual checksum, then remove the tmp
 * copy (and set @a *tempfile to null if appropriate), and return the
 * error @c SVN_ERR_WC_CORRUPT_TEXT_BASE.
 *
 * Note: this is intended for use with both infix and postfix
 * text-delta styled editor drivers.
 */
svn_error_t *svn_wc_transmit_text_deltas (const char *path,
                                          svn_wc_adm_access_t *adm_access,
                                          svn_boolean_t fulltext,
                                          const svn_delta_editor_t *editor,
                                          void *file_baton,
                                          const char **tempfile,
                                          apr_pool_t *pool);


/** Given a @a path with its accompanying @a entry, transmit all local 
 * property modifications using the appropriate @a editor method (in 
 * conjunction with @a baton). @a adm_access is an access baton set
 * that contains @a path.
 *
 * Given a @a path with its accompanying @a entry, transmit all local 
 * property modifications using the appropriate @a editor method (in 
 * conjunction with @a baton).  Use @a pool for all allocations.
 *
 * If a temporary file remains after this function is finished, the
 * path to that file is returned in @a *tempfile (so the caller can 
 * clean this up if it wishes to do so).
 */
svn_error_t *svn_wc_transmit_prop_deltas (const char *path,
                                          svn_wc_adm_access_t *adm_access,
                                          const svn_wc_entry_t *entry,
                                          const svn_delta_editor_t *editor,
                                          void *baton,
                                          const char **tempfile,
                                          apr_pool_t *pool);


/** Get the run-time configured list of ignore patterns from the 
 * @c svn_config_t's in the @a config hash, and store them in @a *patterns.
 * Allocate @a *patterns and its contents in @a pool.
 */
svn_error_t *svn_wc_get_default_ignores (apr_array_header_t **patterns,
                                         apr_hash_t *config,
                                         apr_pool_t *pool);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_WC_H */
