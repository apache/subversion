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


#ifndef SVN_LIBSVN_WC_H
#define SVN_LIBSVN_WC_H

#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define SVN_WC__DIFF_EXT      ".diff"
#define SVN_WC__TMP_EXT       ".tmp"
#define SVN_WC__TEXT_REJ_EXT  ".rej"
#define SVN_WC__PROP_REJ_EXT  ".prej"
#define SVN_WC__BASE_EXT      ".svn-base"




/*** Update traversals. ***/

struct svn_wc_traversal_info_t
{
  /* The pool in which this structure and everything inside it is
     allocated. */
  apr_pool_t *pool;

  /* The before and after values of the SVN_PROP_EXTERNALS property,
   * for each directory on which that property changed.  These have
   * the same layout as those returned by svn_wc_edited_externals(). 
   *
   * The hashes, their keys, and their values are allocated in the
   * above pool.
   */
  apr_hash_t *externals_old;
  apr_hash_t *externals_new;
};



/*** Timestamps. ***/

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
svn_error_t *svn_wc__ensure_directory (const char *path, apr_pool_t *pool);

/* Take out a write-lock, stealing an existing lock if one exists.  This
   function avoids the potential race between checking for an existing lock
   and creating a lock. The cleanup code uses this function, but stealing
   locks is not a good idea because the code cannot determine whether a
   lock is still in use. Try not to write any more code that requires this
   feature. */
svn_error_t *svn_wc__adm_steal_write_lock (svn_wc_adm_access_t **adm_access,
                                           const char *path, apr_pool_t *pool);



/* ### Should this definition go into lock.c?  At present it is visible so
   ### that users can access the path member, we could provide an access
   ### function.  There is one place that directly access the lock_exists
   ### member as well. */
struct svn_wc_adm_access_t
{
   /* PATH to directory which contains the administrative area */
   const char *path;

   enum svn_wc__adm_access_type {

      /* SVN_WC__ADM_ACCESS_UNLOCKED indicates no lock is held allowing
         read-only access without cacheing. */
      svn_wc__adm_access_unlocked,

#if 0 /* How cacheing might work one day */

      /* ### If read-only operations are allowed sufficient write access to
         ### create read locks (did you follow that?) then entries cacheing
         ### could apply to read-only operations as well.  This would
         ### probably want to fall back to unlocked access if the
         ### filesystem permissions prohibit writing to the administrative
         ### area (consider running svn_wc_status on some other user's
         ### working copy). */

      /* SVN_WC__ADM_ACCESS_READ_LOCK indicates that read-only access and
         cacheing are allowed. */
      svn_wc__adm_access_read_lock,
#endif

      /* SVN_WC__ADM_ACCESS_WRITE_LOCK indicates that read-write access and
         cacheing are allowed. */
      svn_wc__adm_access_write_lock,

      /* SVN_WC__ADM_ACCESS_CLOSED indicates that the baton has been
         closed. */
      svn_wc__adm_access_closed

   } type;

   /* LOCK_EXISTS is set TRUE when the write lock exists */
   svn_boolean_t lock_exists;

#if 0 /* How cacheing might work one day */

   /* ENTRIES_MODIFED is set TRUE when the entries cached in ENTRIES have
      been modified from the original values read from the file. */
   svn_boolean_t entries_modified;

   /* Once the 'entries' file has been read, ENTRIES will cache the
      contents if this access baton has an appropriate lock. Otherwise
      ENTRIES will be NULL. */
   apr_hash_t *entries;
#endif

   /* PARENT access baton, may be NULL. */
   svn_wc_adm_access_t *parent;

   /* CHILDREN is a hash of svn_wc_adm_access_t* keyed on char*
      representing the path to sub-directories that are also locked. */
   apr_hash_t *children;

   /* POOL is used to allocate cached items, they need to persist for the
      lifetime of this access baton */
   apr_pool_t *pool;

};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

