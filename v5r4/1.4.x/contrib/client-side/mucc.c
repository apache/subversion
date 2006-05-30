/*  Multiple URL Command Client

    Combine a list of mv, cp and rm commands on URLs into a single commit.

    Copyright 2005 Philip Martin <philip@codematters.co.uk>

    Licenced under the same terms as Subversion.

    How it works: the command line arguments are parsed into an array of
    action structures.  The action structures are interpreted to build a
    tree of operation structures.  The tree of operation structures is
    used to drive an RA commit editor to produce a single commit.  */

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_ra.h"
#include <apr_lib.h>
#include <stdio.h>
#include <string.h>

static void handle_error(svn_error_t *err, apr_pool_t *pool)
{
  if (err)
    svn_handle_error2(err, stderr, FALSE, "mucc: ");
  svn_error_clear(err);
  if (pool)
    svn_pool_destroy(pool);
  exit(EXIT_FAILURE);
}

static apr_pool_t *
init(const char *application)
{
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  svn_error_t *err;
  const svn_version_checklist_t checklist[] = {
    {"svn_client", svn_client_version},
    {"svn_subr", svn_subr_version},
    {"svn_ra", svn_ra_version},
    {NULL, NULL}
  };

  SVN_VERSION_DEFINE(my_version);

  if (svn_cmdline_init(application, stderr) || apr_allocator_create(&allocator))
    exit(EXIT_FAILURE);

  err = svn_ver_check_list(&my_version, checklist);
  if (err)
    handle_error(err, NULL);

  apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);
  pool = svn_pool_create_ex(NULL, allocator);
  apr_allocator_owner_set(allocator, pool);

  return pool;
}

