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



/* Where you see an argument like
 * 
 *   apr_array_header_t *paths
 *
 * it means an array of (svn_string_t *) types, each one of which is
 * a file or directory path.  This is so we can do atomic operations
 * on any random set of files and directories.
 */

/* kff todo: these do nothing and return SVN_NO_ERROR right now. */
svn_error_t *svn_wc_rename (svn_string_t *src, svn_string_t *dst);
svn_error_t *svn_wc_copy   (svn_string_t *src, svn_string_t *dst);
svn_error_t *svn_wc_add    (apr_array_header_t *paths);
svn_error_t *svn_wc_delete (apr_array_header_t *paths);


/* Apply a delta to a working copy, or to create a working copy.
 * 
 * If DEST is non-null, its existence as a directory is ensured (i.e.,
 * it is created if it does not already exist), and it will be
 * prepended to every path the delta causes to be touched.
 *
 * It is the caller's job to make sure that DEST is not some other
 * working copy, or that if it is, it will not be damaged by the
 * application of this delta.  svn_wc_apply_delta() tries to detect
 * such a case and do as little damage as possible, but makes no
 * promises.
 *
 * REPOS is the repository string to be recorded by this working
 * copy.
 *
 * kff todo: Actually, REPOS is one of several possible non-delta-ish
 * things that may need to get passed to svn_wc_apply_delta() so it
 * can create new administrative subdirs.  Other things might be
 * username and/or auth info, which aren't necessarily included in the
 * repository string.
 *
 * Another way might have been to first call svn_wc_create(), or
 * somesuch, and then run apply_delta() on the resulting fresh working
 * copy skeleton -- in other words, apply_delta() wouldn't know
 * anything about where the delta came from, it would just re-use
 * information already present in the working copy, and it would never
 * be applied to anything but a working copy.  But then all sorts of
 * sanity checks would get harder, because apply_delta() couldn't
 * easily check that the delta it is applying is really right for the
 * working copy, and conversely, it would be more difficult to check
 * out a subdir from one repository into the working copy of another.
 * Also, when what's being checked out is a directory, it's nice to
 * make that be the tip-top of the working copy (assuming no loose
 * top-level files are encountered in the delta), and we can't do that
 * if we always have to apply to an existing working copy.
 *
 * Thinking out loud here, as you can see.  */
svn_error_t *svn_wc_apply_delta (void *delta_src,
                                 svn_delta_read_fn_t *delta_stream_reader,
                                 svn_string_t *dest,
                                 svn_string_t *repos,
                                 apr_pool_t *pool);


#if 0
/* Will have to think about the interface here a bit more. */

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
