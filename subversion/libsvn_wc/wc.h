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




/* Lock the working copy administrative area.
   Wait for WAIT seconds if encounter another lock, trying again every
   second, then return 0 if success or an SVN_ERR_ENCOUNTERED_LOCK
   error if failed to obtain the lock. */
svn_error_t *svn_wc__lock (svn_string_t *path, int wait, apr_pool_t *pool);
svn_error_t *svn_wc__unlock (svn_string_t *path, apr_pool_t *pool);

/* Return temporary working name based on PATH.
   For a given PATH, the working name is the same every time. */
svn_string_t *svn_wc__working_name (svn_string_t *path, apr_pool_t *pool);

/* Create DIR as a working copy directory. */
svn_error_t *svn_wc__set_up_new_dir (svn_string_t *path,
                                     svn_string_t *ancestor_path,
                                     svn_vernum_t ancestor_vernum,
                                     apr_pool_t *pool);



/* kff todo: these #defines have to be protected as though they're in
   the global namespace, right?  Because they'd silently override any
   other #define with the same name. */

/* The files within the administrative subdir. */
#define SVN_WC__ADM_REPOSITORY          "repository"
#define SVN_WC__ADM_VERSIONS            "versions"
#define SVN_WC__ADM_PROPERTIES          "properties"
#define SVN_WC__ADM_TREE_EDITS          "tree-edits"
#define SVN_WC__ADM_PROP_EDITS          "prop-edits"
#define SVN_WC__ADM_LOCK                "lock"
#define SVN_WC__ADM_TMP                 "tmp"
#define SVN_WC__ADM_TEXT_BASE           "text-base"
#define SVN_WC__ADM_PROP_BASE           "prop-base"

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



/*** General utilities that may get moved upstairs at some point. */
svn_error_t *svn_wc__ensure_directory (svn_string_t *path, apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

