/*
 * main.c:  Subversion command line client.
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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
#include "svn_hash.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Option Processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum {
  opt_ancestor_path = SVN_OPT_FIRST_LONGOPT_ID,
  opt_auth_password,
  opt_auth_username,
  opt_autoprops,
  opt_changelist,
  opt_config_dir,
  opt_config_options,
  opt_diff_cmd,
  opt_dry_run,
  opt_editor_cmd,
  opt_encoding,
  opt_force_log,
  opt_force,
  opt_keep_changelists,
  opt_ignore_ancestry,
  opt_ignore_externals,
  opt_incremental,
  opt_merge_cmd,
  opt_native_eol,
  opt_new_cmd,
  opt_no_auth_cache,
  opt_no_autoprops,
  opt_no_diff_deleted,
  opt_no_ignore,
  opt_no_unlock,
  opt_non_interactive,
  opt_notice_ancestry,
  opt_old_cmd,
  opt_record_only,
  opt_relocate,
  opt_remove,
  opt_revprop,
  opt_stop_on_copy,
  opt_strict,
  opt_summarize,
  opt_targets,
  opt_depth,
  opt_set_depth,
  opt_version,
  opt_xml,
  opt_keep_local,
  opt_with_revprop,
  opt_with_all_revprops,
  opt_with_no_revprops,
  opt_parents,
  opt_accept,
  opt_show_revs,
  opt_reintegrate,
  opt_trust_server_cert
} svn_cl__longopt_t;

/* Option codes and descriptions for the command line client.
 *
 * The entire list must be terminated with an entry of nulls.
 */
const apr_getopt_option_t svn_cl__options[] =
{
  {"force",         opt_force, 0, N_("force operation to run")},
  {"force-log",     opt_force_log, 0,
                    N_("force validity of log message source")},
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"message",       'm', 1, N_("specify log message ARG")},
  {"quiet",         'q', 0, N_("print nothing, or only summary information")},
  {"recursive",     'R', 0, N_("descend recursively, same as --depth=infinity")},
  {"non-recursive", 'N', 0, N_("obsolete; try --depth=files or --depth=immediates")},
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
  {"incremental",   opt_incremental, 0,
                    N_("give output suitable for concatenation")},
  {"encoding",      opt_encoding, 1,
                    N_("treat value as being in charset encoding ARG")},
  {"version",       opt_version, 0, N_("show program version information")},
  {"verbose",       'v', 0, N_("print extra information")},
  {"show-updates",  'u', 0, N_("display update information")},
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1, N_("specify a password ARG")},
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
                       "       Ignore changes in EOL style.\n"
                       "                            "
                       "    -p (--show-c-function):\n"
                       "                            "
                       "       Show C function name in diff output.")},
  {"targets",       opt_targets, 1,
                    N_("pass contents of file ARG as additional args")},
  {"depth",         opt_depth, 1,
                    N_("limit operation by depth ARG ('empty', 'files',\n"
                       "                            "
                       "'immediates', or 'infinity')")},
  {"set-depth",     opt_set_depth, 1,
                    N_("set new working copy depth to ARG ('exclude',\n"
                       "                            "
                       "'empty', 'files', 'immediates', or 'infinity')")},
  {"xml",           opt_xml, 0, N_("output in XML")},
  {"strict",        opt_strict, 0, N_("use strict semantics")},
  {"stop-on-copy",  opt_stop_on_copy, 0,
                    N_("do not cross copies while traversing history")},
  {"no-ignore",     opt_no_ignore, 0,
                    N_("disregard default and svn:ignore property ignores")},
  {"no-auth-cache", opt_no_auth_cache, 0,
                    N_("do not cache authentication tokens")},
  {"trust-server-cert", opt_trust_server_cert, 0,
                    N_("accept unknown SSL server certificates without\n"
                       "                             "
                       "prompting (but only with '--non-interactive')")},
  {"non-interactive", opt_non_interactive, 0,
                    N_("do no interactive prompting")},
  {"dry-run",       opt_dry_run, 0,
                    N_("try operation but make no changes")},
  {"no-diff-deleted", opt_no_diff_deleted, 0,
                    N_("do not print differences for deleted files")},
  {"notice-ancestry", opt_notice_ancestry, 0,
                    N_("notice ancestry when calculating differences")},
  {"ignore-ancestry", opt_ignore_ancestry, 0,
                    N_("ignore ancestry when calculating merges")},
  {"ignore-externals", opt_ignore_externals, 0,
                    N_("ignore externals definitions")},
  {"diff-cmd",      opt_diff_cmd, 1, N_("use ARG as diff command")},
  {"diff3-cmd",     opt_merge_cmd, 1, N_("use ARG as merge command")},
  {"editor-cmd",    opt_editor_cmd, 1, N_("use ARG as external editor")},
  {"record-only",   opt_record_only, 0,
                    N_("mark revisions as merged (use with -r)")},
  {"old",           opt_old_cmd, 1, N_("use ARG as the older target")},
  {"new",           opt_new_cmd, 1, N_("use ARG as the newer target")},
  {"revprop",       opt_revprop, 0,
                    N_("operate on a revision property (use with -r)")},
  {"relocate",      opt_relocate, 0, N_("relocate via URL-rewriting")},
  {"config-dir",    opt_config_dir, 1,
                    N_("read user configuration files from directory ARG")},
  {"config-option", opt_config_options, 1,
                    N_("set user configuration option in the format:\n"
                       "                             "
                       "    FILE:SECTION:OPTION=[VALUE]\n"
                       "                             "
                       "For example:\n"
                       "                             "
                       "    servers:global:http-library=serf\n")},
  {"auto-props",    opt_autoprops, 0, N_("enable automatic properties")},
  {"no-auto-props", opt_no_autoprops, 0, N_("disable automatic properties")},
  {"native-eol",    opt_native_eol, 1,
                    N_("use a different EOL marker than the standard\n"
                       "                             "
                       "system marker for files with the svn:eol-style\n"
                       "                             "
                       "property set to 'native'.\n"
                       "                             "
                       "ARG may be one of 'LF', 'CR', 'CRLF'")},
  {"limit",         'l', 1, N_("maximum number of log entries")},
  {"no-unlock",     opt_no_unlock, 0, N_("don't unlock the targets")},
  {"summarize",     opt_summarize, 0, N_("show a summary of the results")},
  {"remove",         opt_remove, 0, N_("remove changelist association")},
  {"changelist",    opt_changelist, 1,
                    N_("operate only on members of changelist ARG\n"
                       "                             "
                       "[aliases: --cl]")},
  {"keep-changelists", opt_keep_changelists, 0,
                    N_("don't delete changelists after commit")},
  {"keep-local",    opt_keep_local, 0, N_("keep path in working copy")},
  {"with-all-revprops",  opt_with_all_revprops, 0,
                    N_("retrieve all revision properties")},
  {"with-no-revprops",  opt_with_no_revprops, 0,
                    N_("retrieve no revision properties")},
  {"with-revprop",  opt_with_revprop, 1,
                    N_("set revision property ARG in new revision\n"
                       "                             "
                       "using the name[=value] format")},
  {"parents",       opt_parents, 0, N_("make intermediate directories")},
  {"use-merge-history", 'g', 0,
                    N_("use/display additional information from merge\n"
                       "                             "
                       "history")},
  {"accept",        opt_accept, 1,
                    N_("specify automatic conflict resolution action\n"
                       "                            "
                       "('postpone', 'base', 'mine-conflict',\n"
                       "                            "
                       " 'theirs-conflict', 'mine-full', 'theirs-full',\n"
                       "                            "
                       " 'edit', 'launch')")},
  {"show-revs",     opt_show_revs, 1,
                    N_("specify which collection of revisions to display\n"
                       "                             "
                       "('merged', 'eligible')")},
  {"reintegrate",   opt_reintegrate, 0,
                    N_("lump-merge all of source URL's unmerged changes")},

  /* Long-opt Aliases
   *
   * These have NULL desriptions, but an option code that matches some
   * other option (whose description should probably mention its aliases).
  */

  {"cl",            opt_changelist, 1, NULL},

  {0,               0, 0, 0},
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

