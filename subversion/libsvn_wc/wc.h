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




/*** Asking questions about a working copy. ***/

/* Set *ANSWER to non-zero iff PATH appears to be a working copy. 
   The return error would usually be ignored, but you can examine it
   to find out exactly why *ANSWER is what it is, if you want. */
svn_error_t *svn_wc__working_copy_p (int *answer,
                                     svn_string_t *path,
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

/* The files within the administrative subdir. */
#define SVN_WC__ADM_FORMAT              "format"
#define SVN_WC__ADM_README              "README"
#define SVN_WC__ADM_REPOSITORY          "repository"
#define SVN_WC__ADM_VERSIONS            "versions"
#define SVN_WC__ADM_PROPERTIES          "properties"
#define SVN_WC__ADM_DELTA_HERE          "delta-here"
#define SVN_WC__ADM_LOCK                "lock"
#define SVN_WC__ADM_TMP                 "tmp"
#define SVN_WC__ADM_TEXT_BASE           "text-base"
#define SVN_WC__ADM_PROP_BASE           "prop-base"
#define SVN_WC__ADM_DPROP_BASE          "dprop-base"

/* The directory that does bookkeeping during an operation. */
#define SVN_WC__ADM_DOING               "doing"
#define SVN_WC__ADM_DOING_ACTION        SVN_WC__ADM_DOING  "action"
#define SVN_WC__ADM_DOING_FILES         SVN_WC__ADM_DOING  "files"
#define SVN_WC__ADM_DOING_STARTED       SVN_WC__ADM_DOING  "started"
#define SVN_WC__ADM_DOING_FINISHED      SVN_WC__ADM_DOING  "finished"

/* Return a string containing the admin subdir name. */
svn_string_t *svn_wc__adm_subdir (apr_pool_t *pool);


/* Make `PATH/<adminstrative_subdir>/THING'. */
svn_error_t *svn_wc__make_adm_thing (svn_string_t *path,
                                     char *thing,
                                     int type,
                                     apr_pool_t *pool);


/* Open `PATH/<adminstrative_subdir>/FNAME'.  *HANDLE must be NULL, as
   with apr_open(). */
svn_error_t *svn_wc__open_adm_file (apr_file_t **handle,
                                    svn_string_t *path,
                                    char *fname,
                                    apr_int32_t flags,
                                    apr_pool_t *pool);


/* Close `PATH/<adminstrative_subdir>/FNAME'.  The only reason this
   takes PATH and FNAME is so any error will have the correct path. */
svn_error_t *svn_wc__close_adm_file (apr_file_t *fp,
                                     svn_string_t *path,
                                     char *fname,
                                     apr_pool_t *pool);

/* Remove `PATH/<adminstrative_subdir>/THING'. 
   kff todo: just using it for files, not dirs, at the moment. */
svn_error_t *svn_wc__remove_adm_thing (svn_string_t *path,
                                       char *thing,
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
svn_error_t *svn_wc__ensure_prepare_wc (svn_string_t *path,
                                        svn_string_t *repository,
                                        svn_vernum_t version,
                                        apr_pool_t *pool);


/* Ensure that an administrative area exists for PATH, so that PATH is
 * a working copy subdir.
 *
 * Sets *EXISTS_ALREADY to non-zero iff PATH was already a working
 * copy (i.e., it had an adm area).
 *
 * Does not ensure existence of PATH itself; if PATH does not exist,
 * an error will result. 
 */
svn_error_t *svn_wc__ensure_adm (svn_string_t *path,
                                 svn_string_t *repository,
                                 svn_vernum_t version,
                                 int *exists_already,
                                 apr_pool_t *pool);


/*** Stuff that knows about the working copy XML formats. ***/

/* Initial contents of `versions' for a new adm area. */
svn_string_t *svn_wc__versions_init_contents (svn_vernum_t version,
                                              apr_pool_t *pool);


/*** The working copy unwind stack. ***/

/* Unwindable actions. */
#define SVN_WC__UNWIND_UPDATE "update"  /* no args, use for checkouts too */
#define SVN_WC__UNWIND_MV     "mv"      /* takes SRC and DST args */
#define SVN_WC__UNWIND_MERGE  "merge"   /* takes SRC and DST args */


/* Push an action on the top of the `unwind' stack. 
 * PATH means we're talking about the `PATH/SVN/unwind' file.
 * ACTION is the tag name to push.
 * ATTS are attributes to the action; kff todo: may want a slightly
 * more structured interface when discover similarities among pushes. 
 */
svn_error_t *svn_wc__push_unwind (svn_string_t *path,
                                  const char *action,
                                  const char **atts,
                                  apr_pool_t *pool);

/* Pop (and execute) the action on the top of the `unwind' stack.
 *
 * If ACTION is non-null, then the top item on the stack must match
 * that action -- if it doesn't, the error SVN_ERR_WC_UNWIND_MISMATCH
 * is returned and the top item is not popped.
 *
 * If DEFAULT_TO_DONE is non-zero, than it will not be an error for
 * the top item on the stack to appear to have already been done.
 * This always means it *has* already been done, but that things
 * spooked before the unwind stack could be adjusted.  Such items are
 * treated as a no-op and popped anyway.  Remaining pops should not
 * interpret such an situation as innocent, of course.
 *
 * (For actions which have no operands, the DEFAULT_TO_DONE argument
 * is ignored.)
 *
 * If *EMPTY_STACK is non-null, it gets set to non-zero if the stack
 * is empty _after_ the pop.  (Popping an unwind stack that's already
 * empty is an error, SVN_ERR_WC_UNWIND_EMPTY).
 */
svn_error_t *svn_wc__pop_unwind (svn_string_t *path,
                                 const char *action,
                                 int default_to_done,
                                 int *empty_stack,
                                 apr_pool_t *pool);


/* Unwind the entire stack.
   kff todo: it may be that pop_unwind() should be static & hidden,
   and only ever called from unwind_all(). */
svn_error_t *svn_wc__unwind_all (svn_string_t *path,
                                 apr_pool_t *pool);


/* Sets *ISEMPTY to non-zero iff the unwind stack for PATH is empty.
   Does not affect the stack in any way. */
svn_error_t *svn_wc__unwind_empty_p (svn_string_t *path,
                                     int *isempty,
                                     apr_pool_t *pool);


/*** General utilities that may get moved upstairs at some point. */
svn_error_t *svn_wc__ensure_directory (svn_string_t *path, apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

