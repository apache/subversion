/*
 * propset-cmd.c -- Display status information in current directory
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
 * software developed by CollabNet (http://www.Collab.Net/)."
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__propset (svn_cl__opt_state_t *opt_state,
                 apr_array_header_t *targets,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  char buf[BUFSIZ];
  int i;

  /* kff todo: this may get moved into main, since reading values from
     files is a generic operation that many commands may need to
     support. */
  if (opt_state->valfile)
    {
      /* Load the whole file into `value'.  
         
         What?  Don't look at me like that.  
         
         Don't forget that our entire property implementation happens
         "in-memory" right now.  And we're not just talking about
         single property name/value pairs; whole *lists* of pairs move
         from disk to memory and back. */
      svn_error_t *err2;
      apr_status_t status;
      apr_size_t len = BUFSIZ;
      apr_file_t *the_file = NULL;
      
      /* kff todo: possibly blowing away a previous setting of
         opt_state->value here.  But that redundancy should be checked
         earlier (see other comment above) anyway. */
      opt_state->value = svn_string_create ("", pool);

      status = apr_open (&the_file, opt_state->valfile->data,
                         APR_READ, APR_OS_DEFAULT, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "svn_cl__propset:  failed to open '%s'",
                                  opt_state->valfile->data);
      
      do {
        err2 = svn_io_file_reader (the_file, buf, &len, pool);
        if (err2) return err2;

        svn_string_appendbytes (opt_state->value, buf, len);

      } while (len != 0);
    }


  if (! strcmp (opt_state->value->data, ""))
    /* The user wants to delete the property. */
    opt_state->value = NULL;

  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *target = ((svn_string_t **) (targets->elts))[i];
        err = svn_wc_prop_set (opt_state->name, opt_state->value, target,
                               pool);
        if (err)
          return err;

        /* kff todo: prints may be temporary.  Ben? */
        if (opt_state->value)
          printf ("property `%s' set on %s.\n",
                  opt_state->name->data, target->data);
        else
          printf ("property `%s' deleted from %s\n",
                  opt_state->name->data, target->data);
      }
  else
    {
      fprintf (stderr, "svn propset: arguments required\n");
      err = svn_cl__help (opt_state, targets, pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
