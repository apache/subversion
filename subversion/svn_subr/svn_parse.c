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



#include "svn_parse.h"


/* 
   NOT EXPORTED.

   Input:  an open file, a bytestring ptr, and a pool.

   Returns:  either APR_EOF or APR_SUCCESS, and a filled-in bytestring
             containing the next line of the file.

         (Note that the same bytestring can be reused in multiple
         calls to this routine, because the bytestring is cleared at
         the beginning.)  
*/


ap_status_t
svn__my_readline (ap_file_t *FILE, svn_string_t *line, ap_pool_t *pool)
{
  char c;
  ap_status_t result;

  svn_string_setempty (line);  /* clear the bytestring first! */

  while (1)
    {
      result = ap_getc (&c, FILE);  /* read a byte from the file */

      if (result == APR_EOF)  /* file is finished. */
        {
          return APR_EOF;
        }
      
      if (c == '\n')          /* line is finished. */
        {
          /* store the newline in our bytestring (important!) */
          svn_string_appendbytes (line, &c, 1, pool);

          return APR_SUCCESS;
        }
      
      else  /* otherwise, just append this byte to the bytestring */
        {
          svn_string_appendbytes (line, &c, 1, pool);
        }
    }  
}



/* 
   NOT EXPORTED.

   Input:  a bytestring, a handle for returning a bytestring, the
           starting search offset, search character, and pool 

   Returns:  1. the offset of the search character (-1 if no match)
             2. a newly allocated substring in "substr" (NULL if no match)
                * This substring starts at `start' and goes to the offset
                * This substring has no whitespace at either end

     If used repeatedly, this routine is like a poor man's `split' 
     (combined with chomp).
*/

int
svn__slurp_to (const svn_string_t *searchstr,
           svn_string_t **substr,
           const size_t start, 
           const char sc,
           ap_pool_t *pool)
{
  int i;

  /* Create a new bytestring */
  *substr = svn_string_create ("<nobody home>", pool);
  svn_string_setempty (*substr);

  for (i = start; i < searchstr->len; i++)
    {
      if (searchstr->data[i] == sc)
        {
          /*          printf ("found character '%c' at offset %d\n", sc, i);*/

          svn_string_appendbytes (*substr,               /* new substring */
                                  searchstr->data + start,/* start copy */
                                  (i - start),        /* number to copy */
                                  pool);
          
          svn_string_strip_whitespace (*substr);

          return i;
        }
    }

  /* If we get here, then the bytestring doesn't contain our search
     character.  This is bogus. */
  
  *substr = NULL;
  return -1;
}





/* 
   svn_parse()                        (finally)


   Input:  a filename and pool, pointer to a hash

   Output: a pointer to a hash of hashes, all built within the pool

   This routine parses a file which conforms to the standard
   Subversion config file format (look in notes/).  

   The hash returned is a mapping from section-names to hash pointers;
   each hash contains the keys/vals for each section.  All
   section-names, keys and vals are stored as svn_string_t pointers.
   (These bytestrings are allocated in the same pool as the hashes.)

   This routine makes no attempt to understand the sections, keys or
   values.  :) */


