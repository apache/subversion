/*
 * status.c:  the command-line's portion of the "svn status" command
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

/* ==================================================================== */



/*** Includes. ***/
#include <apr_hash.h>
#include <apr_tables.h>
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_string.h"
#include "cl.h"



/* Edit the three-byte string STR_STATUS, based on the contents of
   TEXT_STATUS and PROP_STATUS. 
   
   If VERBOSE is set, STR_STATUS will show the status of both text and
   prop. 

   If VERBOSE is not set, STR_STATUS will contain only one simplified
   status code, based on a logical combination of the two.  */
static void
generate_status_codes (char *str_status,
                       enum svn_wc_status_kind text_status,
                       enum svn_wc_status_kind prop_status,
                       int verbose)
{
  char text_statuschar, prop_statuschar;

  switch (text_status)
    {
    case svn_wc_status_none:
      text_statuschar = '-';
      break;
    case svn_wc_status_added:
      text_statuschar = 'A';
      break;
    case svn_wc_status_deleted:
      text_statuschar = 'D';
      break;
    case svn_wc_status_modified:
      text_statuschar = 'M';
      break;
    case svn_wc_status_merged:
      text_statuschar = 'G';
      break;
    case svn_wc_status_conflicted:
      text_statuschar = 'C';
      break;
    default:
      text_statuschar = '?';
      break;
    }

  switch (prop_status)
    {
    case svn_wc_status_none:
      prop_statuschar = '-';
      break;
    case svn_wc_status_added:
      prop_statuschar = 'A';
      break;
    case svn_wc_status_deleted:
      prop_statuschar = 'D';
      break;
    case svn_wc_status_modified:
      prop_statuschar = 'M';
      break;
    case svn_wc_status_merged:
      prop_statuschar = 'G';
      break;
    case svn_wc_status_conflicted:
      prop_statuschar = 'C';
      break;
    default:
      prop_statuschar = '?';
      break;
    }

  if (verbose)
    sprintf (str_status, "%c%c", text_statuschar, prop_statuschar);

  else
    {
      char one_char;

      /* Guys, go to town on this section.  Here are some quick rules
         I made up.  Feel free to rewrite them however you want.  :) */

      if ((text_statuschar == '-') && (prop_statuschar == '-'))
        one_char = '-';
          
      else if ((text_statuschar == 'A') || (prop_statuschar == 'A'))
        one_char = 'A';

      if ((text_statuschar == 'D') || (prop_statuschar == 'D'))
        one_char = 'D';

      if ((text_statuschar == 'M') || (prop_statuschar == 'M'))
        one_char = 'M';

      if ((text_statuschar == 'C') || (prop_statuschar == 'C'))
        one_char = 'C';
      
      sprintf (str_status, "%c ", one_char);
    }

  return;
}


void
svn_cl__print_status (svn_string_t *path, svn_wc_status_t *status)
{
  svn_revnum_t entry_rev;
  char str_status[3];

  /* Create either a one or two character status code */
  generate_status_codes (str_status,
                         status->text_status,
                         status->prop_status,
                         1 /* be verbose */);  
  /* TODO: use the verbose switch from a command line option */
  
  /* Grab the entry revision once, safely. */
  if (status->entry)
    entry_rev = status->entry->revision;
  else
    entry_rev = SVN_INVALID_REVNUM;

  /* Use it. */
  if ((entry_rev == SVN_INVALID_REVNUM)
      && (status->repos_rev == SVN_INVALID_REVNUM))
    printf ("%s  none     ( none )   %s\n",
            str_status, path->data);
  else if (entry_rev == SVN_INVALID_REVNUM)
    printf ("%s  none     (%6ld)   %s\n",
            str_status, status->repos_rev, path->data);
  else if (status->repos_rev == SVN_INVALID_REVNUM)
    printf ("%s  %-6ld  ( none )  %s\n",
            str_status, entry_rev, path->data);
  else
    printf ("%s  %-6ld  (%6ld)  %s\n",
            str_status, entry_rev, status->repos_rev, path->data);
}


void
svn_cl__print_status_list (apr_hash_t *statushash, apr_pool_t *pool)
{
  int i;
  apr_array_header_t *statusarray;

  /* Convert the unordered hash to an ordered, sorted array */
  statusarray = apr_hash_sorted_keys (statushash,
                                      svn_sort_compare_as_paths,
                                      pool);

  /* Loop over array, printing each name/status-structure */
  for (i = 0; i < statusarray->nelts; i++)
    {
      apr_item_t *item;
      const char *path;
      svn_wc_status_t *status;
      
      item = (((apr_item_t **)(statusarray)->elts)[i]);
      path = (const char *) item->key;
      status = (svn_wc_status_t *) item->data;

      svn_cl__print_status (svn_string_create (path, pool), status);
    }
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
