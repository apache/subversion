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





/* BEN SEZ: size_t_into_string() is no longer needed, I think, because
   I'm solving this problem with ap_psprintf() now.  Pools are safer
   than dumping into a fixed-size buffer. */

/* Returns the number of bytes used in the result. */
int
size_t_into_string (char *buf, size_t num)
{
  int used;
  sprintf (buf, "%ld%n", (long int) num, &used);
  return used;
}


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
 *  Input:  a hash, a file pointer, pool, and an "unpack" function (see above)
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
            ap_file_t *destfile,
            ap_pool_t *pool)
{
  ap_hash_index_t *this;      /* current hash entry */
  svn_string_t *destbuf;      /* main buffer we write to */

  ap_status_t *status;
  ap_size_t bytes_written;


  /* Assuming that the file pointer already points to a file open for
     writing. */

  /* Strategy: we're going to write everything into a svn_string_t,
     and then do an ap_full_write() of the whole bytestring when we're
     done. */

  /* Malloc the buffer, then clear it */
  destbuf = svn_string_create ("<nobody home>", pool);
  svn_string_setempty (destbuf);

  for (this = ap_hash_first (hash); this; this = ap_hash_next (this))
    {
      const void *key;
      void *val;
      size_t keylen;
      size_t vallen;

      char *valstring;

      char *numstring;
      int bytes_used;

      /* Get this key and val. */
      ap_hash_this (this, &key, &keylen, &val);

      /* Output the name's length, then the name itself. */
      svn_string_appendbytes (destbuf, "K ", 2, pool);
      
      numstring = ap_psprintf (pool, "%ld%n", (long int) keylen, &bytes_used);
      svn_string_appendbytes (destbuf, numstring, bytes_used, pool);

      svn_string_appendbytes (destbuf, "\n", 1, pool);

      svn_string_appendbytes (destbuf, (char *) key, keylen, pool);
      svn_string_appendbytes (destbuf, "\n", 1, pool);

      /* Output the value's length, then the value itself. */
      vallen = (size_t) (*unpack_func) (&valstring, val); /* secret decoder! */

      svn_string_appendbytes (destbuf, "V ", 2, pool);
      
      numstring = ap_psprintf (pool, "%ld%n", (long int) vallen, &bytes_used);
      svn_string_appendbytes (destbuf, numstring, bytes_used, pool);
      svn_string_appendbytes (destbuf, "\n", 1, pool);

      svn_string_appendbytes (destbuf, valstring, vallen, pool);
      svn_string_appendbytes (destbuf, "\n", 1, pool);
    }

  /* Now write the whole bytestring buffer to a file. */

  status = ap_full_write (destfile, 
                          destbuf->data, 
                          destbuf->len, 
                          &bytes_written);

  /* For now, don't deal with any error you get, just pass it up. */

  return status;
}





/* Read a hash table from a file. */
ap_status_t
hash_read (ap_hash_t **h, 
           void *(*pack_value) (size_t len, const char *val),
           ap_file_t *src)
{
  
}





/* kff todo: should it return ap_status_t? */
void
svn_wc_proplist_write (ap_hash_t *proplist, 
                       svn_string_t *destfile_name)
{
  ap_file_t *destfile = NULL;   /* this init to NULL is actually important */
  ap_status_t res;
  ap_pool_t *pool = NULL;
  ap_hash_index_t *this;      /* current hash entry */
  const char *dest_fname;
  
  res = ap_create_pool (&pool, NULL);
  if (res != APR_SUCCESS)
    {
      /* kff todo: need to copy CVS's error-handling better, or something.
       * 
       * Example: here we have a mem allocation error, probably, so
       * our exit plan must include more allocation! :-)
       * 
       * Go through code of svn_handle_error, look for stuff like
       * this.
       */
      exit (1);
    }

  dest_fname = svn_string_2cstring (destfile_name, pool);

  /* kff todo: maybe this whole file-opening thing wants to be
     abstracted?  We jump through these same hoops in svn_parse.c as
     well, after all... */

  res = ap_open (&destfile,
                 dest_fname,
                 (APR_WRITE | APR_CREATE),
                 APR_OS_DEFAULT,  /* kff todo: what's this about? */
                 pool);

  if (res != APR_SUCCESS)
    {
      const char *msg;

      msg = ap_pstrcat(pool,
                       "svn_wc_proplist_write(): "
                       "can't open for writing, file ",
                       dest_fname, NULL);

      /* Declare this a fatal error! */
      svn_handle_error (svn_create_error (res, msg, NULL, pool), stderr);
    }

  /* Else file successfully opened.  Continue. */

  for (this = ap_hash_first (proplist); this; this = ap_hash_next (this))
    {
      const void *key;
      void *val;
      size_t keylen;
      size_t num_len;
      char buf[100];   /* Only holds lengths expressed in decimal digits. */

      /* Get this key and val. */
      ap_hash_this (this, &key, &keylen, &val);

      /* Output the name's length, then the name itself. */
      guaranteed_ap_write (destfile, "N ", 2);
      num_len = size_t_into_string (buf, keylen);
      guaranteed_ap_write (destfile, buf, num_len);
      guaranteed_ap_write (destfile, "\n", 1);
      guaranteed_ap_write (destfile, key, keylen);
      guaranteed_ap_write (destfile, "\n", 1);

      /* Output the value's length, then the value itself. */
      guaranteed_ap_write (destfile, "V ", 2);
      num_len = size_t_into_string (buf, ((svn_string_t *) val)->len);
      guaranteed_ap_write (destfile, buf, num_len);
      guaranteed_ap_write (destfile, "\n", 1);
      guaranteed_ap_write (destfile,
                           ((svn_string_t *) val)->data,
                           ((svn_string_t *) val)->len);
      guaranteed_ap_write (destfile, "\n", 1);
    }

  res = ap_close (destfile);
  if (res != APR_SUCCESS)
    {
      const char *msg;

      msg = ap_pstrcat(pool, 
                       "svn_parse(): warning: can't close file ",
                       dest_fname, NULL);
      
      /* Not fatal, just annoying */
      svn_handle_error (svn_create_error (res, msg, NULL, pool), stderr);
    }
  
  ap_destroy_pool (pool);
}


#if 0
ap_hash_t *
svn_wc_proplist_read (svn_string_t *propfile, apr_pool_t *pool)
{
  /* kff todo: fooo in progress */
}
#endif /* 0 */



#ifdef SVN_TEST

int
main (void)
{
  ap_pool_t *pool = NULL;
  ap_hash_t *proplist;
  svn_string_t *key;

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
  svn_wc_proplist_write
    (proplist, svn_string_create ("propdump.out", pool));

  ap_destroy_pool (pool);

  return 0;
}

#endif /* SVN_TEST */





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

