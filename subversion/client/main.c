/*
 * main.c:  Subversion command line client.
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

#include <string.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"



/*** Command dispatch. ***/
static const svn_cl__cmd_opts_t add_opts = {
  svn_cl__add_command,
  "Add a new file or directory to version control." };

static const svn_cl__cmd_opts_t checkout_opts = {
  svn_cl__checkout_command,
  "Check out a working directory from a repository." };

static const svn_cl__cmd_opts_t commit_opts = {
  svn_cl__commit_command,
  "Commit changes from your working copy to the repository." };

static const svn_cl__cmd_opts_t delete_opts = {
  svn_cl__delete_command,
  "Remove a file or directory from version control." };

static const svn_cl__cmd_opts_t proplist_opts = {
  svn_cl__proplist_command,
  "List all properties for given files and directories." };

static const svn_cl__cmd_opts_t propget_opts = {
  svn_cl__propget_command,
  "Get the value of a file or directory property." };

static const svn_cl__cmd_opts_t propset_opts = {
  svn_cl__propset_command,
  "Set the value of a file or directory property." };

static const svn_cl__cmd_opts_t status_opts = {
  svn_cl__status_command,
  "Print the status of working copy files and directories." };

static const svn_cl__cmd_opts_t update_opts = {
  svn_cl__update_command,
  "Bring changes from the repository into the working copy." };


/* Map names to command routine, option descriptor and
   its "base" command.  */
static const svn_cl__cmd_desc_t cmd_table[] = {
  { "add",        FALSE,  svn_cl__add,      &add_opts },
  { "ad",         TRUE,   svn_cl__add,      &add_opts },
  { "new",        TRUE,   svn_cl__add,      &add_opts },

  { "checkout",   FALSE,  svn_cl__checkout, &checkout_opts },
  { "co",         TRUE,   svn_cl__checkout, &checkout_opts },

  { "commit",     FALSE,  svn_cl__commit,   &commit_opts },
  { "ci",         TRUE,   svn_cl__commit,   &commit_opts },

  { "delete",     FALSE,  svn_cl__delete,   &delete_opts },
  { "del",        TRUE,   svn_cl__delete,   &delete_opts },
  { "remove",     TRUE,   svn_cl__delete,   &delete_opts },
  { "rm",         TRUE,   svn_cl__delete,   &delete_opts },

  { "help",       FALSE,  svn_cl__help,     NULL },

  { "proplist",   FALSE,  svn_cl__proplist, &proplist_opts },
  { "plist",      TRUE,   svn_cl__proplist, &proplist_opts },
  { "pl",         TRUE,   svn_cl__proplist, &proplist_opts },

  { "propget",    FALSE,  svn_cl__propget,  &propget_opts },
  { "pget",       TRUE,   svn_cl__propget,  &propget_opts },
  { "pg",         TRUE,   svn_cl__propget,  &propget_opts },

  { "propset",    FALSE,  svn_cl__propset,  &propset_opts },
  { "pset",       TRUE,   svn_cl__propset,  &propset_opts },
  { "ps",         TRUE,   svn_cl__propset,  &propset_opts },

  { "status",     FALSE,  svn_cl__status,   &status_opts },
  { "stat",       TRUE,   svn_cl__status,   &status_opts },
  { "st",         TRUE,   svn_cl__status,   &status_opts },

  { "update",     FALSE,  svn_cl__update,   &update_opts },
  { "up",         TRUE,   svn_cl__update,   &update_opts },

  { NULL,         FALSE }
};


