/*
 * svnbrowse.c:  TUI (terminal user interface) client for browsing and
 *               exploring remote Subversion targets.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <curses.h>

#include <apr.h>

#include "private/svn_utf_private.h"
#include "svn_cmdline.h"
#include "svn_opt.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_error.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"
#include "svn_time.h"
#include "svnbrowse.h"

/* Option codes and descriptions for the command line client.
 * The entire list must be terminated with an entry of nulls. */
static const apr_getopt_option_t svn_browse__options[] =
{
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1,
                    N_("specify a password ARG (caution: on many operating\n"
                       "                             "
                       "systems, other users will be able to see this)")},
  {"password-from-stdin",
                    opt_auth_password_from_stdin, 0,
                    N_("read password from stdin")},
  {"revision",      'r', 1,
                    N_("ARG\n"
                       "                             "
                       "A revision argument can be one of:\n"
                       "                             "
                       "   NUMBER       revision number\n"
                       "                             "
                       "   '{' DATE '}' revision at start of the date\n"
                       "                             "
                       "   'HEAD'       latest in repository\n"
                       "                             "
                       "   'BASE'       base rev of item's working copy\n"
                       "                             "
                       "   'COMMITTED'  last commit at or before BASE\n"
                       "                             "
                       "   'PREV'       revision just before COMMITTED")},
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"trust-server-cert", opt_trust_server_cert, 0,
                    N_("deprecated; same as\n"
                       "                             "
                       "--trust-server-cert-failures=unknown-ca")},
  {"trust-server-cert-failures", opt_trust_server_cert_failures, 1,
                    N_("with --non-interactive, accept SSL server\n"
                       "                             "
                       "certificates with failures; ARG is comma-separated\n"
                       "                             "
                       "list of 'unknown-ca' (Unknown Authority),\n"
                       "                             "
                       "'cn-mismatch' (Hostname mismatch), 'expired'\n"
                       "                             "
                       "(Expired certificate), 'not-yet-valid' (Not yet\n"
                       "                             "
                       "valid certificate) and 'other' (all other not\n"
                       "                             "
                       "separately classified certificate errors).")},
  {"config-dir",    opt_config_dir, 1,
                    N_("read user configuration files from directory ARG")},
  {"config-option", opt_config_option, 1,
                    N_("set user configuration option in the format:\n"
                       "                             "
                       "    FILE:SECTION:OPTION=[VALUE]\n"
                       "                             "
                       "For example:\n"
                       "                             "
                       "    servers:global:http-library=serf")},
  {"no-auth-cache", opt_no_auth_cache, 0,
                    N_("do not cache authentication tokens")},
  {"version",       opt_version, 0, N_("show program version information")},
  {"verbose",       'v', 0, N_("print extra information")},
  { NULL, 0, 0, NULL }
};

/* Control+ASCII character are represented as values 1-26 according to their
 * alphabetical order. */
#define CTRL(ch) ((ch) - 'a' + 1)

#define KEY_ESC 27

/* handpicked */
#define COLOR_PRIMARY      238
#define COLOR_SECONDARY    240
#define COLOR_BRANDING      96

enum {
  COLOR_PAIR_HEADER = 1,
  COLOR_PAIR_FOOTER,
  COLOR_PAIR_DIR,
  COLOR_PAIR_DIR_SELECTED,
  COLOR_PAIR_FILE,
  COLOR_PAIR_FILE_SELECTED,
  COLOR_COUNT,
};

typedef enum svn_browse__color_mode_e {
  /* use this color mode when we have no colors at all (other attributes like
   * A_BOLD or A_STANDOUT are still possible). */
  svn_browse__color_none,

  /* a limited color support of only 8 basic ones */
  svn_browse__color_limited,

  /* full, 255 color pallete */
  svn_browse__color_full,
} svn_browse__color_mode_e;

typedef struct svn_browse__style_t {
  attr_t file;
  attr_t file_selected;
  attr_t dir;
  attr_t dir_selected;
  attr_t header;
  attr_t footer;
} svn_browse__style_t;

/* creates a new instance of style, initializing all required colors along the
 * way. it will as well change global state of curses. */
