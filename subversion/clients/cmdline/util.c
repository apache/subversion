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
#include <apr_lib.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "cl.h"


#define DEFAULT_ARRAY_SIZE 5

/* Hmm. This should probably find its way into libsvn_subr -Fitz */
/* Create a SVN string from the char* and add it to the array */
static void 
array_push_str (apr_array_header_t *array,
                const char *str,
                apr_pool_t *pool)
{
  /* ### Not sure if this function is still necessary.  It used to
     convert str to svn_stringbuf_t * and push it, but now it just
     dups str in pool and pushes the copy.  So its only effect is
     transfer str's lifetime to pool.  Is that something callers are
     depending on? */

  (*((const char **) apr_array_push (array))) = apr_pstrdup (pool, str);
}


/* Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args */
void
svn_cl__push_implicit_dot_target (apr_array_header_t *targets, 
                                  apr_pool_t *pool)
{
  if (targets->nelts == 0)
    array_push_str (targets, ".", pool);
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
                                    sizeof (const char *));

  /* loop for num_args and add each arg to the args array */
  for (i = 0; i < num_args; i++)
    {
      if (os->ind >= os->argc)
        {
          svn_cl__subcommand_help (subcommand, pool);
          return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 
                                   0, 0, pool, "");
        }
      array_push_str (opt_state->args, os->argv[os->ind++], pool);
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
                                    sizeof (const char *));

  if (os->ind >= os->argc)
    {
      svn_cl__subcommand_help (subcommand, pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  while (os->ind < os->argc)
    {
      array_push_str (opt_state->args, os->argv[os->ind++], pool);
    }

  return SVN_NO_ERROR;
}


/* Parse a working-copy or url PATH, looking for an "@" sign, e.g.

         foo/bar/baz@13
         http://blah/bloo@27
         blarg/snarf@HEAD

   If an "@" is found, return the two halves in *TRUEPATH and *REV,
   allocating in POOL.

   If no "@" is found, set *TRUEPATH to PATH and *REV to kind 'unspecified'.
*/
static svn_error_t *
parse_path (svn_client_revision_t *rev,
            const char **truepath,
            const char *path,
            apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_cl__opt_state_t *os = apr_pcalloc (subpool, sizeof(*os));

  /* scanning from right to left, just to be friendly to any
     screwed-up filenames that might *actually* contain @-signs.  :-) */
  for (i = (strlen (path) - 1); i >= 0; i--)
    {
      if (path[i] == '@')
        {
          if (svn_cl__parse_revision (os, path + i + 1, subpool))
            return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR,
                                      0, NULL, subpool,
                                      "Syntax error parsing revision \"%s\"",
                                      path + 1);

          *truepath = apr_pstrndup (pool, path, i);
          rev->kind = os->start_revision.kind;
          rev->value = os->start_revision.value;

          svn_pool_destroy (subpool);
          return SVN_NO_ERROR;
        }
    }

  /* Didn't find an @-sign. */
  *truepath = path;
  rev->kind = svn_client_revision_unspecified;

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* Create a targets array and add all the remaining arguments
 * to it. We also process arguments passed in the --target file, if
 * specified, just as if they were passed on the command line.  */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
			      svn_cl__opt_state_t *opt_state,
                              svn_boolean_t extract_revisions,
                              apr_pool_t *pool)
{
  svn_client_revision_t *firstrev = NULL, *secondrev = NULL;
  apr_array_header_t *targets =
    apr_array_make (pool, DEFAULT_ARRAY_SIZE, sizeof (const char *));
 
  /* Command line args take precedence.  */
  for (; os->ind < os->argc; os->ind++)
    {
      const char *target = apr_pstrdup (pool, os->argv[os->ind]);

      /* If this path looks like it would work as a URL in one of the
         currently available RA libraries, we add it unconditionally
         to the target array. */
      if (! svn_path_is_url (target))
        {
          const char *base_name = svn_path_basename (target, pool);

          /* If this target is a Subversion administrative directory,
             skip it.  TODO: Perhaps this check should not call the
             target a SVN admin dir unless svn_wc_check_wc passes on
             the target, too? */
          if (! strcmp (base_name, SVN_WC_ADM_DIR_NAME))
            continue;
        }
      else
        {
          target = svn_path_canonicalize_nts (target, pool);
        }

      (*((const char **) apr_array_push (targets))) = target;
    }

  /* Now args from --targets, if any */
  if (NULL != opt_state->targets)
    apr_array_cat(targets, opt_state->targets);

  /* kff todo: need to remove redundancies from targets before
     passing it to the cmd_func. */

  if (extract_revisions)
    {
      int i;
      for (i = 0; i < targets->nelts; i++)
        {
          const char *truepath;
          svn_client_revision_t temprev; 
          const char *path = ((const char **) (targets->elts))[i];

          parse_path (&temprev, &truepath, path, pool);

          if (temprev.kind != svn_client_revision_unspecified)
            {
              ((const char **) (targets->elts))[i] = truepath;

              if (! firstrev)
                {
                  firstrev = apr_pcalloc (pool, sizeof (*firstrev));
                  firstrev->kind = temprev.kind;
                  firstrev->value = temprev.value;
                }
              else if (! secondrev)
                {
                  secondrev = apr_pcalloc (pool, sizeof (*secondrev));
                  secondrev->kind = temprev.kind;
                  secondrev->value = temprev.value;
                }
              else
                break;
            }
        }

      if (firstrev)
        {
          opt_state->start_revision.kind = firstrev->kind;
          opt_state->start_revision.value = firstrev->value;
        }
      
      if (secondrev)
        {
          opt_state->end_revision.kind = secondrev->kind;
          opt_state->end_revision.value = secondrev->value;
        }
    }
  
  return targets;
}


