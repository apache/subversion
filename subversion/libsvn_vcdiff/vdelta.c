/* A VDelta implementation for Subversion.
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



#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "hash.h"
#include "../svn_subr/alloc.h"


#define MIN_MATCH_LEN 4


size_t
file_size (char *file)
{
  struct stat s;
  
  if (stat (file, &s) < 0)
    {
      fprintf (stderr, "can't stat %s (%s)", file, strerror (errno));
      exit (1);
    }
  
  /* else */

  return s.st_size;
}


/* Read LEN bytes from FILE into BUF. 
   BUF must already point to allocated space. */
int
file_into_buffer (char *file, size_t len, char *buf)
{
  FILE *fp;
  size_t total_so_far = 0;
  
  fp = fopen (file, "r");
  
  while (1)
    {
      size_t received;
      
      received = fread (buf, 1, (len - total_so_far), fp);
      if (ferror (fp))
        {
          fprintf (stderr, "can't read %s", file);
          break;
        }
      
      total_so_far += received;
      
      if ((total_so_far >= len) || (feof (fp)))
        break;
    }
  
  if (fclose (fp) < 0)
    fprintf (stderr, "cannot close %s (%s)", file, strerror (errno));
  
  return 0;
}


void
take_delta (char *data, size_t source_len, size_t target_len)
{
  /*
   * PSEUDOCODE:
   *   read file1 into buf1;
   *   read file2 into buf2;
   * 
   *   slide along buf1 a char at a time, recording 4-byte strings in hash;
   *   slide along buf2 a char at a time, checking for matches against buf1;
   *      if no match, output an INSERT and record in hash
   *      if match, extend it as far as can, then output a COPY and bump pos
   */
  
  size_t pos = 0;       /* current position in DATA */
  size_t total_len;     /* put this in a var for readability */
  hash_table_t *table;  /* where we hold the back-lookup table. */
  
  total_len = source_len + target_len;
  table = make_hash_table (1511);
  
  while (pos < total_len)
    {
      hash_entry_t *e;
      
      e = try_match (data + pos, MIN_MATCH_LEN, pos, table);

      if (e && (strncmp (data + e->pos, data + pos, MIN_MATCH_LEN) == 0))
        {
          /* We got a match.  Now try extending it as far as possible. */

          size_t old_pos;       /* Where this substring was seen before. */
          size_t match_len;
          size_t i;

          old_pos = e->pos;

          match_len = (MIN_MATCH_LEN - 1);
          while (((match_len + 1) < (total_len - pos))
                 && (data[old_pos + match_len + 1]
                     == data[pos + match_len + 1]))
            {
              match_len++;
            }

          pos += match_len;

          /* todo: bogus output format for now */
          if (pos >= source_len)
            printf ("COPY %d %d\n", old_pos, match_len);

          /* Record the unrecorded positions from this match. 
             (Step 2a on page 18 of Hunt/Vo/Tichy.) */
          for (i = (MIN_MATCH_LEN - 1); i > 0; i--)
            {
              /* Calling try_match() solely for recording purposes,
                 not because we're actually looking for a match. */
              try_match (data + (pos - i), MIN_MATCH_LEN, (pos - i), table);
            }
        }
      else   /* No match. */
        {
          if (pos >= source_len)
            printf ("INSERT %c\n", data[pos]);
        }

      pos++;
    }
}


/* todo: this initial implementation just reads the source and target
 * files into memory, and works on them there.  Once that's working,
 * we move to sliding window technique.
 */

/* First we're implementing a standalone binary diff and patch.
   Then we'll librarize. */

main (int argc, char **argv)
{
  char *source_file;
  char *target_file;
  char *data;           /* concatenation of source data and target data */
  size_t source_len;    /* data[source_len] is the start of target data */
  size_t target_len;    /* source_len + target_len == length of data    */

  if (argc < 3)
    {
      fprintf (stderr, "Need at least two arguments.\n");
      exit (1);
    }

  /* todo: need sfio-like buffered seekable somethings */

  source_file = argv[1];
  target_file = argv[2];

  source_len = file_size (source_file);
  target_len = file_size (target_file);

  /* todo: for now, window_size is just the summed sizes of the two
     files. */
  data = svn_malloc (source_len + target_len + 1);

  file_into_buffer (source_file, source_len, data);
  file_into_buffer (target_file, target_len, data + source_len);
  data[source_len + target_len] = '\0'; /* todo: just for now */

  take_delta (data, source_len, target_len);

  free (data);
  exit (0);
}
