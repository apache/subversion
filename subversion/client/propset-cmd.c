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
svn_cl__propset( int argc, char** argv, apr_pool_t* pool,
                 svn_cl__opt_state_t *p_opt_state )
{
  svn_error_t *err = NULL;
  svn_string_t *name, *value;
  svn_string_t *target;
  svn_string_t *filename;
  char buf[BUFSIZ];

  name     = GET_OPT_STATE(p_opt_state, name);
  value    = GET_OPT_STATE(p_opt_state, value);
  target   = GET_OPT_STATE(p_opt_state, target);
  filename = GET_OPT_STATE(p_opt_state, filename);

  if (filename)
    {
      /* Load the whole file into `value'.  

         What?  Don't look at me like that.  

         Don't forget that our entire property implementation happens
         "in-memory" right now.  And we're not just talking about
         single property name/value pairs; whole *lists* of pairs move
         from disk to memory and back. */
      svn_error_t *error;
      apr_status_t status;
      apr_size_t len = BUFSIZ;
      apr_file_t *the_file = NULL;
      
      value = svn_string_create ("", pool);

      status = apr_open (&the_file, filename->data,
                         APR_READ, APR_OS_DEFAULT, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "svn_cl__propset:  failed to open '%s'",
                                  filename->data);
      
      do {
        error = svn_io_file_reader (the_file, buf, &len, pool);
        if (error) return error;

        svn_string_appendbytes (value, buf, len);

      } while (len != 0);
    }

  if (! strcmp (value->data, ""))
    /* The user wants to delete the property. */
    value = NULL;

  err = svn_wc_prop_set (name, value, target, pool);

  if (! err) 
    {
      if (value)
        printf ("Property `%s': set on %s.\n", name->data, target->data);
      else
        printf ("Property `%s': deleted from %s\n", name->data, target->data);
    }

  return err;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
