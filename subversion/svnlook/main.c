/*
 * main.c: Subversion server inspection tool.
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

#include <locale.h>
#include <apr_general.h>
#include <apr_pools.h>
#include <apr_time.h>
#include <apr_thread_proc.h>
#include <apr_file_io.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_time.h"
#include "svn_utf.h"
#include "svn_opt.h"


/*** Some convenience macros and types. ***/

/* Temporary subdirectory created for use by svnlook.  ### todo: we
   need to either find a way to query APR for a suitable (that is,
   writable) temporary directory, or add this #define to the
   svn_private_config.h stuffs (with a default of perhaps "/tmp/.svnlook" */
#define SVNLOOK_TMPDIR       ".svnlook"


/* Option handling. */

static svn_opt_subcommand_t
  subcommand_author,
  subcommand_changed,
  subcommand_date,
  subcommand_diff,
  subcommand_dirschanged,
  subcommand_help,
  subcommand_info,
  subcommand_log,
  subcommand_tree,
  subcommand_youngest;

/* Option codes and descriptions. */
enum 
  { 
    svnlook__show_ids = SVN_OPT_FIRST_LONGOPT_ID,
    svnlook__no_diff_on_delete
  };

/*
 * This must not have more than SVN_OPT_MAX_OPTIONS entries; if you
 * need more, increase that limit first. 
 *
 * The entire list must be terminated with an entry of nulls.
 */
static const apr_getopt_option_t options_table[] =
  {
    {"help",          'h', 0,
     "show help on a subcommand"},

    {NULL,            '?', 0,
     "show help on a subcommand"},

    {"revision",      'r', 1,
     "specify revision number ARG"},

    {"transaction",  't', 1,
     "specify transaction name ARG"},

    {"show-ids",      svnlook__show_ids, 0,
     "show node revision ids for each path"},

    {"no-diff-on-delete", svnlook__no_diff_on_delete, 0,
     "do not run diff on deleted files"},

    {0,               0, 0, 0}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc_t cmd_table[] =
  {
    {"author", subcommand_author, {0},
     "usage: svnlook author REPOS_PATH\n\n"
     "Print the author.\n",
     {'r', 't'} },
    
    {"changed", subcommand_changed, {0},
     "usage: svnlook changed REPOS_PATH\n\n"
     "Print the paths that were changed.\n",
     {'r', 't'} },
    
    {"date", subcommand_date, {0},
     "usage: svnlook date REPOS_PATH\n\n"
     "Print the date.\n",
     {'r', 't'} },

    {"diff", subcommand_diff, {0},
     "usage: svnlook diff REPOS_PATH\n\n"
     "Print GNU-style diffs of changed files and properties.\n",
     {'r', 't', svnlook__no_diff_on_delete} },

    {"dirs-changed", subcommand_dirschanged, {0},
     "usage: svnlook dirs-changed REPOS_PATH\n\n"
     "Print the directories that were changed.\n",
     {'r', 't'} },
    
    {"help", subcommand_help, {"?", "h"},
     "usage: svn help [SUBCOMMAND1 [SUBCOMMAND2] ...]\n\n"
     "Display this usage message.\n",
     {0} },

    {"info", subcommand_info, {0},
     "usage: svnlook info REPOS_PATH\n\n"
     "Print the author, date, log message size, and log message.\n",
     {'r', 't'} },

    {"log", subcommand_log, {0},
     "usage: svnlook log REPOS_PATH\n\n"
     "Print the log message.\n",
     {'r', 't'} },

    {"tree", subcommand_tree, {0},
     "usage: svnlook tree REPOS_PATH\n\n"
     "Print the tree, optionally showing node revision ids.\n",
     {'r', 't', svnlook__show_ids} },

    {"youngest", subcommand_youngest, {0},
     "usage: svnlook youngest REPOS_PATH\n\n"
     "Print the youngest revision number.\n",
     {0} },

    { NULL, NULL, {0}, NULL, {0} }
  };


/* Baton for passing option/argument state to a subcommand function. */
struct svnlook_opt_state
{
  const char *repos_path;
  svn_revnum_t rev;
  const char *txn;
  svn_boolean_t show_ids;
  svn_boolean_t help;
  svn_boolean_t no_diff_on_delete;
};


typedef struct svnlook_ctxt_t
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_boolean_t is_revision;
  svn_boolean_t show_ids;
  svn_boolean_t no_diff_on_delete;
  svn_revnum_t rev_id;
  svn_fs_txn_t *txn;
  const char *txn_name /* UTF-8! */;

} svnlook_ctxt_t;



