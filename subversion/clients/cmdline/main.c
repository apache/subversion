/*
 * main.c:  Subversion command line client.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
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
    /* Here begin authentication args, add more as needed: */
    {"username",      svn_cl__auth_username_opt, 1, "specify a username ARG"},
    {"password",      svn_cl__auth_password_opt, 1, "specify a password ARG"},
    {"extensions",    'x', 1, "pass \"ARG\" as bundled options to GNU diff"},
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
    {svn_cl__recursive_opt} },

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
    {'m', 'F', 'q', 
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
    {'m', 'F', 'r', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
  { "delete", svn_cl__delete, {"del", "remove", "rm"},
    "Remove files and directories from version control.\n"
    "usage: svn delete [TARGET | URL]\n\n"
    "    If run on a working-copy TARGET, item is scheduled for deletion\n"
    "    upon next commit.  (The working item itself will only be removed\n"
    "    if --force is passed.)  If run on URL, item is deleted from\n"
    "    repository via an immediate commit.\n",
    {svn_cl__force_opt, 'm', 'F',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
  { "diff", svn_cl__diff, {"di"},
    "Display local changes in the working copy, or changes between the\n"
    "working copy and the repository if a revision is given.\n"
    "usage: svn diff [-r REV] [TARGETS]\n",
    {'r', 'D', 'x', 'n',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
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
    {'F', 'm', 'q', svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__xml_file_opt, 'r'} },
  
  { "log", svn_cl__log, {0},
    "Show the log messages for a set of revision(s) and/or file(s).\n"
    "usage: svn log [PATH1 [PATH2] ...] \n",
    {'r', 'v', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
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
    {'m', 'F', 'r', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },
  
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
    {svn_cl__recursive_opt} },
  
  { "propset", svn_cl__propset, {"pset", "ps"},
    "Set property PROPNAME to PROPVAL on files or directories.\n"
    "usage: propset PROPNAME PROPVAL [TARGETS]\n\n"
    "    Use -F (instead of PROPVAL) to get the value from a file.\n",
    {'F', 'q', svn_cl__recursive_opt} },
  
  { "revert", svn_cl__revert, {0},
    "Restore pristine working copy file (undo all local edits)\n"
    "usage: revert TARGET1 [TARGET2 [TARGET3 ... ]]\n\n"
    "    Note:  this routine does not require network access, and will\n"
    "    remove any .rej produced when a file is in a state of conflict.\n",
    {svn_cl__recursive_opt} },
  
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
    {'r'} },
 
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
format_option (char **string,
               const apr_getopt_option_t *opt,
               svn_boolean_t doc,
               apr_pool_t *pool)
{
  char *opts;

  if (opt == NULL)
    *string = apr_psprintf (pool, "?");

  if (opt->optch <= 255)  
    opts = apr_psprintf (pool, "-%c [--%s]", opt->optch, opt->name);
  else
    opts = apr_psprintf (pool, "--%s", opt->name);

  if (opt->has_arg)
    opts = apr_pstrcat (pool, opts, " arg", NULL);
  
  if (doc)
    opts = apr_pstrcat (pool, opts, ":   ", opt->description, NULL);

  *string = opts;
}



const apr_getopt_option_t *
svn_cl__get_option_from_enum (int code,
                              const apr_getopt_option_t *option_table)
{
  int i;
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
  int i;
  
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
  int i;

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

      fprintf (stream, ": %s\n", canonical_cmd->help);
      fprintf (stream, "Valid options:\n");

      /* Loop over all valid option codes attached to the subcommand */
      for (i = 0; i < SVN_CL__MAX_OPTS; i++)
        {          
          if (canonical_cmd->valid_options[i])
            {
              /* convert each option code into an option */
              option = 
                svn_cl__get_option_from_enum (canonical_cmd->valid_options[i],
                                              svn_cl__options);

              /* print the option's docstring */
              if (option)
                {
                  char *optstr;
                  format_option (&optstr, option, TRUE, pool);
                  fprintf (stream, "  %s\n", optstr);
                }
            }
        }    
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

/* Return non-zero if REV is not all digits, nor "head", "h", nor some
   case variation of same.  Otherwise, return 0. */
static int
validate_revision (const char *rev)
{
  const char *p;
  
  for (p = rev; *p; p++)
    {
      if ((*p < '0') || (*p > '9'))
        {
          if (p != rev) /* We've already seen some digits... */
            return 1;   /* ...and mixing digits w/ non-digits is invalid. */
          else
            {
              /* Okay, it's "head", "h", or some variation thereof. */

              int len = strlen (rev);

              if ((len != 1) && (len != 4))
                return 1;  /* wrong length, must be wrong string */
              else if ((len == 1) && ((rev[0] == 'h') || (rev[0] == 'H')))
                return 0;
              else if (((rev[0] == 'h') || (rev[0] == 'H'))
                       && ((rev[1] == 'e') || (rev[1] == 'E'))
                       && ((rev[2] == 'a') || (rev[2] == 'A'))
                       && ((rev[3] == 'd') || (rev[3] == 'D')))
                return 0;
              else
                return 1;
            }
        }
    }
  
  return 0;
}


/* Set OPT_STATE->start_revision and/or OPT_STATE->end_revision
 * according to ARG, where ARG is "N", "N:", ":N", or "N:M", like so:
 * 
 *    - If ARG is "N", set both OPT_STATE->start_revision and
 *      OPT_STATE->end_revision to N.
 *
 *    - If ARG is "N:head", set OPT_STATE->start_revision to N and
 *      OPT_STATE->end_revision to SVN_INVALID_REVNUM.
 * 
 *    - If ARG is "head:N", set OPT_STATE->start_revision to
 *      SVN_INVALID_REVNUM and OPT_STATE->end_revision to N.
 * 
 *    - If ARG is "N:M", set OPT_STATE->start_revision to N and
 *      OPT_STATE->end_revision to M.
 * 
 * The special case "head" is case-insensitive and may also be written
 * "h" or "H"; it is implied when a revision number is simply omitted. 
 * It means the youngest revision, which is expressed by setting the
 * appropriate field to SVN_INVALID_REVNUM.
 *
 * If ARG is invalid, return non-zero; else return zero.
 */
static int
parse_revision (svn_cl__opt_state_t *os, const char *arg, apr_pool_t *pool)
{
  char *sep = strchr (arg, ':');
  char *left_rev, *right_rev;

  if (sep)
    {
      /* There can only be one colon. */
      if (strchr (sep + 1, ':'))
        return 1;

      /* First, turn one string into two. */
      left_rev = apr_pstrdup (pool, arg);
      *(left_rev + (sep - arg)) = '\0';
      right_rev = (left_rev + (sep - arg)) + 1;
    }
  else  /* If no separator, set left_rev, right_rev to same string. */
    left_rev = right_rev = apr_pstrdup (pool, arg);

  /* Validate each revision individually. */
  if ((validate_revision (left_rev) != 0)
      || (validate_revision (right_rev) != 0))
    return 1;
    
  /* Okay, no syntax problems, parse 'em. */
  
  if ((left_rev[0] == 'h') || (left_rev[0] == 'H') || (left_rev[0] == '\0'))
    os->start_revision = SVN_INVALID_REVNUM;
  else
    os->start_revision = (svn_revnum_t) atoi (left_rev);

  if ((right_rev[0] == 'h') || (right_rev[0] == 'H') || (right_rev[0] == '\0'))
    os->end_revision = SVN_INVALID_REVNUM;
  else
    os->end_revision = (svn_revnum_t) atoi (right_rev);

  return SVN_NO_ERROR;
}


/* Set OPT_STATE->start_date and/or OPT_STATE->end_date according to
 * ARG, where ARG is "X", ":X", or "X:Y", like so:
 * 
 *    - If ARG is "X" set both OPT_STATE->start_date and
        OPT_STATE->end_date to X.
 * 
 *    - If ARG is "X:", set OPT_STATE->start_date to X and don't
 *      touch OPT_STATE->end_date.
 * 
 *    - If ARG is ":X", don't touch OPT_STATE->end_date, and set
 *      OPT_STATE->end_date to X.
 * 
 *    - If ARG is "X:Y", set OPT_STATE->start_date to X and
 *      OPT_STATE->end_date to Y.
 * 
 * If ARG is invalid, return non-zero; else return zero.
 *
 * ### todo: think more carefully about date range syntax, change this
 * accordingly.
 */
static int
parse_date (svn_cl__opt_state_t *os, const char *arg, apr_pool_t *pool)
{
  char *sep = strchr (arg, ':');

  if (sep)
    {
      /* svn_parse_date() originates in getdate.y; while I'd love to
         change it to const char *, that turns out to be a little more
         complex than just changing the qualifier.  So for now, we're
         making the copies plain char stars, to get rid of the
         compilation warning, and have filed issue #408 so we don't
         forget about this.  -kff  */
      char *left_date, *right_date;

      /* ### todo: some standard date formats contain colons.
         Eventually, we should probably allow those, and use some
         other syntax for expressing ranges.  But for now, I'm just
         going to bail if see a non-separator colon, to get this up
         and running.  -kff */
      if (strchr (sep + 1, ':'))
        return 1;

      /* Okay, no syntax problems, parse the date. */

      /* First, turn one string into two. */
      left_date = apr_pstrdup (pool, arg);
      *(left_date + (sep - arg)) = '\0';
      right_date = (left_date + (sep - arg)) + 1;

      /* Treat each string individually. */
      if (*left_date)
        apr_ansi_time_to_apr_time (&(os->start_date),
                                   svn_parse_date (left_date, NULL));
      if (*right_date)
        apr_ansi_time_to_apr_time (&(os->end_date),
                                   svn_parse_date (right_date, NULL));
    }
  else
    {
      apr_ansi_time_to_apr_time (&(os->start_date),
                                 svn_parse_date ((char *) arg, NULL));
      os->end_date = os->start_date;
    }

  return SVN_NO_ERROR;
}



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  int ret;
  apr_status_t apr_err;
  svn_error_t *err;
  apr_pool_t *pool;
  int opt_id;
  const char *opt_arg;
  apr_getopt_t *os;  
  svn_cl__opt_state_t opt_state;
  int received_opts[SVN_CL__MAX_OPTS];
  int i, num_opts = 0;
  const svn_cl__cmd_desc_t *subcommand = NULL;
  svn_boolean_t log_under_version_control = FALSE;
  svn_boolean_t log_is_pathname = FALSE;

  /* FIXME: This is a first step towards support for localization in
     `svn'.  In real life, this call would be

         setlocale (LC_ALL, "");

     so that initial help or error messages are displayed in the
     language defined by the environment.  Right now, though, we don't
     even care if the call fails.

     (Actually, this is a no-op; according to the C standard, "C" is
     the default locale at program startup.) */
  setlocale (LC_ALL, "C");


  apr_initialize ();
  pool = svn_pool_create (NULL);
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.start_revision = SVN_INVALID_REVNUM; /* default to youngest */
  opt_state.end_revision = 1;                    /* default to oldest */

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
      /* Parse the next option. */
      apr_err = apr_getopt_long (os, svn_cl__options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF (apr_err))
        break;
      else if (! APR_STATUS_IS_SUCCESS (apr_err))
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
        opt_state.message = svn_stringbuf_create (opt_arg, pool);
        break;
      case 'r':
        ret = parse_revision (&opt_state, opt_arg, pool);
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
        opt_state.xml_file = svn_stringbuf_create (opt_arg, pool);
        break;
      case 'd':
        opt_state.target = svn_stringbuf_create (opt_arg, pool);
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

          err = svn_wc_entry (&e, svn_stringbuf_create (opt_arg, pool), pool);
          if ((err == SVN_NO_ERROR) && e)
            log_under_version_control = TRUE;
        }
        break;
      case 'M':
        opt_state.modified = TRUE;
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
        opt_state.auth_username = svn_stringbuf_create (opt_arg, pool);
        break;
      case svn_cl__auth_password_opt:
        opt_state.auth_password = svn_stringbuf_create (opt_arg, pool);
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
      case 'x':
        opt_state.extensions = svn_stringbuf_create(opt_arg, pool);
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;  
      }
    }

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = svn_cl__get_canonical_command ("help");

  /* If we didn't request silence, initialize the feedback vtable. */
  if (! opt_state.quiet)
    svn_cl__init_feedback_vtable (pool);

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
    if (! subcommand_takes_option (subcommand, received_opts[i]))
      {
        char *optstr;
        const apr_getopt_option_t *badopt = 
          svn_cl__get_option_from_enum (received_opts[i], svn_cl__options);
        format_option (&optstr, badopt, FALSE, pool);
        fprintf (stderr,
                 "\nError: subcommand '%s' doesn't accept option '%s'\n\n",
                 subcommand->name, optstr);
        svn_cl__subcommand_help (subcommand->name, pool);
        svn_pool_destroy(pool);
        return EXIT_FAILURE;
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
      if (err->apr_err != SVN_ERR_CL_ARG_PARSING_ERROR)
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
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
