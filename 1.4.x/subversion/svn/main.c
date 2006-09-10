/*
 * main.c:  Subversion command line client.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_signal.h>

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_diff.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_auth.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Option Processing ***/

/* Option codes and descriptions for the command line client.
 *
 * The entire list must be terminated with an entry of nulls.
 */
const apr_getopt_option_t svn_cl__options[] =
{
  {"force",         svn_cl__force_opt, 0, N_("force operation to run")},
  {"force-log",     svn_cl__force_log_opt, 0,
                    N_("force validity of log message source")},
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"message",       'm', 1, N_("specify log message ARG")},
  {"quiet",         'q', 0, N_("print as little as possible")},
  {"recursive",     'R', 0, N_("descend recursively")},
  {"non-recursive", 'N', 0, N_("operate on single directory only")},
  {"change",        'c', 1, N_
   ("the change made by revision ARG (like -r ARG-1:ARG)\n"
    "                             If ARG is negative this is like -r ARG:ARG-1")
  },
  {"revision",      'r', 1, N_
   ("ARG (some commands also take ARG1:ARG2 range)\n"
    "                             A revision argument can be one of:\n"
    "                                NUMBER       revision number\n"
    "                                '{' DATE '}' revision at start of the date\n"
    "                                'HEAD'       latest in repository\n"
    "                                'BASE'       base rev of item's working copy\n"
    "                                'COMMITTED'  last commit at or before BASE\n"
    "                                'PREV'       revision just before COMMITTED")
   /* spacing corresponds to svn_opt_format_option */
  },
  {"file",          'F', 1, N_("read log message from file ARG")},
  {"incremental",   svn_cl__incremental_opt, 0,
                    N_("give output suitable for concatenation")},
#ifndef AS400
  {"encoding",      svn_cl__encoding_opt, 1,
                    N_("treat value as being in charset encoding ARG")},
#endif
  {"version",       svn_cl__version_opt, 0,
                    N_("show program version information")},
  {"verbose",       'v', 0, N_("print extra information")},
  {"show-updates",  'u', 0, N_("display update information")},
  {"username",      svn_cl__auth_username_opt, 1,
                    N_("specify a username ARG")},
  {"password",      svn_cl__auth_password_opt, 1,
                    N_("specify a password ARG")},
#ifndef AS400
  {"extensions",    'x', 1,
                    N_("Default: '-u'. When Subversion is invoking an\n"
                       "                            "
                       " external diff program, ARG is simply passed along\n"
                       "                            "
                       " to the program. But when Subversion is using its\n"
                       "                            "
                       " default internal diff implementation, or when\n"
                       "                            "
                       " Subversion is displaying blame annotations, ARG\n"
                       "                            "
                       " could be any of the following:\n"
                       "                            "
                       "    -u (--unified):\n"
                       "                            "
                       "       Output 3 lines of unified context.\n"
                       "                            "
                       "    -b (--ignore-space-change):\n"
                       "                            "
                       "       Ignore changes in the amount of white space.\n"
                       "                            "
                       "    -w (--ignore-all-space):\n"
                       "                            "
                       "       Ignore all white space.\n"
                       "                            "
                       "    --ignore-eol-style:\n"
                       "                            "
                       "       Ignore changes in EOL style")},
#endif
  {"targets",       svn_cl__targets_opt, 1,
                    N_("pass contents of file ARG as additional args")},
  {"xml",           svn_cl__xml_opt, 0, N_("output in XML")},
  {"strict",        svn_cl__strict_opt, 0, N_("use strict semantics")},
  {"stop-on-copy",  svn_cl__stop_on_copy_opt, 0,
                    N_("do not cross copies while traversing history")},
  {"no-ignore",     svn_cl__no_ignore_opt, 0,
                    N_("disregard default and svn:ignore property ignores")},
  {"no-auth-cache", svn_cl__no_auth_cache_opt, 0,
                    N_("do not cache authentication tokens")},
  {"non-interactive", svn_cl__non_interactive_opt, 0,
                    N_("do no interactive prompting")},
  {"dry-run",       svn_cl__dry_run_opt, 0,
                    N_("try operation but make no changes")},
  {"no-diff-deleted", svn_cl__no_diff_deleted, 0,
                    N_("do not print differences for deleted files")},
  {"notice-ancestry", svn_cl__notice_ancestry_opt, 0,
                    N_("notice ancestry when calculating differences")},
  {"ignore-ancestry", svn_cl__ignore_ancestry_opt, 0,
                    N_("ignore ancestry when calculating merges")},
  {"ignore-externals", svn_cl__ignore_externals_opt, 0,
                    N_("ignore externals definitions")},
#ifndef AS400
  {"diff-cmd",      svn_cl__diff_cmd_opt, 1,
                    N_("use ARG as diff command")},
  {"diff3-cmd",     svn_cl__merge_cmd_opt, 1,
                    N_("use ARG as merge command")},
  {"editor-cmd",    svn_cl__editor_cmd_opt, 1,
                    N_("use ARG as external editor")},
#endif
  {"old",           svn_cl__old_cmd_opt, 1,
                    N_("use ARG as the older target")},
  {"new",           svn_cl__new_cmd_opt, 1,
                    N_("use ARG as the newer target")},
  {"revprop",       svn_cl__revprop_opt, 0,
                    N_("operate on a revision property (use with -r)")},
  {"relocate",      svn_cl__relocate_opt, 0,
                    N_("relocate via URL-rewriting")},
  {"config-dir",    svn_cl__config_dir_opt, 1,
                    N_("read user configuration files from directory ARG")},
  {"auto-props",    svn_cl__autoprops_opt, 0,
                    N_("enable automatic properties")},
  {"no-auto-props", svn_cl__no_autoprops_opt, 0,
                    N_("disable automatic properties")},
  {"native-eol",    svn_cl__native_eol_opt, 1,
                    N_("use a different EOL marker than the standard\n"
                       "                             "
                       "system marker for files with the svn:eol-style\n"
                       "                             "
                       "property set to 'native'.\n"
                       "                             "
                       "ARG may be one of 'LF', 'CR', 'CRLF'")},
  {"limit",         svn_cl__limit_opt, 1,
                    N_("maximum number of log entries")},
  {"no-unlock",     svn_cl__no_unlock_opt, 0,
                    N_("don't unlock the targets")},
  {"summarize",     svn_cl__summarize, 0,
                    N_("show a summary of the results")},
  {0,               0, 0, 0}
};



/*** Command dispatch. ***/