static svn_error_t *
prompt_for_creds(const char **username,
                 const char **password,
                 const char *realm,
                 apr_pool_t *pool)
{
  char buffer[100];
  svn_boolean_t prompt_with_username;

  if (realm)
    SVN_ERR(svn_cmdline_printf(pool, "Authentication realm: %s\n", realm));

  if (! *username)
    {
      SVN_ERR(svn_cmdline_printf(pool, "Username: "));
      if (! fgets(buffer, sizeof(buffer), stdin))
        return svn_error_createf(0, NULL, "failed to get username");
      if (strlen(buffer) > 0 && buffer[strlen(buffer)-1] == '\n')
        buffer[strlen(buffer)-1] = '\0';
      *username = buffer;
      prompt_with_username = FALSE;
    }
  else
    prompt_with_username = TRUE;
  *username = apr_pstrdup(pool, *username);

  if (password)
    {
      apr_size_t sz = sizeof(buffer);
      const char *prompt = (prompt_with_username
                            ? apr_psprintf(pool, "Password for %s: ", *username)
                            : "Password: ");
      apr_status_t status = apr_password_get(prompt, buffer, &sz);
      if (status)
        return svn_error_wrap_apr(status, "failed to get password");
      *password = apr_pstrdup(pool, buffer);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
simple_prompt(svn_auth_cred_simple_t **cred,
              void *baton,
              const char *realm,
              const char *username,
              svn_boolean_t may_save,
              apr_pool_t *pool)
{
  const char *password;
  SVN_ERR(prompt_for_creds(&username, &password, realm, pool));
  *cred = apr_palloc(pool, sizeof(**cred));
  (*cred)->username = username;
  (*cred)->password = password;
  return SVN_NO_ERROR;
}

static svn_error_t *
username_prompt(svn_auth_cred_username_t **cred,
                void *baton,
                const char *realm,
                svn_boolean_t may_save,
                apr_pool_t *pool)
{
  const char *username = NULL;
  SVN_ERR(prompt_for_creds(&username, NULL, realm, pool));
  *cred = apr_palloc(pool, sizeof(**cred));
  (*cred)->username = username;
  return SVN_NO_ERROR;
}

static svn_ra_callbacks_t *
ra_callbacks(apr_pool_t *pool)
{
  apr_array_header_t *providers;
  svn_ra_callbacks_t *callbacks;
  svn_auth_provider_object_t *provider;

  providers = apr_array_make(pool, 2, sizeof(svn_auth_provider_object_t *));
  svn_client_get_simple_prompt_provider(&provider, simple_prompt, NULL, 2,
                                        pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
  svn_client_get_username_prompt_provider(&provider, username_prompt, NULL, 2,
                                          pool);
  APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

  callbacks = apr_palloc(pool, sizeof(*callbacks));
  svn_auth_open(&callbacks->auth_baton, providers, pool);
  callbacks->open_tmp_file = NULL;
  callbacks->get_wc_prop = NULL;
  callbacks->set_wc_prop = NULL;
  callbacks->push_wc_prop = NULL;
  callbacks->invalidate_wc_props = NULL;

  return callbacks;
}

static svn_error_t *
commit_callback(svn_revnum_t revision,
                const char *date,
                const char *author,
                void *baton)
{
  apr_pool_t *pool = baton;

  SVN_ERR(svn_cmdline_printf(pool, "r%ld committed by %s at %s\n",
                             revision, author ? author : "(no author)", date));
  return SVN_NO_ERROR;
}

struct operation {
  enum {
    OP_OPEN,
    OP_DELETE,
    OP_ADD,
    OP_REPLACE
  } operation;
  svn_node_kind_t kind;  /* to copy, valid for add and replace */
  svn_revnum_t rev;      /* to copy, valid for add and replace */
  const char *url;       /* to copy, valid for add and replace */
  apr_hash_t *children;  /* key: const char *path, value: struct operation * */
  void *baton;           /* as returned by the commit editor */
};

static svn_error_t *
drive(struct operation *operation,
      svn_revnum_t head,
      const svn_delta_editor_t *editor,
      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(pool, operation->children);
       hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      struct operation *child;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);
      child = val;

      if (child->operation == OP_DELETE || child->operation == OP_REPLACE)
        {
          SVN_ERR(editor->delete_entry(key, head, operation->baton, subpool));
        }
      if (child->operation == OP_OPEN)
        {
          SVN_ERR(editor->open_directory(key, operation->baton, head, subpool,
                                         &child->baton));
        }
      if (child->operation == OP_ADD || child->operation == OP_REPLACE)
        {
          if (child->kind == svn_node_dir)
            {
              SVN_ERR(editor->add_directory(key, operation->baton,
                                            child->url, child->rev,
                                            subpool, &child->baton));
            }
          else
            {
              void *file_baton;
              SVN_ERR(editor->add_file(key, operation->baton,
                                       child->url, child->rev,
                                       subpool, &file_baton));
              SVN_ERR(editor->close_file(file_baton, NULL, subpool));
            }
        }
      if (child->operation == OP_OPEN
          || ((child->operation == OP_ADD || child->operation == OP_REPLACE)
              && child->kind == svn_node_dir))
        {
          SVN_ERR(drive(child, head, editor, subpool));
          SVN_ERR(editor->close_directory(child->baton, subpool));
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

static struct operation *
get_operation(const char *path,
              struct operation *operation,
              apr_pool_t *pool)
{
  struct operation *child = apr_hash_get(operation->children, path,
                                         APR_HASH_KEY_STRING);
  if (! child)
    {
      child = apr_palloc(pool, sizeof(*child));
      child->children = apr_hash_make(pool);
      child->operation = OP_OPEN;
      apr_hash_set(operation->children, path, APR_HASH_KEY_STRING, child);
    }
  return child;
}

static const char *
subtract_anchor(const char *anchor, const char *url, apr_pool_t *pool)
{
  if (! strcmp(url, anchor))
    return "";
  else
    return svn_path_uri_decode(svn_path_is_child(anchor, url, pool), pool);
}

/* Add PATH to the operations tree rooted at OPERATION, creating any
   intermediate nodes that are required.  If URL is null then PATH will be
   deleted, otherwise URL@REV is the source to be copied to create PATH.
   Node type information is obtained for any copy source (to determine
   whether to create a file or directory) and for any deleted path (to
   ensure it exists since svn_delta_editor_t->delete_entry doesn't return
   an error on non-existent nodes). */
static svn_error_t *
build(const char *path,
      const char *url,
      svn_revnum_t rev,
      svn_revnum_t head,
      const char *anchor,
      svn_ra_session_t *session,
      struct operation *operation,
      apr_pool_t *pool)
{
  apr_array_header_t *path_bits = svn_path_decompose(path, pool);
  const char *path_so_far = "";
  const char *copy_src = NULL;
  svn_revnum_t copy_rev = SVN_INVALID_REVNUM;
  int i;

  for (i = 0; i < path_bits->nelts; ++i)
    {
      const char *path_bit = APR_ARRAY_IDX(path_bits, i, const char *);
      path_so_far = svn_path_join(path_so_far, path_bit, pool);
      operation = get_operation(path_so_far, operation, pool);
      if (! url)
        {
          /* Delete can operate on a copy, track it back to the source */
          if (operation->operation == OP_REPLACE
              || operation->operation == OP_ADD)
            {
              copy_src = subtract_anchor(anchor, operation->url, pool);
              copy_rev = operation->rev;
            }
          else if (copy_src)
            copy_src = svn_path_join(copy_src, path_bit, pool);
        }
    }

  if (operation->operation != OP_OPEN && operation->operation != OP_DELETE)
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             "unsupported multiple operations on '%s'", path);

  if (! url)
    {
      operation->operation = OP_DELETE;
      SVN_ERR(svn_ra_check_path(session,
                                copy_src ? copy_src : path,
                                copy_src ? copy_rev : head,
                                &operation->kind, pool));
      if (operation->kind == svn_node_none)
        {
          if (copy_src && strcmp(path, copy_src))
            return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                     "'%s' (from '%s:%ld') not found",
                                     path, copy_src, copy_rev);
          else
            return svn_error_createf(SVN_ERR_BAD_URL, NULL, "'%s' not found",
                                     path);
        }
    }
  else
    {
      operation->operation
        = operation->operation == OP_DELETE ? OP_REPLACE : OP_ADD;
      SVN_ERR(svn_ra_check_path(session, subtract_anchor(anchor, url, pool),
                                rev, &operation->kind, pool));
      if (operation->kind == svn_node_none)
        return svn_error_createf(SVN_ERR_BAD_URL, NULL, "'%s' not found", url);
      operation->url = url;
      operation->rev = rev;
    }

  return SVN_NO_ERROR;
}

struct action {
  enum {
    ACTION_MV,
    ACTION_CP,
    ACTION_RM
  } action;
  svn_revnum_t rev;     /* of url[0] for cp */
  const char *url[2];
};

static svn_error_t *
execute(const apr_array_header_t *actions,
        const char *anchor,
        const char *message,
        apr_pool_t *pool)
{
  svn_ra_session_t *session;
  svn_revnum_t head;
  const svn_delta_editor_t *editor;
  void *editor_baton;
  struct operation root;
  svn_error_t *err;
  int i;

  SVN_ERR(svn_ra_open(&session, anchor, ra_callbacks(pool), NULL, NULL, pool));

  SVN_ERR(svn_ra_get_latest_revnum(session, &head, pool));

  root.children = apr_hash_make(pool);
  root.operation = OP_OPEN;
  for (i = 0; i < actions->nelts; ++i)
    {
      struct action *action = APR_ARRAY_IDX(actions, i, struct action *);
      switch (action->action)
        {
          const char *path1, *path2;
        case ACTION_MV:
          path1 = subtract_anchor(anchor, action->url[0], pool);
          path2 = subtract_anchor(anchor, action->url[1], pool);
          SVN_ERR(build(path2, action->url[0], head,
                        head, anchor, session, &root, pool));
          SVN_ERR(build(path1, NULL, SVN_INVALID_REVNUM,
                        head, anchor, session, &root, pool));
          break;
        case ACTION_CP:
          path1 = subtract_anchor(anchor, action->url[0], pool);
          path2 = subtract_anchor(anchor, action->url[1], pool);
          if (action->rev == SVN_INVALID_REVNUM)
            action->rev = head;
          SVN_ERR(build(path2, action->url[0], action->rev,
                        head, anchor, session, &root, pool));
          break;
        case ACTION_RM:
          path1 = subtract_anchor(anchor, action->url[0], pool);
          SVN_ERR(build(path1, NULL, SVN_INVALID_REVNUM,
                        head, anchor, session, &root, pool));
          break;
        }
    }

  SVN_ERR(svn_ra_get_commit_editor(session, &editor, &editor_baton, message,
                                   commit_callback, pool, NULL, FALSE, pool));

  SVN_ERR(editor->open_root(editor_baton, head, pool, &root.baton));
  err = drive(&root, head, editor, pool);
  if (!err)
    err = editor->close_edit(editor_baton, pool);
  if (err)
    svn_error_clear(editor->abort_edit(editor_baton, pool));

  return err;
}

static void
usage(apr_pool_t *pool, int exit_val)
{
  FILE *stream = exit_val == EXIT_SUCCESS ? stdout : stderr;
  const char msg[]
    =
    "usage: mucc [OPTION]... [ mv URL1 URL2 | cp REV URL1 URL2 | rm URL ]...\n"
    "options:\n"
    "  -m, --message ARG   use ARG as a log message\n"
    "  -F, --file ARG      read log message from file ARG\n"
    "  -h, --help          display this text\n";
  svn_error_clear(svn_cmdline_fputs(msg, stream, pool));
  apr_pool_destroy(pool);
  exit(exit_val);
}

static void
insufficient(apr_pool_t *pool)
{
  handle_error(svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                "insufficient arguments"),
               pool);
}

int
main(int argc, const char **argv)
{
  apr_pool_t *pool = init("mucc");
  apr_array_header_t *actions = apr_array_make(pool, 1, sizeof(struct action*));
  const char *anchor = NULL;
  svn_error_t *err;
  apr_getopt_t *getopt;
  const apr_getopt_option_t options[] = {
    {"message", 'm', 1, ""},
    {"file", 'F', 1, ""},
    {"help", 'h', 0, ""},
    {NULL, 0, 0, NULL}
  };
  const char *message = "committed using mucc";

  apr_getopt_init(&getopt, pool, argc, argv);
  getopt->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      apr_status_t status = apr_getopt_long(getopt, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        handle_error(svn_error_wrap_apr(status, "getopt failure"), pool);
      switch(opt)
        {
        case 'm':
          err = svn_utf_cstring_to_utf8(&message, arg, pool);
          if (err)
            handle_error(err, pool);
          break;
        case 'F':
          {
            const char *arg_utf8;
            svn_stringbuf_t *contents;
            err = svn_utf_cstring_to_utf8(&arg_utf8, arg, pool);
            if (! err)
              err = svn_stringbuf_from_file(&contents, arg, pool);
            if (! err)
              err = svn_utf_cstring_to_utf8(&message, contents->data, pool);
            if (err)
              handle_error(err, pool);
          }
          break;
        case 'h':
          usage(pool, EXIT_SUCCESS);
        }
    }
  while (getopt->ind < getopt->argc)
    {
      int j;
      struct action *action = apr_palloc(pool, sizeof(*action));

      if (! strcmp(getopt->argv[getopt->ind], "mv"))
        action->action = ACTION_MV;
      else if (! strcmp(getopt->argv[getopt->ind], "cp"))
        action->action = ACTION_CP;
      else if (! strcmp(getopt->argv[getopt->ind], "rm"))
        action->action = ACTION_RM;
      else
        handle_error(svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                       "'%s' is not an action\n",
                                       getopt->argv[getopt->ind]),
                     pool);
      if (++getopt->ind == getopt->argc)
        insufficient(pool);

      if (action->action == ACTION_CP)
        {
          if (! strcmp(getopt->argv[getopt->ind], "head"))
            action->rev = SVN_INVALID_REVNUM;
          else
            {
              char *end;
              action->rev = strtol(getopt->argv[getopt->ind], &end, 0);
              if (*end)
                handle_error(svn_error_createf(SVN_ERR_INCORRECT_PARAMS,
                                               NULL, "'%s' is not a revision\n",
                                               getopt->argv[getopt->ind]),
                             pool);
            }
          if (++getopt->ind == getopt->argc)
            insufficient(pool);
        }
      else
        action->rev = SVN_INVALID_REVNUM;

      for (j = 0; j < (action->action == ACTION_RM ? 1 : 2); ++j)
        {
          const char *url = getopt->argv[getopt->ind];
          err = svn_utf_cstring_to_utf8(&url, url, pool);
          if (err)
            handle_error(err, pool);
          if (! svn_path_is_url(url))
            handle_error(svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                           "'%s' is not an URL\n",
                                           getopt->argv[getopt->ind]),
                         pool);
          url = svn_path_uri_from_iri(url, pool);
          url = svn_path_uri_autoescape(url, pool);
          url = svn_path_canonicalize(url, pool);
          action->url[j] = url;

          /* The cp source could be the anchor, but the other URLs should be
             children of the anchor. */
          if (! (action->action == ACTION_CP && j == 0))
            url = svn_path_dirname(url, pool);
          if (! anchor)
            anchor = url;
          else
            anchor = svn_path_get_longest_ancestor(anchor, url, pool);

          if (++getopt->ind == getopt->argc
              && ! (j == 1 || action->action == ACTION_RM))
            insufficient(pool);
        }
      APR_ARRAY_PUSH(actions, struct action *) = action;
    }

  if (! actions->nelts)
    usage(pool, EXIT_FAILURE);

  err = execute(actions, anchor, message, pool);
  if (err)
    handle_error(err, pool);

  svn_pool_destroy(pool);
  return EXIT_SUCCESS;
}
