/* id.h : interface to node ID functions, private to libsvn_fs
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
 * individuals on behalf of Collab.Net.
 */

#ifndef SVN_LIBSVN_FS_ID_H
#define SVN_LIBSVN_FS_ID_H

#include "apr.h"
#include "apr_pools.h"
#include "svn_fs.h"


enum {
  /* Recognize id strings with the `.head' suffix, as found in the
     nodes table.  */
  svn_fs__key_id = 1,
};


/* Parse the LEN bytes at DATA as a node ID.  Return zero if the bytes
   are not a properly-formed node ID.

   A well-formed ID has the form "[0-9](\.[0-9])*".  In the `nodes'
   table, we also use ID's of the form "[0-9](\.[0-9])*\.head", to
   help us find the most recent version of a node.

   If FLAGS is zero, accept only the first form of ID.

   If FLAGS & svn_fs__key_id is non-zero, also accept the second form,
   with the `.head' suffix.  Represent the `.head' element in the
   returned array as a -2.

   Allocate the parsed ID in POOL.  As a special case for the Berkeley
   DB comparison function, if POOL is zero, malloc the ID.  It's
   generally better to use a pool if you can.  */
svn_fs_id_t *svn_fs__parse_id (char *data, apr_size_t len, int flags,
			       apr_pool_t *pool);


/* Return a Subversion string containing the unparsed form of the node
   id ID.  Allocate the buffer for the unparsed form in POOL.  */
svn_string_t *svn_fs__unparse_id (svn_fs_id_t *id,
				  apr_pool_t *pool);



#endif /* SVN_LIBSVN_FS_ID_H */

