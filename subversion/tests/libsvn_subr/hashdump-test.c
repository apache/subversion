/*
 * hashdump-test.c :  testing the reading/writing of hashes
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



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"



int
main (void)
{
  ap_pool_t *pool = NULL;
  ap_hash_t *proplist, *new_proplist;
  svn_string_t *key;
  ap_file_t *f = NULL;     /* init to NULL very important! */
  ap_status_t err;

  /* Our longest piece of test data. */
  char *review =
    "A forthright entrance, yet coquettish on the tongue, its deceptively\n"
    "fruity exterior hides the warm mahagony undercurrent that is the\n"
    "hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will\n"
    "be pleased to note the familiar, subtle hints of mulberries and\n"
    "carburator fluid.  Its confident finish is marred only by a barely\n"
    "detectable suggestion of rancid squid ink.";

  /* Set up APR */

  ap_initialize ();
  ap_create_pool (&pool, NULL);


  /* Build a hash in memory, and fill it with test data. */

  proplist = ap_make_hash (pool);

  key = svn_string_create ("color", pool);
  ap_hash_set (proplist, key->data, key->len,
               svn_string_create ("red", pool));
  
  key = svn_string_create ("wine review", pool);
  ap_hash_set (proplist, key->data, key->len,
               svn_string_create (review, pool));
  
  key = svn_string_create ("price", pool);
  ap_hash_set (proplist, key->data, key->len,
               svn_string_create ("US $6.50", pool));

  /* Test overwriting: same key both times, but different values. */
  key = svn_string_create ("twice-used property name", pool);
  ap_hash_set (proplist, key->data, key->len,
               svn_string_create ("This is the FIRST value.", pool));
  ap_hash_set (proplist, key->data, key->len,
               svn_string_create ("This is the SECOND value.", pool));


  /* Dump the hash to a file. */
  ap_open (&f, "hashdump.out", (APR_WRITE | APR_CREATE), APR_OS_DEFAULT, pool);
  hash_write (proplist, svn_unpack_bytestring, f);
  ap_close (f);


  /* Now create a new hash, and read the file back into it. */

  new_proplist = ap_make_hash (pool);

  ap_open (&f, "hashdump.out", APR_READ, APR_OS_DEFAULT, pool);
  err = hash_read (&new_proplist, svn_pack_bytestring, f, pool);
  ap_close (f);
  
  if (err)
    {
      svn_error_t *readerr = svn_create_error (err, NULL, 
                                               "hash_read() failed.",
                                               NULL, pool);
      svn_handle_error (readerr, stderr);
    }


  /* Now dump the new hash into a SECOND file; an external script will
     compare the two output files.  */

  ap_open (&f, "hashdump2.out", (APR_WRITE | APR_CREATE),APR_OS_DEFAULT, pool);
  hash_write (new_proplist, svn_unpack_bytestring, f);
  ap_close (f);


  /* Clean up and exit */
  ap_destroy_pool (pool);
  return 0;
}





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