void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info)
{
  if ((commit_info) 
      && (SVN_IS_VALID_REVNUM (commit_info->revision)))
    printf ("\nCommitted revision %" SVN_REVNUM_T_FMT ".\n",
            commit_info->revision);

  return;
}


svn_error_t *
svn_cl__edit_externally (const char **edited_contents,
                         const char *base_dir,
                         const char *contents,
                         apr_pool_t *pool)
{
  const char *editor = NULL;
  const char *cmd;
  apr_file_t *tmp_file;
  const char *tmpfile_name;
  apr_status_t apr_err, apr_err2;
  apr_size_t written;
  apr_finfo_t finfo_before, finfo_after;
  svn_error_t *err = SVN_NO_ERROR;
  int sys_err;

  /* Try to find an editor in the environment. */
  editor = getenv ("SVN_EDITOR");
  if (! editor)
    editor = getenv ("VISUAL");
  if (! editor)
    editor = getenv ("EDITOR");

  /* Abort if there is no editor specified */
  if (! editor)
    return svn_error_create 
      (SVN_ERR_CL_NO_EXTERNAL_EDITOR, 0, NULL, pool,
       "None of the environment variables "
       "SVN_EDITOR, VISUAL or EDITOR is set.");

  /* By now, we had better have an EDITOR to work with. */
  assert (editor);

  /* Ask the working copy for a temporary file based on BASE_DIR */
  SVN_ERR (svn_io_open_unique_file 
           (&tmp_file, &tmpfile_name,
            svn_path_join (base_dir, "msg", pool), ".tmp", FALSE, pool));

  /*** From here one, any problems that occur require us to cleanup
       the file we just created!! ***/

  /* Dump initial CONTENTS to TMP_FILE. */
  apr_err = apr_file_write_full (tmp_file, contents,
                                 strlen (contents), &written);

  apr_err2 = apr_file_close (tmp_file);
  if (! apr_err)
    apr_err = apr_err2;
  
  /* Make sure the whole CONTENTS were written, else return an error. */
  if (apr_err || (written != strlen (contents)))
    {
      err = svn_error_createf
        (apr_err ? apr_err : SVN_ERR_INCOMPLETE_DATA, 0, NULL, pool,
         "failed writing '%s'", tmpfile_name);
      goto cleanup;
    }

  /* Get information about the temporary file before the user has
     been allowed to edit its contents. */
  apr_err = apr_stat (&finfo_before, tmpfile_name, 
                      APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (apr_err)
    {
      err =  svn_error_createf (apr_err, 0, NULL, pool,
                                "failed to stat '%s'", tmpfile_name);
      goto cleanup;
    }

  /* Now, run the editor command line.  */
  cmd = apr_psprintf (pool, "%s %s", editor, tmpfile_name);
  sys_err = system (cmd);
  if (sys_err != 0)
    {
      /* Extracting any meaning from sys_err is platform specific, so just
         use the raw value. */
      err =  svn_error_createf (SVN_ERR_EXTERNAL_PROGRAM, 0, NULL, pool,
                                "system('%s') returned %d", cmd, sys_err);
      goto cleanup;
    }
  
  /* Get information about the temporary file after the assumed editing. */
  apr_err = apr_stat (&finfo_after, tmpfile_name, 
                      APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (apr_err)
    {
      err = svn_error_createf (apr_err, 0, NULL, pool,
                               "failed to stat '%s'", tmpfile_name);
      goto cleanup;
    }
  
  /* If the file looks changed... */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      svn_stringbuf_t *edited_contents_s;
      err = svn_string_from_file (&edited_contents_s, tmpfile_name, pool);
      if (err)
        goto cleanup; /* In case more code gets added before cleanup... */

      *edited_contents = edited_contents_s->data;
    }
  else
    {
      /* No edits seem to have been made */
      *edited_contents = NULL;
    }

 cleanup:

  apr_err = apr_file_remove (tmpfile_name, pool);

  /* Only report remove error if there was no previous error. */
  if (! err && apr_err)
    err = svn_error_createf (apr_err, 0, NULL, pool,
                             "failed to remove '%s'", tmpfile_name);

  return err;
}


struct log_msg_baton
{
  const char *message;
  const char *base_dir;
};


void *
svn_cl__make_log_msg_baton (svn_cl__opt_state_t *opt_state,
                            const char *base_dir,
                            apr_pool_t *pool)
{
  struct log_msg_baton *baton = apr_palloc (pool, sizeof (*baton));

  if (opt_state->filedata) 
    baton->message = opt_state->filedata->data;
  else
    baton->message = opt_state->message;

  baton->base_dir = base_dir ? base_dir : ".";

  return baton;
}


/* Return a pointer to the character after the first newline in LINE,
   or NULL if there is no newline.  ### FIXME: this is not fully
   portable for other kinds of newlines! */
static char *
get_next_line (char *line)
{
  char *newline = strchr (line, '\n');
  return (newline ? newline + 1 : NULL);
}


/* Remove all lines from BUFFER that begin with PREFIX. */
static svn_stringbuf_t *
strip_prefix_from_buffer (svn_stringbuf_t *buffer,
                          const char *strip_prefix,
                          apr_pool_t *pool)
{
  /* Start with a pointer to the first letter in the buffer, this is
     also on the beginning of a line */
  char *ptr = buffer->data;
  size_t strip_prefix_len = strlen (strip_prefix);

  while (ptr && (ptr < (buffer->data + buffer->len)))
    {
      char *first_prefix = ptr;

      /* First scan through all consecutive lines WITH prefix. */
      while (ptr && (! strncmp (ptr, strip_prefix, strip_prefix_len)))
        ptr = get_next_line (ptr);

      if (first_prefix != ptr)
        {
          /* One or more prefixed lines were found, cut them off. */
          if (! ptr)
            {
              /* This is the last line, no memmove() is necessary. */
              buffer->len -= (buffer->data + buffer->len - first_prefix);
              break;
            }

          /* Shift over the rest of the buffer to cover up the
             stripped prefix'ed line. */
          memmove (first_prefix, ptr, buffer->data + buffer->len - ptr);
          buffer->len -= (ptr - first_prefix);
          ptr = first_prefix;
        }

      /* Now skip all consecutive lines WITHOUT prefix */
      while (ptr && strncmp (ptr, strip_prefix, strip_prefix_len) )
        ptr = get_next_line (ptr);
    }
  buffer->data[buffer->len] = 0;
  return buffer;
}


#define EDITOR_PREFIX_TXT  "SVN:"

/* This function is of type svn_client_get_commit_log_t. */
svn_error_t *
svn_cl__get_log_message (const char **log_msg,
                         apr_array_header_t *commit_items,
                         void *baton,
                         apr_pool_t *pool)
{
  const char *default_msg = "\n"
    EDITOR_PREFIX_TXT 
    " ---------------------------------------------------------------------\n" 
    EDITOR_PREFIX_TXT " Enter Log.  Lines beginning with '" 
                             EDITOR_PREFIX_TXT "' are removed automatically\n"
    EDITOR_PREFIX_TXT "\n"
    EDITOR_PREFIX_TXT " Current status of the target files and directories:\n"
    EDITOR_PREFIX_TXT "\n";
  struct log_msg_baton *lmb = baton;
  svn_stringbuf_t *message = NULL;

  if (lmb->message)
    {
      *log_msg = apr_pstrdup (pool, lmb->message);
      return SVN_NO_ERROR;
    }

  if (! (commit_items || commit_items->nelts))
    {
      *log_msg = "";
      return SVN_NO_ERROR;
    }

  while (! message)
    {
      /* We still don't have a valid commit message.  Use $EDITOR to
         get one. */
      int i;
      svn_stringbuf_t *tmp_message = svn_stringbuf_create (default_msg, pool);
      svn_error_t *err = NULL;

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
          const char *path = item->path;
          char text_mod = '_', prop_mod = ' ';

          if (! path)
            path = item->url;

          if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
              && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
            text_mod = 'R';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
            text_mod = 'A';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
            text_mod = 'D';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
            text_mod = 'M';

          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
            prop_mod = 'M';

          svn_stringbuf_appendcstr (tmp_message, EDITOR_PREFIX_TXT);
          svn_stringbuf_appendcstr (tmp_message, "   ");
          svn_stringbuf_appendbytes (tmp_message, &text_mod, 1); 
          svn_stringbuf_appendbytes (tmp_message, &prop_mod, 1); 
          svn_stringbuf_appendcstr (tmp_message, "   ");
          svn_stringbuf_appendcstr (tmp_message, path);
          svn_stringbuf_appendcstr (tmp_message, "\n");
        }

      {
        const char *msg2;  /* ### shim */
        err = svn_cl__edit_externally (&msg2, lmb->base_dir,
                                       tmp_message->data, pool);
        if (msg2) {
            message = svn_stringbuf_create (msg2, pool);
        }
      }

      if (err)
        {
          if (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR)
            err = svn_error_quick_wrap 
              (err, "Could not use external editor to fetch log message; "
               "consider setting the $SVN_EDITOR environment variable "
               "or using the --message (-m) or --file (-F) options.");
          return err;
        }

      /* Strip the prefix from the buffer. */
      if (message)
        message = strip_prefix_from_buffer (message, EDITOR_PREFIX_TXT, pool);

      if (message)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          int len;

          for (len = message->len; len >= 0; len--)
            {
              if (! apr_isspace (message->data[len]))
                break;
            }
          if (len < 0)
            message = NULL;
        }

      if (! message)
        {
          char *reply;
          svn_cl__prompt_user (&reply,
                               "\nLog message unchanged or not specified\n"
                               "a)bort, c)ontinue, e)dit\n",
                               FALSE, /* don't hide the reply */
                               NULL, pool);
          if (reply)
            {
              char letter = apr_tolower (reply[0]);

              /* If the user chooses to abort, we exit the loop with a
                 NULL message. */
              if ('a' == letter)
                break;

              /* If the user chooses to continue, we make an empty
                 message, which will cause us to exit the loop. */
              if ('c' == letter) 
                message = svn_stringbuf_create ("", pool);

              /* If the user chooses anything else, the loop will
                 continue on the NULL message. */
            }
        }
    }
  
  *log_msg = message ? message->data : NULL;
  return SVN_NO_ERROR;
}

#undef EDITOR_PREFIX_TXT



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