/* Options that apply to all commands.  (While not every command may
   currently require authentication or be interactive, allowing every
   command to take these arguments allows scripts to just pass them
   willy-nilly to every invocation of 'svn') . */
const int svn_cl__global_options[] =
{ opt_auth_username, opt_auth_password, opt_no_auth_cache, opt_non_interactive,
  opt_trust_server_cert, opt_config_dir, opt_config_options, 0
};

/* Options for giving a log message.  (Some of these also have other uses.)
 */
#define SVN_CL__LOG_MSG_OPTIONS 'm', 'F', \
                                opt_force_log, \
                                opt_editor_cmd, \
                                opt_encoding, \
                                opt_with_revprop

const svn_opt_subcommand_desc2_t svn_cl__cmd_table[] =
{
  { "add", svn_cl__add, {0}, N_
    ("Put files and directories under version control, scheduling\n"
     "them for addition to repository.  They will be added in next commit.\n"
     "usage: add PATH...\n"),
    {opt_targets, 'N', opt_depth, 'q', opt_force, opt_no_ignore, opt_autoprops,
     opt_no_autoprops, opt_parents },
     {{opt_parents, N_("add intermediate parents")}} },

  { "blame", svn_cl__blame, {"praise", "annotate", "ann"}, N_
    ("Output the content of specified files or\n"
     "URLs with revision and author information in-line.\n"
     "usage: blame TARGET[@REV]...\n"
     "\n"
     "  If specified, REV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', 'v', 'g', opt_incremental, opt_xml, 'x', opt_force} },

  { "cat", svn_cl__cat, {0}, N_
    ("Output the content of specified files or URLs.\n"
     "usage: cat TARGET[@REV]...\n"
     "\n"
     "  If specified, REV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r'} },

  { "changelist", svn_cl__changelist, {"cl"}, N_
    ("Associate (or dissociate) changelist CLNAME with the named files.\n"
     "usage: 1. changelist CLNAME TARGET...\n"
     "       2. changelist --remove TARGET...\n"),
    { 'q', 'R', opt_depth, opt_remove, opt_targets, opt_changelist} },

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
     "  being the basename of the URL.\n"
     "\n"
     "  If --force is used, unversioned obstructing paths in the working\n"
     "  copy destination do not automatically cause the check out to fail.\n"
     "  If the obstructing path is the same type (file or directory) as the\n"
     "  corresponding path in the repository it becomes versioned but its\n"
     "  contents are left 'as-is' in the working copy.  This means that an\n"
     "  obstructing directory's unversioned children may also obstruct and\n"
     "  become versioned.  For files, any content differences between the\n"
     "  obstruction and the repository are treated like a local modification\n"
     "  to the working copy.  All properties from the repository are applied\n"
     "  to the obstructing path.\n"
     "\n"
     "  See also 'svn help update' for a list of possible characters\n"
     "  reporting the action taken.\n"),
    {'r', 'q', 'N', opt_depth, opt_force, opt_ignore_externals} },

  { "cleanup", svn_cl__cleanup, {0}, N_
    ("Recursively clean up the working copy, removing locks, resuming\n"
     "unfinished operations, etc.\n"
     "usage: cleanup [PATH...]\n"),
    {opt_merge_cmd} },

  { "commit", svn_cl__commit, {"ci"},
    N_("Send changes from your working copy to the repository.\n"
       "usage: commit [PATH...]\n"
       "\n"
       "  A log message must be provided, but it can be empty.  If it is not\n"
       "  given by a --message or --file option, an editor will be started.\n"
       "  If any targets are (or contain) locked items, those will be\n"
       "  unlocked after a successful commit.\n"),
    {'q', 'N', opt_depth, opt_targets, opt_no_unlock, SVN_CL__LOG_MSG_OPTIONS,
     opt_changelist, opt_keep_changelists} },

  { "copy", svn_cl__copy, {"cp"}, N_
    ("Duplicate something in working copy or repository, remembering\n"
     "history.\n"
     "usage: copy SRC[@REV]... DST\n"
     "\n"
     "When copying multiple sources, they will be added as children of DST,\n"
     "which must be a directory.\n"
     "\n"
     "  SRC and DST can each be either a working copy (WC) path or URL:\n"
     "    WC  -> WC:   copy and schedule for addition (with history)\n"
     "    WC  -> URL:  immediately commit a copy of WC to URL\n"
     "    URL -> WC:   check out URL into WC, schedule for addition\n"
     "    URL -> URL:  complete server-side copy;  used to branch and tag\n"
     "  All the SRCs must be of the same type.\n"
     "\n"
     "WARNING: For compatibility with previous versions of Subversion,\n"
     "copies performed using two working copy paths (WC -> WC) will not\n"
     "contact the repository.  As such, they may not, by default, be able\n"
     "to propagate merge tracking information from the source of the copy\n"
     "to the destination.\n"),
    {'r', 'q', opt_ignore_externals, opt_parents, SVN_CL__LOG_MSG_OPTIONS} },

  { "delete", svn_cl__delete, {"del", "remove", "rm"}, N_
    ("Remove files and directories from version control.\n"
     "usage: 1. delete PATH...\n"
     "       2. delete URL...\n"
     "\n"
     "  1. Each item specified by a PATH is scheduled for deletion upon\n"
     "    the next commit.  Files, and directories that have not been\n"
     "    committed, are immediately removed from the working copy\n"
     "    unless the --keep-local option is given.\n"
     "    PATHs that are, or contain, unversioned or modified items will\n"
     "    not be removed unless the --force option is given.\n"
     "\n"
     "  2. Each item specified by a URL is deleted from the repository\n"
     "    via an immediate commit.\n"),
    {opt_force, 'q', opt_targets, SVN_CL__LOG_MSG_OPTIONS, opt_keep_local} },

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
     "     paths.  OLD-TGT and NEW-TGT may be working copy paths or URL[@REV].\n"
     "     NEW-TGT defaults to OLD-TGT if not specified.  -r N makes OLDREV default\n"
     "     to N, -r N:M makes OLDREV default to N and NEWREV default to M.\n"
     "\n"
     "  3. Shorthand for 'svn diff --old=OLD-URL[@OLDREV] --new=NEW-URL[@NEWREV]'\n"
     "\n"
     "  Use just 'svn diff' to display local modifications in a working copy.\n"),
    {'r', 'c', opt_old_cmd, opt_new_cmd, 'N', opt_depth, opt_diff_cmd, 'x',
     opt_no_diff_deleted, opt_notice_ancestry, opt_summarize, opt_changelist,
     opt_force, opt_xml} },

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
    {'r', 'q', 'N', opt_depth, opt_force, opt_native_eol, opt_ignore_externals} },

  { "help", svn_cl__help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {0} },
  /* This command is also invoked if we see option "--help", "-h" or "-?". */

  { "import", svn_cl__import, {0}, N_
    ("Commit an unversioned file or tree into the repository.\n"
     "usage: import [PATH] URL\n"
     "\n"
     "  Recursively commit a copy of PATH to URL.\n"
     "  If PATH is omitted '.' is assumed.\n"
     "  Parent directories are created as necessary in the repository.\n"
     "  If PATH is a directory, the contents of the directory are added\n"
     "  directly under URL.\n"
     "  Unversionable items such as device files and pipes are ignored\n"
     "  if --force is specified.\n"),
    {'q', 'N', opt_depth, opt_autoprops, opt_force, opt_no_autoprops,
     SVN_CL__LOG_MSG_OPTIONS, opt_no_ignore} },

  { "info", svn_cl__info, {0}, N_
    ("Display information about a local or remote item.\n"
     "usage: info [TARGET[@REV]...]\n"
     "\n"
     "  Print information about each TARGET (default: '.').\n"
     "  TARGET may be either a working-copy path or URL.  If specified, REV\n"
     "  determines in which revision the target is first looked up.\n"),
    {'r', 'R', opt_depth, opt_targets, opt_incremental, opt_xml, opt_changelist}
  },

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
    {'r', 'v', 'R', opt_depth, opt_incremental, opt_xml} },

  { "lock", svn_cl__lock, {0}, N_
    ("Lock working copy paths or URLs in the repository, so that\n"
     "no other user can commit changes to them.\n"
     "usage: lock TARGET...\n"
     "\n"
     "  Use --force to steal the lock from another user or working copy.\n"),
    { opt_targets, 'm', 'F', opt_force_log, opt_encoding, opt_force },
    {{'F', N_("read lock comment from file ARG")},
     {'m', N_("specify lock comment ARG")},
     {opt_force_log, N_("force validity of lock comment source")}} },

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
     "     looked up, and the default revision range is REV:1; otherwise,\n"
     "     the URL is looked up in HEAD, and the default revision range is\n"
     "     HEAD:1.\n"
     "\n"
     "  Multiple '-c' or '-r' options may be specified (but not a\n"
     "  combination of '-c' and '-r' options), and mixing of forward and\n"
     "  reverse ranges is allowed.\n"
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
    {'r', 'q', 'v', 'g', 'c', opt_targets, opt_stop_on_copy, opt_incremental,
     opt_xml, 'l', opt_with_all_revprops, opt_with_no_revprops, opt_with_revprop},
    {{opt_with_revprop, N_("retrieve revision property ARG")},
     {'c', N_("the change made in revision ARG")}} },

  { "merge", svn_cl__merge, {0}, N_
    ("Apply the differences between two sources to a working copy path.\n"
     "usage: 1. merge sourceURL1[@N] sourceURL2[@M] [WCPATH]\n"
     "       2. merge sourceWCPATH1@N sourceWCPATH2@M [WCPATH]\n"
     "       3. merge [-c M[,N...] | -r N:M ...] SOURCE[@REV] [WCPATH]\n"
     "\n"
     "  1. In the first form, the source URLs are specified at revisions\n"
     "     N and M.  These are the two sources to be compared.  The revisions\n"
     "     default to HEAD if omitted.\n"
     "\n"
     "  2. In the second form, the URLs corresponding to the source working\n"
     "     copy paths define the sources to be compared.  The revisions must\n"
     "     be specified.\n"
     "\n"
     "  3. In the third form, SOURCE can be either a URL or a working copy\n"
     "     path (in which case its corresponding URL is used).  SOURCE (in\n"
     "     revision REV) is compared as it existed between revisions N and M\n"
     "     for each revision range provided.  If REV is not specified, HEAD\n"
     "     is assumed.  '-c M' is equivalent to '-r <M-1>:M', and '-c -M'\n"
     "     does the reverse: '-r M:<M-1>'.  If no revision ranges are\n"
     "     specified, the default range of 0:REV is used.  Multiple '-c'\n"
     "     and/or '-r' options may be specified, and mixing of forward\n"
     "     and reverse ranges is allowed.\n"
     "\n"
     "  WCPATH is the working copy path that will receive the changes.\n"
     "  If WCPATH is omitted, a default value of '.' is assumed, unless\n"
     "  the sources have identical basenames that match a file within '.':\n"
     "  in which case, the differences will be applied to that file.\n"
     "\n"
     "  NOTE:  Subversion will only record metadata to track the merge\n"
     "  if the two sources are on the same line of history -- if the\n"
     "  first source is an ancestor of the second, or vice-versa.  This is\n"
     "  guaranteed to be the case when using the third form listed above.\n"
     "  The --ignore-ancestry option overrides this, forcing Subversion to\n"
     "  regard the sources as unrelated and not to track the merge.\n"),
    {'r', 'c', 'N', opt_depth, 'q', opt_force, opt_dry_run, opt_merge_cmd,
     opt_record_only, 'x', opt_ignore_ancestry, opt_accept, opt_reintegrate} },

  { "mergeinfo", svn_cl__mergeinfo, {0}, N_
    ("Display merge-related information.\n"
     "usage: mergeinfo SOURCE[@REV] [TARGET[@REV]]\n"
     "\n"
     "  Display information related to merges (or potential merges) between\n"
     "  SOURCE and TARGET (default: '.').  If the --show-revs option\n"
     "  is not provided, display revisions which have been merged from\n"
     "  SOURCE to TARGET; otherwise, display the type of information\n"
     "  specified by the --show-revs option.\n"),
    {'r', opt_show_revs} },

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
     "  In both cases, all the intermediate directories must already exist,\n"
     "  unless the --parents option is given.\n"),
    {'q', opt_parents, SVN_CL__LOG_MSG_OPTIONS} },

  { "move", svn_cl__move, {"mv", "rename", "ren"}, N_
    ("Move and/or rename something in working copy or repository.\n"
     "usage: move SRC... DST\n"
     "\n"
     "When moving multiple sources, they will be added as children of DST,\n"
     "which must be a directory.\n"
     "\n"
     "  Note:  this subcommand is equivalent to a 'copy' and 'delete'.\n"
     "  Note:  the --revision option has no use and is deprecated.\n"
     "\n"
     "  SRC and DST can both be working copy (WC) paths or URLs:\n"
     "    WC  -> WC:   move and schedule for addition (with history)\n"
     "    URL -> URL:  complete server-side rename.\n"
     "  All the SRCs must be of the same type.\n"),
    {'r', 'q', opt_force, opt_parents, SVN_CL__LOG_MSG_OPTIONS} },

  { "propdel", svn_cl__propdel, {"pdel", "pd"}, N_
    ("Remove a property from files, dirs, or revisions.\n"
     "usage: 1. propdel PROPNAME [PATH...]\n"
     "       2. propdel PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Removes versioned props in working copy.\n"
     "  2. Removes unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"),
    {'q', 'R', opt_depth, 'r', opt_revprop, opt_changelist} },

  { "propedit", svn_cl__propedit, {"pedit", "pe"}, N_
    ("Edit a property with an external editor.\n"
     "usage: 1. propedit PROPNAME TARGET...\n"
     "       2. propedit PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Edits versioned prop in working copy or repository.\n"
     "  2. Edits unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "See 'svn help propset' for more on setting properties.\n"),
    {'r', opt_revprop, SVN_CL__LOG_MSG_OPTIONS, opt_force} },

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
     "  is prefixed with the path with which it is associated.  Use the\n"
     "  --strict option to disable these beautifications (useful when\n"
     "  redirecting a binary property value to a file, but available only\n"
     "  if you supply a single TARGET to a non-recursive propget operation).\n"),
    {'v', 'R', opt_depth, 'r', opt_revprop, opt_strict, opt_xml,
     opt_changelist } },

  { "proplist", svn_cl__proplist, {"plist", "pl"}, N_
    ("List all properties on files, dirs, or revisions.\n"
     "usage: 1. proplist [TARGET[@REV]...]\n"
     "       2. proplist --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Lists versioned props. If specified, REV determines in which\n"
     "     revision the target is first looked up.\n"
     "  2. Lists unversioned remote props on repos revision.\n"
     "     TARGET only determines which repository to access.\n"),
    {'v', 'R', opt_depth, 'r', 'q', opt_revprop, opt_xml, opt_changelist } },

  { "propset", svn_cl__propset, {"pset", "ps"}, N_
    ("Set the value of a property on files, dirs, or revisions.\n"
     "usage: 1. propset PROPNAME PROPVAL PATH...\n"
     "       2. propset PROPNAME --revprop -r REV PROPVAL [TARGET]\n"
     "\n"
     "  1. Changes a versioned file or directory property in a working copy.\n"
     "  2. Changes an unversioned property on a repository revision.\n"
     "     (TARGET only determines which repository to access.)\n"
     "\n"
     "  The value may be provided with the --file option instead of PROPVAL.\n"
     "\n"
     "  Note: svn recognizes the following special versioned properties\n"
     "  but will store any arbitrary properties set:\n"
     "    svn:ignore     - A newline separated list of file glob patterns to ignore.\n"
     "    svn:keywords   - Keywords to be expanded.  Valid keywords are:\n"
     "      URL, HeadURL             - The URL for the head version of the object.\n"
     "      Author, LastChangedBy    - The last person to modify the file.\n"
     "      Date, LastChangedDate    - The date/time the object was last modified.\n"
     "      Rev, Revision,           - The last revision the object changed.\n"
     "      LastChangedRevision\n"
     "      Id                       - A compressed summary of the previous\n"
     "                                   4 keywords.\n"
     "      Header                   - Similar to Id but includes the full URL.\n"
     "    svn:executable - If present, make the file executable.  Use\n"
     "      'svn propdel svn:executable PATH...' to clear.\n"
     "    svn:eol-style  - One of 'native', 'LF', 'CR', 'CRLF'.\n"
     "    svn:mime-type  - The mimetype of the file.  Used to determine\n"
     "      whether to merge the file, and how to serve it from Apache.\n"
     "      A mimetype beginning with 'text/' (or an absent mimetype) is\n"
     "      treated as text.  Anything else is treated as binary.\n"
     "    svn:externals  - A newline separated list of module specifiers,\n"
     "      each of which consists of a relative directory path, optional\n"
     "      revision flags and an URL.  The ordering of the three elements\n"
     "      implements different behavior.  Subversion 1.4 and earlier only\n"
     "      support the following formats and the URLs cannot have peg\n"
     "      revisions:\n"
     "        foo             http://example.com/repos/zig\n"
     "        foo/bar -r 1234 http://example.com/repos/zag\n"
     "      Subversion 1.5 and greater support the above formats and the\n"
     "      following formats where the URLs may have peg revisions:\n"
     "                http://example.com/repos/zig foo\n"
     "        -r 1234 http://example.com/repos/zig foo/bar\n"
     "      Relative URLs are supported in Subversion 1.5 and greater for\n"
     "      all above formats and are indicated by starting the URL with one\n"
     "      of the following strings\n"
     "        ../  to the parent directory of the extracted external\n"
     "        ^/   to the repository root\n"
     "        //   to the scheme\n"
     "        /    to the server root\n"
     "      The ambiguous format 'relative_path relative_path' is taken as\n"
     "      'relative_url relative_path' with peg revision support.\n"
     "    svn:needs-lock - If present, indicates that the file should be locked\n"
     "      before it is modified.  Makes the working copy file read-only\n"
     "      when it is not locked.  Use 'svn propdel svn:needs-lock PATH...'\n"
     "      to clear.\n"
     "\n"
     "  The svn:keywords, svn:executable, svn:eol-style, svn:mime-type and\n"
     "  svn:needs-lock properties cannot be set on a directory.  A non-recursive\n"
     "  attempt will fail, and a recursive attempt will set the property\n"
     "  only on the file children of the directory.\n"),
    {'F', opt_encoding, 'q', 'r', opt_targets, 'R', opt_depth, opt_revprop,
     opt_force, opt_changelist },
    {{'F', N_("read property value from file ARG")}} },

  { "resolve", svn_cl__resolve, {0}, N_
    ("Resolve conflicts on working copy files or directories.\n"
     "usage: resolve --accept=ARG [PATH...]\n"
     "\n"
     "  Note:  the --accept option is currently required.\n"),
    {opt_targets, 'R', opt_depth, 'q', opt_accept},
    {{opt_accept, N_("specify automatic conflict resolution source\n"
                             "                            "
                             "('base', 'working', 'mine-conflict',\n"
                             "                            "
                             " 'theirs-conflict', 'mine-full', 'theirs-full')")}} },

  { "resolved", svn_cl__resolved, {0}, N_
    ("Remove 'conflicted' state on working copy files or directories.\n"
     "usage: resolved PATH...\n"
     "\n"
     "  Note:  this subcommand does not semantically resolve conflicts or\n"
     "  remove conflict markers; it merely removes the conflict-related\n"
     "  artifact files and allows PATH to be committed again.  It has been\n"
     "  deprecated in favor of running 'svn resolve --accept working'.\n"),
    {opt_targets, 'R', opt_depth, 'q'} },

  { "revert", svn_cl__revert, {0}, N_
    ("Restore pristine working copy file (undo most local edits).\n"
     "usage: revert PATH...\n"
     "\n"
     "  Note:  this subcommand does not require network access, and resolves\n"
     "  any conflicted states.  However, it does not restore removed directories.\n"),
    {opt_targets, 'R', opt_depth, 'q', opt_changelist} },

  { "status", svn_cl__status, {"stat", "st"}, N_
    ("Print the status of working copy files and directories.\n"
     "usage: status [PATH...]\n"
     "\n"
     "  With no args, print only locally modified items (no network access).\n"
     "  With -q, print only summary information about locally modified items.\n"
     "  With -u, add working revision and server out-of-date information.\n"
     "  With -v, print full revision information on every item.\n"
     "\n"
     "  The first seven columns in the output are each one character wide:\n"
     "    First column: Says if item was added, deleted, or otherwise changed\n"
     "      ' ' no modifications\n"
     "      'A' Added\n"
     "      'C' Conflicted\n"
     "      'D' Deleted\n"
     "      'I' Ignored\n"
     "      'M' Modified\n"
     "      'R' Replaced\n"
     "      'X' an unversioned directory created by an externals definition\n"
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
     "    Fifth column: Whether the item is switched or a file external\n"
     "      ' ' normal\n"
     "      'S' the item has a Switched URL relative to the parent\n"
     "      'X' a versioned file created by an eXternals definition\n"
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
     "    Seventh column: Whether the item is the victim of a tree conflict\n"
     "      ' ' normal\n"
     "      'C' tree-Conflicted\n"
     "    If the item is a tree conflict victim, an additional line is printed\n"
     "    after the item's status line, explaining the nature of the conflict.\n"
     "\n"
     "  The out-of-date information appears in the ninth column (with -u):\n"
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
     "    Status against revision:   981\n"
     "\n"
     "    svn status\n"
     "     M      wc/bar.c\n"
     "    !     C wc/qaz.c\n"
     "          >   local missing, incoming edit upon update\n"
     "    D       wc/qax.c\n"),
    { 'u', 'v', 'N', opt_depth, 'q', opt_no_ignore, opt_incremental, opt_xml,
      opt_ignore_externals, opt_changelist} },

  { "switch", svn_cl__switch, {"sw"}, N_
    ("Update the working copy to a different URL.\n"
     "usage: 1. switch URL[@PEGREV] [PATH]\n"
     "       2. switch --relocate FROM TO [PATH...]\n"
     "\n"
     "  1. Update the working copy to mirror a new URL within the repository.\n"
     "     This behavior is similar to 'svn update', and is the way to\n"
     "     move a working copy to a branch or tag within the same repository.\n"
     "     If specified, PEGREV determines in which revision the target is first\n"
     "     looked up.\n"
     "\n"
     "     If --force is used, unversioned obstructing paths in the working\n"
     "     copy do not automatically cause a failure if the switch attempts to\n"
     "     add the same path.  If the obstructing path is the same type (file\n"
     "     or directory) as the corresponding path in the repository it becomes\n"
     "     versioned but its contents are left 'as-is' in the working copy.\n"
     "     This means that an obstructing directory's unversioned children may\n"
     "     also obstruct and become versioned.  For files, any content differences\n"
     "     between the obstruction and the repository are treated like a local\n"
     "     modification to the working copy.  All properties from the repository\n"
     "     are applied to the obstructing path.\n"
     "\n"
     "     Use the --set-depth option to set a new working copy depth on the\n"
     "     targets of this operation.\n"
     "\n"
     "  2. Rewrite working copy URL metadata to reflect a syntactic change only.\n"
     "     This is used when repository's root URL changes (such as a scheme\n"
     "     or hostname change) but your working copy still reflects the same\n"
     "     directory within the same repository.\n"
     "\n"
     "  See also 'svn help update' for a list of possible characters\n"
     "  reporting the action taken.\n"),
    { 'r', 'N', opt_depth, opt_set_depth, 'q', opt_merge_cmd, opt_relocate,
      opt_ignore_externals, opt_force, opt_accept} },

  { "unlock", svn_cl__unlock, {0}, N_
    ("Unlock working copy paths or URLs.\n"
     "usage: unlock TARGET...\n"
     "\n"
     "  Use --force to break the lock.\n"),
    { opt_targets, opt_force } },

  { "update", svn_cl__update, {"up"},  N_
    ("Bring changes from the repository into the working copy.\n"
     "usage: update [PATH...]\n"
     "\n"
     "  If no revision is given, bring working copy up-to-date with HEAD rev.\n"
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
     "    E  Existed\n"
     "\n"
     "  A character in the first column signifies an update to the actual file,\n"
     "  while updates to the file's properties are shown in the second column.\n"
     "  A 'B' in the third column signifies that the lock for the file has\n"
     "  been broken or stolen.\n"
     "\n"
     "  If --force is used, unversioned obstructing paths in the working\n"
     "  copy do not automatically cause a failure if the update attempts to\n"
     "  add the same path.  If the obstructing path is the same type (file\n"
     "  or directory) as the corresponding path in the repository it becomes\n"
     "  versioned but its contents are left 'as-is' in the working copy.\n"
     "  This means that an obstructing directory's unversioned children may\n"
     "  also obstruct and become versioned.  For files, any content differences\n"
     "  between the obstruction and the repository are treated like a local\n"
     "  modification to the working copy.  All properties from the repository\n"
     "  are applied to the obstructing path.  Obstructing paths are reported\n"
     "  in the first column with code 'E'.\n"
     "\n"
     "  Use the --set-depth option to set a new working copy depth on the\n"
     "  targets of this operation.\n"),
    {'r', 'N', opt_depth, opt_set_depth, 'q', opt_merge_cmd, opt_force,
     opt_ignore_externals, opt_changelist, opt_editor_cmd, opt_accept} },

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