svn_error_t *
svn_parse (ap_hash_t **uberhash, svn_string_t *filename, ap_pool_t *pool)
{
  ap_hash_t *current_hash;  /* the hash we're currently storing vals in */

  ap_pool_t *scratchpool;
  svn_string_t *currentline;
  ap_status_t result;     
  ap_file_t *FILE = NULL;

  
  /* Create our uberhash */
  *uberhash = ap_make_hash (pool);

  /* Open the config file */
  result = ap_open (&FILE,
                    svn_string_2cstring (filename, pool),
                    APR_READ,
                    APR_OS_DEFAULT, /*TODO: WHAT IS THIS? */
                    pool);
  
  if (result != APR_SUCCESS)
    {
      svn_string_t *msg = svn_string_create 
        ("svn_parse(): can't open for reading, file ", pool);
      svn_string_appendstr (msg, filename, pool);

      return (svn_create_error (result, SVN_NON_FATAL, msg, pool));
    }

  /* Create a scratch memory pool for buffering our file as we read it */
  if ((result = ap_create_pool (&scratchpool, NULL)) != APR_SUCCESS)
    {
      return
        (svn_create_error 
         (result, SVN_FATAL, 
          svn_string_create ("svn_parse(): fatal: can't create scratchpool",
                             pool), pool));
    }


  /* Create a bytestring to hold the current line of FILE */
  currentline = svn_string_create ("<nobody home>", scratchpool);


  /* Now start scanning our file, one line at a time */

  while (svn__my_readline (FILE, currentline, scratchpool) != APR_EOF)
    {
      char c;
      size_t offset = svn_string_first_non_whitespace (currentline);

      if (offset == currentline->len)
        {
          /* whole line is whitespace, read next line! */
          continue;
        }
      
      c = currentline->data[offset];  /* our first non-white character */

      switch (c)
        {
        case '#': 
          {
            /* It's a comment line, so read next line! */
            continue;
          };

        case '[':
          {
            /* It's a new section! */  

            /* Create new hash to hold this section's keys/vals  */
            ap_hash_t *new_section_hash = ap_make_hash (pool);  

            /* Slurp up the section name */
            svn_string_t *new_section;

            svn__slurp_to (currentline,  /* search current line */
                       &new_section,  /* place new substring here */
                       offset + 1,    /* start searching past the '[' */
                       ']',          /* look for this ending character */
                       pool);        /* build our substring in this pool */
           
            if (new_section == NULL)  /* couldn't find a ']' ! */
              {
                svn_string_t *msg = 
                  svn_string_create 
                  ("svn_parse(): warning: skipping malformed line: ", pool);
                svn_string_appendstr (msg, currentline, pool);

                /* Instead of returning an error, just print warning */
                svn_handle_error (svn_create_error 
                                  (SVN_ERR_MALFORMED_LINE, SVN_NON_FATAL,
                                   msg, pool));
                break;
              }
                                        
            /* printf ("Found new section: `");
               svn_string_print (new_section, stdout, FALSE, FALSE);
               printf ("'\n"); */

            /* make this new hash the "active" hash for new keys/vals */
            current_hash = new_section_hash;  

            /* store this new hash in our uberhash */
            ap_hash_set (*uberhash, 
                         new_section->data,   /* key: bytestring */
                         new_section->len,    /* the length of the key */
                         new_section_hash);   /* val: ptr to the new hash */
            break;
          }

        default:
          {
            /* If it's not a blank line, comment line, or section line,
               then it MUST be a key : val line!  */

            /* Slurp up the key by searching for a colon */

            svn_string_t *new_key, *new_val;
            size_t local_offset;

            local_offset = svn__slurp_to (currentline, /* search current line */
                                      &new_key,     /* put substring here */
                                      offset,      /* start at this offset */
                                      ':',         /* look for a colon */
                                      pool);       /* build substr here */

            if (new_key == NULL)  /* didn't find a colon! */
              {
                svn_string_t *msg = 
                  svn_string_create 
                  ("svn_parse(): warning: skipping malformed line: ", pool);
                svn_string_appendstr (msg, currentline, pool);
               
                /* Instead of returning an error, just print warning */
                svn_handle_error (svn_create_error 
                                  (SVN_ERR_MALFORMED_LINE, SVN_NON_FATAL,
                                   msg, pool));
                break;
              }

            /* Now slurp up the value, starting just past the colon */

            svn__slurp_to (currentline,
                       &new_val,
                       local_offset + 1,
                       '\n',
                       pool);

            /*  printf ("Key: `");
                svn_string_print (new_key, stdout, FALSE, FALSE);
                printf ("'\n");
                printf ("Val: `");
                svn_string_print (new_val, stdout, FALSE, FALSE);
                printf ("'\n"); */

            /* Should we check for a NULL result from svn__slurp_to?
               What are the chances it's not going to find a newline? :)
            */

            /* Store key and val in the currently active hash */
            ap_hash_set (current_hash,
                         new_key->data,       /* key: bytestring data */
                         new_key->len,        /* length of key */
                         new_val);            /* val: ptr to bytestring */
            break;
          }         /* default: */
        }           /* switch (c) */
    }               /* while (readline) */

     
  /* Close the file and free our scratchpool */

  result = ap_close (FILE);
  if (result != APR_SUCCESS)
    {
      svn_string_t *msg = svn_string_create 
        ("svn_parse(): warning: can't close file ", pool);
      svn_string_appendstr (msg, filename, pool);
      
      /* Not fatal, just annoying.  Send a warning instead returning error. */
      svn_handle_error (svn_create_error (result, SVN_NON_FATAL, msg, pool));
    }
  
  ap_destroy_pool (scratchpool);


  /* Return success */

  return 0;
}





/*  Convenience Routine:  pretty-print an ap_hash_t.

     (ASSUMING that all keys and vals are of type (svn_string_t *) )

*/

void
svn_hash_print (ap_hash_t *hash, FILE *stream)
{
  ap_hash_index_t *hash_index;   /* this represents a hash entry */
  void *key, *val;
  size_t keylen;
  svn_string_t keystring, *valstring;

  fprintf (stream, "\n-----> Printing hash:\n");

  for (hash_index = ap_hash_first (hash);      /* get first hash entry */
       hash_index;                             /* NULL if out of entries */
       hash_index = ap_hash_next (hash_index)) /* get next hash entry */
    {
      /* Retrieve key and val from current hash entry */
      ap_hash_this (hash_index, &key, &keylen, &val);

      /* Cast things nicely */
      keystring.data = key;
      keystring.len = keylen;
      keystring.blocksize = keylen;

      valstring =  val;

      /* Print them out nicely */
      fprintf (stream, "Key: `");
      svn_string_print (&keystring, stream, FALSE, FALSE);
      fprintf (stream, "', ");

      fprintf (stream, "Val: `");
      svn_string_print (valstring, stream, FALSE, FALSE);
      fprintf (stream, "'\n");
    }
  
  fprintf (stream, "\n");
}



/* Convenience Routine:  pretty-print "uberhash" from svn_parse().

   (ASSUMING that all keys are (svn_string_t *),
                  all vals are (ap_hash_t *) printable by svn_hash_print() )
*/


void
svn_uberhash_print (ap_hash_t *uberhash, FILE *stream)
{
  ap_hash_index_t *hash_index;   /* this represents a hash entry */
  void *key, *val;
  size_t keylen;
  svn_string_t keystring;
  ap_hash_t *valhash;

  fprintf (stream, "\n-> Printing Uberhash:\n");

  for (hash_index = ap_hash_first (uberhash);  /* get first hash entry */
       hash_index;                             /* NULL if out of entries */
       hash_index = ap_hash_next (hash_index)) /* get next hash entry */
    {
      /* Retrieve key and val from current hash entry */
      ap_hash_this (hash_index, &key, &keylen, &val);

      /* Cast things nicely */
      keystring.data = key;
      keystring.len = keylen;
      keystring.blocksize = keylen;

      valhash = val;

      /* Print them out nicely */
      fprintf (stream, "---> Hashname: `");
      svn_string_print (&keystring, stream, FALSE, FALSE);
      fprintf (stream, "'\n");

      svn_hash_print (valhash, stream);
    }
  
  fprintf (stream, "\nUberhash printing complete.\n\n");
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