/*** Helper functions. ***/
static svn_error_t *
get_property (svn_string_t **prop_value,
              svnlook_ctxt_t *c, 
              const char *prop_name /* UTF-8! */,
              apr_pool_t *pool)
{
  /* Fetch transaction property... */
  if (! c->is_revision)
    return svn_fs_txn_prop (prop_value, c->txn, prop_name, pool);

  /* ...or revision property -- it's your call. */
  return svn_fs_revision_prop (prop_value, c->fs, c->rev_id, prop_name, pool);
}


static svn_error_t *
get_root (svn_fs_root_t **root,
          svnlook_ctxt_t *c,
          apr_pool_t *pool)
{
  /* Open up the appropriate root (revision or transaction). */
  if (c->is_revision)
    {
      /* If we didn't get a valid revision number, we'll look at the
         youngest revision. */
      if (! SVN_IS_VALID_REVNUM (c->rev_id))
        SVN_ERR (svn_fs_youngest_rev (&(c->rev_id), c->fs, pool));

      SVN_ERR (svn_fs_revision_root (root, c->fs, c->rev_id, pool));
    }
  else
    {
      SVN_ERR (svn_fs_txn_root (root, c->txn, pool));
    }

  return SVN_NO_ERROR;
}



/*** Tree Routines ***/

/* Generate a generic delta tree. */
static svn_error_t *
generate_delta_tree (svn_repos_node_t **tree,
                     svn_repos_t *repos,
                     svn_fs_root_t *root, 
                     svn_revnum_t base_rev, 
                     svn_boolean_t use_copy_history,
                     apr_pool_t *pool)
{
  svn_fs_root_t *base_root;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  apr_pool_t *edit_pool = svn_pool_create (pool);
  svn_fs_t *fs = svn_repos_fs (repos);

  /* Get the base root. */
  SVN_ERR (svn_fs_revision_root (&base_root, fs, base_rev, pool));

  /* Request our editor. */
  SVN_ERR (svn_repos_node_editor (&editor, &edit_baton, repos,
                                  base_root, root, pool, edit_pool));

  /* Drive our editor. */
  SVN_ERR (svn_repos_dir_delta (base_root, "", NULL, root, "",
                                editor, edit_baton, 
                                FALSE, TRUE, FALSE, 
                                use_copy_history, edit_pool));

  /* Return the tree we just built. */
  *tree = svn_repos_node_from_baton (edit_baton);
  svn_pool_destroy (edit_pool);
  return SVN_NO_ERROR;
}



/*** Tree Printing Routines ***/

/* Recursively print only directory nodes that either a) have property
   mods, or b) contains files that have changed. */
static svn_error_t *
print_dirs_changed_tree (svn_repos_node_t *node,
                         const char *path /* UTF-8! */,
                         apr_pool_t *pool)
{
  svn_repos_node_t *tmp_node;
  int print_me = 0;
  const char *full_path;

  if (! node)
    return SVN_NO_ERROR;

  /* Not a directory?  We're not interested. */
  if (node->kind != svn_node_dir)
    return SVN_NO_ERROR;

  /* Got prop mods?  Excellent. */
  if (node->prop_mod)
    print_me = 1;

  if (! print_me)
    {
      /* Fly through the list of children, checking for modified files. */
      tmp_node = node->child;
      if (tmp_node)
        {
          if ((tmp_node->kind == svn_node_file)
              || (tmp_node->text_mod)
              || (tmp_node->action == 'A')
              || (tmp_node->action == 'D'))
            {
              print_me = 1;
            }
          while (tmp_node->sibling && (! print_me ))
            {
              tmp_node = tmp_node->sibling;
              if ((tmp_node->kind == svn_node_file)
                  || (tmp_node->text_mod)
                  || (tmp_node->action == 'A')
                  || (tmp_node->action == 'D'))
                {
                  print_me = 1;
                }
            }
        }
    }
  
  /* Print the node if it qualifies. */
  if (print_me)
    {
      const char *path_native;
      SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));
      printf ("%s/\n", path_native);
    }

  /* Recursively handle the node's children. */
  tmp_node = node->child;
  if (! tmp_node)
    return SVN_NO_ERROR;

  full_path = svn_path_join (path, tmp_node->name, pool);
  SVN_ERR (print_dirs_changed_tree (tmp_node, full_path, pool));
  while (tmp_node->sibling)
    {
      tmp_node = tmp_node->sibling;
      full_path = svn_path_join (path, tmp_node->name, pool);
      SVN_ERR (print_dirs_changed_tree (tmp_node, full_path, pool));
    }

  return SVN_NO_ERROR;
}