static const svn_cl__cmd_desc_t *
get_cmd_table_entry (const char *cmd_name)
{
  int max = sizeof (cmd_table) / sizeof (cmd_table[0]);
  int i;

  if (cmd_name == NULL)
    {
      fprintf (stderr, "svn error: no command name provided\n");
      return NULL;
    }

  /* Special case: treat `--help' and friends as though they were the
     `help' command, so they work the same as the command. */
  if ((strcmp (cmd_name, "--help") == 0)
      || (strcmp (cmd_name, "-help") == 0)
      || (strcmp (cmd_name, "-h") == 0)
      || (strcmp (cmd_name, "-?") == 0))
    {
      cmd_name = "help";
    }

  /* Regardless of the option chosen, the user gets --help :-) */
  if (cmd_name[0] == '-')
    {
      fputs ("svn error: the base `svn' command accepts no options\n",
             stderr);
      return NULL;
    }

  for (i = 0; i < max; i++)
    if (strcmp (cmd_name, cmd_table[i].cmd_name) == 0)
      return cmd_table + i;

  /* Else command not found. */

  fprintf (stderr, "svn error: `%s' is an unknown command\n", cmd_name);
  return NULL;
}



/*** Option parsing. ***/
static int
parse_command_options (int argc,
                       const char **argv,
                       apr_pool_t *pool,
                       svn_cl__opt_state_t *p_opt_st)
{
  static const char needs_arg[] =
    "svn %s: \"--%s\" needs an argument\n";
  static const char invalid_opt[] =
    "svn %s error:  option `%s' invalid\n";

  const char *cmdname = argv[1];
  svn_cl__command_t cmd_code = p_opt_st->cmd_opts->cmd_code;
  int i;

  for (i = 2; i < argc; i++)
    {
      if (strcmp (argv[i], "--xml-file") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "xml-file");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->xml_file = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--target-dir") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "target-dir");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->target = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--ancestor-path") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "ancestor-path");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->ancestor_path = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--revision") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "revision");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->revision = (svn_revnum_t) atoi (argv[i]);
        }
      else if (strcmp (argv[i], "--name") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "name");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->name = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--value") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "value");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->value = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--filename") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, needs_arg, cmdname, "filename");
              exit (EXIT_FAILURE);
            }
          else
            p_opt_st->filename = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--force") == 0)
        p_opt_st->force = 1;
      else if (*(argv[i]) == '-')
        {
          fprintf (stderr, invalid_opt, cmdname, argv[i]);
          exit (EXIT_FAILURE);
        }
      else
        break;
    }

  /* Sanity checks: make sure we got what we needed. */
  /* Any command may have an xml_file option.  Not really true, but true
     in this framework.  In any event, the commands that do need it
     are listed here. */
  if (p_opt_st->xml_file == NULL)
    switch (cmd_code)
      {
      case svn_cl__commit_command:
      case svn_cl__checkout_command:
      case svn_cl__update_command:
        fprintf (stderr, "svn %s: need \"--xml-file FILE.XML\"\n", cmdname);
        exit (EXIT_FAILURE);
      default:
      }

  if (p_opt_st->force)
    switch (cmd_code)
      {
      case svn_cl__delete_command:
        break;

      default:
        fprintf (stderr, invalid_opt, cmdname, "--force");
        exit (EXIT_FAILURE);
      }

  /* make sure we have a valid revision for these commands */
  if (p_opt_st->revision == SVN_INVALID_REVNUM)
    switch (cmd_code)
      {
      case svn_cl__commit_command:
      case svn_cl__update_command:
        fprintf (stderr, "svn %s: please use \"--revision VER\" "
                 "to specify target revision\n", cmdname);
        exit (EXIT_FAILURE);
      default:
      }

  /* Check for the need for a default target */
  if (p_opt_st->target == NULL)
    switch (cmd_code)
      {
      case svn_cl__checkout_command:
      case svn_cl__update_command:
      case svn_cl__commit_command:
      case svn_cl__status_command:
      case svn_cl__proplist_command:
      case svn_cl__propget_command:
        p_opt_st->target = svn_string_create (".", pool);
      default:
      }

  /* Check the need for a property name */
  if (p_opt_st->name == NULL)
    switch (cmd_code)
      {
      case svn_cl__propget_command:
      case svn_cl__propset_command:
        fprintf (stderr, "svn %s: need \"--name PROPERTY_NAME\"\n", cmdname);
        exit (EXIT_FAILURE);
      default:
      }

  /* Check the need for a property value OR filename */
  if ((p_opt_st->value == NULL) && (p_opt_st->filename == NULL))
    switch (cmd_code)
      {
      case svn_cl__propset_command:
        fprintf
          (stderr,
           "svn %s: need one of \"--value\" or \"--filename\"\n", cmdname);
        exit (EXIT_FAILURE);
      default:
      }


  return i;
}



