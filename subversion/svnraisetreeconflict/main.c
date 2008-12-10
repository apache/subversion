/*
 * ====================================================================
 * Copyright (c) 2003-2008 CollabNet.  All rights reserved.
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_opt.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

#define OPT_VERSION SVN_OPT_FIRST_LONGOPT_ID

/** A statement macro, similar to @c SVN_INT_ERR, but issues a
 * message saying "svnraisetreeconflict:" instead of "svn:".
 *
 * Evaluate @a expr. If it yields an error, handle that error and
 * return @c EXIT_FAILURE.
 */
#define SVNRAISETC_INT_ERR(expr)                                 \
  do {                                                           \
    svn_error_t *svn_err__temp = (expr);                         \
    if (svn_err__temp) {                                         \
      svn_handle_error2(svn_err__temp, stderr, FALSE,            \
                        "svnraisetreeconflict: ");               \
      svn_error_clear(svn_err__temp);                            \
      return EXIT_FAILURE; }                                     \
  } while (0)

static svn_error_t *
version(apr_pool_t *pool)
{
  return svn_opt_print_help3(NULL, "svnraisetreeconflict", TRUE, FALSE, NULL,
                             NULL, NULL, NULL, NULL, NULL, pool);
}

static void
usage(apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fprintf
                  (stderr, pool,
                   _("Type 'svnraisetreeconflict --help' for usage.\n")));
  exit(1);
}

/***************************************************************************
 * "enum mapping" functions copied from subversion/libsvn_wc/tree_conflicts.c
 **************************************************************************/

/* A mapping between a string STR and an enumeration value VAL. */
typedef struct enum_mapping_t
{
  const char *str;
  int val;
} enum_mapping_t;

/* A map for svn_node_kind_t values. */
static const enum_mapping_t node_kind_map[] =
{
  { "none",     svn_node_none },
  { "file",     svn_node_file },
  { "dir",      svn_node_dir },
  { "unknown",  svn_node_unknown },
  { NULL,              0 }
};

/* A map for svn_wc_operation_t values. */
static const enum_mapping_t operation_map[] =
{
  { "update",   svn_wc_operation_update },
  { "switch",   svn_wc_operation_switch },
  { "merge",    svn_wc_operation_merge },
  { NULL,                     0 }
};

/* A map for svn_wc_conflict_action_t values. */
static const enum_mapping_t action_map[] =
{
  { "edit",     svn_wc_conflict_action_edit },
  { "delete",   svn_wc_conflict_action_delete },
  { "add",      svn_wc_conflict_action_add },
  { NULL,                            0 }
};

/* A map for svn_wc_conflict_reason_t values. */
static const enum_mapping_t reason_map[] =
{
  { "edited",     svn_wc_conflict_reason_edited },
  { "deleted",    svn_wc_conflict_reason_deleted },
  { "missing",    svn_wc_conflict_reason_missing },
  { "obstructed", svn_wc_conflict_reason_obstructed },
  { "added",      svn_wc_conflict_reason_added },
  { NULL,                               0 }
};

/* Parse the enumeration field pointed to by *START into *RESULT as a plain
 * 'int', using MAP to convert from strings to enumeration values.
 * In MAP, a null STR field marks the end of the map.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_enum_field(int *result,
                const enum_mapping_t *map,
                const char *str,
                apr_pool_t *pool)
{
  int i;

  /* Find STR in MAP; error if not found. */
  for (i = 0; ; i++)
    {
      if (map[i].str == NULL)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 "Unrecognised parameter value: '%s'", str);
      if (strcmp(str, map[i].str) == 0)
        break;
    }

  *result = map[i].val;
  return SVN_NO_ERROR;
}

static const char*
get_enum_str(const enum_mapping_t *map,
             int enum_val)
{
  int i;
  for (i = 0; map[i].str != NULL; i++)
    {
      if (map[i].val == enum_val)
        return map[i].str;
    }
  return NULL;
}

