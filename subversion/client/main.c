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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"




/*** Command dispatch. ***/

/* Map names to commands and their help strings.
   FIXME: the help strings could give a lot more detail... */
static svn_cl__cmd_desc_t cmd_table[] = {
  /* add */
  { "add",        NULL,       svn_cl__add,
    "Add a new file or directory to version control." },
  { "ad",         "add",      svn_cl__add,      NULL },
  { "new",        "add",      svn_cl__add,      NULL },

  /* checkout */
  { "checkout",   NULL,       svn_cl__checkout,
    "Check out a working directory from a repository." },
  { "co",         "checkout", svn_cl__checkout, NULL },

  /* commit */
  { "commit",     NULL,       svn_cl__commit,
    "Commit changes from your working copy to the repository." },
  { "ci",         "commit",   svn_cl__commit,   NULL },

  /* delete */
  { "delete",     NULL,       svn_cl__delete,
    "Remove a file or directory from version control." },
  { "del",        "delete",   svn_cl__delete,   NULL },
  { "remove",     "delete",   svn_cl__delete,   NULL },
  { "rm",         "delete",   svn_cl__delete,   NULL },

  /* help */
  { "help",       NULL,       svn_cl__help, 
    "Funny you should ask." },

  /* proplist */
  { "proplist",   NULL,       svn_cl__proplist,
    "List all properties for given files and directories." },
  { "plist",      "proplist", svn_cl__proplist, NULL },
  { "pl",         "proplist", svn_cl__proplist, NULL },

  /* status */
  { "status",     NULL,       svn_cl__status,
    "Print the status of working copy files and directories."},
  { "stat",       "status",   svn_cl__status,   NULL },
  { "st",         "status",   svn_cl__status,   NULL },

  /* update */
  { "update",     NULL,       svn_cl__update,
    "Bring changes from the repository into the working copy." },
  { "up",         "update",   svn_cl__update,   NULL }
};


static svn_cl__cmd_desc_t *
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
    return NULL;

  for (i = 0; i < max; i++)
    if (strcmp (cmd_name, cmd_table[i].cmd_name) == 0)
      return cmd_table + i;

  /* Else command not found. */

  fprintf (stderr, "svn error: `%s' is an unknown command\n", cmd_name);
  return NULL;
}



/*** Option parsing. ***/
static void
parse_command_options (int argc,
                       char **argv,
                       char *progname,
                       svn_string_t **xml_file,
                       svn_string_t **target,
                       svn_revnum_t *revision,
                       svn_string_t **ancestor_path,
                       svn_boolean_t *force,
                       apr_pool_t *pool)
{
  int i;

  for (i = 0; i < argc; i++)
    {
      if (strcmp (argv[i], "--xml-file") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, "%s: \"--xml-file\" needs an argument\n",
                       progname);
              exit (1);
            }
          else
            *xml_file = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--target-dir") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, "%s: \"--target-dir\" needs an argument\n",
                       progname);
              exit (1);
            }
          else
            *target = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--ancestor-path") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, "%s: \"--ancestor-path\" needs an argument\n",
                       progname);
              exit (1);
            }
          else
            *ancestor_path = svn_string_create (argv[i], pool);
        }
      else if (strcmp (argv[i], "--revision") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, "%s: \"--revision\" needs an argument\n",
                       progname);
              exit (1);
            }
          else
            *revision = (svn_revnum_t) atoi (argv[i]);
        }
      else if (strcmp (argv[i], "--force") == 0)
        *force = 1;
      else
        *target = svn_string_create (argv[i], pool);
    }
}


/* We'll want an off-the-shelf option parsing system soon... too bad
   GNU getopt is out for copyright reasons (?).  In the meantime,
   reinvent the wheel: */  
void
svn_cl__parse_options (int argc,
                       char **argv,
                       enum svn_cl__command command,
                       svn_string_t **xml_file,
                       svn_string_t **target,   /* dest_dir or file to add */
                       svn_revnum_t *revision,  /* ancestral or new */
                       svn_string_t **ancestor_path,
                       svn_boolean_t *force,
                       apr_pool_t *pool)
{
  char *s = argv[0];  /* svn progname */

  /* Skip the program and subcommand names.  Parse the rest. */
  parse_command_options (argc-2, argv+2, s,
                         xml_file, target, revision, ancestor_path, force,
                         pool);

  /* Sanity checks: make sure we got what we needed. */
  /* Any command may have an xml_file, but ADD, STATUS and DELETE
     *must* have the xml_file option */
  /* kff todo: I am confused by the above comment for two reasons.
     One, add status and delete *don't* need an xml_file option.  Two,
     even if that were true, isn't the test below testing that this
     is *not* one those commands, instead of the other way around? */
  if ((! *xml_file)
      && (command != svn_cl__add_command)
      && (command != svn_cl__status_command)
      && (command != svn_cl__proplist_command)
      && (command != svn_cl__delete_command))
    {
      fprintf (stderr, "%s: need \"--xml-file FILE.XML\"\n", s);
      exit (1);
    }
  if (*force && (command != svn_cl__delete_command))
    {
      fprintf (stderr, "%s: \"--force\" meaningless except for delete\n", s);
      exit (1);
    }
  /* COMMIT and UPDATE must have a valid revision */
  if ((*revision == SVN_INVALID_REVNUM)
      && (  (command == svn_cl__commit_command)
         || (command == svn_cl__update_command)))
    {
      fprintf (stderr, "%s: please use \"--revision VER\" "
               "to specify target revision\n", s);
      exit (1);
    }
  /* CHECKOUT, UPDATE, COMMIT and STATUS have a default target */
  if ((*target == NULL)
      &&  (  (command == svn_cl__checkout_command) 
          || (command == svn_cl__update_command)
          || (command == svn_cl__commit_command)
          || (command == svn_cl__status_command)
          || (command == svn_cl__proplist_command)))
    *target = svn_string_create (".", pool);
}



