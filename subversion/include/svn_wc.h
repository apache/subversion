/*
 * svn_wc.h :  public interface for the Subversion Working Copy Library
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


/* Structure containing the "status" of a working copy dirent.  

   Note that this overlaps somewhat with the private declaration of an
   "entry" in wc.h; so there's a bit of redundancy going on.  But so
   far, it hasn't made sense to completely contain one structure in
   another.  I mean, entry structs don't want "modified_p" or
   "repos_ver" fields, and status structs don't want full xml
   attribute hashes.  :) 
*/
typedef struct svn_wc__status_t 
{
  svn_vernum_t local_ver;        /* working copy version number */
  svn_vernum_t repos_ver;        /* repository version number */
  
  /* MUTUALLY EXCLUSIVE states. One of
     these will always be set. */
  enum                           
  {
    svn_wc_status_none = 1,
    svn_wc_status_added,
    svn_wc_status_deleted,
    svn_wc_status_modified,
    svn_wc_status_conflicted
    
  }  flag;

  /* For the future: we can place information in here about ancestry
     "sets". */

} svn_wc__status_t;


/* Given a PATH to a working copy file or dir, return a STATUS
   structure describing it.  All fields will be filled in _except_ for
   the field containing the current repository version; this will be
   filled in by svn_client_status(), the primary caller of this
   routine. */
svn_error_t *
svn_wc_get_status (svn_wc__status_t **status,
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

/* Update working copy PATH with NEW_VERSION after a commit has succeeded.
 * TARGETS is a hash of files/dirs that actually got committed --
 * these are the only ones who we can write log items for, and whose
 * version numbers will get set.  todo: eventually this hash will be
 * of the sort used by svn_wc__compose_paths(), as with all entries
 * recursers.
 */
svn_error_t *
svn_wc_close_commit (svn_string_t *path,
                     svn_vernum_t new_version,
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
 * TARGET_VERSION is the repository version that results from this set
 * of changes.
 *
 * EDITOR, EDIT_BATON, and DIR_BATON are all returned by reference,
 * and the latter two should be used as parameters to editor
 * functions.
 */
svn_error_t *svn_wc_get_update_editor (svn_string_t *dest,
                                       svn_vernum_t target_version,
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
                                         svn_vernum_t target_version,
                                         const svn_delta_edit_fns_t **editor,
                                         void **edit_baton,
                                         apr_pool_t *pool);



/*** Generic diffing and patching (tentative). ***/

/* Users of the working copy library can define custom diff/merge
   functions to preserve local mods across updates.  The working copy
   library itself only knows how to do GNU diff and patch on text
   files. */

/* kff todo: yes, ppl can define these, but it's not yet clear where
   these functions will get plugged in.  For now, the wc library will
   define one versatile pair of functions, one taking an actual system
   command to produce the diff, and another the command to apply the
   diff.  If that turns out to be sufficient, then the interface below
   can become strictly internal to libsvn_wc. */

/* Compute the diff between SRC and TARGET, store the result in
 * *RESULT, which should be the USER_DATA for a matching call to
 * svn_wc_patcher().
 */
typedef svn_error_t *svn_wc_diff_fn_t (void **result,
                                       svn_string_t *src,
                                       svn_string_t *target,
                                       apr_pool_t *pool);

/* Apply the diff in USER_DATA to SRC to obtain TARGET. */
typedef svn_error_t *svn_wc_patch_fn_t (void *user_data,
                                        svn_string_t *src,
                                        svn_string_t *target,
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

/* kff todo: does nothing and returns SVN_NO_ERROR, currently. */
/* Return local value of PROPNAME for the file or directory PATH. */
svn_error_t *svn_wc_get_path_prop (svn_string_t **value,
                                   svn_string_t *propname,
                                   svn_string_t *path);

/* kff todo: does nothing and returns SVN_NO_ERROR, currently. */
/* Return local value of PROPNAME for the directory entry PATH. */
svn_error_t *svn_wc_get_dirent_prop (svn_string_t **value,
                                     svn_string_t *propname,
                                     svn_string_t *path);

#endif  /* SVN_WC_H */

/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
