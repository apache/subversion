/*
 * svn_fs.h :  interface to the Subversion filesystem
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



#ifndef SVN_FS_H
#define SVN_FS_H

#include "svn_types.h"
#include "svn_error.h"

/* An object representing a Subversion filesystem.  */
typedef struct svn_fs svn_fs_t;

/* Create a new filesystem object in POOL.  It doesn't refer to any
   actual database yet; you need to invoke svn_fs_open or
   svn_fs_create on it for that to happen.  */
extern svn_fs_t *svn_fs_new (apr_pool_t *pool);

/* The type of a warning callback function.  BATON is the value specified
   in the call to `svn_fs_set_warning_func'; the filesystem passes it through
   to the callback.  FMT is a printf-style format string, which tells us
   how to interpret any successive arguments.  */
typedef void (svn_fs_warning_callback_t) (void *baton, const char *fmt, ...);

/* Provide a callback function, WARNING, that FS should use to report
   warning messages.  To print a warning message, the filesystem will
   call WARNING, passing it BATON, a printf-style format string, and
   any further arguments as appropriate for the format string.

   If it's acceptable to print messages on stderr, then the function
   `svn_handle_warning', declared in "svn_error.h", would be a
   suitable warning function.

   By default, this is set to a function that will crash the process.
   Dumping to stderr or /dev/tty is not acceptable default behavior
   for server processes, since those may both be equivalent to
   /dev/null.  */
extern void svn_fs_set_warning_func (svn_fs_t *fs,
				     svn_fs_warning_callback_t *warning,
				     void *warning_baton);

/* Create a new, empty Subversion filesystem, stored in a Unix file or
   directory named PATH, and make FS refer to it.  PATH must be
   absolute.  FS provides the memory pool, warning function, etc.  */
extern svn_error_t *svn_fs_newfs (svn_fs_t *fs, const char *path);

/* Set *FS to point to a Subversion filesystem object representing the
   Subverision filesystem stored in the Unix file or directory named
   by PATH.  PATH must be an absolute filename, referring to a file or
   directory created by `svn_fs_newfs'.  Do all allocation for *FS in
   a subpool of POOL.  */
extern svn_error_t *svn_fs_open (svn_fs_t **fs, const char *path,
				 apr_pool_t *pool);

#endif /* SVN_FS_H */