static void
print_enum_map(const enum_mapping_t *map,
               apr_pool_t *pool)
{
  int i;
  for (i = 0; map[i].str != NULL; i++)
    svn_error_clear(svn_cmdline_fprintf(stdout, pool,
        " %s", map[i].str));
}

static svn_error_t *
raise_tree_conflict(int argc, const char **argv, apr_pool_t *pool)
{
  int i = 0;
  svn_wc_conflict_version_t *left, *right;
  svn_wc_conflict_description_t *c;
  svn_wc_adm_access_t *adm_access;

  /* Conflict description parameters */
  const char *wc_path, *repos_url1, *repos_url2, *path_in_repos1, *path_in_repos2;
  int operation, action, reason;
  int peg_rev1, peg_rev2;
  int kind, kind1, kind2;

  if (argc != 13)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            "Wrong number of arguments");

  /* Read the parameters */
  wc_path = svn_path_internal_style(argv[i++], pool);
  SVN_ERR(read_enum_field(&kind, node_kind_map, argv[i++], pool));
  SVN_ERR(read_enum_field(&operation, operation_map, argv[i++], pool));
  SVN_ERR(read_enum_field(&action, action_map, argv[i++], pool));
  SVN_ERR(read_enum_field(&reason, reason_map, argv[i++], pool));
  repos_url1 = argv[i++];
  path_in_repos1 = argv[i++];
  peg_rev1 = atol(argv[i++]);
  SVN_ERR(read_enum_field(&kind1, node_kind_map, argv[i++], pool));
  repos_url2 = argv[i++];
  path_in_repos2 = argv[i++];
  peg_rev2 = atol(argv[i++]);
  SVN_ERR(read_enum_field(&kind2, node_kind_map, argv[i++], pool));


  /* Allocate and fill in the description data structures */
  left = svn_wc_conflict_version_create(repos_url1, path_in_repos1, peg_rev1,
                                        kind1, pool);
  right = svn_wc_conflict_version_create(repos_url2, path_in_repos2, peg_rev2,
                                         kind2, pool);
  c = svn_wc_conflict_description_create_tree(wc_path, NULL, kind,
                                              operation, left, right, pool);
  c->action = (svn_wc_conflict_action_t)action;
  c->reason = (svn_wc_conflict_reason_t)reason;

  /* Raise the conflict */
  {
    const char *parent_path;

    parent_path = svn_path_dirname(wc_path, pool);
    SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, parent_path, TRUE, 0,
                             NULL, NULL, pool));
  }

  /* Raise the conflict */
  SVN_ERR(svn_wc__add_tree_conflict(c, adm_access, pool));

  return SVN_NO_ERROR;
}