static svn_browse__style_t *
style_init(svn_browse__color_mode_e color_mode, apr_pool_t *result_pool)
{
  svn_browse__style_t *result = apr_pcalloc(result_pool, sizeof(*result));

  if (color_mode > svn_browse__color_none)
    {
      init_pair(COLOR_PAIR_HEADER, COLOR_YELLOW, COLOR_BRANDING);
      init_pair(COLOR_PAIR_FOOTER, COLOR_YELLOW, COLOR_SECONDARY);
      init_pair(COLOR_PAIR_DIR, COLOR_CYAN, -1);
      init_pair(COLOR_PAIR_DIR_SELECTED, COLOR_CYAN, COLOR_PRIMARY);
      init_pair(COLOR_PAIR_FILE, -1, -1);
      init_pair(COLOR_PAIR_FILE_SELECTED, -1, COLOR_PRIMARY);
    }

  switch (color_mode)
    {
      case svn_browse__color_none:
        result->file = 0;
        result->file_selected = A_STANDOUT;
        result->dir = A_BOLD;
        result->dir_selected = A_STANDOUT | A_BOLD;
        result->header = A_DIM;
        result->footer = A_DIM;
        break;
      case svn_browse__color_limited:
        result->file = COLOR_PAIR(COLOR_PAIR_FILE);
        result->file_selected = A_STANDOUT;
        result->dir = COLOR_PAIR(COLOR_PAIR_DIR);
        result->dir_selected = A_STANDOUT;
        result->header = A_DIM;
        result->footer = A_DIM;
        break;
      case svn_browse__color_full:
        result->file = COLOR_PAIR(COLOR_PAIR_FILE);
        result->file_selected = COLOR_PAIR(COLOR_PAIR_FILE_SELECTED);
        result->dir = COLOR_PAIR(COLOR_PAIR_DIR);
        result->dir_selected = COLOR_PAIR(COLOR_PAIR_DIR_SELECTED);
        result->header = COLOR_PAIR(COLOR_PAIR_HEADER);
        result->footer = COLOR_PAIR(COLOR_PAIR_FOOTER);
        break;
      default:
        abort();
    }

  return result;
}

typedef struct svn_browse__view_t {
  svn_browse__model_t *model;
  svn_browse__style_t *style;
  WINDOW *screen;
  WINDOW *header;
  WINDOW *footer;
  WINDOW *list;
} svn_browse__view_t;

static apr_status_t
view_cleanup(void *ctx)
{
  svn_browse__view_t *view = ctx;
  delwin(view->header);
  delwin(view->list);
  return APR_SUCCESS;
}

static void
view_layout(svn_browse__view_t *view)
{
  int cols = getmaxx(view->screen);
  int rows = getmaxy(view->screen);
  int header_height = 1;
  int footer_height = 1;
  int list_height = rows - header_height - footer_height;

  delwin(view->header);
  delwin(view->footer);
  delwin(view->list);

  view->header = subwin(view->screen, header_height, cols, 0, 0);
  view->list = subwin(view->screen, list_height, cols, header_height, 0);
  view->footer = subwin(view->screen, footer_height, cols,
                        header_height + list_height, 0);
}

static svn_browse__view_t *
view_make(svn_browse__model_t *model, svn_browse__style_t *style, WINDOW *win,
          apr_pool_t *result_pool)
{
  svn_browse__view_t *view = apr_pcalloc(result_pool, sizeof(*view));
  view->model = model;
  view->style = style;
  view->screen = win;
  view_layout(view);
  apr_pool_cleanup_register(result_pool, view, view_cleanup,
                            apr_pool_cleanup_null);
  return view;
}

