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



/* The command dispatch table. */
static svn_cl__cmd_desc_t cmd_table[] = {
  { "add",        NULL,       TRUE,  svn_cl__add      },
  { "ad",         "add",      TRUE,  svn_cl__add      },
  { "new",        "add",      TRUE,  svn_cl__add      },
  { "checkout",   NULL,       TRUE,  svn_cl__checkout },
  { "co",         "checkout", TRUE,  svn_cl__checkout },
  { "commit",     NULL,       TRUE,  svn_cl__commit   },
  { "ci",         "commit",   TRUE,  svn_cl__commit   },
  { "delete",     NULL,       TRUE,  svn_cl__delete   },
  { "del",        "delete",   TRUE,  svn_cl__delete   },
  { "remove",     "delete",   TRUE,  svn_cl__delete   },
  { "rm",         "delete",   TRUE,  svn_cl__delete   },
  { "help",       NULL,       FALSE, svn_cl__help     },
  { "propfind",   NULL,       TRUE,  svn_cl__propfind },
  { "pfind",      "propfind", TRUE,  svn_cl__propfind },
  { "pf",         "propfind", TRUE,  svn_cl__propfind },
  { "status",     NULL,       TRUE,  svn_cl__status   },
  { "stat",       "status",   TRUE,  svn_cl__status   },
  { "st",         "status",   TRUE,  svn_cl__status   },
  { "update",     NULL,       TRUE,  svn_cl__update   },
  { "up",         "update",   TRUE,  svn_cl__update   }
};




/*** Code. ***/

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
               svn_cl__te_command command,
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
      && (command != svn_cl__propfind_command)
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
          || (command == svn_cl__propfind_command)))
    *target = svn_string_create (".", pool);
}


svn_error_t *
svn_cl__help (int argc, char **argv, apr_pool_t* pool)
{
  static const char zUsage[] =
    "What command do you need help with?\n"
    "You must type the command you need help with along with the `--help'\n"
    "command line option.  Choose from the following commands:\n\n";

  int ix = 0;
  svn_cl__cmd_desc_t* pCD = cmd_table;

  fputs( zUsage, stdout );

  for (;;)
    {
      printf( "  %-8s", (pCD++)->cmd_name );
      if (++ix >= sizeof( cmd_table ) / sizeof( cmd_table[0] ))
        break;
      if ((ix % 7) == 0)
        fputc( '\n', stdout );
    }

  fputc( '\n', stdout );
  return NULL;
}


static svn_cl__cmd_desc_t *
get_cmd_table_entry (const char *cmd_name)
{
  int i;

  if (cmd_name == NULL)
    {
      fprintf (stderr, "svn error: no command name provided\n");
      svn_cl__help (0, NULL, NULL);
      return NULL;
    }

  /* Regardless of the option chosen, the user gets --help :-) */
  if (cmd_name[0] == '-')
    {
      svn_cl__help (0, NULL, NULL);
      return NULL;
    }

  for (i = 0; i < sizeof (cmd_table); i++)
    if (strcmp (cmd_name, cmd_table[i].cmd_name) == 0)
      return cmd_table + i;

  /* Else command not found. */

  fprintf (stderr, "svn error:  `%s' is an unknown command\n", cmd_name);
  svn_cl__help (0, NULL, NULL);
  return NULL;
}


int
main (int argc, char **argv)
{
  svn_cl__cmd_desc_t* p_cmd = get_cmd_table_entry (argv[1]);
  svn_error_t *err;
  apr_pool_t *pool;

  if (p_cmd == NULL)
    return EXIT_FAILURE;

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
