/*
 * hashdump.c :  dumping and reading hash tables to/from files.
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




/* 
 * The format of a dumped hash table is:
 *
 *   K <nlength>
 *   name (a string of <nlength> bytes, followed by a newline)
 *   V <vlength>
 *   val (a string of <vlength> bytes, followed by a newline)
 *
 * For example:
 *
 *   K 5
 *   color
 *   V 3
 *   red
 *   K 11
 *   wine review
 *   V 376
 *   A forthright entrance, yet coquettish on the tongue, its deceptively
 *   fruity exterior hides the warm mahagony undercurrent that is the
 *   hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will
 *   be pleased to note the familiar, subtle hints of mulberries and
 *   carburator fluid.  Its confident finish is marred only by a barely
 *   detectable suggestion of rancid squid ink.
 *   K 5 
 *   price
 *   V 8
 *   US $6.50
 */



/* svn_unpack_bytestring():  for use as helper with hash_write().
 *
 *  Input:  a hash value which points to an svn_string_t
 *
 *  Returns:  the size of the svn_string_t,
 *            and (by indirection) the string data itself
 *
 */

ap_size_t 
svn_unpack_bytestring (char **returndata, void *value)
{
  svn_string_t *valstring = (svn_string_t *) value;

  *returndata = valstring->data;

  return (size_t) valstring->len;
}



/* hash_write():  dump a hash table to a file.
 * 
 *  Input:  a hash, an "unpack" function (see above), an opened file pointer
 * 
 *  Returns:  error status
 *
 *     The "unpack" routine knows how to convert a hash value into a
 *     printable bytestring of a certain length.
 *
 */

ap_status_t
hash_write (ap_hash_t *hash, 
            ap_size_t (*unpack_func) (char **unpacked_data, void *val),
            ap_file_t *destfile)
{
  ap_hash_index_t *this;      /* current hash entry */
  ap_status_t err;
  char buf[100];

  for (this = ap_hash_first (hash); this; this = ap_hash_next (this))
    {
      const void *key;
      void *val;
      size_t keylen;
      size_t vallen;
      int bytes_used;
      char *valstring;

      /* Get this key and val. */
      ap_hash_this (this, &key, &keylen, &val);

      /* Output name length, then name. */

      err = ap_full_write (destfile, "K ", 2, NULL);
      if (err) return err;

      sprintf (buf, "%ld%n", (long int) keylen, &bytes_used);
      err = ap_full_write (destfile, buf, bytes_used, NULL);
      if (err) return err;

      err = ap_full_write (destfile, "\n", 1, NULL);
      if (err) return err;

      err = ap_full_write (destfile, (char *) key, keylen, NULL);
      if (err) return err;

      err = ap_full_write (destfile, "\n", 1, NULL);
      if (err) return err;

      /* Output value length, then value. */

      vallen = (size_t) (*unpack_func) (&valstring, val); /* secret decoder! */
      err = ap_full_write (destfile, "V ", 2, NULL);
      if (err) return err;

      sprintf (buf, "%ld%n", (long int) vallen, &bytes_used);
      err = ap_full_write (destfile, buf, bytes_used, NULL);
      if (err) return err;

      err = ap_full_write (destfile, "\n", 1, NULL);
      if (err) return err;

      err = ap_full_write (destfile, valstring, vallen, NULL);
      if (err) return err;

      err = ap_full_write (destfile, "\n", 1, NULL);
      if (err) return err;
    }

  return SVN_NO_ERROR;
}


#if 0
/* Read a hash table from a file. */
ap_status_t
hash_read (ap_hash_t **h, 
           void *(*pack_value) (size_t len, const char *val),
           ap_file_t *src)
{
  
}
#endif /* 0 */



#ifdef SVN_TEST

int
main (void)
{
  ap_pool_t *pool = NULL;
  ap_hash_t *proplist;
  svn_string_t *key;
  ap_file_t *f = NULL;     /* init to NULL very important! */

  /* Our longest piece of test data. */
  char *review =
    "A forthright entrance, yet coquettish on the tongue, its deceptively\n"
    "fruity exterior hides the warm mahagony undercurrent that is the\n"
    "hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will\n"
    "be pleased to note the familiar, subtle hints of mulberries and\n"
    "carburator fluid.  Its confident finish is marred only by a barely\n"
    "detectable suggestion of rancid squid ink.";

  ap_initialize ();
  ap_create_pool (&pool, NULL);

  proplist = ap_make_hash (pool);
  
  /* Fill it in with test data. */

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


  /* Dump it. */
  ap_open (&f, "hashdump.out", (APR_WRITE | APR_CREATE), APR_OS_DEFAULT, pool);
  hash_write (proplist, svn_unpack_bytestring, f);
  ap_close (f);
  ap_destroy_pool (pool);

  return 0;
}

#endif /* SVN_TEST */





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