typedef struct config_option_t
{
  const char *file;
  const char *section;
  const char *option;
  const char *value;
} config_option_t;

/* Parse argument of '--config-option'. */
static svn_error_t *
parse_config_option(apr_array_header_t **config_options,
                    const char *opt_arg,
                    apr_pool_t *pool)
{
  config_option_t *config_option;
  const char *first_colon, *second_colon, *equals_sign;
  apr_size_t len = strlen(opt_arg);
  if ((first_colon = strchr(opt_arg, ':')) && (first_colon != opt_arg))
    {
      if ((second_colon = strchr(first_colon + 1, ':')) && (second_colon != first_colon + 1))
        {
          if ((equals_sign = strchr(second_colon + 1, '=')) && (equals_sign != second_colon + 1))
            {
              config_option = apr_pcalloc(pool, sizeof(config_option_t));
              config_option->file = apr_pstrndup(pool, opt_arg, first_colon - opt_arg);
              config_option->section = apr_pstrndup(pool, first_colon + 1, second_colon - first_colon - 1);
              config_option->option = apr_pstrndup(pool, second_colon + 1, equals_sign - second_colon - 1);
              if (! (strchr(config_option->option, ':')))
                {
                  config_option->value = apr_pstrndup(pool, equals_sign + 1, opt_arg + len - equals_sign - 1);
                  if (! *config_options)
                    {
                      *config_options = apr_array_make(pool, 1, sizeof(config_option_t *));
                    }
                  APR_ARRAY_PUSH(*config_options, config_option_t *) = config_option;
                  return SVN_NO_ERROR;
                }
            }
        }
    }
  return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                          _("Invalid syntax of argument of --config-option"));
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
  svn_cl__opt_state_t opt_state = { 0, { 0 } };
  svn_client_ctx_t *ctx;
  apr_array_header_t *received_opts;
  int i;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  const char *dash_m_arg = NULL, *dash_F_arg = NULL;
  const char *path_utf8;
  apr_status_t apr_err;
  svn_cl__cmd_baton_t command_baton;
  svn_auth_baton_t *ab;
  svn_config_t *cfg_config, *cfg_servers;
  svn_boolean_t descend = TRUE;
  svn_boolean_t interactive_conflicts = FALSE;
  apr_hash_t *changelists;

  /* Initialize the app. */
  if (svn_cmdline_init("svn", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
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

  /* Init our changelists hash. */
  changelists = apr_hash_make(pool);

  /* Begin processing arguments. */
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;
  opt_state.revision_ranges =
    apr_array_make(pool, 0, sizeof(svn_opt_revision_range_t *));
  opt_state.depth = svn_depth_unknown;
  opt_state.set_depth = svn_depth_unknown;
  opt_state.accept_which = svn_cl__accept_unspecified;
  opt_state.show_revs = svn_cl__show_revs_merged;

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
      case 'l':
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
          svn_opt_revision_range_t *range;
          apr_array_header_t *change_revs =
            svn_cstring_split(opt_arg, ", \n\r\t\v", TRUE, pool);

          if (opt_state.old_target)
            {
              err = svn_error_create
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Can't specify -c with --old"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }

          for (i = 0; i < change_revs->nelts; i++)
            {
              const char *change_str =
                APR_ARRAY_IDX(change_revs, i, const char *);

              /* Allow any number of 'r's to prefix a revision number.
                 ### TODO: Any reason we're not just using opt.c's
                 ### revision-parsing code here?  Then -c could take
                 ### "{DATE}" and the special words. */
              while (*change_str == 'r')
                change_str++;
              changeno = strtol(change_str, &end, 10);
              if (end == change_str || *end != '\0')
                {
                  err = svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                          _("Non-numeric change argument (%s) "
                                            "given to -c"), change_str);
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
              range = apr_palloc(pool, sizeof(*range));
              if (changeno > 0)
                {
                  range->start.value.number = changeno - 1;
                  range->end.value.number = changeno;
                }
              else
                {
                  changeno = -changeno;
                  range->start.value.number = changeno;
                  range->end.value.number = changeno - 1;
                }
              opt_state.used_change_arg = TRUE;
              range->start.kind = svn_opt_revision_number;
              range->end.kind = svn_opt_revision_number;
              APR_ARRAY_PUSH(opt_state.revision_ranges,
                             svn_opt_revision_range_t *) = range;
            }
        }
        break;
      case 'r':
        opt_state.used_revision_arg = TRUE;
        if (svn_opt_parse_revision_to_range(opt_state.revision_ranges,
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
      case opt_incremental:
        opt_state.incremental = TRUE;
        break;
      case 'F':
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (! err)
          err = svn_stringbuf_from_file2(&(opt_state.filedata),
                                         utf8_opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        dash_F_arg = opt_arg;
        break;
      case opt_targets:
        {
          svn_stringbuf_t *buffer, *buffer_utf8;

          /* We need to convert to UTF-8 now, even before we divide
             the targets into an array, because otherwise we wouldn't
             know what delimiter to use for svn_cstring_split().  */

          err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);

          if (! err)
            err = svn_stringbuf_from_file2(&buffer, utf8_opt_arg, pool);
          if (! err)
            err = svn_utf_stringbuf_to_utf8(&buffer_utf8, buffer, pool);
          if (err)
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          opt_state.targets = svn_cstring_split(buffer_utf8->data, "\n\r",
                                                TRUE, pool);
        }
        break;
      case opt_force:
        opt_state.force = TRUE;
        break;
      case opt_force_log:
        opt_state.force_log = TRUE;
        break;
      case opt_dry_run:
        opt_state.dry_run = TRUE;
        break;
      case opt_revprop:
        opt_state.revprop = TRUE;
        break;
      case 'R':
        opt_state.depth = SVN_DEPTH_INFINITY_OR_FILES(TRUE);
        break;
      case 'N':
        descend = FALSE;
        break;
      case opt_depth:
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error
            (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("Error converting depth "
                                 "from locale to UTF-8")), pool, "svn: ");
        opt_state.depth = svn_depth_from_word(utf8_opt_arg);
        if (opt_state.depth == svn_depth_unknown
            || opt_state.depth == svn_depth_exclude)
          {
            return svn_cmdline_handle_exit_error
              (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' is not a valid depth; try "
                                   "'empty', 'files', 'immediates', "
                                   "or 'infinity'"),
                                 utf8_opt_arg), pool, "svn: ");
          }
        break;
      case opt_set_depth:
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error
            (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("Error converting depth "
                                 "from locale to UTF-8")), pool, "svn: ");
        opt_state.set_depth = svn_depth_from_word(utf8_opt_arg);
        /* svn_depth_exclude is okay for --set-depth. */
        if (opt_state.set_depth == svn_depth_unknown)
          {
            return svn_cmdline_handle_exit_error
              (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' is not a valid depth; try "
                                   "'exclude', 'empty', 'files', 'immediates', "
                                   "or 'infinity'"),
                                 utf8_opt_arg), pool, "svn: ");
          }
        break;
      case opt_version:
        opt_state.version = TRUE;
        break;
      case opt_auth_username:
        err = svn_utf_cstring_to_utf8(&opt_state.auth_username,
                                      opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case opt_auth_password:
        err = svn_utf_cstring_to_utf8(&opt_state.auth_password,
                                      opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case opt_encoding:
        opt_state.encoding = apr_pstrdup(pool, opt_arg);
        break;
      case opt_xml:
        opt_state.xml = TRUE;
        break;
      case opt_stop_on_copy:
        opt_state.stop_on_copy = TRUE;
        break;
      case opt_strict:
        opt_state.strict = TRUE;
        break;
      case opt_no_ignore:
        opt_state.no_ignore = TRUE;
        break;
      case opt_no_auth_cache:
        opt_state.no_auth_cache = TRUE;
        break;
      case opt_non_interactive:
        opt_state.non_interactive = TRUE;
        break;
      case opt_trust_server_cert:
        opt_state.trust_server_cert = TRUE;
        break;
      case opt_no_diff_deleted:
        opt_state.no_diff_deleted = TRUE;
        break;
      case opt_notice_ancestry:
        opt_state.notice_ancestry = TRUE;
        break;
      case opt_ignore_ancestry:
        opt_state.ignore_ancestry = TRUE;
        break;
      case opt_ignore_externals:
        opt_state.ignore_externals = TRUE;
        break;
      case opt_relocate:
        opt_state.relocate = TRUE;
        break;
      case 'x':
        err = svn_utf_cstring_to_utf8(&opt_state.extensions, opt_arg, pool);
        if (err)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case opt_diff_cmd:
        opt_state.diff_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case opt_merge_cmd:
        opt_state.merge_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case opt_record_only:
        opt_state.record_only = TRUE;
        break;
      case opt_editor_cmd:
        opt_state.editor_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case opt_old_cmd:
        if (opt_state.used_change_arg)
          {
            err = svn_error_create
              (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
               _("Can't specify -c with --old"));
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        opt_state.old_target = apr_pstrdup(pool, opt_arg);
        break;
      case opt_new_cmd:
        opt_state.new_target = apr_pstrdup(pool, opt_arg);
        break;
      case opt_config_dir:
        err = svn_utf_cstring_to_utf8(&path_utf8, opt_arg, pool);
        opt_state.config_dir = svn_path_canonicalize(path_utf8, pool);
        break;
      case opt_config_options:
        {
          err = parse_config_option(&opt_state.config_options, opt_arg, pool);
          if (err)
            {
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
        }
        break;
      case opt_autoprops:
        opt_state.autoprops = TRUE;
        break;
      case opt_no_autoprops:
        opt_state.no_autoprops = TRUE;
        break;
      case opt_native_eol:
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
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        break;
      case opt_no_unlock:
        opt_state.no_unlock = TRUE;
        break;
      case opt_summarize:
        opt_state.summarize = TRUE;
        break;
      case opt_remove:
        opt_state.remove = TRUE;
        break;
      case opt_changelist:
        opt_state.changelist = apr_pstrdup(pool, opt_arg);
        if (opt_state.changelist[0] == '\0')
          {
            err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("Changelist names must not be empty"));
            return svn_cmdline_handle_exit_error(err, pool, "svn: ");
          }
        apr_hash_set(changelists, opt_state.changelist,
                     APR_HASH_KEY_STRING, (void *)1);
        break;
      case opt_keep_changelists:
        opt_state.keep_changelists = TRUE;
        break;
      case opt_keep_local:
        opt_state.keep_local = TRUE;
        break;
      case opt_with_all_revprops:
        /* If --with-all-revprops is specified along with one or more
         * --with-revprops options, --with-all-revprops takes precedence. */
        opt_state.all_revprops = TRUE;
        break;
      case opt_with_no_revprops:
        opt_state.no_revprops = TRUE;
        break;
      case opt_with_revprop:
        err = svn_opt_parse_revprop(&opt_state.revprop_table, opt_arg, pool);
        if (err != SVN_NO_ERROR)
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        break;
      case opt_parents:
        opt_state.parents = TRUE;
        break;
      case 'g':
        opt_state.use_merge_history = TRUE;
        break;
      case opt_accept:
        opt_state.accept_which = svn_cl__accept_from_word(opt_arg);
        if (opt_state.accept_which == svn_cl__accept_invalid)
          return svn_cmdline_handle_exit_error
            (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("'%s' is not a valid --accept value"),
                               opt_arg),
             pool, "svn: ");
        break;
      case opt_show_revs:
        opt_state.show_revs = svn_cl__show_revs_from_word(opt_arg);
        if (opt_state.show_revs == svn_cl__show_revs_invalid)
          return svn_cmdline_handle_exit_error
            (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("'%s' is not a valid --show-revs value"),
                               opt_arg),
             pool, "svn: ");
        break;
      case opt_reintegrate:
        opt_state.reintegrate = TRUE;
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;
      }
    }

  /* Turn our hash of changelists into an array of unique ones. */
  err = svn_hash_keys(&(opt_state.changelists), changelists, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");

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
                  {opt_version,    /* must accept its own option */
                   'q',            /* brief output */
                   opt_config_dir  /* all commands accept this */
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

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id,
                                             svn_cl__global_options))
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

  /* Only merge supports multiple revisions/revision ranges. */
  if (subcommand->cmd_func != svn_cl__merge
      && subcommand->cmd_func != svn_cl__log)
    {
      if (opt_state.revision_ranges->nelts > 1)
        {
          err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Multiple revision arguments "
                                   "encountered; can't specify -c twice, "
                                   "or both -c and -r"));
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        }
    }

  /* Merge doesn't support specifying a revision range
     when using --reintegrate. */
  if (subcommand->cmd_func == svn_cl__merge
      && opt_state.revision_ranges->nelts
      && opt_state.reintegrate)
    {
      err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("-r and -c can't be used with --reintegrate"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  /* Disallow simultaneous use of both --depth and --set-depth. */
  if ((opt_state.depth != svn_depth_unknown)
      && (opt_state.set_depth != svn_depth_unknown))
    {
      err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("--depth and --set-depth are mutually "
                               "exclusive"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  /* Disallow simultaneous use of both --with-all-revprops and
     --with-no-revprops.  */
  if (opt_state.all_revprops && opt_state.no_revprops)
    {
      err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("--with-all-revprops and --with-no-revprops "
                               "are mutually exclusive"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  /* Disallow simultaneous use of both --with-revprop and
     --with-no-revprops.  */
  if (opt_state.revprop_table && opt_state.no_revprops)
    {
      err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("--with-revprop and --with-no-revprops "
                               "are mutually exclusive"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  /* --trust-server-cert can only be used with --non-interactive */
  if (opt_state.trust_server_cert && !opt_state.non_interactive)
    {
      err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("--trust-server-cert requires "
                               "--non-interactive"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  /* Ensure that 'revision_ranges' has at least one item, and that
     'start_revision' and 'end_revision' match that item. */
  if (opt_state.revision_ranges->nelts == 0)
    {
      svn_opt_revision_range_t *range = apr_palloc(pool, sizeof(*range));
      range->start.kind = svn_opt_revision_unspecified;
      range->end.kind = svn_opt_revision_unspecified;
      APR_ARRAY_PUSH(opt_state.revision_ranges,
                     svn_opt_revision_range_t *) = range;
    }
  opt_state.start_revision = APR_ARRAY_IDX(opt_state.revision_ranges, 0,
                                           svn_opt_revision_range_t *)->start;
  opt_state.end_revision = APR_ARRAY_IDX(opt_state.revision_ranges, 0,
                                         svn_opt_revision_range_t *)->end;

  /* If we're running a command that could result in a commit, verify
     that any log message we were given on the command line makes
     sense (unless we've also been instructed not to care). */
  if ((! opt_state.force_log)
      && (subcommand->cmd_func == svn_cl__commit
          || subcommand->cmd_func == svn_cl__copy
          || subcommand->cmd_func == svn_cl__delete
          || subcommand->cmd_func == svn_cl__import
          || subcommand->cmd_func == svn_cl__mkdir
          || subcommand->cmd_func == svn_cl__move
          || subcommand->cmd_func == svn_cl__lock
          || subcommand->cmd_func == svn_cl__propedit))
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

  if (opt_state.relocate && (opt_state.depth != svn_depth_unknown))
    {
      err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                             _("--relocate and --depth are mutually "
                               "exclusive"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
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

  /* -N has a different meaning depending on the command */
  if (descend == FALSE)
    {
      if (subcommand->cmd_func == svn_cl__status)
        {
          opt_state.depth = SVN_DEPTH_INFINITY_OR_IMMEDIATES(FALSE);
        }
      else if (subcommand->cmd_func == svn_cl__revert
               || subcommand->cmd_func == svn_cl__add
               || subcommand->cmd_func == svn_cl__commit)
        {
          /* In pre-1.5 Subversion, some commands treated -N like
             --depth=empty, so force that mapping here.  Anyway, with
             revert it makes sense to be especially conservative,
             since revert can lose data. */
          opt_state.depth = svn_depth_empty;
        }
      else
        {
          opt_state.depth = SVN_DEPTH_INFINITY_OR_FILES(FALSE);
        }
    }
  /* Create a client context object. */
  command_baton.opt_state = &opt_state;
  if ((err = svn_client_create_context(&ctx, pool)))
    return svn_cmdline_handle_exit_error(err, pool, "svn: ");
  command_baton.ctx = ctx;

  err = svn_config_get_config(&(ctx->config),
                              opt_state.config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable. */
      if (err->apr_err == APR_EACCES)
        {
          svn_handle_warning2(stderr, err, "svn: ");
          svn_error_clear(err);
        }
      else
        return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  cfg_config = apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                            APR_HASH_KEY_STRING);
  cfg_servers = apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_SERVERS,
                             APR_HASH_KEY_STRING);

  /* Update the options in the config */
  if (opt_state.config_options)
    {
      for (i = 0; i < opt_state.config_options->nelts; i++)
        {
          config_option_t *config_option = APR_ARRAY_IDX(opt_state.config_options,
                                                         i, config_option_t *);
          if (strcmp(config_option->file, "config") == 0)
            {
              svn_config_set(cfg_config, config_option->section,
                             config_option->option, config_option->value);
            }
          else if (strcmp(config_option->file, "servers") == 0)
            {
              svn_config_set(cfg_servers, config_option->section,
                             config_option->option, config_option->value);
            }
          else
            {
              err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Unrecognized file in argument of --config-option"));
              svn_handle_warning2(stderr, err, "svn: ");
              svn_error_clear(err);
            }
        }
    }

  /* XXX: Only diff_cmd for now, overlay rest later and stop passing
     opt_state altogether? */
  if (opt_state.diff_cmd)
    svn_config_set(cfg_config, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF_CMD, opt_state.diff_cmd);
  if (opt_state.merge_cmd)
    svn_config_set(cfg_config, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, opt_state.merge_cmd);

  /* Check for mutually exclusive args --auto-props and --no-auto-props */
  if (opt_state.autoprops && opt_state.no_autoprops)
    {
      err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                             _("--auto-props and --no-auto-props are "
                               "mutually exclusive"));
      return svn_cmdline_handle_exit_error(err, pool, "svn: ");
    }

  /* The --reintegrate option is mutually exclusive with both
     --ignore-ancestry and --record-only. */
  if (opt_state.reintegrate)
    {
      if (opt_state.ignore_ancestry)
        {
          if (opt_state.record_only)
            {
              err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                     _("--reintegrate cannot be used with "
                                       "--ignore-ancestry or "
                                       "--record-only"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          else
            {
              err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                     _("--reintegrate cannot be used with "
                                       "--ignore-ancestry"));
              return svn_cmdline_handle_exit_error(err, pool, "svn: ");
            }
          }
      else if (opt_state.record_only)
        {
          err = svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                 _("--reintegrate cannot be used with "
                                   "--record-only"));
          return svn_cmdline_handle_exit_error(err, pool, "svn: ");
        }
    }

  /* Update auto-props-enable option, and populate the MIME types map,
     for add/import commands */
  if (subcommand->cmd_func == svn_cl__add
      || subcommand->cmd_func == svn_cl__import)
    {
      const char *mimetypes_file;
      svn_config_get(cfg_config, &mimetypes_file,
                     SVN_CONFIG_SECTION_MISCELLANY,
                     SVN_CONFIG_OPTION_MIMETYPES_FILE, FALSE);
      if (mimetypes_file && *mimetypes_file)
        {
          if ((err = svn_io_parse_mimetypes_file(&(ctx->mimetypes_map),
                                                 mimetypes_file, pool)))
            svn_handle_error2(err, stderr, TRUE, "svn: ");
        }

      if (opt_state.autoprops)
        {
          svn_config_set_bool(cfg_config, SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, TRUE);
        }
      if (opt_state.no_autoprops)
        {
          svn_config_set_bool(cfg_config, SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, FALSE);
        }
    }

  /* Update the 'keep-locks' runtime option */
  if (opt_state.no_unlock)
    svn_config_set_bool(cfg_config, SVN_CONFIG_SECTION_MISCELLANY,
                        SVN_CONFIG_OPTION_NO_UNLOCK, TRUE);

  /* Set the log message callback function.  Note that individual
     subcommands will populate the ctx->log_msg_baton3. */
  ctx->log_msg_func3 = svn_cl__get_log_message;

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
  if ((err = svn_cmdline_create_auth_baton(&ab,
                                           opt_state.non_interactive,
                                           opt_state.auth_username,
                                           opt_state.auth_password,
                                           opt_state.config_dir,
                                           opt_state.no_auth_cache,
                                           opt_state.trust_server_cert,
                                           cfg_config,
                                           ctx->cancel_func,
                                           ctx->cancel_baton,
                                           pool)))
    svn_handle_error2(err, stderr, TRUE, "svn: ");

  ctx->auth_baton = ab;

  /* Set up conflict resolution callback. */
  if ((err = svn_config_get_bool(cfg_config, &interactive_conflicts,
                                 SVN_CONFIG_SECTION_MISCELLANY,
                                 SVN_CONFIG_OPTION_INTERACTIVE_CONFLICTS,
                                 TRUE)))  /* ### interactivity on by default.
                                                 we can change this. */
    svn_handle_error2(err, stderr, TRUE, "svn: ");

  if ((opt_state.accept_which == svn_cl__accept_unspecified
       && (!interactive_conflicts || opt_state.non_interactive))
      || opt_state.accept_which == svn_cl__accept_postpone)
    {
      /* If no --accept option at all and we're non-interactive, we're
         leaving the conflicts behind, so don't need the callback.  Same if
         the user said to postpone. */
      ctx->conflict_func = NULL;
      ctx->conflict_baton = NULL;
    }
  else
    {
      svn_cmdline_prompt_baton_t *pb = apr_palloc(pool, sizeof(*pb));
      pb->cancel_func = ctx->cancel_func;
      pb->cancel_baton = ctx->cancel_baton;

      if (opt_state.non_interactive)
        {
          if (opt_state.accept_which == svn_cl__accept_edit)
            return svn_cmdline_handle_exit_error
              (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("--accept=%s incompatible with"
                                   " --non-interactive"), SVN_CL__ACCEPT_EDIT),
               pool, "svn: ");
          if (opt_state.accept_which == svn_cl__accept_launch)
            return svn_cmdline_handle_exit_error
              (svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("--accept=%s incompatible with"
                                   " --non-interactive"),
                                 SVN_CL__ACCEPT_LAUNCH),
               pool, "svn: ");
        }

      ctx->conflict_func = svn_cl__conflict_handler;
      ctx->conflict_baton = svn_cl__conflict_baton_make(
          opt_state.accept_which,
          ctx->config,
          opt_state.editor_cmd,
          pb,
          pool);
    }

  /* And now we finally run the subcommand. */
  err = (*subcommand->cmd_func)(os, &command_baton, pool);
  if (err)
    {
      svn_error_t *tmp_err;

      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svn help' for more info"));
        }
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