static svn_error_t *
view_on_event(svn_browse__view_t *view, int ch, apr_pool_t *scratch_pool)
{
  int scrollsize;
  view_layout(view);

  /* scrollable height is one row less than the whole view */
  scrollsize = getmaxy(view->list);

  /* ch is received from getch() which would read the next character/key with
   * the following additional rules:
   * 1. as we configured it to use keypad(), arrows and other special keys
   *    are handled as KEY_XXX.
   * 2. Control (CTRL) version are handled as literal 1-26 values of ch where
   *    1 is <C-A> and 26 is <C-Z>.
   * 3. The rest of keys remain as their equivalents on the current layout.
   * 4. If shift is held, they just become uppercased.
   */

  switch (ch)
    {
      case KEY_BACKSPACE:
      case '-':
      case 'u':
        SVN_ERR(svn_browse__model_go_up(view->model, scratch_pool));
        view->model->current->scroller_offset =
            view->model->current->selection - scrollsize / 2;
        break;
      case 'q':
      case KEY_ESC:
        return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);
      default:
        break;
    }

  if (view->model->current->type == svn_browse__state_dir)
    {
      switch (ch)
        {
          case KEY_UP:
          case 'k':
          case CTRL('p'):
            SVN_ERR(svn_browse__model_move_selection(view->model, -1));
            break;
          case KEY_DOWN:
          case 'j':
          case CTRL('n'):
            SVN_ERR(svn_browse__model_move_selection(view->model, 1));
            break;
          case '\n':
          case '\r':
            SVN_ERR(svn_browse__model_go_enter(view->model, scratch_pool));
            break;
          case CTRL('e'):
            view->model->current->scroller_offset += 1;
            break;
          case CTRL('y'):
            view->model->current->scroller_offset -= 1;
            break;
          case CTRL('d'):
            SVN_ERR(svn_browse__model_move_selection(view->model,
                                                     scrollsize / 2));
            break;
          case CTRL('f'):
          case KEY_NPAGE:
            SVN_ERR(svn_browse__model_move_selection(view->model,
                                                     scrollsize));
            break;
          case CTRL('u'):
            SVN_ERR(svn_browse__model_move_selection(view->model,
                                                     -scrollsize / 2));
            break;
          case CTRL('b'):
          case KEY_PPAGE:
            SVN_ERR(svn_browse__model_move_selection(view->model,
                                                     -scrollsize));
            break;
          case 'g':
          case KEY_HOME:
            view->model->current->selection = 0;
            break;
          case 'G':
          case KEY_END:
            view->model->current->selection =
                view->model->current->list->nelts - 1;
            break;
          case 'z':
            view->model->current->scroller_offset =
                view->model->current->selection - scrollsize / 2;
            break;
        }

      SVN_ERR(svn_browse__model_scroll_in_view(view->model, scrollsize));
    }

  return SVN_NO_ERROR;
}

static const char *
format_node_size(const svn_browse__item_t *item, apr_pool_t *pool)
{
  if (item->dirent->kind == svn_node_dir)
    return "(dir)";
  else
    return apr_psprintf(pool, "%ld KiB", item->dirent->size / 1024);
}

static const char *
format_node_name(const svn_browse__item_t *item, apr_pool_t *pool)
{
  if (item->dirent->kind == svn_node_dir)
    return apr_pstrcat(pool, item->name, "/", SVN_VA_NULL);
  else
    return item->name;
}

static int
get_item_style(const svn_browse__style_t *style, svn_node_kind_t kind,
               svn_boolean_t selected)
{
  switch (kind)
    {
      case svn_node_dir:
        return (selected) ? style->dir_selected : style->dir;
      case svn_node_file:
        return (selected) ? style->file_selected : style->file;
      default:
        abort();
    }
}

static void
view_draw_item(const svn_browse__style_t *style,
               const svn_browse__item_t *item, WINDOW *win, int y,
               svn_boolean_t selected, apr_pool_t *scratch_pool)
{
  move(y, 0);
  wattrset(win, get_item_style(style, item->dirent->kind, selected));

  /* 12 + 12 + (20 + 1) + 2 = 47 */

  waddch(win, ' ');
  waddstr(win, svn_utf__cstring_align_left(
                   format_node_name(item, scratch_pool), getmaxx(win) - 47,
                   scratch_pool));

  wattrset(win, get_item_style(style, svn_node_file, selected));
  waddstr(win, svn_utf__cstring_align_right_trim_left(
                   format_node_size(item, scratch_pool),
                   12, scratch_pool));
  waddstr(win, svn_utf__cstring_align_right_trim_left(
                   apr_psprintf(scratch_pool, "r%ld",
                                item->dirent->created_rev),
                   12, scratch_pool));
  waddch(win, ' ');
  waddstr(win, svn_utf__cstring_align_left(
                   item->dirent->last_author, 20, scratch_pool));
  waddch(win, ' ');
}

static char *
format_revision_header(svn_revnum_t revnum, svn_revnum_t fetched_revnum,
                       apr_pool_t *pool)
{
  if (! SVN_IS_VALID_REVNUM(revnum))
    return apr_psprintf(pool, "r%ld (HEAD)", fetched_revnum);
  else
    return apr_psprintf(pool, "r%ld", fetched_revnum);
}

