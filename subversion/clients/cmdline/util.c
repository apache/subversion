/*
 * util.c: Subversion command line client utility functions. Any
 * functions that need to be shared across subcommands should be put
 * in here.
 *
      * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_io.h"
#include "cl.h"


#define DEFAULT_ARRAY_SIZE 5

/* Hmm. This should probably find its way into libsvn_subr -Fitz */
/* Create a SVN string from the char* and add it to the array */
static void 
array_push_svn_stringbuf (apr_array_header_t *array,
                          const char *str,
                          apr_pool_t *pool)
{
  (*((svn_stringbuf_t **) apr_array_push (array)))
    = svn_stringbuf_create (str, pool);
}


/* Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args */
void
svn_cl__push_implicit_dot_target (apr_array_header_t *targets, 
                                  apr_pool_t *pool)
{
  if (targets->nelts == 0)
    array_push_svn_stringbuf (targets, ".", pool);
  assert (targets->nelts);
}

/* Parse a given number of non-target arguments from the
 * command line args passed in by the user. Put them
 * into the opt_state args array */
svn_error_t *
svn_cl__parse_num_args (apr_getopt_t *os,
                        svn_cl__opt_state_t *opt_state,
                        const char *subcommand,
                        int num_args,
                        apr_pool_t *pool)
{
  int i;
  
  opt_state->args = apr_array_make (pool, DEFAULT_ARRAY_SIZE, 
                                    sizeof (svn_stringbuf_t *));

  /* loop for num_args and add each arg to the args array */
  for (i = 0; i < num_args; i++)
    {
      if (os->ind >= os->argc)
        {
          svn_cl__subcommand_help (subcommand, pool);
          return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 
                                   0, 0, pool, "");
        }
      array_push_svn_stringbuf (opt_state->args, os->argv[os->ind++], pool);
    }

  return SVN_NO_ERROR;
}

/* Parse all of the arguments from the command line args
 * passed in by the user. Put them into the opt_state
 * args array */
svn_error_t *
svn_cl__parse_all_args (apr_getopt_t *os,
                        svn_cl__opt_state_t *opt_state,
                        const char *subcommand,
                        apr_pool_t *pool)
{
  opt_state->args = apr_array_make (pool, DEFAULT_ARRAY_SIZE, 
                                    sizeof (svn_stringbuf_t *));

  if (os->ind >= os->argc)
    {
      svn_cl__subcommand_help (subcommand, pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  while (os->ind < os->argc)
    {
      array_push_svn_stringbuf (opt_state->args, os->argv[os->ind++], pool);
    }

  return SVN_NO_ERROR;
}

/* Create a targets array and add all the remaining arguments
 * to it. */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
                              apr_pool_t *pool)
{
  apr_array_header_t *targets =
    apr_array_make (pool, DEFAULT_ARRAY_SIZE, sizeof (svn_stringbuf_t *));
  
  for (; os->ind < os->argc; os->ind++)
    {
      svn_stringbuf_t *target = svn_stringbuf_create (os->argv[os->ind], pool);
      svn_stringbuf_t *basename;
      svn_string_t tstr;

      /* If this path looks like it would work as a URL in one of the
         currently available RA libraries, we add it unconditionally
         to the target array. */
      tstr.data = target->data;
      tstr.len  = target->len;
      if (! svn_path_is_url (&tstr))
        {
          /* If this target is a Subversion administrative directory,
             skip it.  TODO: Perhaps this check should not call the
             target a SVN admin dir unless svn_wc_check_wc passes on
             the target, too? */
          svn_path_canonicalize (target);
          basename = svn_path_last_component (target, pool);
          if (! strcmp (basename->data, SVN_WC_ADM_DIR_NAME))
            continue;
        }
      else
        {
          svn_path_canonicalize (target);
        }
      (*((svn_stringbuf_t **) apr_array_push (targets))) = target;
    }

  /* kff todo: need to remove redundancies from targets before
     passing it to the cmd_func. */
     
  return targets;
}

/* Convert a whitespace separated list of items into an apr_array_header_t */
apr_array_header_t*
svn_cl__stringlist_to_array(svn_stringbuf_t *buffer, apr_pool_t *pool)
{
  apr_array_header_t *array = apr_array_make(pool, DEFAULT_ARRAY_SIZE,
                                             sizeof(svn_stringbuf_t *));
  if (buffer != NULL)
    {
      apr_size_t start = 0, end = 0;
      svn_stringbuf_t *item;
      while (end < buffer->len)
        {
          while (isspace(buffer->data[start]))
                start++; 

          end = start;

          while (end < buffer->len && !isspace(buffer->data[end]))
              end++;

          item  = svn_stringbuf_ncreate(&buffer->data[start],
                                          end - start, pool);
          *((svn_stringbuf_t**)apr_array_push(array)) = item;

          start = end;
        }
    }
  return array;
} 



void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info)
{
  if ((commit_info) 
      && (SVN_IS_VALID_REVNUM (commit_info->revision)))
    printf ("Committed revision %ld.\n", commit_info->revision);

  return;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
