/*
 * main.c:  Subversion command line client.
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
#include <assert.h>
#include <locale.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_time.h"
#include "cl.h"


/*** Option Processing ***/

const apr_getopt_option_t svn_cl__options[] =
  {
    {"destination",   'd', 1, "put results in new directory ARG"}, 
    {"force",         svn_cl__force_opt, 0, "force operation to run"},
    {"help",          'h', 0, "show help on a subcommand"},
    /* ### APR is broken. we can't pass NULL for the name, as the doc says */
    {"--eek--",       '?', 0, "show help on a subcommand"},
    {"message",       'm', 1, "specify commit message \"ARG\""},
    {"quiet",         'q', 0, "print as little as possible"},
    {"recursive",     svn_cl__recursive_opt, 0, "descend recursively"},
    {"nonrecursive",  'n', 0, "operate on single directory only"},
    {"revision",      'r', 1, "specify revision number ARG (or X:Y range)"},
    {"date",          'D', 1, "specify a date ARG (instead of a revision)"},
    {"file",          'F', 1, "read data from file ARG"},
    {"xml-file",      svn_cl__xml_file_opt, 1, "read/write xml to specified file ARG"},
    {"locale",        svn_cl__locale_opt, 1, "specify a locale ARG"},
    {"version",       svn_cl__version_opt, 0, "print client version info"},
    {"verbose",       'v', 0, "print extra information"},
    {"very-verbose",  'V', 0, "print maxmimum information"},
    {"show-updates",  'u', 0, "display update information"},
    {"username",      svn_cl__auth_username_opt, 1, "specify a username ARG"},
    {"password",      svn_cl__auth_password_opt, 1, "specify a password ARG"},
    {"extensions",    'x', 1, "pass \"ARG\" as bundled options to GNU diff"},
    {"targets",       svn_cl__targets_opt, 1, "pass contents of file \"ARG\" as additional args"},
    {"xml",           svn_cl__xml_opt, 0, "output in xml"},
    {"strict",        svn_cl__strict_opt, 0, "use strict semantics"},
    {0,               0, 0}
  };


/* The maximum number of options that can be accepted by a subcommand;
   this is simply the number of unique switches that exist in the
   table above.  */
#define SVN_CL__MAX_OPTS sizeof(svn_cl__options)/sizeof(svn_cl__options[0])



/*** Command dispatch. ***/

/* The maximum number of aliases a subcommand can have. */
#define SVN_CL__MAX_ALIASES 3


/* One element of the command dispatch table. */
typedef struct svn_cl__cmd_desc_t
{
  /* The full name of this command. */
  const char *name;

  /* The function this command invokes. */
  svn_cl__cmd_proc_t *cmd_func;

  /* A list of alias names for this command. */
  const char *aliases[SVN_CL__MAX_ALIASES];

  /* A brief string describing this command, for usage messages. */
  const char *help;

  /* A list of options accepted by this command.  Each value in the
     array is a unique enum (the 2nd field in apr_getopt_option_t) */
  int valid_options[SVN_CL__MAX_OPTS];

} svn_cl__cmd_desc_t;



/* Our array of available subcommands.
 *
 * The entire list must be terminated with a entry of nulls.
 */