static void
view_draw_header(svn_browse__view_t *view, WINDOW *win,
                 apr_pool_t *scratch_pool)
{
  const char *abspath = svn_path_url_add_component2(
      view->model->root, view->model->current->relpath, scratch_pool);

  const char *prefix = "  ";
  const char *suffix = format_revision_header(view->model->revision,
                                              view->model->current->revision,
                                              scratch_pool);
  suffix = apr_pstrcat(scratch_pool, suffix, "  ", SVN_VA_NULL);

  wmove(win, 0, 0);
  wattrset(win, view->style->header);
  waddstr(win, prefix);
  waddstr(win, svn_utf__cstring_align_left(
                   apr_psprintf(scratch_pool, "URL: %s", abspath),
                   getmaxx(win) - strlen(prefix) - strlen(suffix),
                   scratch_pool));
  waddstr(win, suffix);
}

static const char *
format_percentage_scroll(int scroll, int size, int height, apr_pool_t *pool)
{
  if (size <= height)
    return "All";
  else if (scroll <= 0)
    return "Top";
  else if (scroll + height >= size)
    return "Bot";
  else
    {
      /* Oops, if size and heigth perfectly line up, we would segfault to
       * division by zero. Nope... We would not. There is a check right
       * above. */

      int percentage = (scroll) * 100 / (size - height);
      return apr_psprintf(pool, "%d%%", percentage);
    }
}

static int
view_get_list_height(const svn_browse__state_t *state)
{
  if (state->type == svn_browse__state_dir)
    return state->list->nelts;
  else
    return 0;
}

static void
view_draw_footer(svn_browse__view_t *view, WINDOW *win,
                 apr_pool_t *scratch_pool)
{
  const char *brand = "Apache Subversion  ";
  const svn_browse__state_t *state = view->model->current;

  wmove(win, 0, 0);
  wattrset(win, view->style->footer);
  waddstr(win, "  ");
  waddstr(win, svn_utf__cstring_align_left(
                  apr_psprintf(scratch_pool, "Ready"),
                  getmaxx(win) - 4 - strlen(brand) - 16,
                  scratch_pool));
  waddstr(win, brand);

  waddstr(win, svn_utf__cstring_align_right_trim_left(
                  apr_psprintf(scratch_pool, "%d/%d",
                               state->selection + 1,
                               view_get_list_height(state)),
                  8, scratch_pool));
  waddstr(win, svn_utf__cstring_align_right_trim_left(
                  format_percentage_scroll(state->scroller_offset,
                                           view_get_list_height(state),
                                           getmaxy(view->list),
                                           scratch_pool),
                  8, scratch_pool));
  waddstr(win, "  ");
}

static void
dir_draw(svn_browse__view_t *view, apr_pool_t *pool)
{
  int i;

  for (i = 0; i < view->model->current->list->nelts; i++)
    {
      svn_browse__item_t *item = APR_ARRAY_IDX(view->model->current->list, i,
                                               svn_browse__item_t *);
      svn_boolean_t selected = (i == view->model->current->selection);
      int y = i - view->model->current->scroller_offset;

      if (0 <= y && y < LINES)
        view_draw_item(view->style, item, view->list, y + 1, selected, pool);
    }
}

static void
file_draw(svn_browse__view_t *view, apr_pool_t *pool)
{
  const svn_browse__state_t *state = view->model->current;

  mvwprintw(view->list, 0, 0, " File: %s",
            svn_relpath_basename(state->relpath, pool));

  mvwprintw(view->list, 1, 0, " Last Changed Rev: %ld",
            state->this_dirent->created_rev);
  mvwprintw(view->list, 2, 0, " Last Changed Author: %s",
            state->this_dirent->last_author);
  mvwprintw(view->list, 3, 0, " Last Changed Date: %s",
            svn_time_to_human_cstring(state->this_dirent->time, pool));
}

static void
view_draw(svn_browse__view_t *view, apr_pool_t *pool)
{
  view_draw_header(view, view->header, pool);
  view_draw_footer(view, view->footer, pool);

  switch (view->model->current->type)
    {
      case svn_browse__state_dir:
        dir_draw(view, pool);
        break;
      case svn_browse__state_file:
        file_draw(view, pool);
        break;
    }
}

