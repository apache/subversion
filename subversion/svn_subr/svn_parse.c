/* svn_parse:  shared parsing routines for reading config files
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
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */



#include <svn_types.h>
#include <svn_error.h>
#include <apr_pools.h>
#include <apr_hash.h>


/* 
   Input:  a filename and pool

   Output: a pointer to a hash of hashes, all built within the pool

   This routine parses a file which conforms to the standard
   Subversion config file format (look in notes/).  

   The hash returned is a mapping from section-names to hash pointers;
   each hash contains the keys/vals for each section.  All
   section-names, keys and vals are stored as bytestrings pointers.
   (These bytestrings are allocated in the same pool as the hashes.)

   This routine makes no attempt to understand the sections, keys or
   values.  :) 
*/


ap_hash_t *
svn_parse (svn_string_t *filename, ap_pool_t *pool)
{
  ap_hash_t *uberhash;      /* our hash of hashes */
  ap_hash_t *current_hash;  /* the hash we're currently storing vals in */

  uberhash = ap_make_hash (pool);

  /* now loop through the file (using apr file routines?)...

     each time we find a comment line, break back to top of loop;

     each time we find a new section, place the section name in
     a bytestring called "new_section".  Then: */
  {
    ap_hash_t new_section_hash = ap_make_hash (pool);  /* create new hash */

    current_hash = new_section_hash;  /* make this the "active" hash */

    /* store this new hash in our uberhash */
    
    ap_hash_set (uberhash, 
                 new_section,         /* key: ptr to new_section bytestring */
                 sizeof(svn_string_t),/* the length of the key */
                 new_section_hash);   /* val: ptr to the new hash */
  }

  /* 
     each time we find a key/val pair, put them in bytestrings called
     "new_key" and "new_val", then:
  */
  {
    ap_hash_set (current_hash,
                 new_key,
                 sizeof(svn_string_t),
                 new_val);
  }

  return uberhash;
}







/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