/* Our array of available subcommands.
 *
 * The entire list must be terminated with an entry of nulls.
 *
 * In most of the help text "PATH" is used where a working copy path is
 * required, "URL" where a repository URL is required and "TARGET" when
 * either a path or an url can be used.  Hmm, should this be part of the
 * help text?
 */
/* Options for authentication. */
#define SVN_CL__AUTH_OPTIONS svn_cl__auth_username_opt, \
                             svn_cl__auth_password_opt, \
                             svn_cl__no_auth_cache_opt, \
                             svn_cl__non_interactive_opt
/* Options for giving a log message.  (Some of these also have other uses.) 
 *
 * In theory, we should include svn_cl__non_interactive_opt here too,
 * because all the log-message-taking commands have the potential to
 * pop up an editor, and svn_cl__non_interactive_opt is the way to
 * prevent that.  But so far, any command that includes these options
 * also includes SVN_CL__AUTH_OPTIONS, which of course already
 * contains svn_cl__non_interactive_opt, so we get it for free.
 */
#define SVN_CL__LOG_MSG_OPTIONS 'm', 'F', \
                                svn_cl__force_log_opt, \
                                svn_cl__editor_cmd_opt, \
                                svn_cl__encoding_opt

const svn_opt_subcommand_desc2_t svn_cl__cmd_table[] =
{
  { "add", svn_cl__add, {0}, N_
    ("Put files and directories under version control, scheduling\n"
     "them for addition to repository.  They will be added in next commit.\n"
     "usage: add PATH...\n"),
    {svn_cl__targets_opt, 'N', 'q', svn_cl__config_dir_opt,
     svn_cl__force_opt, svn_cl__no_ignore_opt, svn_cl__autoprops_opt,
     svn_cl__no_autoprops_opt} },

  { "blame", svn_cl__blame, {"praise", "annotate", "ann"}, N_
    ("Output the content of specified files or\n"
     "URLs with revision and author information in-line.\n"
     "usage: blame TARGET[@REV]...\n"
     "\n"
     "  If specified, REV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', 'v', svn_cl__incremental_opt, svn_cl__xml_opt, 'x',
     svn_cl__force_opt, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "cat", svn_cl__cat, {0}, N_
    ("Output the content of specified files or URLs.\n"
     "usage: cat TARGET[@REV]...\n"
     "\n"
     "  If specified, REV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "checkout", svn_cl__checkout, {"co"}, N_
    ("Check out a working copy from a repository.\n"
     "usage: checkout URL[@REV]... [PATH]\n"
     "\n"
     "  If specified, REV determines in which revision the URL is first\n"
     "  looked up.\n"
     "\n"
     "  If PATH is omitted, the basename of the URL will be used as\n"
     "  the destination. If multiple URLs are given each will be checked\n"
     "  out into a sub-directory of PATH, with the name of the sub-directory\n"
     "  being the basename of the URL.\n"),
    {'r', 'q', 'N', SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt,
     svn_cl__ignore_externals_opt} },

  { "cleanup", svn_cl__cleanup, {0}, N_
    ("Recursively clean up the working copy, removing locks, resuming\n"
     "unfinished operations, etc.\n"
     "usage: cleanup [PATH...]\n"),
    {svn_cl__merge_cmd_opt, svn_cl__config_dir_opt} },

  { "commit", svn_cl__commit, {"ci"},
#ifndef AS400
    N_("Send changes from your working copy to the repository.\n"
       "usage: commit [PATH...]\n"
       "\n"
       "  A log message must be provided, but it can be empty.  If it is not\n"
       "  given by a --message or --file option, an editor will be started.\n"
       "  If any targets are (or contain) locked items, those will be\n"
       "  unlocked after a successful commit.\n"),
#else
    N_("Send changes from your working copy to the repository.\n"
       "usage: commit [PATH...]\n"
       "\n"
       "  A log message must be provided, but it can be empty.\n"
       "  OS400 does not support the starting of an editor,\n"
       "  so --message or --file must be used. If any targets are\n"
       "  (or contain) locked items, those will be unlocked after a\n"
       "  successful commit.\n"),
#endif
    {'q', 'N', svn_cl__targets_opt, svn_cl__no_unlock_opt,
     SVN_CL__LOG_MSG_OPTIONS, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "copy", svn_cl__copy, {"cp"}, N_
    ("Duplicate something in working copy or repository, remembering history.\n"
     "usage: copy SRC DST\n"
     "\n"
     "  SRC and DST can each be either a working copy (WC) path or URL:\n"
     "    WC  -> WC:   copy and schedule for addition (with history)\n"
     "    WC  -> URL:  immediately commit a copy of WC to URL\n"
     "    URL -> WC:   check out URL into WC, schedule for addition\n"
     "    URL -> URL:  complete server-side copy;  used to branch & tag\n"),
    {'r', 'q',
     SVN_CL__LOG_MSG_OPTIONS, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "delete", svn_cl__delete, {"del", "remove", "rm"}, N_
    ("Remove files and directories from version control.\n"
     "usage: 1. delete PATH...\n"
     "       2. delete URL...\n"
     "\n"
     "  1. Each item specified by a PATH is scheduled for deletion upon\n"
     "    the next commit.  Files, and directories that have not been\n"
     "    committed, are immediately removed from the working copy.\n"
     "    PATHs that are, or contain, unversioned or modified items will\n"
     "    not be removed unless the --force option is given.\n"
     "\n"
     "  2. Each item specified by a URL is deleted from the repository\n"
     "    via an immediate commit.\n"),
    {svn_cl__force_opt, 'q', svn_cl__targets_opt,
     SVN_CL__LOG_MSG_OPTIONS, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "diff", svn_cl__diff, {"di"}, N_
    ("Display the differences between two revisions or paths.\n"
     "usage: 1. diff [-c M | -r N[:M]] [TARGET[@REV]...]\n"
     "       2. diff [-r N[:M]] --old=OLD-TGT[@OLDREV] [--new=NEW-TGT[@NEWREV]] \\\n"
     "               [PATH...]\n"
     "       3. diff OLD-URL[@OLDREV] NEW-URL[@NEWREV]\n"
     "\n"
     "  1. Display the changes made to TARGETs as they are seen in REV between\n"
     "     two revisions.  TARGETs may be all working copy paths or all URLs.\n"
     "     If TARGETs are working copy paths, N defaults to BASE and M to the\n"
     "     working copy; if URLs, N must be specified and M defaults to HEAD.\n"
     "     The '-c M' option is equivalent to '-r N:M' where N = M-1.\n"
     "     Using '-c -M' does the reverse: '-r M:N' where N = M-1.\n"
     "\n"
     "  2. Display the differences between OLD-TGT as it was seen in OLDREV and\n"
     "     NEW-TGT as it was seen in NEWREV.  PATHs, if given, are relative to\n"
     "     OLD-TGT and NEW-TGT and restrict the output to differences for those\n"
     "     paths.  OLD-TGT and NEW-TGT may be working copy paths or URL[@REV]. \n"
     "     NEW-TGT defaults to OLD-TGT if not specified.  -r N makes OLDREV default\n"
     "     to N, -r N:M makes OLDREV default to N and NEWREV default to M.\n"
     "\n"
     "  3. Shorthand for 'svn diff --old=OLD-URL[@OLDREV] --new=NEW-URL[@NEWREV]'\n"
     "\n"
     "  Use just 'svn diff' to display local modifications in a working copy.\n"),
    {'r', 'c', svn_cl__old_cmd_opt, svn_cl__new_cmd_opt, 'N',
     svn_cl__diff_cmd_opt, 'x', svn_cl__no_diff_deleted,
     svn_cl__notice_ancestry_opt, svn_cl__summarize,
     svn_cl__force_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt} },

  { "export", svn_cl__export, {0}, N_
    ("Create an unversioned copy of a tree.\n"
     "usage: 1. export [-r REV] URL[@PEGREV] [PATH]\n"
     "       2. export [-r REV] PATH1[@PEGREV] [PATH2]\n"
     "\n"
     "  1. Exports a clean directory tree from the repository specified by\n"
     "     URL, at revision REV if it is given, otherwise at HEAD, into\n"
     "     PATH. If PATH is omitted, the last component of the URL is used\n"
     "     for the local directory name.\n"
     "\n"
     "  2. Exports a clean directory tree from the working copy specified by\n"
     "     PATH1, at revision REV if it is given, otherwise at WORKING, into\n"
     "     PATH2.  If PATH2 is omitted, the last component of the PATH1 is used\n"
     "     for the local directory name. If REV is not specified, all local\n"
     "     changes will be preserved.  Files not under version control will\n"
     "     not be copied.\n"
     "\n"
     "  If specified, PEGREV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', 'q', 'N', svn_cl__force_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt, svn_cl__native_eol_opt,
     svn_cl__ignore_externals_opt} },

  { "help", svn_cl__help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {svn_cl__config_dir_opt} },
  /* This command is also invoked if we see option "--help", "-h" or "-?". */

  { "import", svn_cl__import, {0}, N_
    ("Commit an unversioned file or tree into the repository.\n"
     "usage: import [PATH] URL\n"
     "\n"
     "  Recursively commit a copy of PATH to URL.\n"
     "  If PATH is omitted '.' is assumed.\n"
     "  Parent directories are created as necessary in the repository.\n"
     "  If PATH is a directory, the contents of the directory are added\n"
     "  directly under URL.\n"),
    {'q', 'N', svn_cl__autoprops_opt, svn_cl__no_autoprops_opt,
     SVN_CL__LOG_MSG_OPTIONS, svn_cl__no_ignore_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt} },

  { "info", svn_cl__info, {0}, N_
    ("Display information about a local or remote item.\n"
     "usage: info [TARGET[@REV]...]\n"
     "\n"
     "  Print information about each TARGET (default: '.')\n"
     "  TARGET may be either a working-copy path or URL.  If specified, REV\n"
     "  determines in which revision the target is first looked up.\n"),
    {'r', 'R', svn_cl__targets_opt, svn_cl__incremental_opt, svn_cl__xml_opt,
     SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "list", svn_cl__list, {"ls"}, N_
    ("List directory entries in the repository.\n"
     "usage: list [TARGET[@REV]...]\n"
     "\n"
     "  List each TARGET file and the contents of each TARGET directory as\n"
     "  they exist in the repository.  If TARGET is a working copy path, the\n"
     "  corresponding repository URL will be used. If specified, REV determines\n"
     "  in which revision the target is first looked up.\n"
     "\n"
     "  The default TARGET is '.', meaning the repository URL of the current\n"
     "  working directory.\n"
     "\n"
     "  With --verbose, the following fields will be shown for each item:\n"
     "\n"
     "    Revision number of the last commit\n"
     "    Author of the last commit\n"
     "    If locked, the letter 'O'.  (Use 'svn info URL' to see details)\n"
     "    Size (in bytes)\n"
     "    Date and time of the last commit\n"),
    {'r', 'v', 'R', svn_cl__incremental_opt, svn_cl__xml_opt,
     SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "lock", svn_cl__lock, {0}, N_
    ("Lock working copy paths or URLs in the repository, so that\n"
     "no other user can commit changes to them.\n"
     "usage: lock TARGET...\n"
     "\n"
     "  Use --force to steal the lock from another user or working copy.\n"),
    { svn_cl__targets_opt, 'm', 'F', svn_cl__force_log_opt,
      svn_cl__encoding_opt, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt,
      svn_cl__force_opt },
    {{'F', N_("read lock comment from file ARG")},
     {'m', N_("specify lock comment ARG")}} },

  { "log", svn_cl__log, {0}, N_
    ("Show the log messages for a set of revision(s) and/or file(s).\n"
     "usage: 1. log [PATH]\n"
     "       2. log URL[@REV] [PATH...]\n"
     "\n"
     "  1. Print the log messages for a local PATH (default: '.').\n"
     "     The default revision range is BASE:1.\n"
     "\n"
     "  2. Print the log messages for the PATHs (default: '.') under URL.\n"
     "     If specified, REV determines in which revision the URL is first\n"
     "     looked up.  The default revision range is HEAD:1.\n"
     "\n"
     "  With -v, also print all affected paths with each log message.\n"
     "  With -q, don't print the log message body itself (note that this is\n"
     "  compatible with -v).\n"
     "\n"
     "  Each log message is printed just once, even if more than one of the\n"
     "  affected paths for that revision were explicitly requested.  Logs\n"
     "  follow copy history by default.  Use --stop-on-copy to disable this\n"
     "  behavior, which can be useful for determining branchpoints.\n"
     "\n"
     "  Examples:\n"
     "    svn log\n"
     "    svn log foo.c\n"
     "    svn log http://www.example.com/repo/project/foo.c\n"
     "    svn log http://www.example.com/repo/project foo.c bar.c\n"),
    {'r', 'q', 'v', svn_cl__targets_opt, svn_cl__stop_on_copy_opt,
     svn_cl__incremental_opt, svn_cl__xml_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt, svn_cl__limit_opt} },

  { "merge", svn_cl__merge, {0}, N_
    ("Apply the differences between two sources to a working copy path.\n"
     "usage: 1. merge sourceURL1[@N] sourceURL2[@M] [WCPATH]\n"
     "       2. merge sourceWCPATH1@N sourceWCPATH2@M [WCPATH]\n"
     "       3. merge [-c M | -r N:M] SOURCE[@REV] [WCPATH]\n"
     "\n"
     "  1. In the first form, the source URLs are specified at revisions\n"
     "     N and M.  These are the two sources to be compared.  The revisions\n"
     "     default to HEAD if omitted.\n"
     "\n"
     "  2. In the second form, the URLs corresponding to the source working\n"
     "     copy paths define the sources to be compared.  The revisions must\n"
     "     be specified.\n"
     "\n"
     "  3. In the third form, SOURCE can be a URL, or working copy item\n"
     "     in which case the corresponding URL is used.  This URL in\n"
     "     revision REV is compared as it existed between revisions N and \n"
     "     M.  If REV is not specified, HEAD is assumed.\n"
     "     The '-c M' option is equivalent to '-r N:M' where N = M-1.\n"
     "     Using '-c -M' does the reverse: '-r M:N' where N = M-1.\n"
     "\n"
     "  WCPATH is the working copy path that will receive the changes.\n"
     "  If WCPATH is omitted, a default value of '.' is assumed, unless\n"
     "  the sources have identical basenames that match a file within '.':\n"
     "  in which case, the differences will be applied to that file.\n"),
    {'r', 'c', 'N', 'q', svn_cl__force_opt, svn_cl__dry_run_opt,
     svn_cl__merge_cmd_opt, 'x', svn_cl__ignore_ancestry_opt,
     SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "mkdir", svn_cl__mkdir, {0}, N_
    ("Create a new directory under version control.\n"
     "usage: 1. mkdir PATH...\n"
     "       2. mkdir URL...\n"
     "\n"
     "  Create version controlled directories.\n"
     "\n"
     "  1. Each directory specified by a working copy PATH is created locally\n"
     "    and scheduled for addition upon the next commit.\n"
     "\n"
     "  2. Each directory specified by a URL is created in the repository via\n"
     "    an immediate commit.\n"
     "\n"
     "  In both cases, all the intermediate directories must already exist.\n"),
    {'q',
     SVN_CL__LOG_MSG_OPTIONS, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "move", svn_cl__move, {"mv", "rename", "ren"}, N_
    ("Move and/or rename something in working copy or repository.\n"
     "usage: move SRC DST\n"
     "\n"
     "  Note:  this subcommand is equivalent to a 'copy' and 'delete'.\n"
     "  Note:  the --revision option has no use and is deprecated.\n"
     "\n"
     "  SRC and DST can both be working copy (WC) paths or URLs:\n"
     "    WC  -> WC:   move and schedule for addition (with history)\n"
     "    URL -> URL:  complete server-side rename.\n"),
    {'r', 'q', svn_cl__force_opt,
     SVN_CL__LOG_MSG_OPTIONS, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "propdel", svn_cl__propdel, {"pdel", "pd"}, N_
    ("Remove a property from files, dirs, or revisions.\n"
     "usage: 1. propdel PROPNAME [PATH...]\n"
     "       2. propdel PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Removes versioned props in working copy.\n"
     "  2. Removes unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"),
    {'q', 'R', 'r', svn_cl__revprop_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt} },

#ifndef AS400
  { "propedit", svn_cl__propedit, {"pedit", "pe"}, N_
    ("Edit a property with an external editor.\n"
     "usage: 1. propedit PROPNAME PATH...\n"
     "       2. propedit PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Edits versioned props in working copy.\n"
     "  2. Edits unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"),
    {'r', svn_cl__revprop_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__encoding_opt, svn_cl__editor_cmd_opt, svn_cl__force_opt,
     svn_cl__config_dir_opt} },
#endif

  { "propget", svn_cl__propget, {"pget", "pg"}, N_
    ("Print the value of a property on files, dirs, or revisions.\n"
     "usage: 1. propget PROPNAME [TARGET[@REV]...]\n"
     "       2. propget PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Prints versioned props. If specified, REV determines in which\n"
     "     revision the target is first looked up.\n"
     "  2. Prints unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "  By default, this subcommand will add an extra newline to the end\n"
     "  of the property values so that the output looks pretty.  Also,\n"
     "  whenever there are multiple paths involved, each property value\n"
     "  is prefixed with the path with which it is associated.  Use\n"
     "  the --strict option to disable these beautifications (useful,\n"
     "  for example, when redirecting binary property values to a file).\n"),
    {'R', 'r', svn_cl__revprop_opt, svn_cl__strict_opt,
     SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "proplist", svn_cl__proplist, {"plist", "pl"}, N_
    ("List all properties on files, dirs, or revisions.\n"
     "usage: 1. proplist [TARGET[@REV]...]\n"
     "       2. proplist --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Lists versioned props. If specified, REV determines in which\n"
     "     revision the target is first looked up.\n"
     "  2. Lists unversioned remote props on repos revision.\n"
     "     TARGET only determines which repository to access.\n"),
    {'v', 'R', 'r', 'q', svn_cl__revprop_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt} },

  { "propset", svn_cl__propset, {"pset", "ps"}, N_
    ("Set the value of a property on files, dirs, or revisions.\n"
     "usage: 1. propset PROPNAME PROPVAL PATH...\n"
     "       2. propset PROPNAME --revprop -r REV PROPVAL [TARGET]\n"
     "\n"
     "  1. Creates a versioned, local propchange in working copy.\n"
     "  2. Creates an unversioned, remote propchange on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "  The value may be provided with the --file option instead of PROPVAL.\n"
     "\n"
     "  Note: svn recognizes the following special versioned properties\n"
     "  but will store any arbitrary properties set:\n"
     "    svn:ignore     - A newline separated list of file patterns to ignore.\n"
     "    svn:keywords   - Keywords to be expanded.  Valid keywords are:\n"
     "      URL, HeadURL             - The URL for the head version of the object.\n"
     "      Author, LastChangedBy    - The last person to modify the file.\n"
     "      Date, LastChangedDate    - The date/time the object was last modified.\n"
     "      Rev, Revision,           - The last revision the object changed.\n"
     "      LastChangedRevision\n"
     "      Id                       - A compressed summary of the previous\n"
     "                                   4 keywords.\n"
     "    svn:executable - If present, make the file executable.\n"
     "    svn:eol-style  - One of 'native', 'LF', 'CR', 'CRLF'.\n"
     "    svn:mime-type  - The mimetype of the file.  Used to determine\n"
     "      whether to merge the file, and how to serve it from Apache.\n"
     "      A mimetype beginning with 'text/' (or an absent mimetype) is\n"
     "      treated as text.  Anything else is treated as binary.\n"
     "    svn:externals  - A newline separated list of module specifiers,\n"
     "      each of which consists of a relative directory path, optional\n"
     "      revision flags, and an URL.  For example\n"
     "        foo             http://example.com/repos/zig\n"
     "        foo/bar -r 1234 http://example.com/repos/zag\n"
     "    svn:needs-lock - If present, indicates that the file should be locked\n"
     "      before it is modified.  Makes the working copy file read-only\n"
     "      when it is not locked.\n"
     "  The svn:keywords, svn:executable, svn:eol-style, svn:mime-type and\n"
     "  svn:needs-lock properties cannot be set on a directory.  A non-recursive\n"
     "  attempt will fail, and a recursive attempt will set the property\n"
     "  only on the file children of the directory.\n"),
    {'F', svn_cl__encoding_opt, 'q', 'r', svn_cl__targets_opt, 'R',
     svn_cl__revprop_opt, SVN_CL__AUTH_OPTIONS, svn_cl__force_opt,
     svn_cl__config_dir_opt},
    {{'F', N_("read property value from file ARG")}} },

  { "resolved", svn_cl__resolved, {0}, N_
    ("Remove 'conflicted' state on working copy files or directories.\n"
     "usage: resolved PATH...\n"
     "\n"
     "  Note:  this subcommand does not semantically resolve conflicts or\n"
     "  remove conflict markers; it merely removes the conflict-related\n"
     "  artifact files and allows PATH to be committed again.\n"),
    {svn_cl__targets_opt, 'R', 'q', svn_cl__config_dir_opt} },

  { "revert", svn_cl__revert, {0}, N_
    ("Restore pristine working copy file (undo most local edits).\n"
     "usage: revert PATH...\n"
     "\n"
     "  Note:  this subcommand does not require network access, and resolves\n"
     "  any conflicted states.  However, it does not restore removed directories.\n"),
    {svn_cl__targets_opt, 'R', 'q', svn_cl__config_dir_opt} },

  { "status", svn_cl__status, {"stat", "st"}, N_
    ("Print the status of working copy files and directories.\n"
     "usage: status [PATH...]\n"
     "\n"
     "  With no args, print only locally modified items (no network access).\n"
     "  With -u, add working revision and server out-of-date information.\n"
     "  With -v, print full revision information on every item.\n"
     "\n"
     "  The first six columns in the output are each one character wide:\n"
     "    First column: Says if item was added, deleted, or otherwise changed\n"
     "      ' ' no modifications\n"
     "      'A' Added\n"
     "      'C' Conflicted\n"
     "      'D' Deleted\n"
     "      'I' Ignored\n"
     "      'M' Modified\n"
     "      'R' Replaced\n"
     "      'X' item is unversioned, but is used by an externals definition\n"
     "      '?' item is not under version control\n"
     "      '!' item is missing (removed by non-svn command) or incomplete\n"
     "      '~' versioned item obstructed by some item of a different kind\n"
     "    Second column: Modifications of a file's or directory's properties\n"
     "      ' ' no modifications\n"
     "      'C' Conflicted\n"
     "      'M' Modified\n"
     "    Third column: Whether the working copy directory is locked\n"
     "      ' ' not locked\n"
     "      'L' locked\n"
     "    Fourth column: Scheduled commit will contain addition-with-history\n"
     "      ' ' no history scheduled with commit\n"
     "      '+' history scheduled with commit\n"
     "    Fifth column: Whether the item is switched relative to its parent\n"
     "      ' ' normal\n"
     "      'S' switched\n"
     "    Sixth column: Repository lock token\n"
     "      (without -u)\n"
     "      ' ' no lock token\n"
     "      'K' lock token present\n"
     "      (with -u)\n"
     "      ' ' not locked in repository, no lock token\n"
     "      'K' locked in repository, lock toKen present\n"
     "      'O' locked in repository, lock token in some Other working copy\n"
     "      'T' locked in repository, lock token present but sTolen\n"
     "      'B' not locked in repository, lock token present but Broken\n"
     "\n"
     "  The out-of-date information appears in the eighth column (with -u):\n"
     "      '*' a newer revision exists on the server\n"
     "      ' ' the working copy is up to date\n"
     "\n"
     "  Remaining fields are variable width and delimited by spaces:\n"
     "    The working revision (with -u or -v)\n"
     "    The last committed revision and last committed author (with -v)\n"
     "    The working copy path is always the final field, so it can\n"
     "      include spaces.\n"
     "\n"
     "  Example output:\n"
     "    svn status wc\n"
     "     M     wc/bar.c\n"
     "    A  +   wc/qax.c\n"
     "\n"
     "    svn status -u wc\n"
     "     M           965    wc/bar.c\n"
     "           *     965    wc/foo.c\n"
     "    A  +         965    wc/qax.c\n"
     "    Status against revision:   981\n"
     "\n"
     "    svn status --show-updates --verbose wc\n"
     "     M           965       938 kfogel       wc/bar.c\n"
     "           *     965       922 sussman      wc/foo.c\n"
     "    A  +         965       687 joe          wc/qax.c\n"
     "                 965       687 joe          wc/zig.c\n"
     "    Status against revision:   981\n"),
    { 'u', 'v', 'N', 'q', svn_cl__no_ignore_opt, svn_cl__incremental_opt,
      svn_cl__xml_opt, SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt,
      svn_cl__ignore_externals_opt} },

  { "switch", svn_cl__switch, {"sw"}, N_
    ("Update the working copy to a different URL.\n"
     "usage: 1. switch URL [PATH]\n"
     "       2. switch --relocate FROM TO [PATH...]\n"
     "\n"
     "  1. Update the working copy to mirror a new URL within the repository.\n"
     "     This behaviour is similar to 'svn update', and is the way to\n"
     "     move a working copy to a branch or tag within the same repository.\n"
     "\n"
     "  2. Rewrite working copy URL metadata to reflect a syntactic change only.\n"
     "     This is used when repository's root URL changes (such as a scheme\n"
     "     or hostname change) but your working copy still reflects the same\n"
     "     directory within the same repository.\n"),
    { 'r', 'N', 'q', svn_cl__merge_cmd_opt, svn_cl__relocate_opt,
      SVN_CL__AUTH_OPTIONS, svn_cl__config_dir_opt} },

  { "unlock", svn_cl__unlock, {0}, N_
    ("Unlock working copy paths or URLs.\n"
     "usage: unlock TARGET...\n"
     "\n"
     "  Use --force to break the lock.\n"),
    { svn_cl__targets_opt, SVN_CL__AUTH_OPTIONS,
      svn_cl__config_dir_opt, svn_cl__force_opt } },

  { "update", svn_cl__update, {"up"},  N_
    ("Bring changes from the repository into the working copy.\n"
     "usage: update [PATH...]\n"
     "\n"
     "  If no revision given, bring working copy up-to-date with HEAD rev.\n"
     "  Else synchronize working copy to revision given by -r.\n"
     "\n"
     "  For each updated item a line will start with a character reporting the\n"
     "  action taken.  These characters have the following meaning:\n"
     "\n"
     "    A  Added\n"
     "    D  Deleted\n"
     "    U  Updated\n"
     "    C  Conflict\n"
     "    G  Merged\n"
     "\n"
     "  A character in the first column signifies an update to the actual file,\n"
     "  while updates to the file's properties are shown in the second column.\n"
     "  A 'B' in the third column signifies that the lock for the file has\n"
     "  been broken or stolen.\n"),
    {'r', 'N', 'q', svn_cl__merge_cmd_opt, SVN_CL__AUTH_OPTIONS,
     svn_cl__config_dir_opt, svn_cl__ignore_externals_opt} },

  { NULL, NULL, {0}, NULL, {0} }
};


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_client", svn_client_version },
      { "svn_wc",     svn_wc_version },
      { "svn_ra",     svn_ra_version },
      { "svn_delta",  svn_delta_version },
      { "svn_diff",   svn_diff_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE(my_version);
  return svn_ver_check_list(&my_version, checklist);
}


/* A flag to see if we've been cancelled by the client or not. */
static volatile sig_atomic_t cancelled = FALSE;

/* A signal handler to support cancellation. */
static void
signal_handler(int signum)
{
  apr_signal(signum, SIG_IGN);
  cancelled = TRUE;
}

/* Our cancellation callback. */
svn_error_t *
svn_cl__check_cancel(void *baton)
{
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}



/*** Main. ***/

int
main(int argc, const char *argv[])
{
  svn_error_t *err;
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  int opt_id;
  apr_getopt_t *os;  
  svn_cl__opt_state_t opt_state = { { 0 } };
  svn_client_ctx_t *ctx;
  apr_array_header_t *received_opts;
  int i;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  const char *dash_m_arg = NULL, *dash_F_arg = NULL;
  const char *path_utf8;
  apr_status_t apr_err;
  svn_cl__cmd_baton_t command_baton;
  svn_auth_baton_t *ab;
  svn_config_t *cfg;
  svn_boolean_t used_change_arg = FALSE;

  /* Initialize the app. */
  if (svn_cmdline_init("svn", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a seperate mutexless allocator,
   * given this application is single threaded.
   */
  if (apr_allocator_create(&allocator))
    return EXIT_FAILURE;

  apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  pool = svn_pool_create_ex(NULL, allocator);
  apr_allocator_owner_set(allocator, pool);

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Check library versions */
  err = check_lib_versions();
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");

#if defined(WIN32) || defined(__CYGWIN__)
  /* Set the working copy administrative directory name. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      err = svn_wc_set_adm_dir("_svn", pool);
      if (err)
        return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }
#endif

  /* Initialize the RA library. */
  err = svn_ra_initialize(pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");

  /* Begin processing arguments. */
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;
 
  /* No args?  Show usage. */
  if (argc <= 1)
    {
      svn_cl__help(NULL, NULL, pool);
      svn_pool_destroy(pool);
      return EXIT_FAILURE;
    }

  /* Else, parse options. */
  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");

  os->interleave = 1;
  while (1)
    {
      const char *opt_arg;
      const char *utf8_opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long(os, svn_cl__options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          svn_cl__help(NULL, NULL, pool);
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id) {
      case svn_cl__limit_opt:
        {
          char *end;
          opt_state.limit = strtol(opt_arg, &end, 10);
          if (end == opt_arg || *end != '\0')
            {
              err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Non-numeric limit argument given"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          if (opt_state.limit <= 0)
            {
              err = svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                    _("Argument to --limit must be positive"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
        }
        break;
      case 'm':
        /* Note that there's no way here to detect if the log message
           contains a zero byte -- if it does, then opt_arg will just
           be shorter than the user intended.  Oh well. */
        opt_state.message = apr_pstrdup(pool, opt_arg);
        dash_m_arg = opt_arg;
        break;
      case 'c':
        {
          char *end;
          svn_revnum_t changeno;
          if (opt_state.start_revision.kind != svn_opt_revision_unspecified)
            {
              err = svn_error_create
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Multiple revision arguments encountered; "
                   "can't specify -c twice, or both -c and -r"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          if (opt_state.old_target)
            {
              err = svn_error_create
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Can't specify -c with --old"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          changeno = strtol(opt_arg, &end, 10);
          if (end == opt_arg || *end != '\0')
            {
              err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Non-numeric change argument given to -c"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          if (changeno == 0)
            {
              err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("There is no change 0"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          /* Figure out the range:
                -c N  -> -r N-1:N
                -c -N -> -r N:N-1 */
          if (changeno > 0)
            {
              opt_state.start_revision.value.number = changeno - 1;
              opt_state.end_revision.value.number = changeno;
            }
          else
            {
              changeno = -changeno;
              opt_state.start_revision.value.number = changeno;
              opt_state.end_revision.value.number = changeno - 1;
            }
          opt_state.start_revision.kind = svn_opt_revision_number;
          opt_state.end_revision.kind = svn_opt_revision_number;
          used_change_arg = TRUE;
        }
        break;
      case 'r':
        if (opt_state.start_revision.kind != svn_opt_revision_unspecified)
          {
            err = svn_error_create
              (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
               _("Multiple revision arguments encountered; "
                 "can't specify -r and -c, or "
                 "try '-r N:M' instead of '-r N -r M'"));
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        if (svn_opt_parse_revision(&(opt_state.start_revision),
                                   &(opt_state.end_revision),
                                   opt_arg, pool) != 0)
          {
            err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
            if (! err)
              err = svn_error_createf
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Syntax error in revision argument '%s'"),
                 utf8_opt_arg);
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        break;
      case 'v':
        opt_state.verbose = TRUE;
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
      case svn_cl__incremental_opt:
        opt_state.incremental = TRUE;
        break;
      case 'F':
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (! err)
          err = svn_stringbuf_from_file(&(opt_state.filedata),
                                        utf8_opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        dash_F_arg = opt_arg;
        break;
      case svn_cl__targets_opt:
        {
          svn_stringbuf_t *buffer, *buffer_utf8;

          /* We need to convert to UTF-8 now, even before we divide
             the targets into an array, because otherwise we wouldn't
             know what delimiter to use for svn_cstring_split().  */

          err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);

          if (! err)
            err = svn_stringbuf_from_file(&buffer, utf8_opt_arg, pool);
          if (! err)
            err = svn_utf_stringbuf_to_utf8(&buffer_utf8, buffer, pool);
          if (err)
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          opt_state.targets = svn_cstring_split(buffer_utf8->data, "\n\r",
                                                TRUE, pool);
        }
        break;
      case svn_cl__force_opt:
        opt_state.force = TRUE;
        break;
      case svn_cl__force_log_opt:
        opt_state.force_log = TRUE;
        break;
      case svn_cl__dry_run_opt:
        opt_state.dry_run = TRUE;
        break;
      case svn_cl__revprop_opt:
        opt_state.revprop = TRUE;
        break;
      case 'R':
        opt_state.recursive = TRUE;
        break;
      case 'N':
        opt_state.nonrecursive = TRUE;
        break;
      case svn_cl__version_opt:
        opt_state.version = TRUE;
        break;
      case svn_cl__auth_username_opt:
        err = svn_utf_cstring_to_utf8(&opt_state.auth_username,
                                      opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case svn_cl__auth_password_opt:
        err = svn_utf_cstring_to_utf8(&opt_state.auth_password,
                                      opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case svn_cl__encoding_opt:
        opt_state.encoding = apr_pstrdup(pool, opt_arg);
        break;
      case svn_cl__xml_opt:
        opt_state.xml = TRUE;
        break;
      case svn_cl__stop_on_copy_opt:
        opt_state.stop_on_copy = TRUE;
        break;
      case svn_cl__strict_opt:
        opt_state.strict = TRUE;
        break;
      case svn_cl__no_ignore_opt:
        opt_state.no_ignore = TRUE;
        break;
      case svn_cl__no_auth_cache_opt:
        opt_state.no_auth_cache = TRUE;
        break;
      case svn_cl__non_interactive_opt:
        opt_state.non_interactive = TRUE;
        break;
      case svn_cl__no_diff_deleted:
        opt_state.no_diff_deleted = TRUE;
        break;
      case svn_cl__notice_ancestry_opt:
        opt_state.notice_ancestry = TRUE;
        break;
      case svn_cl__ignore_ancestry_opt:
        opt_state.ignore_ancestry = TRUE;
        break;
      case svn_cl__ignore_externals_opt:
        opt_state.ignore_externals = TRUE;
        break;
      case svn_cl__relocate_opt:
        opt_state.relocate = TRUE;
        break;
      case 'x':
        err = svn_utf_cstring_to_utf8(&opt_state.extensions, opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case svn_cl__diff_cmd_opt:
        opt_state.diff_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case svn_cl__merge_cmd_opt:
        opt_state.merge_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case svn_cl__editor_cmd_opt:
        opt_state.editor_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case svn_cl__old_cmd_opt:
        if (used_change_arg)
          {
            err = svn_error_create
              (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
               _("Can't specify -c with --old"));
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        opt_state.old_target = apr_pstrdup(pool, opt_arg);
        break;
      case svn_cl__new_cmd_opt:
        opt_state.new_target = apr_pstrdup(pool, opt_arg);
        break;
      case svn_cl__config_dir_opt:
        err = svn_utf_cstring_to_utf8(&path_utf8, opt_arg, pool);
        opt_state.config_dir = svn_path_canonicalize(path_utf8, pool);
        break;
      case svn_cl__autoprops_opt:
        if (opt_state.no_autoprops)
          {
            err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                   _("--auto-props and --no-auto-props are "
                                     "mutually exclusive"));
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        opt_state.autoprops = TRUE;
        break;
      case svn_cl__no_autoprops_opt:
        if (opt_state.autoprops)
          {
            err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                   _("--auto-props and --no-auto-props are "
                                     "mutually exclusive"));
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        opt_state.no_autoprops = TRUE;
        break;
      case svn_cl__native_eol_opt:
        if ( !strcmp("LF", opt_arg) || !strcmp("CR", opt_arg) ||
             !strcmp("CRLF", opt_arg))
          opt_state.native_eol = apr_pstrdup(pool, opt_arg);
        else
          {
            err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
            if (! err)
              err = svn_error_createf
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Syntax error in native-eol argument '%s'"),
                 utf8_opt_arg);
            svn_handle_error2(err, stderr, FALSE, "svn: ");
            svn_error_clear(err);
            svn_pool_destroy(pool);
            return EXIT_FAILURE;
          }
        break;
      case svn_cl__no_unlock_opt:
        opt_state.no_unlock = TRUE;
        break;
      case svn_cl__summarize:
        opt_state.summarize = TRUE;
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
     hand, the alternative is effectively to demand that they call
     svn_config_ensure() instead, so maybe we should have a generic
     init function anyway.  Thoughts?  */
  err = svn_config_ensure(opt_state.config_dir, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(svn_cl__cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_state.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", svn_cl__help, {0}, "",
                  {svn_cl__version_opt,    /* must accept its own option */
                   'q',                    /* brief output */
                   svn_cl__config_dir_opt  /* all commands accept this */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Subcommand argument required\n")));
              svn_cl__help(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand2(svn_cl__cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              const char *first_arg_utf8;
              err = svn_utf_cstring_to_utf8(&first_arg_utf8, first_arg, pool);
              if (err)
                return svn_cmdline_handle_exit_error(err, pool, "svn: ");
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Unknown command: '%s'\n"),
                                     first_arg_utf8));
              svn_cl__help(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < received_opts->nelts; i++)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option2(subcommand, opt_id))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt = 
            svn_opt_get_option_from_code2(opt_id, svn_cl__options,
                                          subcommand, pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            svn_cl__help(NULL, NULL, pool);
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool, _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svn help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }
    }

  /* if we're running a command that could result in a commit, verify
     that any log message we were given on the command line makes
     sense (unless we've also been instructed not to care). */
  if ((! opt_state.force_log) 
      && (subcommand->cmd_func == svn_cl__commit
          || subcommand->cmd_func == svn_cl__copy
          || subcommand->cmd_func == svn_cl__delete
          || subcommand->cmd_func == svn_cl__import
          || subcommand->cmd_func == svn_cl__mkdir
          || subcommand->cmd_func == svn_cl__move
          || subcommand->cmd_func == svn_cl__lock))
    {
      /* If the -F argument is a file that's under revision control,
         that's probably not what the user intended. */
      if (dash_F_arg)
        {
          svn_wc_adm_access_t *adm_access;
          const svn_wc_entry_t *e;
          const char *fname_utf8 = svn_path_internal_style(dash_F_arg, pool);
          err = svn_wc_adm_probe_open3(&adm_access, NULL, fname_utf8,
                                       FALSE, 0, NULL, NULL, pool);
          if (! err)
            err = svn_wc_entry(&e, fname_utf8, adm_access, FALSE, pool);
          if ((err == SVN_NO_ERROR) && e)
            {
              if (subcommand->cmd_func != svn_cl__lock)
                {
                  err = svn_error_create 
                    (SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE, NULL,
                     _("Log message file is a versioned file; "
                       "use '--force-log' to override"));
                }
              else
                {
                  err = svn_error_create 
                    (SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE, NULL,
                     _("Lock comment file is a versioned file; "
                       "use '--force-log' to override"));
                }
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          if (err)
            svn_error_clear(err);
        }

      /* If the -m argument is a file at all, that's probably not what
         the user intended. */
      if (dash_m_arg)
        {
          apr_finfo_t finfo;
          if (apr_stat(&finfo, dash_m_arg, 
                       APR_FINFO_MIN, pool) == APR_SUCCESS)
            {
              if (subcommand->cmd_func != svn_cl__lock)
                {
                  err = svn_error_create 
                    (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME, NULL,
                     _("The log message is a pathname "
                       "(was -F intended?); use '--force-log' to override"));
                }
              else
                {
                  err = svn_error_create 
                    (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME, NULL,
                     _("The lock comment is a pathname "
                       "(was -F intended?); use '--force-log' to override"));
                }
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
        }
    }

  /* Only a few commands can accept a revision range; the rest can take at
     most one revision number. */
  if (subcommand->cmd_func != svn_cl__blame
      && subcommand->cmd_func != svn_cl__diff
      && subcommand->cmd_func != svn_cl__log
      && subcommand->cmd_func != svn_cl__merge)
    {
      if (opt_state.end_revision.kind != svn_opt_revision_unspecified)
        {
          err = svn_error_create(SVN_ERR_CLIENT_REVISION_RANGE, NULL, NULL);
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        }
    }

  /* Create a client context object. */
  command_baton.opt_state = &opt_state;
  if ((err = svn_client_create_context(&ctx, pool)))
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");
  command_baton.ctx = ctx;

  if ((err = svn_config_get_config(&(ctx->config),
                                   opt_state.config_dir, pool)))
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");

  cfg = apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                     APR_HASH_KEY_STRING);

  /* Update the options in the config */
  /* XXX: Only diff_cmd for now, overlay rest later and stop passing
     opt_state altogether? */
  if (opt_state.diff_cmd)
    svn_config_set(cfg, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF_CMD, opt_state.diff_cmd);
  if (opt_state.merge_cmd)
    svn_config_set(cfg, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, opt_state.merge_cmd);

  /* Update auto-props-enable option for add/import commands */
  if (subcommand->cmd_func == svn_cl__add
      || subcommand->cmd_func == svn_cl__import)
    {
      if (opt_state.autoprops)
        {
          svn_config_set_bool(cfg, SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, TRUE);
        }
      if (opt_state.no_autoprops)
        {
          svn_config_set_bool(cfg, SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, FALSE);
        }
    }

  /* Update the 'keep-locks' runtime option */
  if (opt_state.no_unlock)
    svn_config_set_bool(cfg, SVN_CONFIG_SECTION_MISCELLANY,
                        SVN_CONFIG_OPTION_NO_UNLOCK, TRUE);

  /* Set the log message callback function.  Note that individual
     subcommands will populate the ctx->log_msg_baton2 */
  ctx->log_msg_func2 = svn_cl__get_log_message;

  /* Set up our cancellation support. */
  ctx->cancel_func = svn_cl__check_cancel;
  apr_signal(SIGINT, signal_handler);
#ifdef SIGBREAK
  /* SIGBREAK is a Win32 specific signal generated by ctrl-break. */
  apr_signal(SIGBREAK, signal_handler);
#endif
#ifdef SIGHUP
  apr_signal(SIGHUP, signal_handler);
#endif
#ifdef SIGTERM
  apr_signal(SIGTERM, signal_handler);
#endif

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it, otherwise
   * working with large files when compiled against an APR that doesn't have
   * large file support will crash the program, which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif

  /* Set up Authentication stuff. */
  if ((err = svn_cmdline_setup_auth_baton(&ab,
                                          opt_state.non_interactive,
                                          opt_state.auth_username,
                                          opt_state.auth_password,
                                          opt_state.config_dir,
                                          opt_state.no_auth_cache,
                                          cfg,
                                          ctx->cancel_func,
                                          ctx->cancel_baton,
                                          pool)))
    svn_handle_error2(err, stderr, TRUE, "svn: ");

  ctx->auth_baton = ab;

  /* And now we finally run the subcommand. */
  err = (*subcommand->cmd_func)(os, &command_baton, pool);
  if (err)
    {
      svn_error_t *tmp_err;

      svn_handle_error2(err, stderr, FALSE, "svn: ");

      /* Tell the user about 'svn cleanup' if any error on the stack
         was about locked working copies. */
      for (tmp_err = err; tmp_err; tmp_err = tmp_err->child)
        if (tmp_err->apr_err == SVN_ERR_WC_LOCKED)
          {
            svn_error_clear
              (svn_cmdline_fputs(_("svn: run 'svn cleanup' to remove locks "
                                   "(type 'svn help cleanup' for details)\n"),
                                 stderr, pool));
            break;
          }

      svn_error_clear(err);
      svn_pool_destroy(pool);
      return EXIT_FAILURE;
    }
  else
    {
      /* Ensure that stdout is flushed, so the user will see any write errors.
         This makes sure that output is not silently lost. */
      err = svn_cmdline_fflush(stdout);
      if (err)
        return svn_cmdline_handle_exit_error(err, pool, "svn: ");

      svn_pool_destroy(pool);
      return EXIT_SUCCESS;
    }
}