/* Recursively print all nodes in the tree that have been modified
   (do not include directories affected only by "bubble-up"). */
static svn_error_t *
print_changed_tree (svn_repos_node_t *node,
                    const char *path /* UTF-8! */,
                    apr_pool_t *pool)
{
  const char *full_path;
  char status[3] = "_ ";
  int print_me = 1;

  if (! node)
    return SVN_NO_ERROR;

  /* Print the node. */
  if (node->action == 'A')
    status[0] = 'A';
  else if (node->action == 'D')
    status[0] = 'D';
  else if (node->action == 'R')
    {
      if ((! node->text_mod) && (! node->prop_mod))
        print_me = 0;
      if (node->text_mod)
        status[0] = 'U';
      if (node->prop_mod)
        status[1] = 'U';
    }
  else
    print_me = 0;

  /* Print this node unless told to skip it. */
  if (print_me)
    {
      const char *path_native;
      SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));
      printf ("%s  %s%s\n",
              status,
              path_native,
              node->kind == svn_node_dir ? "/" : "");
    }
  
  /* Return here if the node has no children. */
  node = node->child;
  if (! node)
    return SVN_NO_ERROR;

  /* Recursively handle the node's children. */
  full_path = svn_path_join (path, node->name, pool);
  SVN_ERR (print_changed_tree (node, full_path, pool));
  while (node->sibling)
    {
      node = node->sibling;
      full_path = svn_path_join (path, node->name, pool);
      SVN_ERR (print_changed_tree (node, full_path, pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
open_writable_binary_file (apr_file_t **fh, 
                           const char *path /* UTF-8! */, 
                           apr_pool_t *pool)
{
  apr_array_header_t *path_pieces;
  svn_error_t *err;
  int i;
  const char *full_path, *dir;
  
  /* Try the easy way to open the file. */
  err = svn_io_file_open (fh, path, 
                          APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BINARY,
                          APR_OS_DEFAULT, pool);
  if (! err)
    return SVN_NO_ERROR;

  svn_path_split (path, &dir, NULL, pool);

  /* If the file path has no parent, then we've already tried to open
     it as best as we care to try above. */
  if (svn_path_is_empty (dir))
    return svn_error_createf (err->apr_err, err->src_err, err,
                              "Error opening writable file %s", path);

  path_pieces = svn_path_decompose (dir, pool);
  if (! path_pieces->nelts)
    return SVN_NO_ERROR;

  full_path = "";
  for (i = 0; i < path_pieces->nelts; i++)
    {
      svn_node_kind_t kind;
      const char *piece = ((const char **) (path_pieces->elts))[i];
      full_path = svn_path_join (full_path, piece, pool);
      SVN_ERR (svn_io_check_path (full_path, &kind, pool));

      /* Does this path component exist at all? */
      if (kind == svn_node_none)
        {
          SVN_ERR (svn_io_dir_make (full_path, APR_OS_DEFAULT, pool));
        }
      else if (kind != svn_node_dir)
        {
          if (err)
            return svn_error_createf (err->apr_err, err->src_err, err,
                                      "Error creating dir %s (path exists)", 
                                      full_path);
        }
    }

  /* Now that we are ensured that the parent path for this file
     exists, try once more to open it. */
  err = svn_io_file_open (fh, path, 
                          APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BINARY,
                          APR_OS_DEFAULT, pool);
  if (err)
    return svn_error_createf (err->apr_err, err->src_err, err,
                              "Error opening writable file %s", path);
    
  return SVN_NO_ERROR;
}


static svn_error_t *
dump_contents (apr_file_t *fh,
               svn_fs_root_t *root,
               const char *path /* UTF-8! */,
               apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_size_t len, len2;
  svn_stream_t *stream;
  char buffer[1024];

  /* Get a stream to the current file's contents. */
  SVN_ERR (svn_fs_file_contents (&stream, root, path, pool));
  
  /* Now, route that data into our temporary file. */
  while (1)
    {
      len = sizeof (buffer);
      SVN_ERR (svn_stream_read (stream, buffer, &len));
      len2 = len;
      apr_err = apr_file_write (fh, buffer, &len2);
      if ((apr_err) || (len2 != len))
        return svn_error_createf 
          (apr_err ? apr_err : SVN_ERR_INCOMPLETE_DATA, 0, NULL,
           "Error writing contents of %s", path);
      if (len != sizeof (buffer))
        break;
    }

  /* And close the file. */
  apr_file_close (fh);
  return SVN_NO_ERROR;
}


/* Recursively print all nodes in the tree that have been modified
   (do not include directories affected only by "bubble-up"). */
static svn_error_t *
print_diff_tree (svn_fs_root_t *root,
                 svn_fs_root_t *base_root,
                 svn_repos_node_t *node, 
                 const char *path /* UTF-8! */,
                 const char *base_path /* UTF-8! */,
                 svn_boolean_t no_diff_on_delete,
                 apr_pool_t *pool)
{
  const char *orig_path = NULL, *new_path = NULL;
  apr_file_t *fh1, *fh2;
  svn_boolean_t is_copy = FALSE;

  if (! node)
    return SVN_NO_ERROR;

  /* Print copyfrom history for the top node of a copied tree. */
  if ((SVN_IS_VALID_REVNUM (node->copyfrom_rev))
      && (node->copyfrom_path != NULL))
    {
      const char *base_path_native;

      /* This is ... a copy. */
      is_copy = TRUE;

      /* Propagate the new base.  Copyfrom paths usually start with a
         slash; we remove it for consistency with the target path.
         ### Yes, it would be *much* better for something in the path
             library to be taking care of this! */
      if (node->copyfrom_path[0] == '/')
        base_path = apr_pstrdup (pool, node->copyfrom_path + 1);
      else
        base_path = apr_pstrdup (pool, node->copyfrom_path);

      SVN_ERR (svn_utf_cstring_from_utf8 (&base_path_native, base_path, pool));

      printf ("Copied: %s (from rev %" SVN_REVNUM_T_FMT ", %s)\n",
              node->name, node->copyfrom_rev, base_path_native);

      SVN_ERR (svn_fs_revision_root (&base_root,
                                     svn_fs_root_fs (base_root),
                                     node->copyfrom_rev, pool));
    }

  /* First, we'll just print file content diffs. */
  if (node->kind == svn_node_file)
    {
      /* Here's the generalized way we do our diffs:

         - First, dump the contents of the new version of the file
           into the svnlook temporary directory, building out the
           actual directories that need to be created in order to
           fully represent the filesystem path inside the tmp
           directory.

         - Then, dump the contents of the old version of the file into
           the top level of the svnlook temporary directory using a
           unique temporary file name (we do this *after* putting the
           new version of the file there in case something actually
           versioned has a name that looks like one of our unique
           identifiers).
           
         - Next, we run 'diff', passing the repository path as the
           label.  

         - Finally, we delete the temporary files (but leave the
           built-out directories in place until after all diff
           handling has been finished).  */
      if ((node->action == 'R') && (node->text_mod))
        {
          new_path = svn_path_join (SVNLOOK_TMPDIR, path, pool);
          SVN_ERR (open_writable_binary_file (&fh1, new_path, pool));
          SVN_ERR (dump_contents (fh1, root, path, pool));
          apr_file_close (fh1);

          SVN_ERR (svn_io_open_unique_file (&fh2, &orig_path, new_path,
                                            NULL, FALSE, pool));
          SVN_ERR (dump_contents
                   (fh2, base_root, base_path, pool));
          apr_file_close (fh2);
        }
      if ((node->action == 'A') && (node->text_mod))
        {
          new_path = svn_path_join (SVNLOOK_TMPDIR, path, pool);
          SVN_ERR (open_writable_binary_file (&fh1, new_path, pool));
          SVN_ERR (dump_contents (fh1, root, path, pool));
          apr_file_close (fh1);

          /* Create an empty file. */
          SVN_ERR (svn_io_open_unique_file (&fh2, &orig_path, new_path,
                                            NULL, FALSE, pool));
          apr_file_close (fh2);
        }
      if (node->action == 'D')
        {
          new_path = svn_path_join (SVNLOOK_TMPDIR, path, pool);
          SVN_ERR (open_writable_binary_file (&fh1, new_path, pool));
          apr_file_close (fh1);

          SVN_ERR (svn_io_open_unique_file (&fh2, &orig_path, new_path,
                                            NULL, FALSE, pool));
          SVN_ERR (dump_contents
                   (fh2, base_root, base_path, pool));
          apr_file_close (fh2);
        }
    }

  if (orig_path && new_path)
    {
      apr_file_t *outhandle;
      apr_status_t apr_err;
      const char *label;
      const char *abs_path;
      int exitcode;
      const char *path_native;
      
      if (! is_copy)
        {
          SVN_ERR (svn_utf_cstring_from_utf8 (&path_native, path, pool));
          printf ("%s: %s\n", 
                  ((node->action == 'A') ? "Added" : 
                   ((node->action == 'D') ? "Deleted" :
                    ((node->action == 'R') ? "Modified" : "Index"))),
                  path_native);
        }

      if ((! no_diff_on_delete) || (node->action != 'D'))
        {
          printf ("===========================================================\
===================\n");
          fflush (stdout);

          /* Get an apr_file_t representing stdout, which is where
             we'll have the diff program print to. */
          apr_err = apr_file_open_stdout (&outhandle, pool);
          if (apr_err)
            return svn_error_create 
              (apr_err, 0, NULL,
               "print_diff_tree: can't open handle to stdout");

          label = apr_psprintf (pool, "%s\t(original)", base_path);
          SVN_ERR (svn_path_get_absolute (&abs_path, orig_path, pool));
          SVN_ERR (svn_io_run_diff (SVNLOOK_TMPDIR, NULL, 0, label, NULL,
                                    abs_path, path, 
                                    &exitcode, outhandle, NULL, pool));

          /* TODO: Handle exit code == 2 (i.e. diff error) here. */
        }

      printf ("\n");
      fflush (stdout);
    }
  else if (is_copy)
    {
      printf ("\n");
    }
    
  /* Now, delete any temporary files. */
  if (orig_path)
    svn_io_remove_file (orig_path, pool);
  if (new_path)
    svn_io_remove_file (new_path, pool);

  /* Return here if the node has no children. */
  node = node->child;
  if (! node)
    return SVN_NO_ERROR;

  /* Handle children and siblings. */
  {
    apr_pool_t *subpool = svn_pool_create (pool);

    /* Recurse down into children. */
    SVN_ERR (print_diff_tree
             (root, base_root, node,
              svn_path_join (path, node->name, subpool),
              svn_path_join (base_path, node->name, subpool),
              no_diff_on_delete,
              subpool));

    /* Recurse across siblings. */
    while (node->sibling)
      {
        node = node->sibling;
       
        SVN_ERR (print_diff_tree
                 (root, base_root, node,
                  svn_path_join (path, node->name, subpool),
                  svn_path_join (base_path, node->name, subpool),
                  no_diff_on_delete,
                  pool));
      }
    
    apr_pool_destroy (subpool);
  }

  return SVN_NO_ERROR;
}


/* Recursively print all nodes, and (optionally) their node revision ids.

   ROOT is the revision or transaction root used to build that tree.
   PATH and ID are the current path and node revision id being
   printed, and INDENTATION the number of spaces to prepent to that
   path's printed output.  ID may be NULL if SHOW_IDS is FALSE (in
   which case, ids won't be printed at all).  

   Use POOL for all allocations.  */
static svn_error_t *
print_tree (svn_fs_root_t *root,
            const char *path /* UTF-8! */,
            const svn_fs_id_t *id,
            int indentation,
            svn_boolean_t show_ids,
            apr_pool_t *pool)
{
  apr_pool_t *subpool;
  int i;
  const char *name_native;
  int is_dir;
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  /* Print the indentation. */
  for (i = 0; i < indentation; i++)
    {
      printf (" ");
    }

  /* Print the node. */
  SVN_ERR (svn_fs_is_dir (&is_dir, root, path, pool));
  SVN_ERR (svn_utf_cstring_from_utf8 (&name_native, 
                                      svn_path_basename (path, pool), 
                                      pool));
  printf ("%s%s", name_native, is_dir ? "/" : "");

  if (show_ids)
    {
      svn_string_t *unparsed_id = NULL;
      if (id)
        unparsed_id = svn_fs_unparse_id (id, pool);
      printf (" <%s>", unparsed_id ? unparsed_id->data : "unknown");
    }
  printf ("\n");

  /* Return here if PATH is not a directory. */
  if (! is_dir)
    return SVN_NO_ERROR;
  
  /* Recursively handle the node's children. */
  SVN_ERR (svn_fs_dir_entries (&entries, root, path, pool));
  subpool = svn_pool_create (pool);
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_fs_dirent_t *entry;

      apr_hash_this (hi, &key, &keylen, &val);
      entry = val;
      SVN_ERR (print_tree (root, svn_path_join (path, entry->name, pool),
                           entry->id, indentation + 1, show_ids, subpool));
      svn_pool_clear (subpool);
    }
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/*** Subcommand handlers. ***/

/* Print the revision's log message to stdout, followed by a newline. */
static svn_error_t *
do_log (svnlook_ctxt_t *c, svn_boolean_t print_size, apr_pool_t *pool)
{
  svn_string_t *prop_value;
  const char *log_native;

  SVN_ERR (get_property (&prop_value, c, SVN_PROP_REVISION_LOG, pool));
  if (! (prop_value && prop_value->data))
    {
      printf ("%s\n", print_size ? "0" : "");
      return SVN_NO_ERROR;
    }
  
  if (print_size)
    printf ("%" APR_SIZE_T_FMT "\n", prop_value->len);

  SVN_ERR (svn_utf_cstring_from_utf8 (&log_native, prop_value->data, pool));
  printf ("%s\n", log_native);
  return SVN_NO_ERROR;
}


/* Print the timestamp of the commit (in the revision case) or the
   empty string (in the transaction case) to stdout, followed by a
   newline. */
static svn_error_t *
do_date (svnlook_ctxt_t *c, apr_pool_t *pool)
{
  svn_string_t *prop_value;

  SVN_ERR (get_property (&prop_value, c, SVN_PROP_REVISION_DATE, pool));
  if (prop_value && prop_value->data)
    {
      /* Convert the date for humans. */
      apr_time_t aprtime;
      
      SVN_ERR (svn_time_from_cstring (&aprtime, prop_value->data, pool));
      printf ("%s", svn_time_to_human_cstring (aprtime, pool));
    }

  printf ("\n");
  return SVN_NO_ERROR;
}


/* Print the author of the commit to stdout, followed by a newline. */
static svn_error_t *
do_author (svnlook_ctxt_t *c, apr_pool_t *pool)
{
  svn_string_t *prop_value;

  SVN_ERR (get_property (&prop_value, c, SVN_PROP_REVISION_AUTHOR, pool));
  if (prop_value && prop_value->data) 
    {
      const char *native;
      SVN_ERR (svn_utf_cstring_from_utf8 (&native, prop_value->data, pool));
      printf ("%s", native);
    }
  
  printf ("\n");
  return SVN_NO_ERROR;
}


/* Print a list of all directories in which files, or directory
   properties, have been modified. */
static svn_error_t *
do_dirs_changed (svnlook_ctxt_t *c, apr_pool_t *pool)
{ 
  svn_fs_root_t *root;
  svn_revnum_t base_rev_id;
  svn_repos_node_t *tree;

  SVN_ERR (get_root (&root, c, pool));
  if (c->is_revision)
    base_rev_id = c->rev_id - 1;
  else
    base_rev_id = svn_fs_txn_base_revision (c->txn);

  if (! SVN_IS_VALID_REVNUM (base_rev_id))
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, NULL,
       "Transaction '%s' is not based on a revision.  How odd.",
       c->txn_name);
  
  SVN_ERR (generate_delta_tree (&tree, c->repos, root, base_rev_id, 
                                TRUE, pool)); 
  if (tree)
    SVN_ERR (print_dirs_changed_tree (tree, "", pool));

  return SVN_NO_ERROR;
}


/* Print a list of all paths modified in a format compatible with `svn
   update'. */
static svn_error_t *
do_changed (svnlook_ctxt_t *c, apr_pool_t *pool)
{
  svn_fs_root_t *root;
  svn_revnum_t base_rev_id;
  svn_repos_node_t *tree;

  SVN_ERR (get_root (&root, c, pool));
  if (c->is_revision)
    base_rev_id = c->rev_id - 1;
  else
    base_rev_id = svn_fs_txn_base_revision (c->txn);

  if (! SVN_IS_VALID_REVNUM (base_rev_id))
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, NULL,
       "Transaction '%s' is not based on a revision.  How odd.",
       c->txn_name);
  
  SVN_ERR (generate_delta_tree (&tree, c->repos, root, base_rev_id, 
                                TRUE, pool)); 
  if (tree)
    SVN_ERR (print_changed_tree (tree, "", pool));

  return SVN_NO_ERROR;
}


/* Print some diff-y stuff in a TBD way. :-) */
static svn_error_t *
do_diff (svnlook_ctxt_t *c, apr_pool_t *pool)
{
  svn_fs_root_t *root, *base_root;
  svn_revnum_t base_rev_id;
  svn_repos_node_t *tree;

  SVN_ERR (get_root (&root, c, pool));
  if (c->is_revision)
    base_rev_id = c->rev_id - 1;
  else
    base_rev_id = svn_fs_txn_base_revision (c->txn);

  if (! SVN_IS_VALID_REVNUM (base_rev_id))
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, NULL,
       "Transaction '%s' is not based on a revision.  How odd.",
       c->txn_name);
  
  SVN_ERR (generate_delta_tree (&tree, c->repos, root, base_rev_id, 
                                TRUE, pool)); 
  if (tree)
    {
      svn_node_kind_t kind;
      SVN_ERR (svn_fs_revision_root (&base_root, c->fs, base_rev_id, pool));
      SVN_ERR (print_diff_tree (root, base_root, tree, "", "",
                                c->no_diff_on_delete, pool));
      SVN_ERR (svn_io_check_path (SVNLOOK_TMPDIR, &kind, pool));
      if (kind == svn_node_dir)
        SVN_ERR (svn_io_remove_dir (SVNLOOK_TMPDIR, pool));
    }
  return SVN_NO_ERROR;
}


