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
#include "svn_opt.h"
#include "svn_time.h"
#include "svn_utf.h"
#include "cl.h"


/*** Option Processing ***/

/* Option codes and descriptions for the command line client.
 *
 * This must not have more than SVN_OPT_MAX_OPTIONS entries; if you
 * need more, increase that limit first. 
 *
 * The entire list must be terminated with an entry of nulls.
 */
const apr_getopt_option_t svn_cl__options[] =
  {
    {"force",         svn_cl__force_opt, 0, "force operation to run"},
    {"help",          'h', 0, "show help on a subcommand"},
    {NULL,            '?', 0, "show help on a subcommand"},
    {"message",       'm', 1, "specify commit message \"ARG\""},
    {"quiet",         'q', 0, "print as little as possible"},
    {"recursive",     'R', 0, "descend recursively"},
    {"nonrecursive",  'N', 0, "operate on single directory only"},
    {"revision",      'r', 1,
                  "revision ARG, X:Y range ({ARG}, {X}:{Y} for dates)"},
    {"file",          'F', 1, "read data from file ARG"},
    {"incremental",   svn_cl__incremental_opt, 0,
                      "give output suitable for concatenation"},
    {"encoding",      svn_cl__encoding_opt, 1,
                      "treat value as being in charset encoding ARG"},
    {"version",       svn_cl__version_opt, 0, "print client version info"},
    {"verbose",       'v', 0, "print extra information"},
    {"show-updates",  'u', 0, "display update information"},
    {"username",      svn_cl__auth_username_opt, 1, "specify a username ARG"},
    {"password",      svn_cl__auth_password_opt, 1, "specify a password ARG"},
    {"extensions",    'x', 1, "pass \"ARG\" as bundled options to GNU diff"},
    {"targets",       svn_cl__targets_opt, 1,
                      "pass contents of file \"ARG\" as additional args"},
    {"xml",           svn_cl__xml_opt, 0, "output in xml"},
    {"strict",        svn_cl__strict_opt, 0, "use strict semantics"},
    {"no-ignore",     svn_cl__no_ignore_opt, 0,
                      "disregard default and svn:ignore property ignores"},
    {"no-auth-cache", svn_cl__no_auth_cache_opt, 0,
                      "do not cache authentication tokens"},
    {"non-interactive", svn_cl__non_interactive_opt, 0,
                      "do no interactive prompting"},
    {"dry-run",       svn_cl__dry_run_opt, 0,
                      "try operation but make no changes"},

    /* ### Perhaps the option should be named "--rev-prop" instead?
           Generally, we do include the hyphen; the only reason not to
           here is that in code and emails we almost always refer to
           them as "revprops", like with "wcprops".  So this
           inconsistency is justified in the name of consistency.  How
           distressingly typical.  Thoughts? :-) */
    {"revprop",       svn_cl__revprop_opt, 0,
                      "operate on a revision property (use with -r)"},

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
const svn_opt_subcommand_desc_t svn_cl__cmd_table[] =
{
  { "add", svn_cl__add, {0},
    "Put files and directories under revision control, scheduling\n"
    "them for addition to repository.  They will be added in next commit.\n"
    "usage: add PATH [PATH [PATH ... ]]\n",
    {svn_cl__targets_opt, 'R', 'q'} },

  { "cat", svn_cl__cat, {0},
    "Output the content of specified files or URLs.\n"
    "usage: cat TARGET [TARGET [TARGET ... ]]\n",
    {'r', svn_cl__auth_username_opt, svn_cl__auth_password_opt} },

  { "checkout", svn_cl__checkout, {"co"},
    "Check out a working copy from a repository.\n"
    "usage: checkout URL [URL [URL ... ]] [PATH]\n"
    "  Note: If PATH is omitted, the basename of the URL will be used as\n"
    "  the destination. If multiple URLs are given each will be checked\n"
    "  out into a sub-directory of PATH, with the name of the sub-directory\n"
    "  being the basename of the URL.\n",
    {'r', 'q', 'N',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt } },

  { "cleanup", svn_cl__cleanup, {0},
    "Recursively clean up the working copy, removing locks, resuming\n"
    "unfinished operations, etc.\n"
    "usage: cleanup [PATH [PATH ... ]]\n",
    {0} },
  
  { "commit", svn_cl__commit, {"ci"},
    "Send changes from your working copy to the repository.\n"
    "usage: commit [PATH [PATH ... ]]\n\n"
    "  Be sure to use one of -m or -F to send a log message.\n",
    {'m', 'F', 'q', 'N', svn_cl__targets_opt,
     svn_cl__force_opt, svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt,  svn_cl__non_interactive_opt,
     svn_cl__encoding_opt} },
  
  { "copy", svn_cl__copy, {"cp"},
    "Duplicate something in working copy or repos, remembering history.\n"
    "usage: copy SRC DST\n\n"
    "  SRC and DST can each be either a working copy (WC) path or URL:\n"
    "    WC  -> WC:   copy and schedule for addition (with history)\n"
    "    WC  -> URL:  immediately commit a copy of WC to URL\n"
    "    URL -> WC:   check out URL into WC, schedule for addition\n"
    "    URL -> URL:  complete server-side copy;  used to branch & tag\n",
    {'m', 'F', 'r', 'q',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt,  svn_cl__non_interactive_opt,
     svn_cl__encoding_opt} },
  
  { "delete", svn_cl__delete, {"del", "remove", "rm"},
    "Remove files and directories from version control.\n"
    "usage: delete [TARGET [TARGET ... ]]\n\n"
    "  If run on a working copy TARGET, the item is scheduled for deletion\n"
    "  upon the next commit.  Files, and directories that have not been\n"
    "  committed, are immediately removed from the working copy.  The\n"
    "  command will not remove TARGETs that are, or contain, unversioned\n"
    "  or modified items; use the --force option to override this\n"
    "  behaviour.\n\n"
    "  If run on an URL, the item is deleted from the repository via an\n"
    "  immediate commit.\n",
    {svn_cl__force_opt, 'm', 'F', 'q', svn_cl__targets_opt,
     svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt,
     svn_cl__encoding_opt} },
  
  { "diff", svn_cl__diff, {"di"},
    "display the differences between two paths.\n"
    "usage: 1. diff [-r N[:M]] [TARGET [TARGET ... ]]\n"
    "       2. diff URL1[@N] URL2[@M]\n\n"
    "  1. Each TARGET can be either a working copy path or URL.  If no\n"
    "     TARGET is specified, a value of '.' is assumed.\n\n"
    "     If TARGET is a URL, then revs N and M must be given via -r.\n\n"
    "     If TARGET is a working copy path, then -r switch means:\n"
    "       -r N:M  : server compares TARGET@N and TARGET@M,\n"
    "       -r N    : client compares TARGET@N against working copy\n"
    "       (no -r) : client compares base and working copies of TARGET\n\n"
    "  2. If the alternate syntax is used, the server compares URL1 and URL2\n"
    "     at revisions N and M respectively.  If either N or M are ommitted,\n"
    "     a value of HEAD is assumed.\n",
    {'r', 'x', 'N',
     svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt} },

  { "export", svn_cl__export, {0},
    "export stuff.\n"
    "usage: 1. export [-r REV] URL [PATH]\n"
    "       2. export PATH1 PATH2\n\n"
    "  1. Exports a clean directory tree from the repository specified by\n"
    "     URL, at revision REV if it is given, otherwise at HEAD, into \n"
    "     PATH. If PATH is omitted, the last component of the URL is used\n"
    "     for the local directory name.\n\n"
    "  2. Exports a clean directory tree from the working copy specified by\n"
    "     PATH1 into PATH2.  all local changes will be preserved, but files\n"
    "     not under revision control will not be copied.\n",
    {'r', 'q', svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt} },

  { "help", svn_cl__help, {"?", "h"},
    "Display this usage message.\n"
    "usage: help [SUBCOMMAND [SUBCOMMAND ... ]]\n",
    {svn_cl__version_opt, 'q'} },
  /* We need to support "--help", "-?", and all that good stuff, of
     course.  But those options, since unknown, will result in the
     help message being printed out anyway, so there's no need to
     support them explicitly. */
  
  { "import", svn_cl__import, {0},
    "Commit an unversioned file or tree into the repository.\n"
    "usage: import URL [PATH] [NEW_ENTRY_IN_REPOS]\n\n"
    "  Recursively commit a copy of PATH to URL.\n"
    "  If no 3rd arg, copy top-level contents of PATH into URL\n"
    "  directly.  Otherwise, create NEW_ENTRY underneath URL and\n"
    "  begin copy there.\n",
    {'m', 'F', 'q', 'N', svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt,
     svn_cl__encoding_opt} },
 
  { "info", svn_cl__info, {0},
    "Display info about a resource.\n"
    "usage: info [PATH [PATH ... ]]\n\n"
    "  Print information about PATHs.\n",
    {svn_cl__targets_opt, 'R'} },
 
  { "list", svn_cl__ls, {"ls"},
    "List directory entries of a URL.\n"
    "usage: list URL [URL ... ]\n\n"
    "  If URL is a file, just file entry will be displayed.\n",
    {'r', 'v', 'R', svn_cl__auth_username_opt, 
     svn_cl__auth_password_opt, svn_cl__no_auth_cache_opt,
     svn_cl__non_interactive_opt} },
  
  { "log", svn_cl__log, {0},
    "Show the log messages for a set of revision(s) and/or file(s).\n"
    "usage: log [URL] [PATH [PATH ... ]]\n"
    "  Print the log messages for local PATHs, or for PATHs under\n"
    "  URL, if URL is given.  If URL is given by itself, then print log\n"
    "  messages for everything under it.  With -v, also print all affected\n"
    "  paths with each log message.\n"
    "\n"
    "  Each log message is printed just once, even if more than one of the\n"
    "  affected paths for that revision were explicitly requested.  Logs\n"
    "  cross copy history by default; use --strict to disable this.\n"
    "  For example:\n"
    "\n"
    "    svn log\n"
    "    svn log foo.c\n"
    "    svn log http://www.example.com/repo/project/foo.c\n"
    "    svn log http://www.example.com/repo/project foo.c bar.c\n",
    {'r', 'v', svn_cl__targets_opt, svn_cl__auth_username_opt,
     svn_cl__auth_password_opt, svn_cl__no_auth_cache_opt,
     svn_cl__non_interactive_opt, svn_cl__strict_opt,
     svn_cl__incremental_opt, svn_cl__xml_opt} },

  { "merge", svn_cl__merge, {0},
    "apply the differences between two URLs to a working copy path.\n"
    "usage: 1. merge URL1[@N] URL2[@M] [PATH]\n"
    "       2. merge -r N:M TARGET [PATH]\n\n"
    "  1. In the first form, URL1 and URL2 are URLs specified at revisions\n"
    "     N and M.  These are the two sources to be compared.  The revisons\n"
    "     default to HEAD if omitted.\n\n"
    "  2. In the second form TARGET can be an URL, or it can be a working copy\n"
    "     path in which case the corresponding URL is used.  This URL, at\n"
    "     revisions N and M, defines the two sources to be compared.\n\n"
    "  PATH is the working copy path that will receive the changes.\n"
    "  If omitted, a default value of '.' is assumed.\n\n",
    {'r', 'N', 'q', svn_cl__force_opt, svn_cl__dry_run_opt,
     svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt} },
  
  { "mkdir", svn_cl__mkdir, {0},
    "Create a new directory under revision control.\n"
    "usage: mkdir TARGET [TARGET [TARGET ... ]]\n\n"
    "  Create a directory with a name given by the final component of\n"
    "  TARGET.  If TARGET is a working copy path the directory is scheduled\n"
    "  for addition in the working copy.  If TARGET is an URL the directory\n"
    "  is created in the repository via an immediate commit.  In both cases\n"
    "  all the intermediate directories must already exist.\n",
    {'m', 'F', 'q', svn_cl__auth_username_opt, svn_cl__auth_password_opt,
     svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt,
     svn_cl__encoding_opt} },

  { "move", svn_cl__move, {"mv", "rename", "ren"},
    "Move/rename something in working copy or repository.\n"
    "usage: move SRC DST\n\n"
    "  NOTE:  this command is equivalent to a 'copy' and 'delete'.\n\n"
    "  SRC and DST can both be working copy (WC) paths or URLs:\n"
    "    WC  -> WC:   move and schedule for addition (with history)\n"
    "    URL -> URL:  complete server-side rename.\n",    
    {'m', 'F', 'r', 'q', svn_cl__auth_username_opt,
     svn_cl__auth_password_opt, svn_cl__no_auth_cache_opt, 
     svn_cl__non_interactive_opt, svn_cl__force_opt,
     svn_cl__encoding_opt} },
  
  { "propdel", svn_cl__propdel, {"pdel"},
    "Remove PROPNAME from files, dirs, or revisions.\n"
    "usage: 1. propdel PROPNAME [PATH [PATH ... ]]\n"
    "       2. propdel PROPNAME --revprop -r REV [URL]\n\n"
    "  1. Removes versioned props in working copy.\n"
    "  2. Removes unversioned remote prop on repos revision.\n\n",
    {'q', 'R', 'r', svn_cl__revprop_opt} },
  
  { "propedit", svn_cl__propedit, {"pedit", "pe"},
    "Edit property PROPNAME with $EDITOR on targets.\n"
    "usage: 1. propedit PROPNAME PATH [PATH [PATH ... ]]\n"
    "       2. propedit PROPNAME --revprop -r REV [URL]\n\n"
    "  1. Edits versioned props in working copy.\n"
    "  2. Edits unversioned remote prop on repos revision.\n\n",
    {'r', svn_cl__revprop_opt, svn_cl__encoding_opt} },
  
  { "propget", svn_cl__propget, {"pget", "pg"},
    "Print value of PROPNAME on files, dirs, or revisions.\n"
    "usage: 1. propget PROPNAME [PATH [PATH ... ]]\n"
    "       2. propget PROPNAME --revprop -r REV [URL]\n\n"
    "  1. Prints versioned prop in working copy.\n"
    "  2. Prints unversioned remote prop on repos revision.\n\n",
    {'R', 'r', svn_cl__revprop_opt} },
  
  { "proplist", svn_cl__proplist, {"plist", "pl"},
    "List all properties on files, dirs, or revisions.\n"
    "usage: 1. proplist [PATH [PATH ... ]]\n"
    "       2. proplist --revprop -r REV [URL]\n\n"
    "  1. Lists versioned props in working copy.\n"
    "  2. Lists unversioned remote props on repos revision.\n\n",
    {'v', 'R', 'r', svn_cl__revprop_opt} },
  
  { "propset", svn_cl__propset, {"pset", "ps"},
    "Set PROPNAME to PROPVAL on files, dirs, or revisions.\n\n"
    "usage: 1. propset PROPNAME [PROPVAL | -F VALFILE] PATH [PATH [PATH ... ]]\n"
    "       2. propset PROPNAME --revprop -r REV [PROPVAL | -F VALFILE] [URL]\n"
    "\n"
    "  1. Creates a versioned, local propchange in working copy.\n"
    "  2. Creates an unversioned, remote propchange on repos revision.\n\n"
    "  Note: svn recognizes the following special versioned properties\n"
    "  but will store any arbitrary properties set:\n"
    "    svn:ignore     - A newline separated list of file patterns to ignore.\n"
    "    svn:keywords   - Keywords to be expanded.  Valid keywords are:\n"
    "      URL, HeadURL             - The URL for the head version of "
    "the object.\n"
    "      Author, LastChangedBy    - The last person to modify the file.\n"
    "      Date, LastChangedDate    - The date/time the object was last "
    "modified.\n"
    "      Rev, LastChangedRevision - The last revision the object changed.\n"
    "      Id                       - A compressed summary of the previous\n"
    "                                   4 keywords.\n"
    "    svn:executable - If present, make the file executable. This\n"
    "      property cannot be set on a directory.  A non-recursive attempt\n"
    "      will fail, and a recursive attempt will set the property only\n"
    "      on the file children of the directory.\n"
    "    svn:eol-style  - One of 'native', 'LF', 'CR', 'CRLF'. \n"
    "    svn:mime-type  - The mimetype of the file.  Used to determine\n"
    "      whether to merge the file, and how to serve it from Apache.\n"
    "      A mimetype beginning with 'text/' (or an absent mimetype) is\n"
    "      treated as text.  Anything else is treated as binary.\n"
    "    svn:externals  - A newline separated list of module specifiers,\n"
    "      each of which consists of a relative directory path, optional\n"
    "      revision flags, and an URL.  For example\n"
    "        foo             http://example.com/repos/zig\n"
    "        foo/bar -r 1234 http://example.com/repos/zag\n",
    {'F', 'q', 'r', svn_cl__targets_opt, 'R', svn_cl__revprop_opt,
      svn_cl__encoding_opt} },
  
  { "revert", svn_cl__revert, {0},
    "Restore pristine working copy file (undo all local edits)\n"
    "usage: revert PATH [PATH [PATH ... ]]\n\n"
    "  Note:  this routine does not require network access, and \n"
    "  resolves any conflicted states.\n",
    {svn_cl__targets_opt, 'R', 'q'} },

  { "resolve", svn_cl__resolve, {0},
    "Remove 'conflicted' state on working copy files or directories.\n"
    "usage: resolve PATH [PATH [PATH ... ]]\n\n"
    "  Note:  this routine does not semantically resolve conflict markers;\n"
    "  it merely removes conflict-related artifact files and allows PATH\n"
    "  to be committed again.\n",
    {svn_cl__targets_opt, 'R', 'q'} },
 
  { "status", svn_cl__status, {"stat", "st"},
    "Print the status of working copy files and directories.\n"
    "usage: status [PATH [PATH ... ]]\n\n"
    "  With no args, print only locally modified items (no network access).\n"
    "  With -u, add working revision and server out-of-date information.\n"
    "  With -v, print full revision information on every item.\n"
    "\n"
    "The first five columns in the output are each one character wide:\n"
    "    First column: Says if item was added, deleted, or otherwise changed\n"
    "      ' ' no modifications\n"
    "      'A' Added\n"
    "      'D' Deleted\n"
    "      'M' Modified\n"
    "      'C' Conflicted\n"
    "      '?' item is not under revision control\n"
    "      '!' item is missing and was removed via a non-svn command\n"
    "      '~' versioned as a directory, but is a file, or vice versa\n"
    "    Second column: Modifications of a file's or directory's properties\n"
    "      ' ' no modifications\n"
    "      'M' Modified\n"
    "      'C' Conflicted\n"
    "    Third column: Whether the working copy directory is locked\n"
    "      ' ' not locked\n"
    "      'L' locked\n"
    "    Fourth column: Scheduled commit will contain addition-with-history\n"
    "      ' ' no history scheduled with commit\n"
    "      '+' history scheduled with commit\n"
    "    Fifth column: Whether the item is switched relative to its parent\n"
    "      ' ' normal\n"
    "      'S' switched\n"
    "    The out-of-date information appears in the eighth column\n"
    "      '*' a newer revision exists on the server\n"
    "      ' ' the working copy is up to date\n"
    "\n"
    "Remaining fields are variable width and delimited by spaces:\n"
    "\n"
    "The working revision is the next field if -u or -v is given, followed\n"
    "by both the last committed revision and last committed author if -v is\n"
    "given.  The working copy path is always the final field, so it can\n"
    "include spaces.\n"
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
    "    Head revision:   981\n"
    "\n"
    "    svn status --show-updates --verbose wc\n"
    "     M           965       938     kfogel   wc/bar.c\n"
    "           *     965       922    sussman   wc/foo.c\n"
    "    A  +         965       687        joe   wc/qax.c\n"
    "                 965       687        joe   wc/zig.c\n"
    "    Head revision:   981\n",
    { 'u', 'v', 'N', 'q',
      svn_cl__auth_username_opt, svn_cl__auth_password_opt,
      svn_cl__no_auth_cache_opt, svn_cl__non_interactive_opt,
      svn_cl__no_ignore_opt } },
  
  { "switch", svn_cl__switch, {"sw"},
    "Update working copy to mirror a new URL\n"
    "usage: switch URL [PATH]\n\n"
    "  Note:  this is the way to move a working copy to a new branch.\n",
    { 'r', 'N', 'q', svn_cl__auth_username_opt,
      svn_cl__auth_password_opt, svn_cl__no_auth_cache_opt,
      svn_cl__non_interactive_opt } },
 
  { "update", svn_cl__update, {"up"}, 
    "Bring changes from the repository into the working copy.\n"
    "usage: update [PATH [PATH ... ]]\n\n"
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
    "  while updates to the file's props are shown in the second column.\n",
    {'r', 'N', 'q', svn_cl__auth_username_opt,
     svn_cl__auth_password_opt, svn_cl__no_auth_cache_opt,
     svn_cl__non_interactive_opt } },

  { NULL, NULL, {0}, NULL, {0} }
};



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  svn_error_t *err;
  apr_pool_t *pool;
  int opt_id, err2;
  apr_getopt_t *os;  
  svn_cl__opt_state_t opt_state;
  int received_opts[SVN_OPT_MAX_OPTIONS];
  int i, num_opts = 0;
  const svn_opt_subcommand_desc_t *subcommand = NULL;
  svn_boolean_t log_under_version_control = FALSE;
  svn_boolean_t log_is_pathname = FALSE;
  apr_status_t apr_err;

  /* C programs default to the "C" locale by default.  But because svn
     is supposed to be i18n-aware, it should inherit the default
     locale of its environment.  */
  setlocale (LC_ALL, "");

  /* Initialize the APR subsystem, and register an atexit() function
     to Uninitialize that subsystem at program exit. */
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

  /* Create our top-level pool. */
  pool = svn_pool_create (NULL);

  /* Begin processing arguments. */
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;
  
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
      const char *utf8_opt_arg;

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
          if (apr_stat (&finfo, opt_arg, APR_FINFO_MIN, pool) == APR_SUCCESS)
            {
              /* woah! that log message is a file. I doubt the user
                 intended that. */
              log_is_pathname = TRUE;
            }
        }
        opt_state.message = apr_pstrdup (pool, opt_arg);
        break;
      case 'r':
        if (opt_state.start_revision.kind != svn_opt_revision_unspecified)
          {
            svn_handle_error (svn_error_create
                              (SVN_ERR_CL_ARG_PARSING_ERROR,
                               0, NULL,
                               "Multiple revision arguments encountered; "
                               "try '-r M:N' instead of '-r M -r N'"),
                              stderr, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        if (svn_opt_parse_revision (&(opt_state.start_revision),
                                    &(opt_state.end_revision),
                                    opt_arg, pool) != 0)
          {
            err = svn_utf_cstring_to_utf8 (&utf8_opt_arg, opt_arg, NULL, pool);
            if (err)
              svn_handle_error (err, stderr, FALSE);
            else
              svn_handle_error (svn_error_createf
                                (SVN_ERR_CL_ARG_PARSING_ERROR,
                                 0, NULL,
                                 "Syntax error in revision argument \"%s\"",
                                 utf8_opt_arg),
                                stderr, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
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
        err = svn_utf_cstring_to_utf8 (&utf8_opt_arg, opt_arg, NULL, pool);
        if (!err)
          err = svn_stringbuf_from_file (&(opt_state.filedata),
                                         utf8_opt_arg, pool);
        if (err)
          {
            svn_handle_error (err, stderr, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        /* Find out if log message file is under revision control. */
        {
          svn_wc_adm_access_t *adm_access;
          const svn_wc_entry_t *e;

          err = svn_wc_adm_probe_open (&adm_access, NULL, utf8_opt_arg, FALSE,
                                       FALSE, pool);
          if (! err)
            err = svn_wc_entry (&e, utf8_opt_arg, adm_access, FALSE, pool);
          if ((err == SVN_NO_ERROR) && e)
            log_under_version_control = TRUE;
          if (err)
            svn_error_clear (err);
        }
        break;
      case svn_cl__targets_opt:
	{
 	  svn_stringbuf_t *buffer, *buffer_utf8;

          /* We need to convert to UTF-8 now, even before we divide
             the targets into an array, because otherwise we wouldn't
             know what delimiter to use for svn_cstring_split().  */

          err = svn_utf_cstring_to_utf8 (&utf8_opt_arg, opt_arg, NULL, pool);

          if (! err)
            err = svn_stringbuf_from_file (&buffer, utf8_opt_arg, pool);
          if (! err)
            err = svn_utf_stringbuf_to_utf8 (&buffer_utf8, buffer, pool);
  	  if (err)
  	    {
  	      svn_handle_error (err, stdout, FALSE);
  	      svn_pool_destroy (pool);
  	      return EXIT_FAILURE;
  	    }
	  opt_state.targets = svn_cstring_split (buffer_utf8->data, "\n\r",
                                                 TRUE, pool);
	}
        break;
      case svn_cl__force_opt:
        opt_state.force = TRUE;
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
        opt_state.help = TRUE;
        break;
      case svn_cl__auth_username_opt:
        err = svn_utf_cstring_to_utf8 (&opt_state.auth_username,
                                       opt_arg, NULL, pool);
        if (err)
          {
            svn_handle_error (err, stdout, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        break;
      case svn_cl__auth_password_opt:
        err = svn_utf_cstring_to_utf8 (&opt_state.auth_password,
                                       opt_arg, NULL, pool);
        if (err)
          {
            svn_handle_error (err, stdout, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        break;
      case svn_cl__encoding_opt:
        opt_state.encoding = apr_pstrdup (pool, opt_arg);
        break;
      case svn_cl__xml_opt:
        opt_state.xml = TRUE;
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
      case 'x':
        err = svn_utf_cstring_to_utf8 (&opt_state.extensions, opt_arg,
                                       NULL, pool);
        if (err) {
          svn_handle_error (err, stderr, FALSE);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
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
    subcommand = svn_opt_get_canonical_subcommand (svn_cl__cmd_table, "help");

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
          subcommand = svn_opt_get_canonical_subcommand (svn_cl__cmd_table,
                                                         first_arg);
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

      if (! svn_opt_subcommand_takes_option (subcommand, opt_id))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt = 
            svn_opt_get_option_from_code (opt_id, svn_cl__options);
          svn_opt_format_option (&optstr, badopt, FALSE, pool);
          fprintf (stderr,
                   "\nError: subcommand '%s' doesn't accept option '%s'\n\n",
                   subcommand->name, optstr);
          svn_opt_subcommand_help (subcommand->name,
                                   svn_cl__cmd_table,
                                   svn_cl__options,
                                   pool);
          svn_pool_destroy (pool);
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
                               0, NULL,
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
                               0, NULL,
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
      svn_error_t *tmp_err;

      if (err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        svn_opt_subcommand_help (subcommand->name, svn_cl__cmd_table,
                                 svn_cl__options, pool);
      else
        svn_handle_error (err, stderr, 0);

      /* Tell the user about 'svn cleanup' if any error on the stack
         was about locked working copies. */
      for (tmp_err = err; tmp_err; tmp_err = tmp_err->child)
        if (tmp_err->apr_err == SVN_ERR_WC_LOCKED)
          {
            fputs ("svn: run 'svn cleanup' to remove locks"
                   " (type 'svn help cleanup' for details)\n", stderr);
            break;
          }

      svn_error_clear (err);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }
  else
    {
      svn_pool_destroy (pool);
      return EXIT_SUCCESS;
    }
}
