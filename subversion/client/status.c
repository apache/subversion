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
#include "svn_wc.h"
#include "svn_string.h"
#include "cl.h"


void
svn_cl__print_status (apr_hash_t *statushash)
{
  char statuschar;
  apr_hash_index_t *hi;

  /* TODO: perhaps we should guarantee that the parent dir always
     prints first?  Or perhaps we should sort these hash entries??
     Sure wish we had perl or python here.  :) */

  /* Loop over hash, printing each name/status-structure */
  for (hi = apr_hash_first (statushash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *value;
      const char *path;
      svn_wc_status_t *status;
      
      apr_hash_this (hi, &key, &keylen, &value);
      path = (const char *) key;
      status = (svn_wc_status_t *) value;

      switch (status->flag)
        {
        case svn_wc_status_none:
          statuschar = '-';
          break;
        case svn_wc_status_added:
          statuschar = 'A';
          break;
        case svn_wc_status_deleted:
          statuschar = 'D';
          break;
        case svn_wc_status_modified:
          statuschar = 'M';
          break;
        case svn_wc_status_merged:
          statuschar = 'G';
          break;
        case svn_wc_status_conflicted:
          statuschar = 'C';
        default:
          statuschar = '?';
          break;
        }
      
      if ((status->local_ver == SVN_INVALID_VERNUM)
          && (status->repos_ver == SVN_INVALID_VERNUM))
        printf ("%c  none     ( none )   %s\n",
                statuschar, path);
      else if (status->local_ver == SVN_INVALID_VERNUM)
        printf ("%c  none     (%6ld)   %s\n",
                statuschar, status->repos_ver, path);
      else if (status->repos_ver == SVN_INVALID_VERNUM)
        printf ("%c  %-6ld  ( none )  %s\n",
                statuschar, status->local_ver, path);
      else
        printf ("%c  %-6ld  (%6ld)  %s\n",
                statuschar, status->local_ver, status->repos_ver, path);
    }
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
