/*
 * hashdump-test.c :  testing the reading/writing of hashes
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
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


/* Convert an apr_status_t into an svn_error_t.  */
static svn_error_t *
check (apr_status_t status, apr_pool_t *pool)
{
  if (status != APR_SUCCESS)
    return svn_error_create (status, 0, 0, pool, "");
  else
    return SVN_NO_ERROR;
}


/* Our own global variables */
apr_hash_t *proplist, *new_proplist;

const char *review =
"A forthright entrance, yet coquettish on the tongue, its deceptively\n"
"fruity exterior hides the warm mahagony undercurrent that is the\n"
"hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will\n"
"be pleased to note the familiar, subtle hints of mulberries and\n"
"carburator fluid.  Its confident finish is marred only by a barely\n"
"detectable suggestion of rancid squid ink.";




static svn_error_t *
test1 (const char **msg, 
       svn_boolean_t msg_only,
       apr_pool_t *pool)
{
  apr_status_t result;
  svn_stringbuf_t *key;
  apr_file_t *f;

  *msg = "write a hash to a file";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Build a hash in memory, and fill it with test data. */
  proplist = apr_hash_make (pool);

  key = svn_stringbuf_create ("color", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_stringbuf_create ("red", pool));
  
  key = svn_stringbuf_create ("wine review", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_stringbuf_create (review, pool));
  
  key = svn_stringbuf_create ("price", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_stringbuf_create ("US $6.50", pool));

  /* Test overwriting: same key both times, but different values. */
  key = svn_stringbuf_create ("twice-used property name", pool);
  apr_hash_set (proplist, key->data, key->len,
               svn_stringbuf_create ("This is the FIRST value.", pool));
  apr_hash_set (proplist, key->data, key->len,
               svn_stringbuf_create ("This is the SECOND value.", pool));

  /* Dump the hash to a file. */
  apr_file_open (&f, "hashdump.out",
            (APR_WRITE | APR_CREATE),
            APR_OS_DEFAULT, pool);

  result = svn_hash_write (proplist, svn_unpack_bytestring, f, pool);

  apr_file_close (f);

  return check (result, pool);
}




static svn_error_t *
test2 (const char **msg, 
       svn_boolean_t msg_only,
       apr_pool_t *pool)
{
  apr_status_t result;
  apr_file_t *f;

  *msg = "read a file into a hash";

  if (msg_only)
    return SVN_NO_ERROR;

  new_proplist = apr_hash_make (pool);

  apr_file_open (&f, "hashdump.out", APR_READ, APR_OS_DEFAULT, pool);

  result = svn_hash_read (new_proplist, svn_pack_bytestring, f, pool);

  apr_file_close (f);

  return check (result, pool);
}



static svn_error_t *
test3 (const char **msg, 
       svn_boolean_t msg_only,
       apr_pool_t *pool)
{
  apr_hash_index_t *this;
  svn_error_t *err;
  int found_discrepancy = 0;
  const char *ignored;

  *msg = "write hash out, read back in, compare";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Build a hash in global variable "proplist", then write to a file. */
  err = test1 (&ignored, FALSE, pool);
  if (err)
    return err;

  /* Read this file back into global variable "new_proplist" */
  err = test2 (&ignored, FALSE, pool);
  if (err)
    return err;

  /* Now let's make sure that proplist and new_proplist contain the
     same data. */
  
  /* Loop over our original hash */
  for (this = apr_hash_first (pool, proplist); 
       this; 
       this = apr_hash_next (this))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_stringbuf_t *orig_str, *new_str;
      
      /* Get a key and val. */
      apr_hash_this (this, &key, &keylen, &val);
      orig_str = (svn_stringbuf_t *) val;

      /* Look up the key in the new hash */
      new_str = (svn_stringbuf_t *) apr_hash_get (new_proplist, key, keylen);

      /* Does the new hash contain the key at all? */
      if (new_str == NULL)
        found_discrepancy = 1;

      /* Do the two strings contain identical data? */
      else if (! svn_stringbuf_compare (orig_str, new_str))
        found_discrepancy = 1;
    }


  if (found_discrepancy)
    return svn_error_createf (SVN_ERR_TEST_FAILED, 0, 0, pool,
                              "found discrepancy reading back hash table");

  return SVN_NO_ERROR;
}




/*
   ====================================================================
   If you add a new test to this file, update this array.

*/

/* An array of all test functions */
svn_error_t *(*test_funcs[])(const char **msg, 
                             svn_boolean_t msg_only,
                             apr_pool_t *pool) =
{
  NULL,
  test1,
  test2,
  test3,
  NULL
};



/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
