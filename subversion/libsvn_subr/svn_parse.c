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



#include <svn_parse.h>


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
my__readline (ap_file_t *FILE, svn_string_t *line, ap_pool_t *pool)
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

   Input:  a bytestring, starting search offset, search character, and pool

   Returns:  1. the offset of the search character
             2. a newly allocated substring in "substr".
                * This substring starts at `start' and goes to the offset
                * This substring has no whitespace at either end

     If used repeatedly, this routine is like a poor man's `split' 
     (combined with chomp).

     If search character ISN'T in the bytestring, then return NULL as
     the substring!
             
*/

size_t
slurp__to (const svn_string_t *searchstr,
           svn_string_t *substr,
           const size_t start, 
           const char sc,
           ap_pool_t *pool)
{
  size_t i;

  /* Create a new bytestring */
  substr = ap_palloc (pool, sizeof(svn_string_t));

  for (i = start; i < searchstr->len; i++)
    {
      if (searchstr->data[i] == sc)
        {
          svn_string_appendbytes (substr,                 /* new substring */
                                  searchstr->data + start,/* start copy here */
                                  (i - start - 1),        /* number to copy */
                                  pool);
          
          svn_string_strip_whitespace (substr);

          return i;
        }
    }

  /* If we get here, then the bytestring doesn't contain our search
     character.  This is bogus. */
  
  substr = NULL;
  return NULL;

}





/* 
   svn_parse()                        (finally)


   Input:  a filename and pool

   Output: a pointer to a hash of hashes, all built within the pool

   This routine parses a file which conforms to the standard
   Subversion config file format (look in notes/).  

   The hash returned is a mapping from section-names to hash pointers;
   each hash contains the keys/vals for each section.  All
   section-names, keys and vals are stored as svn_string_t pointers.
   (These bytestrings are allocated in the same pool as the hashes.)

   This routine makes no attempt to understand the sections, keys or
   values.  :) */


ap_hash_t *
svn_parse (svn_string_t *filename, ap_pool_t *pool)
{
  ap_hash_t *uberhash;      /* our hash of hashes */
  ap_hash_t *current_hash;  /* the hash we're currently storing vals in */

  ap_pool_t *scratchpool;
  svn_string_t *currentline;
  ap_status_t result;     
  ap_file_t *FILE = NULL;

  
  /* Create our uberhash */
  uberhash = ap_make_hash (pool);

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

      /* Declare this a fatal error! */
      svn_handle_error (svn_create_error (result, TRUE, msg, pool));
    }

  /* Create a scratch memory pool for buffering our file as we read it */
  if ((result = ap_create_pool (&scratchpool, NULL)) != APR_SUCCESS)
    {
      /* hoo boy, look at this yummy syntax: */
      svn_handle_error 
        (svn_create_error 
         (result, TRUE, 
          svn_string_create ("svn_parse(): fatal: can't create scratchpool",
                             pool), pool));
    }


  /* Create a bytestring to hold the current line of FILE */
  currentline = svn_string_create ("<nobody home>", scratchpool);


  /* Now start scanning our file, one line at a time */

  while (my__readline (FILE, currentline, scratchpool) != APR_EOF)
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

            slurp__to (currentline,  /* search current line */
                       new_section,  /* place new substring here */
                       offset,       /* start searching at latest offset */
                       ']',          /* look for this ending character */
                       pool);        /* build our substring in this pool */
           
            if (new_section == NULL)  /* couldn't find a ']' ! */
              {
                svn_string_t *msg = 
                  svn_string_create 
                  ("svn_parse(): warning: skipping malformed line: ", pool);
                svn_string_appendstr (msg, currentline, pool);
                
                svn_handle_error (svn_create_error 
                                  (result, FALSE, msg, pool));
                break;
              }
                                        

            /* make this the "active" hash */
            current_hash = new_section_hash;  

            /* store this new hash in our uberhash */
            ap_hash_set (uberhash, 
                         new_section,         /* key: ptr to bytestring */
                         sizeof(svn_string_t),/* the length of the key */
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

            local_offset = slurp__to (currentline, /* search current line */
                                      new_key,     /* put substring here */
                                      offset,      /* start at this offset */
                                      ':',         /* look for a colon */
                                      pool);       /* build substr here */

            if (new_key == NULL)  /* didn't find a colon! */
              {
                svn_string_t *msg = 
                  svn_string_create 
                  ("svn_parse(): warning: skipping malformed line: ", pool);
                svn_string_appendstr (msg, currentline, pool);
                
                svn_handle_error (svn_create_error 
                                  (result, FALSE, msg, pool));
                break;
              }

            /* Now slurp up the value, starting just past the colon */

            slurp__to (currentline,
                       new_val,
                       local_offset + 1,
                       '\n',
                       pool);

            /* Should we check for a NULL result from slurp__to?
               What are the chances it's not going to find a newline? :)
            */

            /* Store key and val in the currently active hash */
            ap_hash_set (current_hash,
                         new_key,             /* key: ptr to bytestring */
                         sizeof(svn_string_t),
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
      
      /* Not fatal, just annoying */
      svn_handle_error (svn_create_error (result, FALSE, msg, pool));
    }
  
  ap_destroy_pool (scratchpool);


  /* Return the hash of hashes */

  return uberhash;
}







/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
