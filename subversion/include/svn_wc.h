/*
 * svn_wc.h :  public interface for the Subversion Working Copy Library
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
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

svn_error_t *svn_wc_rename (svn_string_t *src, svn_string_t *dst);
svn_error_t *svn_wc_copy   (svn_string_t *src, svn_string_t *dst);
svn_error_t *svn_wc_add    (apr_array_header_t *paths);
svn_error_t *svn_wc_delete (apr_array_header_t *paths);


/* Apply a delta to a working copy, or to create a working copy.
 * 
 * If ENCASING_DIR exists and is a working copy, then it must be a
 * working copy for the delta we're applying.  If it is, it gets
 * massaged into the updated state, else just error out.
 *
 * If ENCASING_DIR does not exist, a working copy is created there.
 *
 * If ENCASING_DIR exists but is not a working copy, return error.
 *
 * ENCASING_DIR may not be NULL; we need to know where to put this
 * working copy.
 *
 * ENCASING_DIR can be ".", of course.
 *
 * (The reason for these rules is that a tree delta might include
 * loose files in its top level -- tree deltas are not always nicely
 * packaged single directories.  So we want an interface that insists
 * on being told where to put this potentially messy delta.)
 *
 * (kff todo: thinking on this whole question of checkouts vs updates.
 * It sounds nice in theory to treat them identically, but it may not
 * be working out that way... anyway, ignore the documentation above
 * and what you see in libsvn_wc, things may change when I get back
 * from lunch.)
 */
svn_error_t *svn_wc_apply_delta (void *delta_src,
                                 svn_delta_read_fn_t *delta_stream_reader,
                                 svn_string_t *encasing_dir,
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

/* Return local value of PROPNAME for the file or directory PATH. */
svn_error_t *svn_wc_get_path_prop (svn_string_t **value,
                                   svn_string_t *propname,
                                   svn_string_t *path);

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
