/*
 * update.c :  routines for update and checkout
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
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"



/* If PATH exists already, return an svn error containing ERR_TO_REPORT.
 *
 * If PATH doesn't exist, return 0.
 *
 * If unable to determine whether or not PATH exists, due to another
 * error condition, return an svn error containing that apr error.
 */
static svn_error_t *
check_existence (svn_string_t *path,
                 apr_status_t err_to_report,
                 apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *tmp_f;

  apr_err = apr_open (&tmp_f, path->data,
                     (APR_CREATE | APR_APPEND | APR_EXCL),
                     APR_OS_DEFAULT, pool);

  if (apr_err == APR_EEXIST)
    {
      svn_error_t *err 
        = svn_create_error (err_to_report, 0, path->data, NULL, pool);
      return err;
    }
  else if (apr_err)  /* some error other than APR_EEXIST */
    {
      svn_error_t *err 
        = svn_create_error (apr_err, 0, path->data, NULL, pool);
      return err;
    }
  else              /* path definitely does not exist */
    {
      apr_close (tmp_f);
      return 0;
    }
}


/* kff todo: this will want to be somewhere else, and get decided at
   configure time too probably.  For now let's just get checkout
   working. */
#define SVN_DIR_SEPARATOR '/'


/* Return a path from a delta stack. */
static svn_string_t *
delta_stack_to_path (svn_delta_stackframe_t *stack, apr_pool_t *pool)
{
  svn_delta_stackframe_t *p = stack;
  svn_string_t *path;

  /* Start out with an empty path... */
  path = svn_string_create ("", pool);

  /* ... then walk down the stack appending to it. */
  while (p)
    {
      if (stack->kind == svn_XML_content) /* "<dir ...>" or "<file ...>" */
        {
          if (! svn_string_isempty (path))
            {
              char dirsep = SVN_DIR_SEPARATOR;
              svn_string_appendbytes (path, &dirsep, 1, pool);
            }

          svn_string_appendstr (path, stack->name, pool);

          if (stack->content_kind == svn_content_file)
            break;
        }

      p = p->next;
    }

  return path;
}


static svn_error_t *
update_dir_handler (svn_delta_digger_t *diggy, svn_delta_stackframe_t *frame)
{
  svn_delta_stackframe_t *stack = diggy->stack;
  svn_string_t *dir = delta_stack_to_path (stack, diggy->pool);

  if (! dir)
    {
      /* kff todo: make an error */
      printf ("Unable to get a dir.\n");
    }

  /* Else, make the directory. */
  printf ("Got dir \"%s\".\n", dir->data);  /* kff todo */

  return 0;
}



/* Do an update/checkout, with src delta streaming from SRC, to DST (a path).
 * 
 * SRC must be already opened.
 * 
 * If DST exists and is a working copy, or a subtree of a working
 * copy, then it is massaged into the updated state.
 *
 * If DST does not exist, a working copy is created there.
 *
 * If DST exists but is not a working copy, return error.
 *
 * (And if DST is NULL, the above rules apply with DST set to the top
 * directory mentioned in the delta.) 
 *
 * kff todo: instead of apr_file_t *SRC, use a generic streamer like
 * JimB made for the text delta interface.
 */
svn_error_t *
update (apr_file_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  char buf[BUFSIZ];
  apr_status_t err;
  int len;
  int done;
  svn_delta_digger_t diggy;
  XML_Parser parsimonious;

  /* Init everything to null. */
  memset (&diggy, '\0', sizeof (diggy));

  diggy.pool = pool;
  diggy.dir_handler = update_dir_handler;

  /* Make a parser with the usual shared handlers and diggy as userData. */
  parsimonious = svn_delta_make_xml_parser (&diggy);

  /* Check existence of DST.  If present, just error out for now -- we
     can't do real updates, only fresh checkouts. */
  if (dst)
    {
      svn_error_t *err;
      err = check_existence (dst, SVN_ERR_OBSTRUCTED_UPDATE, pool);

      /* Whether or not err->apr_err == SVN_ERR_OBSTRUCTED_UPDATE, we
         want to return it to caller. */
      if (err)
        return err;
    }


  /* Else nothing in the way, so contine. */

  do {
    /* Grab some stream. */
    err = apr_full_read (src, buf, sizeof (buf), &len);
    done = (len < sizeof (buf));
    
    /* Parse the chunk of stream. */
    if (! XML_Parse (parsimonious, buf, len, done))
    {
      svn_error_t *err
        = svn_create_error
        (SVN_ERR_MALFORMED_XML, 0,
         apr_psprintf (pool, "%s at line %d",
                       XML_ErrorString (XML_GetErrorCode (parsimonious)),
                       XML_GetCurrentLineNumber (parsimonious)),
         NULL, pool);
      XML_ParserFree (parsimonious);
      return err;
    }
  } while (! done);

  XML_ParserFree (parsimonious);
  return 0;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