/*** Help. ***/

/* Return the canonical command table entry for CMD (which may be the
 * entry for CMD itself, or some other entry if CMD is a short
 * synonym).  
 * 
 * CMD must be a valid command name; the behavior is
 * undefined if it is not.
 */
static const svn_cl__cmd_desc_t *
get_canonical_command (const char *cmd)
{
  const svn_cl__cmd_desc_t *cmd_desc = get_cmd_table_entry (cmd);

  if (cmd_desc == NULL)
    return cmd_desc;

  while (cmd_desc->is_alias)  cmd_desc--;

  return cmd_desc;
}


static void
print_command_info (const char *cmd,
                    svn_boolean_t help,
                    apr_pool_t *pool)
{
  const svn_cl__cmd_desc_t *canonical_cmd = get_canonical_command (cmd);

  /*  IF we get a NULL back, then an informative message has already
      been printed.  */
  if (canonical_cmd == NULL)
    return;

  fputs (canonical_cmd->cmd_name, stdout);
  if (canonical_cmd[1].is_alias)
    {
      const svn_cl__cmd_desc_t *p_alias = canonical_cmd + 1;
      fputs (" (", stdout);
      for (;;)
        {
          fputs ((p_alias++)->cmd_name, stdout);
          if (! p_alias->is_alias)
            break;
          fputs (", ", stdout);
        }
      fputc (')', stdout);
    }

  if (help)
    printf (": %s\n", canonical_cmd->cmd_opts->help);
}


static void
print_generic_help (apr_pool_t *pool)
{
  size_t max = sizeof (cmd_table) / sizeof (cmd_table[0]);
  static const char usage[] =
    "usage: svn <subcommand> [options] [args]\n"
    "Type \"svn help <subcommand>\" for help on a specific subcommand.\n"
    "Available subcommands are:\n";
  int i;

  printf ("%s", usage);
  for (i = 0; i < max; i++)
    if (! cmd_table[i].is_alias)
      {
        printf ("   ");
        print_command_info (cmd_table[i].cmd_name, FALSE, pool);
        printf ("\n");
      }
}


/*  Unlike all the other command routines, ``help'' has its own
    option processing.  Of course, it does not accept any options :-),
    just command line args.  */
svn_error_t *
svn_cl__help (int argc, const char **argv, apr_pool_t *pool,
              svn_cl__opt_state_t *p_opt_state)
{
  if (argc > 2)
    {
      int i;
      for (i = 2; i < argc; i++)
        print_command_info (argv[i], TRUE, pool);
    }
  else
    {
      print_generic_help (pool);
    }

  return SVN_NO_ERROR;
}



/*** Main. ***/

int
main (int argc, const char **argv)
{
  const svn_cl__cmd_desc_t *p_cmd = get_cmd_table_entry (argv[1]);
  apr_pool_t *pool;
  svn_cl__opt_state_t opt_state;

  if (p_cmd == NULL)
    {
      svn_cl__help (0, NULL, NULL, NULL);
      return EXIT_FAILURE;
    }

  apr_initialize ();
  pool = svn_pool_create (NULL);
  memset ((void*)&opt_state, 0, sizeof (opt_state));
  opt_state.cmd_opts = p_cmd->cmd_opts;

  /*  IF the command descriptor has an option processing descriptor,
      go do it.  Otherwise, the command routine promises to do the work */
  if (p_cmd->cmd_opts != NULL)
    {
      int used_ct = parse_command_options (argc, argv, pool, &opt_state);
      argc -= used_ct;
      argv += used_ct;
    }

  {
    svn_error_t *err = (*p_cmd->cmd_func) (argc, argv, pool, &opt_state);
    if (err != SVN_NO_ERROR)
      svn_handle_error (err, stdout, 0);
  }

  apr_destroy_pool (pool);

  return EXIT_SUCCESS;
}

/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
