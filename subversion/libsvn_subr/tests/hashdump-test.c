/*
 * hashdump-test.c :  testing the reading/writing of hashes
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
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
test1 (const char **msg)
{
  apr_status_t result;

  *msg = "write a hash to a file";

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
test2 (const char **msg)
{
  apr_status_t result;

  *msg = "read a file into a hash";

  new_proplist = apr_make_hash (pool);

  apr_open (&f, "hashdump.out", APR_READ, APR_OS_DEFAULT, pool);

  result = svn_hash_read (new_proplist, svn_pack_bytestring, f, pool);

  apr_close (f);

  return ((int) result);
}



static int
test3 (const char **msg)
{
  apr_hash_index_t *this;
  int err;
  int found_discrepancy = 0;
  const char *ignored;

  *msg = "write hash out, read back in, compare";

  /* Build a hash in global variable "proplist", then write to a file. */
  err = test1 (&ignored);
  if (err)
    return err;

  /* Read this file back into global variable "new_proplist" */
  err = test2 (&ignored);
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
   If you add a new test to this file, update this array.

*/

/* An array of all test functions */
int (*test_funcs[])(const char **msg) =
{
  NULL,
  test1,
  test2,
  test3,
  NULL
};



/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
