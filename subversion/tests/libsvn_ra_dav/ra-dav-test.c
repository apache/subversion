/*
 * ra-dav-test.c :  basic test program for the RA/DAV library
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
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
 */



#include <apr_general.h>
#include <apr_pools.h>

#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_wc.h"


int
main (int argc, char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  svn_ra_session_t *ras;
  const char *url;
  const svn_delta_walk_t *walker;
  void *walk_baton;
  void *dir_baton;
  svn_string_t *repos;

  apr_initialize ();
  apr_create_pool (&pool, NULL);

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s REPOSITORY_URL\n", argv[0]);
      return 1;
    }
  else
    url = argv[1];

  err = svn_ra_open(&ras, url, pool);
  if (err)
    {
      svn_handle_error (err, stdout);
      return 1;
    }

  repos = svn_string_create(url, pool);
  err = svn_wc_get_change_walker(NULL, repos, 1,
                                 &walker, &walk_baton, &dir_baton, pool);
  if (err)
    {
      svn_handle_error (err, stdout);
      return 1;
    }

  err = svn_ra_checkout(ras, "", 1, walker, walk_baton, dir_baton, pool);

  svn_ra_close(ras);

  apr_destroy_pool(pool);
  apr_terminate();

  return 0;
}





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