const svn_cl__cmd_desc_t svn_cl__cmd_table[] =
{
  { "add", svn_cl__add, {0},
    "Put files and directories under revision control, scheduling\n"
    "them for addition to repository.  They will be added in next commit.\n"
    "usage: svn add [OPTIONS] [TARGETS]\n", 
    {svn_cl__targets_opt, svn_cl__recursive_opt} },

  { "checkout", svn_cl__checkout, {"co"},
    "Check out a working copy from a repository.\n"
    "usage: svn checkout REPOS_URL\n",    
    {'d', 'r', 'D', 'q', 'n',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__xml_file_opt }  },

  { "cleanup", svn_cl__cleanup, {0},
    "Recursively clean up the working copy, removing locks, resuming\n"
    "unfinished operations, etc.\n"
    "usage: svn cleanup [TARGETS]\n",
    {0} },
  
  { "commit", svn_cl__commit, {"ci"},
    "Send changes from your working copy to the repository.\n"
    "usage: svn commit [TARGETS]\n\n"
    "   Be sure to use one of -m or -F to send a log message;\n"
    "   the -r switch is only for use with --xml-file.\n",
    {'m', 'F', 'q', 'n', svn_cl__targets_opt,
     svn_cl__force_opt, svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__xml_file_opt, 'r'} },
  
  { "copy", svn_cl__copy, {"cp"},
    "Duplicate something in working copy or repos, remembering history.\n"
    "usage: svn copy SRC DST.\n\n"
    "  SRC and DST can each be either a working copy (WC) path or URL:\n"
    "    WC  -> WC:   copy and schedule for addition (with history)\n"
    "    WC  -> URL:  immediately commit a copy of WC to URL\n"
    "    URL -> WC:   check out URL into WC, schedule for addition\n"
    "    URL -> URL:  complete server-side copy;  used to branch & tag\n",
    {'m', 'F', 'r', 'D',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
  { "delete", svn_cl__delete, {"del", "remove", "rm"},
    "Remove files and directories from version control.\n"
    "usage: svn delete [TARGET | URL]\n\n"
    "  * If run on a working-copy TARGET, item is scheduled for deletion\n"
    "    upon the next commit. Files, and directories that have not been\n"
    "    committed, are immediately removed from the working copy. The\n"
    "    command will not remove TARGETs that are, or contain, unversioned\n"
    "    or modified items; use the --force option to override this\n"
    "    behaviour.\n"
    "  * If run on an URL, item is deleted from the repository via an\n"
    "    immediate commit.\n",
    {svn_cl__force_opt, 'm', 'F', svn_cl__targets_opt,
     svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
  { "diff", svn_cl__diff, {"di"},
    "display the differences between two paths.\n"
    "usage: 1. svn diff [-r N[:M]] [PATH1 [PATH2 [PATH3 ...]]]\n"
    "       2. svn diff URL1[@N] URL2[@M]\n\n"
    "   1. each PATH can be either a working-copy path or URL.\n"
    "      If no PATHs are specified, a value of '.' is assumed.\n\n"
    "        + if PATH is a URL, then revs N and M must be given via -r.\n\n"
    "        + if PATH is a working-copy path, then -r switch means:\n"
    "            -r N:M  : server compares PATH@N and PATH@M,\n"
    "            -r N    : client compares PATH@N against working copy\n"
    "            (no -r) : client compares base and working copies of PATH\n\n"
    "   2. if alternate syntax is used, the server compares URL1 and URL2\n"
    "      at revisions N and M respectively.  If either N or M are\n"
    "      ommitted, value of HEAD is assumed.\n",
    {'r', 'D', 'x', 'n',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt} },

  { "export", svn_cl__export, {0},
    "export stuff.\n"
    "usage: 1. svn export [-r REV] URL PATH\n"
    "       2. svn export PATH1 PATH2\n\n"
    "   1. exports a clean directory tree from the repository specified by\n"
    "      URL, at revision REV if it is given, otherwise at HEAD, into \n"
    "      PATH\n\n"
    "   2. exports a clean directory tree from the working copy specified by\n"
    "      PATH1 into PATH2.  all local changes will be preserved, but files\n"
    "      not under revision control will not be copied.\n",
    {'r', 'D', 'q', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },

  { "help", svn_cl__help, {"?", "h"},
    "Display this usage message.\n"
    "usage: svn help [SUBCOMMAND1 [SUBCOMMAND2] ...]\n",
    {svn_cl__version_opt} },
  /* We need to support "--help", "-?", and all that good stuff, of
     course.  But those options, since unknown, will result in the
     help message being printed out anyway, so there's no need to
     support them explicitly. */
  
  { "import", svn_cl__import, {0},
    "Commit an unversioned file or tree into the repository.\n"
    "usage: svn import REPOS_URL [PATH] [NEW_ENTRY_IN_REPOS]\n\n"
    "    Recursively commit a copy of PATH to REPOS_URL.\n"
    "    If no 3rd arg, copy top-level contents of PATH into REPOS_URL\n"
    "    directly.  Otherwise, create NEW_ENTRY underneath REPOS_URL and\n"
    "    begin copy there.  (-r is only needed if importing to --xml-file)\n",
    {'F', 'm', 'q', 'n', svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__xml_file_opt, 'r'} },
 
  { "info", svn_cl__info, {0},
    "Display info about a resource.\n"
    "usage: svn info [PATH1 [PATH2] ...]\n\n"
    "    Print information about PATHs.\n",
    {svn_cl__targets_opt} },
 
  { "log", svn_cl__log, {0},
    "Show the log messages for a set of revision(s) and/or file(s).\n"
    "usage: svn log [URL] [PATH1 [PATH2] ...] \n"
    "    Either get the log messages for local PATHs or PATHs at the\n"
    "    URL.  If URL is given by itself, then log messages are output for\n"
    "    that specific path.  The -v option will include a list of affected\n"
    "    files for each log message.  By default, the resultant collection\n"
    "    of log messages for a given path will include logs for all the\n"
    "    revisions in which that path's node was modified, spanning copy\n"
    "    history where such exists.  This can be disabled by using the\n"
    "    --strict option.  Examples are:\n"
    "\n"
    "       svn log\n"
    "       svn log foo.c\n"
    "       svn log http://www.example.com/repo/project/foo.c\n"
    "       svn log http://www.example.com/repo/project foo.c bar.c\n",
    {'r', 'D', 'v', svn_cl__targets_opt, svn_cl__auth_username_opt,
     svn_cl__auth_password_opt, svn_cl__strict_opt, svn_cl__xml_opt} },
  
  { "merge", svn_cl__merge, {0},
    "merge:  apply the differences between two paths to a working copy path.\n"
    "usage:  svn merge PATH1[@N] PATH2[@M] [WCPATH]\n\n"
    "  * PATH1 and PATH2 are either working-copy paths or URLs, specified at\n"
    "    revisions N and M.  These are the two sources to be compared.\n"
    "  * WCPATH is the working-copy path that will receive the changes.\n\n"
    "    - If either N or M are omitted, HEAD revision is assumed.\n"   
    "    - If WCPATH is omitted, a value of '.' is assumed.\n"
    "    - If PATH1 and PATH2 are identical, an alternate syntax is allowed:\n"
    "            svn merge -rN:M PATH [WCPATH]\n",
    {'r', 'D', 'n', svn_cl__force_opt,
     svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
  { "mkdir", svn_cl__mkdir, {0},
    "Create a new directory under revision control.\n"
    "usage: mkdir [NEW_DIR | REPOS_URL].\n\n"
    "    Either create NEW_DIR in working copy scheduled for addition,\n"
    "    or create REPOS_URL via immediate commit.\n",
    {'m', 'F', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },

  { "move", svn_cl__move, {"mv", "rename", "ren"},
    "Move/rename something in working copy or repository.\n"
    "usage: move SRC DST.\n\n"
    "  NOTE:  this command is equivalent to a 'copy' and 'delete'.\n\n"
    "  SRC and DST can both be working copy (WC) paths or URLs:\n"
    "    WC  -> WC:   move and schedule for addition (with history)\n"
    "    URL -> URL:  complete server-side rename.\n",    
    {'m', 'F', 'r', 'D', svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__force_opt} },
  
  { "propdel", svn_cl__propdel, {"pdel"},
    "Remove property PROPNAME on files and directories.\n"
    "usage: propdel PROPNAME [TARGETS]\n",
    {'q', svn_cl__recursive_opt} },
  
  { "propedit", svn_cl__propedit, {"pedit", "pe"},
    "Edit property PROPNAME with $EDITOR on targets.\n"
    "usage: propedit PROPNAME [TARGETS]\n",
    {0} },
  
  { "propget", svn_cl__propget, {"pget", "pg"},
    "Print value of property PROPNAME on files or directories.\n"
    "usage: propget PROPNAME [TARGETS]\n",
    {svn_cl__recursive_opt} },
  
  { "proplist", svn_cl__proplist, {"plist", "pl"},
    "List all properties attached to files or directories.\n"
    "usage: proplist [TARGETS]\n",
    {'v', svn_cl__recursive_opt} },
  
  { "propset", svn_cl__propset, {"pset", "ps"},
    "Set property PROPNAME to PROPVAL on files or directories.\n"
    "usage: propset PROPNAME PROPVAL [TARGETS]\n\n"
    "    Use -F (instead of PROPVAL) to get the value from a file.\n",
    {'F', 'q', svn_cl__targets_opt, svn_cl__recursive_opt} },
  
  { "revert", svn_cl__revert, {0},
    "Restore pristine working copy file (undo all local edits)\n"
    "usage: revert TARGET1 [TARGET2 [TARGET3 ... ]]\n\n"
    "    Note:  this routine does not require network access, and \n"
    "    resolves any conflicted states.\n",
    {svn_cl__targets_opt, svn_cl__recursive_opt} },

  { "resolve", svn_cl__resolve, {0},
    "Remove 'conflicted' state on working copy files or directories.\n"
    "usage: resolve TARGET1 [TARGET2 [TARGET3 ... ]]\n\n"
    "    Note:  this routine does not semantically resolve conflict markers;\n"
    "    it merely removes conflict-related artifact files and allows TARGET\n"
    "    to be committed again.\n",
    {svn_cl__targets_opt} },
 
  { "status", svn_cl__status, {"stat", "st"},
    "Print the status of working copy files and directories.\n"
    "usage: svn status [TARGETS]\n\n"
    "   With no args, print only locally modified files (no network access).\n"
    "   With -u, add out-of-date information from server.\n"
    "   With -v, print excessive information on every file.\n\n"
    "Decoding --verbose output:\n"
    "Status| Out-of-date? | Local Rev | Last changed info |  Path\n"
    "    _                    965       938     kfogel      ./autogen.sh\n"
    "    _      *             965       970    sussman      ./build.conf\n"
    "    M                    965       687        joe      ./buildcheck.sh\n",
    { 'u', 'v', 'n', 'q',
      svn_cl__auth_username_opt, svn_cl__auth_password_opt } },
  
  { "switch", svn_cl__switch, {"sw"},
    "Update working copy to mirror a new URL\n"
    "usage: switch [TARGET] REPOS_URL\n\n" /* ### should args be reversed? */
    "   Note:  this is the way to move a working copy to a new branch.\n",
    {'r', 'D', 'n', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
 
  { "update", svn_cl__update, {"up"}, 
    "Bring changes from the repository into the working copy.\n"
    "usage: update [TARGETS]\n\n"
    "  If no revision given, bring working copy up-to-date with HEAD rev.\n"
    "  Else synchronize working copy to revision given by -r or -D.\n",
    {'r', 'D', 'n', svn_cl__auth_username_opt,
     svn_cl__auth_password_opt, svn_cl__xml_file_opt} },

  { NULL, NULL, {0}, NULL, {0} }
};




/* Return the entry in svn_cl__cmd_table whose name matches CMD_NAME,
 * or NULL if none.  CMD_NAME may be an alias. */
static const svn_cl__cmd_desc_t *
svn_cl__get_canonical_command (const char *cmd_name)
{
  int i = 0;

  if (cmd_name == NULL)
    return NULL;

  while (svn_cl__cmd_table[i].name) {
    int j;
    if (strcmp (cmd_name, svn_cl__cmd_table[i].name) == 0)
      return svn_cl__cmd_table + i;
    for (j = 0; 
         (j < SVN_CL__MAX_ALIASES) && svn_cl__cmd_table[i].aliases[j]; 
         j++)
      if (strcmp (cmd_name, svn_cl__cmd_table[i].aliases[j]) == 0)
        return svn_cl__cmd_table + i;

    i++;
  }

  /* If we get here, there was no matching command name or alias. */
  return NULL;
}




/*** 'help' processing ***/

/* Print an option OPT nicely into a STRING allocated in POOL.  If DOC
   is set, include generic documentation string of option.*/
static void
format_option (const char **string,
               const apr_getopt_option_t *opt,
               svn_boolean_t doc,
               apr_pool_t *pool)
{
  char *opts;

  if (opt == NULL)
    {
      *string = "?";
      return;
    }

  /* We have a valid option which may or may not have a "short
     name" (a single-character alias for the long option). */
  if (opt->optch <= 255)  
    opts = apr_psprintf (pool, "-%c [--%s]", opt->optch, opt->name);
  else
    opts = apr_psprintf (pool, "--%s", opt->name);

  if (opt->has_arg)
    opts = apr_pstrcat (pool, opts, " arg", NULL);

  if (doc)
    opts = apr_psprintf (pool, "%-24s : %s", opts, opt->description);

  *string = opts;
}



const apr_getopt_option_t *
svn_cl__get_option_from_enum (int code,
                              const apr_getopt_option_t *option_table)
{
  apr_size_t i;
  const apr_getopt_option_t *opt = NULL;

  for (i = 0; i < SVN_CL__MAX_OPTS; i++)
    {
      if (option_table[i].optch == code)
        {
          opt = &(option_table[i]);
          break;
        }
    }
  
  return opt;
}


/* Return TRUE iff subcommand COMMAND has OPTION_CODE listed within
   it.  Else return FALSE. */
static svn_boolean_t
subcommand_takes_option (const svn_cl__cmd_desc_t *command,
                         int option_code)
{
  apr_size_t i;
  
  for (i = 0; i < SVN_CL__MAX_OPTS; i++)
    {          
      if (command->valid_options[i] == option_code)
        return TRUE;
    }
  return FALSE;
}


/* Print the canonical command name for CMD, all its aliases,
   and if HELP is set, print the help string for the command too. */
static void
print_command_info (const svn_cl__cmd_desc_t *cmd_desc,
                    svn_boolean_t help, 
                    apr_pool_t *pool,
                    FILE *stream)
{
  const svn_cl__cmd_desc_t *canonical_cmd
    = svn_cl__get_canonical_command (cmd_desc->name);
  svn_boolean_t first_time;
  apr_size_t i;

  /* Print the canonical command name. */
  fputs (canonical_cmd->name, stream);

  /* Print the list of aliases. */
  first_time = TRUE;
  for (i = 0; i < SVN_CL__MAX_ALIASES; i++) 
    {
      if (canonical_cmd->aliases[i] == NULL)
        break;

      if (first_time) {
        fprintf (stream, " (");
        first_time = FALSE;
      }
      else
        fprintf (stream, ", ");
      
      fprintf (stream, "%s", canonical_cmd->aliases[i]);
    }

  if (! first_time)
    fprintf (stream, ")");
  
  if (help)
    {
      const apr_getopt_option_t *option;
      svn_boolean_t have_options = FALSE;

      fprintf (stream, ": %s", canonical_cmd->help);

      /* Loop over all valid option codes attached to the subcommand */
      for (i = 0; i < SVN_CL__MAX_OPTS; i++)
        {
          if (canonical_cmd->valid_options[i])
            {
              if (have_options == FALSE)
                {
                  fprintf (stream, "\nValid options:\n");
                  have_options = TRUE;
                }

              /* convert each option code into an option */
              option = 
                svn_cl__get_option_from_enum (canonical_cmd->valid_options[i],
                                              svn_cl__options);

              /* print the option's docstring */
              if (option)
                {
                  const char *optstr;
                  format_option (&optstr, option, TRUE, pool);
                  fprintf (stream, "  %s\n", optstr);
                }
            }
        }

      if (have_options)
        fprintf (stream, "\n");
    }
}



/* Print a generic (non-command-specific) usage message. */
void
svn_cl__print_generic_help (apr_pool_t *pool, FILE *stream)
{
  static const char usage[] =
    "usage: svn <subcommand> [options] [args]\n"
    "Type \"svn help <subcommand>\" for help on a specific subcommand.\n"
    "\n"
    "Most subcommands take file and/or directory arguments, recursing\n"
    "on the directories.  If no arguments are supplied to such a\n"
    "command, it will recurse on the current directory (inclusive) by\n" 
    "default.\n"
    "\n"
    "Available subcommands:\n";

  static const char info[] =
    "Subversion is a tool for revision control.\n"
    "For additional information, see http://subversion.tigris.org\n";

  int i = 0;

  fprintf (stream, "%s", usage);
  while (svn_cl__cmd_table[i].name) 
    {
      fprintf (stream, "   ");
      print_command_info (svn_cl__cmd_table + i, FALSE, pool, stream);
      fprintf (stream, "\n");
      i++;
    }

  fprintf (stream, "\n");
  fprintf (stream, "%s\n", info);

}


/* Helper function that will print the usage test of a subcommand
 * given the subcommand name as a char*. This function is also
 * used by subcommands that need to print a usage message */

void
svn_cl__subcommand_help (const char* subcommand,
                         apr_pool_t *pool)
{
  const svn_cl__cmd_desc_t *cmd =
    svn_cl__get_canonical_command (subcommand);
    
  if (cmd)
    print_command_info (cmd, TRUE, pool, stdout);
  else
    fprintf (stderr, "\"%s\": unknown command.\n\n", subcommand);
}



/*** Parsing "X:Y"-style arguments. ***/

/* If WORD matches one of the special revision descriptors,
 * case-insensitively, set *REVISION accordingly:
 *
 *   - For "head", set REVISION->kind to svn_client_revision_head.
 *
 *   - For "first", set REVISION->kind to svn_client_revision_number
 *     and REVISION->value.number to 0.  ### iffy, but might be useful
 *     when mixed with dates ###
 *
 *   - For "prev", set REVISION->kind to svn_client_revision_previous.
 *
 *   - For "base", set REVISION->kind to svn_client_revision_base.
 *
 *   - For "committed" or "changed", set REVISION->kind to
 *     svn_client_revision_committed.
 *
 * If match, return 1, else return 0 and don't touch REVISION.
 *
 * ### should we enforce a requirement that users write out these
 * words in full?  Actually, we probably will need to start enforcing
 * it as date parsing gets more sophisticated and the chances of a
 * first-letter overlap between a valid date and a valid word go up.
 */
static int
revision_from_word (svn_client_revision_t *revision, const char *word)
{
  if (strcasecmp (word, "head") == 0)
    {
      revision->kind = svn_client_revision_head;
    }
  else if (strcasecmp (word, "first") == 0)
    {
      revision->kind = svn_client_revision_number;
      revision->value.number = 0;
    }
  else if (strcasecmp (word, "prev") == 0)
    {
      revision->kind = svn_client_revision_previous;
    }
  else if (strcasecmp (word, "base") == 0)
    {
      revision->kind = svn_client_revision_base;
    }
  else if ((strcasecmp (word, "committed") == 0)
           || (strcasecmp (word, "changed") == 0))
    {
      revision->kind = svn_client_revision_committed;
    }
  else
    return 0;

  return 1;
}


/* Return non-zero if REV is all digits, else return 0. */
static int
valid_revision_number (const char *rev)
{
  while (*rev)
    {
      if (! apr_isdigit (*rev))
        return 0;

      /* Note: Keep this increment out of the apr_isdigit call, which
         is probably a macro, although you can supposedly #undef to
         get the function definition... But wait, I've said too much
         already.  Let us speak of this no more tonight, for there are
         strange doings in the Shire of late. */
      rev++;
    }

  return 1;
}


svn_boolean_t
svn_cl__parse_revision (svn_cl__opt_state_t *os,
                        const char *arg,
                        apr_pool_t *pool)
{
  char *left_rev, *right_rev;
  char *sep;

  /* Operate on a copy of the argument. */
  left_rev = apr_pstrdup (pool, arg);
  
  if ((sep = strchr (arg, ':')))
    {
      /* There can only be one colon. */
      if (strchr (sep + 1, ':'))
        return TRUE;

      *(left_rev + (sep - arg)) = '\0';
      right_rev = (left_rev + (sep - arg)) + 1;

      /* If there was a separator, both revisions must be present. */
      if ((! *left_rev) || (! *right_rev))
        return TRUE;
    }
  else  /* no separator */
    right_rev = NULL;

  /* Now left_rev holds N and right_rev holds M or null. */

  if (! revision_from_word (&(os->start_revision), left_rev))
    {
      if (! valid_revision_number (left_rev))
        return TRUE;

      os->start_revision.kind = svn_client_revision_number;
      os->start_revision.value.number = SVN_STR_TO_REV (left_rev);
    }

  if (right_rev)
    {
      if (! revision_from_word (&(os->end_revision), right_rev))
        {
          if (! valid_revision_number (right_rev))
            return TRUE;

          os->end_revision.kind = svn_client_revision_number;
          os->end_revision.value.number = SVN_STR_TO_REV (right_rev);
        }
    }

  return FALSE;
}


/* Set OPT_STATE->start_revision and/or OPT_STATE->end_revision
 * according to ARG, where ARG is "X" or "X:Y", like so:
 * 
 *    - If ARG is "X", set OPT_STATE->start_revision's kind to
 *      svn_client_revision_date and value to the apr_time_t for X,
 *      and leave OPT_STATE->end_revision untouched.
 *
 *    - If ARG is "X:Y", set OPT_STATE->start_revision's and
 *      OPT_STATE->end_revision's kinds to svn_client_revision_date
 *      and values to (apr_time_t) X and Y respectively.
 * 
 * X and/or Y may be one of the special revision descriptors
 * recognized by revision_from_word().
 *
 * If ARG is invalid, return TRUE; else return FALSE.
 * It is invalid to omit a revision (as in, ":", "X:" or ":Y").
 *
 * Note:
 *
 * It is typical, though not required, for OPT_STATE->start_revision
 * and OPT_STATE->end_revision to be svn_client_revision_unspecified
 * kind on entry.
 */
static svn_boolean_t
parse_date (svn_cl__opt_state_t *os, const char *arg, apr_pool_t *pool)
{
  char *left_date, *right_date;
  char *sep;

  /* Operate on a copy of the argument. */
  left_date = apr_pstrdup (pool, arg);

  if ((sep = strchr (arg, ':')))
    {
      /* ### todo: some standard date formats contain colons.
         Eventually, we should probably allow those, and use some
         other syntax for expressing ranges.  But for now, I'm just
         going to bail if see a non-separator colon, to get this up
         and running.  -kff */
      if (strchr (sep + 1, ':'))
        return TRUE;

      /* First, turn one string into two. */
      *(left_date + (sep - arg)) = '\0';
      right_date = (left_date + (sep - arg)) + 1;

      /* If there was a separator, both dates must be present. */
      if ((! *left_date) || (! *right_date))
        return TRUE;
    }
  else  /* no separator */
    right_date = NULL;
    
  /* Now left_date holds X and right_date holds Y or null. */

  if (! revision_from_word (&(os->start_revision), left_date))
    {
      os->start_revision.kind = svn_client_revision_date;
      apr_time_ansi_put (&(os->start_revision.value.date),
                         svn_parse_date (left_date, NULL));
      /* ### todo: check if apr_time_t is valid? */
    }

  if (right_date)
    {
      if (! revision_from_word (&(os->end_revision), right_date))
        {
          os->end_revision.kind = svn_client_revision_date;
          apr_time_ansi_put (&(os->end_revision.value.date),
                             svn_parse_date (right_date, NULL));
          /* ### todo: check if apr_time_t is valid? */
        }
    }

  return FALSE;
}



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  svn_error_t *err;
  apr_pool_t *pool;
  int opt_id, err2;
  apr_getopt_t *os;  
  svn_cl__opt_state_t opt_state;
  int received_opts[SVN_CL__MAX_OPTS];
  int i, num_opts = 0;
  const svn_cl__cmd_desc_t *subcommand = NULL;
  svn_boolean_t log_under_version_control = FALSE;
  svn_boolean_t log_is_pathname = FALSE;
  apr_status_t apr_err;

  /* FIXME: This is a first step towards support for localization in
     `svn'.  In real life, this call would be

         setlocale (LC_ALL, "");

     so that initial help or error messages are displayed in the
     language defined by the environment.  Right now, though, we don't
     even care if the call fails.

     (Actually, this is a no-op; according to the C standard, "C" is
     the default locale at program startup.) */
  setlocale (LC_ALL, "C");


  apr_err = apr_initialize ();
  if (apr_err)
    {
      fprintf (stderr, "error: apr_initialize\n");
      return EXIT_FAILURE;
    }
  err2 = atexit (apr_terminate);
  if (err2)
    {
      fprintf (stderr, "error: atexit returned %d\n", err2);
      return EXIT_FAILURE;
    }
  pool = svn_pool_create (NULL);
  memset (&opt_state, 0, sizeof (opt_state));

  opt_state.start_revision.kind = svn_client_revision_unspecified;
  opt_state.end_revision.kind = svn_client_revision_unspecified;
  
  /* No args?  Show usage. */
  if (argc <= 1)
    {
      svn_cl__help (NULL, NULL, pool);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* Else, parse options. */
  apr_getopt_init (&os, pool, argc, argv);
  os->interleave = 1;
  while (1)
    {
      const char *opt_arg;
      svn_boolean_t ret;

      /* Parse the next option. */
      apr_err = apr_getopt_long (os, svn_cl__options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF (apr_err))
        break;
      else if (apr_err)
        {
          svn_cl__help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      /* Stash the option code in an array before parsing it. */
      received_opts[num_opts] = opt_id;
      num_opts++;

      switch (opt_id) {
      case 'm':
        {
          apr_finfo_t finfo;
          if (apr_stat(&finfo, opt_arg, APR_FINFO_MIN, pool) == APR_SUCCESS)
            {
              /* woah! that log message is a file. I doubt the user
                 intended that. */
              log_is_pathname = TRUE;
            }
        }
        opt_state.message = apr_pstrdup (pool, opt_arg);
        break;
      case 'r':
        ret = svn_cl__parse_revision (&opt_state, opt_arg, pool);
        if (ret)
          {
            svn_handle_error (svn_error_createf
                              (SVN_ERR_CL_ARG_PARSING_ERROR,
                               0, NULL, pool,
                               "Syntax error in revision argument \"%s\"",
                               opt_arg),
                              stderr, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        break;
      case 'D':
        ret = parse_date (&opt_state, opt_arg, pool);
        if (ret)
          {
            svn_handle_error (svn_error_createf
                              (SVN_ERR_CL_ARG_PARSING_ERROR,
                               0, NULL, pool,
                               "Unable to parse \"%s\"", opt_arg),
                              stderr, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        break;
      case 'v':
        opt_state.verbose = TRUE;
        break;
      case 'V':
        opt_state.very_verbose = TRUE;
        break;
      case 'u':
        opt_state.update = TRUE;
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case svn_cl__xml_file_opt:
        opt_state.xml_file = apr_pstrdup (pool, opt_arg);
        break;
      case 'd':
        opt_state.target = apr_pstrdup (pool, opt_arg);
        break;
      case 'F':
        err = svn_string_from_file (&(opt_state.filedata), opt_arg, pool);
        if (err)
          {
            svn_handle_error (err, stdout, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        /* Find out if log message file is under revision control. */
        {
          svn_wc_entry_t *e;

          err = svn_wc_entry (&e, opt_arg, FALSE, pool);
          if ((err == SVN_NO_ERROR) && e)
            log_under_version_control = TRUE;
        }
        break;
      case svn_cl__targets_opt:
	{
	  svn_stringbuf_t *buffer;
	  err = svn_string_from_file (&buffer, opt_arg, pool);
	  if (err)
	    {
	      svn_handle_error (err, stdout, FALSE);
	      svn_pool_destroy (pool);
	      return EXIT_FAILURE;
	    }
	  opt_state.targets = svn_cstring_split (buffer->data, "\n\r",
                                                 TRUE, pool);
	}
        break;
      case svn_cl__force_opt:
        opt_state.force = TRUE;
        break;
      case svn_cl__recursive_opt:
        opt_state.recursive = TRUE;
        break;
      case 'n':
        opt_state.nonrecursive = TRUE;
        break;
      case svn_cl__version_opt:
        opt_state.version = TRUE;
        opt_state.help = TRUE;
        break;
      case svn_cl__auth_username_opt:
        opt_state.auth_username = apr_pstrdup (pool, opt_arg);
        break;
      case svn_cl__auth_password_opt:
        opt_state.auth_password = apr_pstrdup (pool, opt_arg);
        break;
      case svn_cl__locale_opt:
        /* The only locale name that ISO C defines is the "C" locale;
           using any other argument is not portable. But that's O.K.,
           because the main purpose of this option is:

              a) support for wrapper programs which parse `svn's
                 output, and should call `svn --locale=C' to get
                 predictable results; and

              b) for testing various translations without having to
                 twiddle with the environment.
        */
        if (NULL == setlocale (LC_ALL, opt_arg))
          {
            err = svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR,
                                     0, NULL, pool,
                                     "The locale `%s' can not be set",
                                     opt_arg);
            svn_handle_error (err, stderr, FALSE);
          }
        break;
      case svn_cl__xml_opt:
        opt_state.xml = TRUE;
        break;
      case svn_cl__strict_opt:
        opt_state.strict = TRUE;
        break;
      case 'x':
        opt_state.extensions = apr_pstrdup (pool, opt_arg);
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;  
      }
    }

  /* ### This really belongs in libsvn_client.  The trouble is,
     there's no one place there to run it from, no
     svn_client_init().  We'd have to add it to all the public
     functions that a client might call.  It's unmaintainable to do
     initialization from within libsvn_client itself, but it seems
     burdensome to demand that all clients call svn_client_init()
     before calling any other libsvn_client function... On the other
     hand, the alternative is effective to demand that they call
     svn_config_ensure() instead, so maybe we should have a generic
     init function anyway.  Thoughts?  */
  err = svn_config_ensure (pool);
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = svn_cl__get_canonical_command ("help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "subcommand argument required\n");
          svn_cl__help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_cl__get_canonical_command (first_arg);
          if (subcommand == NULL)
            {
              /* FIXME: should we print "unknown foo" ?? seems ok */
              fprintf (stderr, "unknown command: %s\n", first_arg);
              svn_cl__help (NULL, NULL, pool);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
    }

  /* If we made it this far, then we definitely have the subcommand,
     so call it.  But first check that it wasn't passed any
     inappropriate options. */
  for (i = 0; i < num_opts; i++)
    {
      opt_id = received_opts[i];

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! subcommand_takes_option (subcommand, opt_id))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt = 
            svn_cl__get_option_from_enum (opt_id, svn_cl__options);
          format_option (&optstr, badopt, FALSE, pool);
          fprintf (stderr,
                   "\nError: subcommand '%s' doesn't accept option '%s'\n\n",
                   subcommand->name, optstr);
          svn_cl__subcommand_help (subcommand->name, pool);
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }
    }

  if (subcommand->cmd_func == svn_cl__commit)
    {
      /* If the log message file is under revision control, that's
         probably not what the user intended. */
      if (log_under_version_control && (! opt_state.force))
        {
          svn_handle_error
            (svn_error_create (SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE,
                               0, NULL, pool,
                               "Log message file is a versioned file; "
                               "use `--force' to override."),
             stderr, FALSE);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      /* If the log message is just a pathname, then the user probably did
         not intend that. */
      if (log_is_pathname && !opt_state.force)
        {
          svn_handle_error
            (svn_error_create (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME,
                               0, NULL, pool,
                               "The log message is a pathname "
                               "(was -F intended?); use `--force' "
                               "to override."),
             stderr, FALSE);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
    }

  err = (*subcommand->cmd_func) (os, &opt_state, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        svn_cl__subcommand_help (subcommand->name, pool);
      else
        svn_handle_error (err, stderr, 0);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }
  else
    {
      svn_pool_destroy (pool);
      return EXIT_SUCCESS;
    }
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