static void
help(const apr_getopt_option_t *options, apr_pool_t *pool)
{
  svn_error_clear
    (svn_cmdline_fprintf
     (stdout, pool,
      _("usage: svnraisetreeconflict [OPTIONS] WC_PATH NODE_KIND OPERATION ACTION REASON REPOS_URL1 PATH_IN_REPOS1 PEG_REV1 NODE_KIND1 REPOS_URL2 PATH_IN_REPOS2 PEG_REV2 NODE_KIND2\n\n"
        "  Mark the working-copy node WC_PATH as being the victim of a tree conflict.\n"
        "\n"
        "  WC_PATH's parent directory must be a working copy, otherwise a\n"
        "  tree conflict cannot be raised.\n"
        "\n"
        "Valid options:\n")));
  while (options->description)
    {
      const char *optstr;
      svn_opt_format_option(&optstr, options, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
      ++options;
    }
  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
      _("\n"
      "Valid enum argument values:\n"
      "  NODE_KIND, NODE_KIND1, NODE_KIND2:\n"
      "   ")));
  print_enum_map(node_kind_map, pool);
  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
      _("\n"
      "  OPERATION:\n"
      "   ")));
  print_enum_map(operation_map, pool);
  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
      _("\n"
      "  ACTION (what svn tried to do):\n"
      "   ")));
  print_enum_map(action_map, pool);
  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
      _("\n"
      "  REASON (what local change made svn fail):\n"
      "   ")));
  print_enum_map(reason_map, pool);
  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
      _("\n"
      "  REPOS_URL1, REPOS_URL2:\n"
      "    The URL of the repository itself, e.g.: file://usr/repos\n"
      "  PATH_IN_REPOS1, PATH_IN_REPOS2:\n"
      "    The complete path of the node in the repository, e.g.: sub/dir/foo\n"
      "  PEG_REV1, PEG_REV2:\n"
      "    The revision number at which the given path is relevant.\n"
      "\n"
      "Example:\n"
      "  svnraisetreeconflict ./foo %s %s %s %s file://usr/repos sub/dir/foo 1 %s file://usr/repos sub/dir/foo 3 %s\n\n"),
      get_enum_str(node_kind_map, svn_node_file),
      get_enum_str(operation_map, svn_wc_operation_update),
      get_enum_str(action_map, svn_wc_conflict_action_delete),
      get_enum_str(reason_map, svn_wc_conflict_reason_deleted),
      get_enum_str(node_kind_map, svn_node_file),
      get_enum_str(node_kind_map, svn_node_none)
      ));
  exit(0);
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_wc",     svn_wc_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE(my_version);
  return svn_ver_check_list(&my_version, checklist);
}

/*
 * Why is this not an svn subcommand?  I have this vague idea that it could
 * be run as part of the build process, with the output embedded in the svn
 * program.  Obviously we don't want to have to run svn when building svn.
 */
int
main(int argc, const char *argv[])
{
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  svn_error_t *err;
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"help", 'h', 0, N_("display this help")},
      {"version", OPT_VERSION, 0,
       N_("show program version information")},
      {0,             0,  0,  0}
    };
  apr_array_header_t *remaining_argv;

  /* Initialize the app. */
  if (svn_cmdline_init("svnraisetreeconflict", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  if (apr_allocator_create(&allocator))
    return EXIT_FAILURE;

  apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  pool = svn_pool_create_ex(NULL, allocator);
  apr_allocator_owner_set(allocator, pool);

  /* Check library versions */
  err = check_lib_versions();
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnraisetreeconflict: ");

#if defined(WIN32) || defined(__CYGWIN__)
  /* Set the working copy administrative directory name. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      err = svn_wc_set_adm_dir("_svn", pool);
      if (err)
        return svn_cmdline_handle_exit_error(err, pool, "svnraisetreeconflict: ");
    }
#endif

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnraisetreeconflict: ");

  os->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      apr_status_t status = apr_getopt_long(os, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          usage(pool);
          return EXIT_FAILURE;
        }
      switch (opt)
        {
        case 'h':
          help(options, pool);
          break;
        case OPT_VERSION:
          SVNRAISETC_INT_ERR(version(pool));
          exit(0);
          break;
        default:
          usage(pool);
          return EXIT_FAILURE;
        }
    }

  /* Convert the remaining arguments to UTF-8. */
  remaining_argv = apr_array_make(pool, 0, sizeof(const char *));
  while (os->ind < argc)
    {
      const char *s;

      SVNRAISETC_INT_ERR(svn_utf_cstring_to_utf8(&s, os->argv[os->ind++],
                                                 pool));
      APR_ARRAY_PUSH(remaining_argv, const char *) = s;
    }

  if (remaining_argv->nelts < 1)
    {
      usage(pool);
      return EXIT_FAILURE;
    }

  /* Do the main task */
  SVNRAISETC_INT_ERR(raise_tree_conflict(remaining_argv->nelts,
                                         (const char **)remaining_argv->elts,
                                         pool));

  svn_pool_destroy(pool);

  /* Flush stdout to make sure that the user will see any printing errors. */
  SVNRAISETC_INT_ERR(svn_cmdline_fflush(stdout));

  return EXIT_SUCCESS;
}