/* Print the diff between revision 0 and our root. */
static svn_error_t *
do_tree (svnlook_ctxt_t *c, svn_boolean_t show_ids, apr_pool_t *pool)
{
  svn_fs_root_t *root;
  const svn_fs_id_t *id;

  SVN_ERR (get_root (&root, c, pool));
  SVN_ERR (svn_fs_node_id (&id, root, "", pool));
  SVN_ERR (print_tree (root, "", id, 0, show_ids, pool));
  return SVN_NO_ERROR;
}


/*** Subcommands. ***/
static svn_error_t *
get_ctxt_baton (svnlook_ctxt_t **baton_p,
                struct svnlook_opt_state *opt_state,
                apr_pool_t *pool)
{
  svnlook_ctxt_t *baton = apr_pcalloc (pool, sizeof (*baton));

  SVN_ERR (svn_repos_open (&(baton->repos), opt_state->repos_path, pool));
  baton->fs = svn_repos_fs (baton->repos);
  baton->show_ids = opt_state->show_ids;
  baton->no_diff_on_delete = opt_state->no_diff_on_delete;
  baton->is_revision = opt_state->txn ? FALSE : TRUE;
  baton->rev_id = opt_state->rev;
  baton->txn_name = apr_pstrdup (pool, opt_state->txn);
  if (baton->txn_name)
    SVN_ERR (svn_fs_open_txn (&(baton->txn), baton->fs, 
                              baton->txn_name, pool));
  else if (baton->rev_id == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&(baton->rev_id), baton->fs, pool));

  *baton_p = baton;
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_author (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_author (c, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_changed (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_changed (c, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_date (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_date (c, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_diff (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_diff (c, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_dirschanged (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_dirs_changed (c, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_help (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  const char *header =
    "general usage: svnlook SUBCOMMAND REPOS_PATH [ARGS & OPTIONS ...]\n"
    "Note: any subcommand which takes the '--revision' and '--transaction'\n"
    "      options will, if invoked without one of those options, act on\n"
    "      the repository's youngest revision.\n"
    "Type \"svnlook help <subcommand>\" for help on a specific subcommand.\n"
    "\n"
    "Available subcommands:\n";

  SVN_ERR (svn_opt_print_help (os, "svnlook", FALSE, FALSE, NULL,
                               header, cmd_table, options_table, NULL,
                               pool));
  
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_info (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_author (c, pool));
  SVN_ERR (do_date (c, pool));
  SVN_ERR (do_log (c, TRUE, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_log (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_log (c, FALSE, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_tree (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  SVN_ERR (do_tree (c, opt_state->show_ids, pool));
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_youngest (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnlook_opt_state *opt_state = baton;
  svnlook_ctxt_t *c;

  SVN_ERR (get_ctxt_baton (&c, opt_state, pool));
  printf ("%" SVN_REVNUM_T_FMT "\n", c->rev_id);
  return SVN_NO_ERROR;
}


/*** Main. ***/

#define INT_ERR(expr)                                       \
  do {                                                      \
    svn_error_t *svnlook_err__temp = (expr);                \
    if (svnlook_err__temp) {                                \
      svn_handle_error (svnlook_err__temp, stderr, FALSE);  \
      return EXIT_FAILURE; }                                \
  } while (0)


int
main (int argc, const char * const *argv)
{
  svn_error_t *err;
  apr_status_t apr_err;
  int err2;
  apr_pool_t *pool;

  const svn_opt_subcommand_desc_t *subcommand = NULL;
  struct svnlook_opt_state opt_state;
  apr_getopt_t *os;  
  int opt_id;
  int received_opts[SVN_OPT_MAX_OPTIONS];
  int i, num_opts = 0;

  setlocale (LC_CTYPE, "");

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

  if (argc <= 1)
    {
      subcommand_help (NULL, NULL, pool);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* Initialize opt_state. */
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.rev = SVN_INVALID_REVNUM;

  /* Parse options. */
  apr_getopt_init (&os, pool, argc, argv);
  os->interleave = 1;
  while (1)
    {
      const char *opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long (os, options_table, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF (apr_err))
        break;
      else if (apr_err)
        {
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      /* Stash the option code in an array before parsing it. */
      received_opts[num_opts] = opt_id;
      num_opts++;

      switch (opt_id) 
        {
        case 'r':
          opt_state.rev = atoi (opt_arg);
          if (! SVN_IS_VALID_REVNUM (opt_state.rev))
            INT_ERR (svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                                       "Invalid revision number supplied."));
          break;

        case 't':
          opt_state.txn = opt_arg;
          break;

        case 'h':
        case '?':
          opt_state.help = TRUE;
          break;

        case svnlook__show_ids:
          opt_state.show_ids = TRUE;
          break;

        case svnlook__no_diff_on_delete:
          opt_state.no_diff_on_delete = TRUE;
          break;

        default:
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;

        }
    }

  /* The --transaction and --revision options may not co-exist. */
  if ((opt_state.rev != SVN_INVALID_REVNUM) && opt_state.txn)
    INT_ERR (svn_error_create 
             (SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, 0, NULL,
              "The '--transaction' (-t) and '--revision' (-r) arguments "
              "may no co-exist."));

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is subcommand_help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand (cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "subcommand argument required\n");
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand (cmd_table, first_arg);
          if (subcommand == NULL)
            {
              fprintf (stderr, "unknown command: %s\n", first_arg);
              subcommand_help (NULL, NULL, pool);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
    }

  /* If there's a second argument, it's probably the repository.
     Every subcommand except `help' requires one, so we parse it out
     here and store it in opt_state. */
  if (subcommand->cmd_func != subcommand_help)
    {
      const char *repos_path = NULL;

      if (os->ind < os->argc)
        {
          INT_ERR (svn_utf_cstring_to_utf8 (&repos_path, os->argv[os->ind++],
                                            NULL, pool));
          repos_path = svn_path_canonicalize (repos_path, pool);
        }

      if (repos_path == NULL)
        {
          fprintf (stderr, "repository argument required\n");
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      /* Copy repos path into the OPT_STATE structure. */
      opt_state.repos_path = repos_path;      
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
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
            svn_opt_get_option_from_code (opt_id, options_table);
          svn_opt_format_option (&optstr, badopt, FALSE, pool);
          fprintf (stderr,
                   "\nError: subcommand '%s' doesn't accept option '%s'\n\n",
                   subcommand->name, optstr);
          svn_opt_subcommand_help (subcommand->name,
                                   cmd_table,
                                   options_table,
                                   pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
    }

  /* Run the subcommand. */
  err = (*subcommand->cmd_func) (os, &opt_state, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          svn_handle_error (err, stderr, 0);
          svn_opt_subcommand_help (subcommand->name, cmd_table,
                                   options_table, pool);
        }
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
