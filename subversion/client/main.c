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
#include "svn_error.h"



/*** Code. ***/

/* Hrm... how to get a private enum namespace?  Do I have to use the
   svn prefix, or is there another way?  There's no storage being
   declared here, so `static' shouldn't work. */
enum command { checkout_command = 1,
               update_command,
               add_command,
               commit_command };


static void
parse_command_options (int argc,
                       char **argv,
                       int i,
                       char *progname,
                       svn_string_t **xml_file,
                       svn_string_t **target,
                       svn_vernum_t *version,
                       apr_pool_t *pool)
{
  for (; i < argc; i++)
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
      else if (strcmp (argv[i], "--version") == 0)
        {
          if (++i >= argc)
            {
              fprintf (stderr, "%s: \"--version\" needs an argument\n",
                       progname);
              exit (1);
            }
          else
            *version = (svn_vernum_t) atoi (argv[i]);
        }
      else
        *target = svn_string_create (argv[i], pool);
    }
}


/* We'll want an off-the-shelf option parsing system soon... too bad
   GNU getopt is out for copyright reasons (?).  In the meantime,
   reinvent the wheel: */  
static void
parse_options (int argc,
               char **argv,
               enum command *command,
               svn_string_t **xml_file,
               svn_string_t **target,  /* dest_dir or file */
               svn_vernum_t *version,
               apr_pool_t *pool)
{
  char *s = argv[0];  /* svn progname */
  int i;

  for (i = 1; i < argc; i++)
    {
      /* todo: do the cvs synonym thing eventually */
      if (strcmp (argv[i], "checkout") == 0)
        {
          *command = checkout_command;
          goto do_command_opts;
        }
      else if (strcmp (argv[i], "update") == 0)
        {
          *command = update_command;
          goto do_command_opts;
        }
      else if (strcmp (argv[i], "add") == 0)
        {
          *command = add_command;
          goto do_command_opts;
        }
      else if (strcmp (argv[i], "commit") == 0)
        {
          *command = commit_command;
          goto do_command_opts;
        }
      else
        {
          fprintf (stderr, "%s: unknown or untimely argument \"%s\"\n",
                   s, argv[i]);
          exit (1);
        }
    }

 do_command_opts:
  parse_command_options (argc, argv, ++i, s, xml_file, target, version, pool);

  /* Sanity checks: make sure we got what we needed. */
  if (! *command)
    {
      fprintf (stderr, "%s: no command given\n", s);
      exit (1);
    }
  if (! *xml_file)
    {
      fprintf (stderr, "%s: need \"--xml-file FILE.XML\"\n", s);
      exit (1);
    }
  else if ((*command == commit_command) && (*version == SVN_INVALID_VERNUM))
    {
      fprintf (stderr, "%s: please use \"--version VER\" "
               "to specify target version\n", s);
      exit (1);
    }
  else if ((*command == checkout_command) && (*target == NULL))
    *target = svn_string_create (".", pool);
}


int
main (int argc, char **argv)
{
  svn_error_t *err;
  apr_pool_t *pool;
  svn_vernum_t new_version = SVN_INVALID_VERNUM;
  svn_string_t *xml_file = NULL;
  svn_string_t *target = NULL;
  enum command command = 0;

  apr_initialize ();
  pool = svn_pool_create (NULL, NULL);

  parse_options (argc, argv, &command, &xml_file, &target, &new_version,
                 pool);
  
  switch (command)
    {
    case checkout_command:
      err = svn_client_checkout (target, xml_file, pool);
      break;
    case update_command:
      err = svn_client_update (target, xml_file, pool);
      break;
    case add_command:
      err = svn_client_add (target, pool);
      break;
    case commit_command:
      err = svn_client_commit (target, xml_file, new_version, pool);
      break;
    default:
      fprintf (stderr, "no command given");
      exit (1);
    }

  if (err)
    svn_handle_error (err, stdout, 1);

  apr_destroy_pool (pool);

  return 0;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
