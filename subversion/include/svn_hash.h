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
 * software developed by CollabNet (http://www.Collab.Net/)."
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




#ifndef SVN_HASH_H
#define SVN_HASH_H

/*----------------------------------------------------*/

/*** Reading/writing hashtables to disk ***/

/* svn_hash_read() and svn_hash_write() each take a "helper" routine
   to encode/decode hash values. */

/*  Read a hash table from a file.
 *  Input:  a hash, a "pack" function, an opened file pointer, a pool
 *  Returns:  error status
 *
 *     The "pack" routine should take a specific-length bytestring and
 *     return a pointer to something meant to be stored in the hash.
 *
 *     The hash should be ready to receive key/val pairs.
 */
apr_status_t svn_hash_read (apr_hash_t *hash, 
                            void *(*pack_func) (size_t len, const char *val,
                                                apr_pool_t *pool),
                            apr_file_t *srcfile,
                            apr_pool_t *pool);

/*  Dump a hash table to a file.
 *  Input:  a hash, an "unpack" function (see above), an opened file pointer
 *  Returns:  error status
 *
 *     The "unpack" routine knows how to convert a hash value into a
 *     printable bytestring of a certain length.
 */
apr_status_t svn_hash_write (apr_hash_t *hash, 
                             apr_size_t (*unpack_func) (char **unpacked_data,
                                                        void *val),
                             apr_file_t *destfile);



/*** Helper routines specific to Subversion proplists. ***/

/* A helper for hash_write(): 
 * Input:   a hash value which points to an svn_string_t
 * Returns: the size of the svn_string_t, and (by indirection) the
 *          string data itself 
 */
apr_size_t svn_unpack_bytestring (char **returndata, void *value);

/* A helper for hash_read():
 * Input:   some bytes, a length, a pool
 * Returns: an svn_string_t containing them, to store as a hash value.
 *
 * Just copies the pointer, does not duplicate the data!
 */
void *svn_pack_bytestring (size_t len, const char *val, apr_pool_t *pool);


/*----------------------------------------------------*/

/*** Converting a hash into a sorted array ***/

/* FIXME: this may go away in a moment, please ignore it for now. */
int svn_sort_compare_as_paths (const void *obj1, const void *obj2);

#ifndef apr_hash_sorted_keys

/* Grab the keys (and values) in apr_hash HT and return them in an a
   sorted apr_array_header_t ARRAY allocated from POOL.  The array
   will contain pointers of type (apr_item_t *).  */
apr_array_header_t *
apr_hash_sorted_keys (apr_hash_t *ht,
                      int (*comparison_func) (const void *, const void *),
                      apr_pool_t *pool);
#endif /* apr_hash_sorted_keys */

#endif /* SVN_HASH_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
