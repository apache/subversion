/*
 *  add-cmd.c - Subversion add command
 *
 *  svn is free software copyrighted by CollabNet.
 *  
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name ``CollabNet'' nor the name of any other
 *     contributor may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *  
 *  svn IS PROVIDED BY CollabNet ``AS IS'' AND ANY EXPRESS
 *  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL CollabNet OR ANY OTHER CONTRIBUTORS
 *  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

static char* get_help[] = {
  "svn", "help", "add", NULL };


/*** Code. ***/

svn_error_t *
svn_cl__add( int argc, char** argv, apr_pool_t* pool,
             svn_cl__opt_state_t *p_opt_state )
{
  svn_error_t *err = NULL;
  svn_string_t *target = GET_OPT_STATE(p_opt_state, target);

  if (target != NULL)
    err = svn_client_add (target, pool);
  else if (argc > 0)
    {
      while (--argc >= 0)
        {
          target = svn_string_create (*(argv++), pool);
          err = svn_client_add (target, pool);
          /* free (target); */
          if (err != SVN_NO_ERROR)
            break;
        }
    }
  else
    {
      fputs ("svn add: object-to-add required\n", stderr);
      err = svn_cl__help (3, get_help, pool, p_opt_state);
    }
  return err;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
