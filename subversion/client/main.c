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



/*** kff todo: this trace editor will get moved to its own file ***/

static svn_cl__t_cmd_desc cmd_table[] = {
  { "ad",            2, TRUE,  svn_cl__add         },
  { "add",           3, TRUE,  svn_cl__add         },
  { "checkout",      8, TRUE,  svn_cl__checkout    },
  { "ci",            2, TRUE,  svn_cl__commit      },
  { "co",            2, TRUE,  svn_cl__checkout    },
  { "commit",        6, TRUE,  svn_cl__commit      },
  { "del",           3, TRUE,  svn_cl__delete      },
  { "delete",        6, TRUE,  svn_cl__delete      },
  { "help",          4, FALSE, svn_cl__help        },
  { "new",           3, TRUE,  svn_cl__add         },
  { "pf",            2, TRUE,  svn_cl__prop_find   },
  { "pfind",         5, TRUE,  svn_cl__prop_find   },
  { "prop-find",     9, TRUE,  svn_cl__prop_find   },
  { "rm",            2, TRUE,  svn_cl__delete      },
  { "st",            2, TRUE,  svn_cl__status      },
  { "stat",          4, TRUE,  svn_cl__status      },
  { "status",        6, TRUE,  svn_cl__status      },
  { "up",            2, TRUE,  svn_cl__update      },
  { "update",        6, TRUE,  svn_cl__update      }
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
               svn_string_t **target,  /* dest_dir or file to add */
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
  if ((! *xml_file)
      && (command != ADD_COMMAND)
      && (command != STATUS_COMMAND)
      && (command != DELETE_COMMAND))
    {
      fprintf (stderr, "%s: need \"--xml-file FILE.XML\"\n", s);
      exit (1);
    }
  if (*force && (command != DELETE_COMMAND))
    {
      fprintf (stderr, "%s: \"--force\" meaningless except for delete\n", s);
      exit (1);
    }
  /* COMMIT and UPDATE must have a valid revision */
  if ((*revision == SVN_INVALID_REVNUM)
      && (  (command == COMMIT_COMMAND)
         || (command == UPDATE_COMMAND)))
    {
      fprintf (stderr, "%s: please use \"--revision VER\" "
               "to specify target revision\n", s);
      exit (1);
    }
  /* CHECKOUT, UPDATE, COMMIT and STATUS have a default target */
  if ((*target == NULL)
      &&  (  (command == CHECKOUT_COMMAND) 
          || (command == UPDATE_COMMAND)
          || (command == COMMIT_COMMAND)
          || (command == STATUS_COMMAND)
          || (command == PROP_FIND_COMMAND)))
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
  svn_cl__t_cmd_desc* pCD = cmd_table;

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


static svn_cl__t_cmd_desc*
get_cmd_table_entry (char* pz_cmd)
{
  int  hi  = (sizeof( cmd_table )/sizeof( cmd_table[0] )) - 1;
  int  lo  = 0;
  int  av, cmp;

  if (pz_cmd == NULL)
    {
      fputs( "svn error:  no command name provided\n", stderr );
      (void)svn_cl__help( 0, NULL, NULL );
      return NULL;
    }

  /* Regardless of the option chosen, the user gets --help :-) */
  if (*pz_cmd == '-')
    {
      (void)svn_cl__help( 0, NULL, NULL );
      return NULL;
    }

  for (;;)
    {
      av  = (hi + lo) / 2;
      cmp = strcmp (pz_cmd, cmd_table[av].cmd_name);

      if (cmp == 0)
        break;

      if (cmp > 0)
        lo = av + 1;
      else
        hi = av - 1;

      if (hi < lo)
        {
          fprintf (stderr, "svn error:  `%s' is an unknown command\n", pz_cmd);
          (void)svn_cl__help( 0, NULL, NULL );
          return NULL;
        }
    }

  return cmd_table + av;
}


int
main (int argc, char **argv)
{
  svn_cl__t_cmd_desc* p_cmd = get_cmd_table_entry (argv[1]);
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
 * end: */