static svn_error_t *
show_usage(apr_pool_t *scratch_pool)
{
  fprintf(stderr, "Type 'svnbrowse --help' for usage.\n");
  return SVN_NO_ERROR;
}

static svn_error_t *
show_help(apr_pool_t *scratch_pool,
          const svn_browse__opt_state_t *opt_state)
{
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(scratch_pool);
  const apr_getopt_option_t *opt;

  svn_stringbuf_appendcstr(buf, N_(
      "usage: svnbrowse <target> [options]\n"
      "Interactively browse Subversion repositories\n"
      "\n"
  ));

  svn_stringbuf_appendcstr(buf, N_("Valid options:\n"));
  for (opt = svn_browse__options; opt->description; opt++)
    {
      if (opt->name)
        {
          const char *opts;
          svn_opt_format_option(&opts, opt, TRUE /* doc */, scratch_pool);
          svn_stringbuf_appendcstr(buf, "  ");
          svn_stringbuf_appendcstr(buf, opts);
          svn_stringbuf_appendbyte(buf, '\n');
        }
    }

  return svn_error_trace(svn_cmdline_fputs(buf->data, stderr, scratch_pool));
}

static svn_error_t *
show_version(apr_pool_t *scratch_pool,
             const svn_browse__opt_state_t *opt_state)
{
  const char *ra_desc_start
    = "The following repository access (RA) modules are available:\n\n";
  svn_stringbuf_t *version_footer;

  version_footer = svn_stringbuf_create(ra_desc_start, scratch_pool);
  SVN_ERR(svn_ra_print_modules(version_footer, scratch_pool));

  SVN_ERR(svn_opt_print_help5(NULL,
                              "svnbrowse",
                              TRUE /* print_version */,
                              opt_state->quiet,
                              opt_state->verbose,
                              version_footer->data,
                              NULL, NULL, NULL, NULL, NULL,
                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_browse__color_mode_e
detect_color_mode(void)
{
  if (has_colors())
    {
      if (COLORS >= 255)
        return svn_browse__color_full;
      else
        return svn_browse__color_limited;
    }
  else
    return svn_browse__color_none;
}

static svn_error_t *
sub_main(int *code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_client_ctx_t *client;
  svn_auth_baton_t *auth;
  svn_browse__model_t *ctx;
  svn_browse__view_t *view;
  svn_browse__style_t *style;
  svn_browse__opt_state_t opt_state = { 0 };
  /* NOT USED: svn_boolean_t read_pass_from_stdin = FALSE; */
  apr_pool_t *iterpool;
  apr_getopt_t *os;
  apr_array_header_t *targets = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  opt_state.revision.kind = svn_opt_revision_head;
  opt_state.config_options =
      apr_array_make(pool, 0, sizeof(svn_cmdline__config_argument_t *));

  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));
  os->interleave = 1;

  while (TRUE)
    {
      const char *opt_arg;
      int opt_id;

      /* Parse the next option. */
      apr_status_t status = apr_getopt_long(os, svn_browse__options, &opt_id,
                                            &opt_arg);

      if (APR_STATUS_IS_EOF(status))
        break;
      else if (status != APR_SUCCESS)
        {
          SVN_ERR(show_usage(pool));
          *code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      switch (opt_id)
        {
          case 'r':
            SVN_ERR(svn_opt_parse_one_revision(&opt_state.revision, opt_arg, pool));
            break;
          case 'h':
          case '?':
            opt_state.help = TRUE;
            break;
          case opt_version:
            opt_state.version = TRUE;
            break;
          case opt_auth_username:
            opt_state.auth_username = apr_pstrdup(pool, opt_arg);
            break;
          case opt_auth_password:
            opt_state.auth_password = apr_pstrdup(pool, opt_arg);
            break;
        /* NOT USED:
          case opt_auth_password_from_stdin:
            read_pass_from_stdin = TRUE;
            break;
         */
          case opt_no_auth_cache:
            opt_state.no_auth_cache = TRUE;
            break;
          /* ### can we drop it in a 1.16 tool? */
          case opt_trust_server_cert: /* backwards compat to 1.8 */
            opt_state.trust_server_cert_unknown_ca = TRUE;
            break;
          case opt_trust_server_cert_failures:
            SVN_ERR(svn_cmdline__parse_trust_options(
                          &opt_state.trust_server_cert_unknown_ca,
                          &opt_state.trust_server_cert_cn_mismatch,
                          &opt_state.trust_server_cert_expired,
                          &opt_state.trust_server_cert_not_yet_valid,
                          &opt_state.trust_server_cert_other_failure,
                          opt_arg, pool));
            break;
          case opt_config_dir:
            opt_state.config_dir = svn_dirent_internal_style(opt_arg, pool);
            break;
          case opt_config_option:
            SVN_ERR(svn_cmdline__parse_config_option(opt_state.config_options,
                                                     opt_arg, "svnbrowse: ", pool));
            break;
          default:
            break;
        }
    }

  if (opt_state.version)
    {
      SVN_ERR(show_version(pool, &opt_state));
      return SVN_NO_ERROR;
    }

  if (opt_state.help)
    {
      SVN_ERR(show_help(pool, &opt_state));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_config_ensure(opt_state.config_dir, pool));

  /* Set up Authentication stuff. */
  SVN_ERR(svn_cmdline_create_auth_baton2(
            &auth,
            FALSE /* non_interactive */,
            opt_state.auth_username,
            opt_state.auth_password,
            opt_state.config_dir,
            opt_state.no_auth_cache,
            opt_state.trust_server_cert_unknown_ca,
            opt_state.trust_server_cert_cn_mismatch,
            opt_state.trust_server_cert_expired,
            opt_state.trust_server_cert_not_yet_valid,
            opt_state.trust_server_cert_other_failure,
            NULL, NULL, NULL,
            pool));

  SVN_ERR(svn_client_create_context2(&client, NULL, pool));
  client->auth_baton = auth;

  SVN_ERR(svn_client_args_to_target_array3(&targets, os, NULL, client, FALSE,
                                           pool));
  svn_opt_push_implicit_dot_target(targets, pool);

  /* we must fail if there are extra arguments */
  if (targets->nelts != 1)
    {
      *code = EXIT_FAILURE;
      SVN_ERR(show_usage(pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_opt_parse_path(&opt_state.peg_revision, &opt_state.path_or_url,
                             APR_ARRAY_IDX(targets, 0, const char *), pool));

  SVN_ERR(svn_browse__model_create(&ctx, client, opt_state.path_or_url,
                                   &opt_state.peg_revision,
                                   &opt_state.revision, pool, pool));

  /* init the display */
  initscr();

  /* put ncurses into keypad mode to handle arrow inputs */
  intrflush(stdscr, FALSE);
  keypad(stdscr, TRUE);
  nonl();

  /* ESCDELAY is a ncurses-exclusive variable that controls how curses will
   * handle escape sequences - when a user hits ESC and inputs a command for
   * the application to potentially do some handling. We don't really care
   * about this capability and would rather prefer to get out of svnbrowse by
   * ESC. Other backends may require further testing. It still works fine with
   * this variable misconfigured.
   *
   * Generally, the decision to exit by ESC itself is questionable, as the
   * majority of default applications don't do that (like vim, less, man). Some
   * users would spam escape after each action and the others to would expect
   * it to close a 37-deep stacked dialog. Anyway we can just drop associated
   * case statement at any time if we found that annoying.
   *
   * Find more info in 'man ESCDELAY'. */

#ifdef NCURSES_VERSION
  ESCDELAY = 0;
#endif /* NCURSES_VERSION */

  start_color();
  use_default_colors();

  style = style_init(detect_color_mode(), pool);
  view = view_make(ctx, style, stdscr, pool);

  iterpool = svn_pool_create(pool);

  /* Loop forever, unless we're not in an error state. */
  while (! err)
    {
      int ch;

      svn_pool_clear(iterpool);

      clear();
      view_draw(view, iterpool);
      refresh();

      ch = getch();
      err = view_on_event(view, ch, iterpool);
    }

  endwin();

  /* Treat cancellation success. */
  if (! err || err->apr_err == SVN_ERR_CANCELLED)
    return SVN_NO_ERROR;
  else
    {
      *code = EXIT_FAILURE;
      return err;
    }
}

int main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  if (svn_cmdline_init("svnbrowse", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  err = sub_main(&exit_code, argc, argv, pool);

  if (err)
    {
      exit_code = EXIT_FAILURE;
      svn_cmdline_handle_exit_error(err, NULL, "svnbrowse: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
