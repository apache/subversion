/*
 * wc.h :  shared stuff internal to the svn_wc library.
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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
#include "svn_error.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define SVN_WC__TMP_EXT       ".tmp"
#define SVN_WC__TEXT_REJ_EXT  ".rej"
#define SVN_WC__PROP_REJ_EXT  ".prej"
#define SVN_WC__BASE_EXT      ".svn-base" /* for text and prop bases */
#define SVN_WC__WORK_EXT      ".svn-work" /* for working propfiles */




/* We can handle this format or anything lower, and we (should) error
 * on anything higher.
 *
 * There is no format version 0; we started with 1.
 *
 * The change from 1 to 2 was the introduction of SVN_WC__WORK_EXT.
 * For example, ".svn/props/foo" became ".svn/props/foo.svn-work".
 *
 * The change from 2 to 3 was the introduction of the entry attribute
 * SVN_WC__ENTRY_ATTR_ABSENT.
 *
 * The change from 3 to 4 was the renaming of the magic "svn:this_dir"
 * entry name to "".
 *
 * Please document any further format changes here.
 */
#define SVN_WC__VERSION       4


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
#define SVN_WC__TIMESTAMP_WC   "working"



/*** Names and file/dir operations in the administrative area. ***/

/* kff todo: namespace-protecting these #defines so we never have to
   worry about them conflicting with future all-caps symbols that may
   be defined in svn_wc.h. */

/** The files within the administrative subdir. **/
#define SVN_WC__ADM_FORMAT              "format"
#define SVN_WC__ADM_README              "README.txt"
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
#define SVN_WC__ADM_EMPTY_FILE          "empty-file"


/* The basename of the ".prej" file, if a directory ever has property
   conflicts.  This .prej file will appear *within* the conflicted
   directory.  */
#define SVN_WC__THIS_DIR_PREJ           "dir_conflicts"



/* A few declarations for stuff in util.c.
 * If this section gets big, move it all out into a new util.h file. */

/* Ensure that DIR exists. */
svn_error_t *svn_wc__ensure_directory (const char *path, apr_pool_t *pool);

/* Baton for svn_wc__compat_call_notify_func below. */
typedef struct svn_wc__compat_notify_baton_t {
  /* Wrapped func/baton. */
  svn_wc_notify_func_t func;
  void *baton;
} svn_wc__compat_notify_baton_t;

/* Implements svn_wc_notify_func2_t.  Call BATON->func (BATON is of type
   svn_wc__compat_notify_baton_t), passing BATON->baton and the appropriate
   arguments from NOTIFY. */
void svn_wc__compat_call_notify_func (void *baton,
                                      const svn_wc_notify_t *notify,
                                      apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_H */
