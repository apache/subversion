/*
 * hashdump-test.c :  testing the reading/writing of hashes
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



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"


/* Global variables required by included main() */
apr_pool_t *pool = NULL;


/* Our own global variables */
apr_hash_t *proplist, *new_proplist;
svn_string_t *key;
apr_file_t *f = NULL;     /* init to NULL very important! */
apr_status_t err;

char *review =
"A forthright entrance, yet coquettish on the tongue, its deceptively\n"
"fruity exterior hides the warm mahagony undercurrent that is the\n"
"hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will\n"
"be pleased to note the familiar, subtle hints of mulberries and\n"
"carburator fluid.  Its confident finish is marred only by a barely\n"
"detectable suggestion of rancid squid ink.";




static int
test1()
{
  apr_status_t result;

  /* Build a hash in memory, and fill it with test data. */
  proplist = apr_make_hash (pool);

  key = svn_string_create ("color", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("red", pool));
  
  key = svn_string_create ("wine review", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create (review, pool));
  
  key = svn_string_create ("price", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("US $6.50", pool));

  /* Test overwriting: same key both times, but different values. */
  key = svn_string_create ("twice-used property name", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("This is the FIRST value.", pool));
  apr_hash_set (proplist, key->data, key->len,
               svn_string_create ("This is the SECOND value.", pool));

  /* Dump the hash to a file. */
  apr_open (&f, "hashdump.out",
            (APR_WRITE | APR_CREATE),
            APR_OS_DEFAULT, pool);

  result = svn_hash_write (proplist, svn_unpack_bytestring, f);

  apr_close (f);

  return ((int) result);
}




static int
test2()
{
  apr_status_t result;

  new_proplist = apr_make_hash (pool);

  apr_open (&f, "hashdump.out", APR_READ, APR_OS_DEFAULT, pool);

  result = svn_hash_read (&new_proplist, svn_pack_bytestring, f, pool);

  apr_close (f);

  return ((int) result);
}



static int
test3()
{
  apr_hash_index_t *this;
  int err;
  int found_discrepancy = 0;

  /* Build a hash in global variable "proplist", then write to a file. */
  err = test1();
  if (err)
    return err;

  /* Read this file back into global variable "new_proplist" */
  err = test2();
  if (err)
    return err;

  /* Now let's make sure that proplist and new_proplist contain the
     same data. */
  
  /* Loop over our original hash */
  for (this = apr_hash_first (proplist); this; this = apr_hash_next (this))
    {
      const void *key;
      size_t keylen;
      void *val;
      svn_string_t *orig_str, *new_str;
      
      /* Get a key and val. */
      apr_hash_this (this, &key, &keylen, &val);
      orig_str = (svn_string_t *) val;

      /* Look up the key in the new hash */
      new_str = (svn_string_t *) apr_hash_get (new_proplist, key, keylen);

      /* Does the new hash contain the key at all? */
      if (new_str == NULL)
        found_discrepancy = 1;

      /* Do the two strings contain identical data? */
      else if (! svn_string_compare (orig_str, new_str))
        found_discrepancy = 1;
    }


  return found_discrepancy;
}




/*
   ====================================================================
   If you add a new test to this file, update these two arrays.

*/

/* An array of all test functions */
int (*test_funcs[])() = 
{
  NULL,
  test1,
  test2,
  test3,
  NULL
};


/* Descriptions of each test we can run */
char *descriptions[] = 
{
  NULL,
  "test 1: write a hash to a file",
  "test 2: read a file into a hash",
  "test 3: write hash out, read back in, compare",
  NULL
};




/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
