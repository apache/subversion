/* A VDelta implementation for Subversion.
 *
 * (See comments in make_vdelta() for algorithm details.)
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
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of CollabNet.
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
make_vdelta (char *data, size_t source_len, size_t target_len)
{
  /*
   * This implements an approximation of the vdelta algorithm as
   * described in Appendix B of
   *
   *    "Delta Algorithms: An Empirical Analysis"
   *     Hunt, J. J., Vo, K.-P., and Tichy, W. F.
   *     An empirical study of delta algorithms.
   *     Lecture Notes in Computer Science 1167 (July 1996), 49-66.
   *
   * The plan is to coax this to output vcdiff format, as described in
   * 
   *    http://www.ietf.org/internet-drafts/draft-korn-vcdiff-01.txt
   * 
   * and write a `patch' program that takes vcdiff input.  Once that's
   * done, the delta generator will be improved, adding windowing, the
   * use of the vdelta matching technique, and whatever else is called
   * for.
   * 
   * Here's how it works right now.  Step 1 all happened before this
   * function, Step 2 is what this function does: 
   * 
   *   1. Read source_text and target_text into buf, concatenated.
   *      (And know where the dividing point between them is, of
   *      course.) 
   * 
   *   2. Slide along buf a byte at a time.  At each location, look up
   *      the current position in a hash table, using the 4-byte chunk
   *      starting here as key.
   *
   *        a) If lookup succeeds, go back in the source text to the
   *           matching position, make sure it's a real match and not
   *           just a hash collision.  If real, extend it as far as
   *           possible with the current text, and if already into the
   *           target data, then output a COPY instruction with the
   *           old position and the length of the match as parameters.
   *           (Also, store the last three positions of the match in
   *           the hash table.)
   *
   *        b) If lookup fails, store the current position, output an
   *           INSERT for the current byte if we're already into
   *           target data, and move on.
   *
   * Some things to notice:
   * 
   * This differencing algorithm is really a compression algorithm in
   * disguise -- one that happens not to generate any output until
   * it's in the target data.
   * 
   * Hash collisions are just ignored -- the older data wins.  This
   * strategy simply means that some matches won't be noticed.  One
   * could also overwrite it (that's XDelta's answer), or keep a
   * bucket chain so as not to lose data (vdelta's answer), or store
   * the last N matches (for some constant N, probably 4), or keep
   * scores and try not to toss ones which have matched well in the
   * past, or... you get the idea.  For now, oldest wins.
   *
   * It holds the source and target data together in memory.  This
   * loses, of course; it will be changed to one of the various
   * sliding window techniques.  Doing so is not trivial, but not
   * hugely difficult either, and if one maintains the requirement
   * that the source be seekable, that helps somewhat.  The big thing
   * you lose is the ability to go back and directly compare against
   * buf, but you can fake that by storing the 4-byte chunks along
   * with the positions in the hash table, and doing hash compares
   * where formerly did direct byte compares.
   */
  
  size_t pos = 0;       /* current position in DATA */
  size_t total_len;     /* put this in a var for readability */
  hash_table_t *table;  /* where we hold the back-lookup table. */
  
  total_len = source_len + target_len;
  table = make_hash_table (1511);
  
  /* todo: fix o-b-o-e, see test 0 */

  while (pos < (total_len - (MIN_MATCH_LEN - 1)))
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

  /* Cleanup the last (MIN_MATCH_LEN - 1) characters if necessary. */
  while (pos < total_len)
    {
      if (pos >= source_len)
        printf ("INSERT %c\n", data[pos]);
      pos++;
    }
}


main (int argc, char **argv)
{
  /* Curious what's going on here?
   *
   * Read the comment at the top of make_vdelta(), above, and
   * understand all.
   */

  char *source_file = NULL;
  char *target_file = NULL;
  char *data        = NULL; /* concatenation of source data and target data */
  size_t source_len = 0;    /* data[source_len] is the start of target data */
  size_t target_len = 0;    /* source_len + target_len == length of data    */

  if (argc == 2)
    {
      target_file = argv[1];
    }
  else if (argc == 3)
    {
      source_file = argv[1];
      target_file = argv[2];
    }
  else
    {
      fprintf (stderr, "Need two or three arguments.\n");
      exit (1);
    }

  if (source_file)
    source_len = file_size (source_file);

  target_len = file_size (target_file);

  data = svn_malloc (source_len + target_len + 1);

  if (source_file)
    file_into_buffer (source_file, source_len, data);
  file_into_buffer (target_file, target_len, data + source_len);
  data[source_len + target_len] = '\0'; /* todo: just for now */

  make_vdelta (data, source_len, target_len);

  free (data);
  exit (0);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
