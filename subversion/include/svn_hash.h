/*
 * svn_hash.h :  dumping and reading hash tables to/from files.
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
 * software developed by CollabNet (http://www.CollabNet/)."
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
 * individuals on behalf of CollabNet.
 */




#ifndef SVN_HASH_H
#define SVN_HASH_H


/* Generalized routines for reading/writing hashtables to disk.
 *  
 * Each takes a "helper" routine which can encode/decode a hash value.
 *
 */

apr_status_t hash_read (apr_hash_t **hash, 
                       void *(*pack_func) (size_t len, const char *val,
                                           apr_pool_t *pool),
                       apr_file_t *srcfile,
                       apr_pool_t *pool);

apr_status_t hash_write (apr_hash_t *hash, 
                        apr_size_t (*unpack_func) (char **unpacked_data,
                                                  void *val),
                        apr_file_t *destfile);


/* Helper routines specific to Subversion proplists;  
 *                 passed to hash_read() and hash_write().
 *
 *  (Subversion's proplists are hashes whose "values" are pointers to
 *  svn_string_t objects.)
 */

apr_size_t svn_unpack_bytestring (char **returndata, void *value);

void * svn_pack_bytestring (size_t len, const char *val, apr_pool_t *pool);




#endif /* SVN_HASH_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