/*** Help. ***/

/* Return the canonical command table entry for CMD (which may be the
 * entry for CMD itself, or some other entry if CMD is a short
 * synonym).  
 * 
 * CMD must be a valid command name; the behavior is
 * undefined if it is not.
 */
static svn_cl__cmd_desc_t *
get_canonical_command (const char *cmd)
{
  svn_cl__cmd_desc_t *cmd_desc = get_cmd_table_entry (cmd);

  if ((! cmd_desc) || (! cmd_desc->short_for))
    return cmd_desc;
  else
    return get_cmd_table_entry (cmd_desc->short_for);
}


/* Return an apr array of the command table entries for all synonyms
 * of canonical command CMD.  The array will not include the entry for
 * CMD itself.
 */
static apr_array_header_t *
get_canonical_cmd_synonyms (const char *cmd, apr_pool_t *pool)
{
  size_t max = sizeof (cmd_table) / sizeof (cmd_table[0]);
  apr_array_header_t *ary
    = apr_make_array (pool, 0, sizeof (svn_cl__cmd_desc_t *));
  int i;

  for (i = 0; i < max; i++)
    if ((cmd_table[i].short_for != NULL)
        && (strcmp (cmd, cmd_table[i].short_for) == 0))
      *((svn_cl__cmd_desc_t **)apr_push_array (ary)) = cmd_table + i;

  return ary;
}


static void
print_command_and_maybe_help (const char *cmd,
                              svn_boolean_t help,
                              apr_pool_t *pool)
{
  svn_cl__cmd_desc_t *canonical_cmd = get_canonical_command (cmd);
  apr_array_header_t *synonyms;
  int i;

  if (! canonical_cmd)
    return;

  synonyms = get_canonical_cmd_synonyms (canonical_cmd->cmd_name, pool);

  printf ("%s", canonical_cmd->cmd_name);

  if (! (apr_is_empty_table (synonyms)))
    printf (" (");
  for (i = 0; i < synonyms->nelts; i++)
    {
      svn_cl__cmd_desc_t *this
        = (((svn_cl__cmd_desc_t **) (synonyms)->elts)[i]);

      printf ("%s", this->cmd_name);
      if (! (i == (synonyms->nelts - 1)))
        printf (", ");
    }
  if (! (apr_is_empty_table (synonyms)))
    printf (")");

  if (help)
    {
      printf (": ");
      printf ("%s\n", canonical_cmd->help);
    }
}


static void
print_command_help (const char *cmd, apr_pool_t *pool)
{
  print_command_and_maybe_help (cmd, TRUE, pool);
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
    if (cmd_table[i].short_for == NULL)
      {
        printf ("   ");
        print_command_and_maybe_help (cmd_table[i].cmd_name, FALSE, pool);
        printf ("\n");
      }
}


svn_error_t *
svn_cl__help (int argc, char **argv, apr_pool_t *pool)
{
  if (argc > 2)
    {
      int i;
      for (i = 2; i < argc; i++)
        print_command_help (argv[i], pool);
    }
  else
    {
      print_generic_help (pool);
    }

  return SVN_NO_ERROR;
}



/*** Main. ***/

int
main (int argc, char **argv)
{
  svn_cl__cmd_desc_t* p_cmd = get_cmd_table_entry (argv[1]);
  svn_error_t *err;
  apr_pool_t *pool;

  if (p_cmd == NULL)
    {
      svn_cl__help (0, NULL, NULL);
      return EXIT_FAILURE;
    }

  apr_initialize ();
  pool = svn_pool_create (NULL);

  err = (*p_cmd->cmd_func) (argc, argv, pool);
  if (err)
    svn_handle_error (err, stdout, 0);

  apr_destroy_pool (pool);

  return EXIT_SUCCESS;
}

/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
