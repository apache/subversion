/*
 * main.c: Subversion dump stream filtering tool.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include <apr_file_io.h>

#include "svn_error.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_hash.h"
#include "svn_repos.h"
#include "svn_pools.h"

/* a reasonable guess at number of nodes that would be dropped */
#define REASONABLE_GUESS 42



/*** Code. ***/

/* Helper to open stdio streams */

/* NOTE: we used to call svn_stream_from_stdio(), which wraps a stream
   around a standard stdio.h FILE pointer.  The problem is that these
   pointers operate through C Run Time (CRT) on Win32, which does all
   sorts of translation on them: LF's become CRLF's, and ctrl-Z's
   embedded in Word documents are interpreted as premature EOF's.

   So instead, we use apr_file_open_std*, which bypass the CRT and
   directly wrap the OS's file-handles, which don't know or care about
   translation.  Thus dump/load works correctly on Win32.
*/
static svn_error_t *
create_stdio_stream (svn_stream_t **stream,
                     APR_DECLARE(apr_status_t) open_fn (apr_file_t **,
                                                        apr_pool_t *),
                     apr_pool_t *pool)
{
  apr_file_t *stdio_file;

  apr_status_t apr_err = open_fn (&stdio_file, pool);
  if (apr_err)
    return svn_error_create (apr_err, NULL,
                             "error opening stdio file");

  *stream = svn_stream_from_aprfile (stdio_file, pool);
  return SVN_NO_ERROR;
}
#if 0
static void
lx_dump_2cstring_ht (apr_pool_t *p, apr_hash_t *ht)
{
  apr_hash_index_t *hi;
  void *key, *val;

  for (hi = apr_hash_first (p, ht); hi; hi = apr_hash_next (hi))
    {
      apr_hash_this (hi, &key, NULL, &val);
      fprintf (stderr, "\n '%s' => '%s'",
               (const char *)key, (const char *)val);
    }
}
#endif
static void
lx_dump_2revnum_ht (apr_pool_t *p, apr_hash_t *ht)
{
  apr_hash_index_t *hi;
  const void *key;
  void *val;

  for (hi = apr_hash_first (p, ht); hi; hi = apr_hash_next (hi))
    {
      apr_hash_this (hi, &key, NULL, &val);
      fprintf (stderr, "\n '%" SVN_REVNUM_T_FMT "' => '%" SVN_REVNUM_T_FMT
               "'", *((svn_revnum_t *)key), *((svn_revnum_t *)val));
    }
}

static void
lx_dump_cstring_ary (apr_array_header_t *array)
{
  int i;
  const char *elt;

  for (i= 0; i < array->nelts; i++)
    {
      elt = APR_ARRAY_IDX (array , i, const char *);
      fprintf (stderr,"%s\n", elt);
    }
}


/* Writes a property in dumpfile format to given stringbuf. */
static void
write_prop_to_stringbuf (svn_stringbuf_t **strbuf,
                         const char *name,
                         const svn_string_t *value)
{
  int bytes_used, namelen;
  char buf[SVN_KEYLINE_MAXLEN];

  /* Output name length, then name. */
  namelen = strlen (name);
  svn_stringbuf_appendbytes (*strbuf, "K ", 2);

  sprintf (buf, "%d%n", namelen, &bytes_used);
  svn_stringbuf_appendbytes (*strbuf, buf, bytes_used);
  svn_stringbuf_appendbytes (*strbuf, "\n", 1);

  svn_stringbuf_appendbytes (*strbuf, name, namelen);
  svn_stringbuf_appendbytes (*strbuf, "\n", 1);

  /* Output value length, then value. */
  svn_stringbuf_appendbytes (*strbuf, "V ", 2);

  sprintf (buf, "%" APR_SIZE_T_FMT "%n", value->len, &bytes_used);
  svn_stringbuf_appendbytes (*strbuf, buf, bytes_used);
  svn_stringbuf_appendbytes (*strbuf, "\n", 1);

  svn_stringbuf_appendbytes (*strbuf, value->data, value->len);
  svn_stringbuf_appendbytes (*strbuf, "\n", 1);
}

/* Prefix matching function to compare node-path with set of prefixes. */
static svn_boolean_t
ary_prefix_match (apr_array_header_t *pfxlist, const char *path)
{
  int i, path_len, pfx_len;
  const char *pfx;
  svn_boolean_t rval = FALSE;

  path_len = strlen (path);

  for (i= 0; i < pfxlist->nelts; i++)
    {
      pfx = APR_ARRAY_IDX (pfxlist , i, const char *);
      pfx_len = strlen (pfx);
      if (path_len < pfx_len)
        continue;
      rval = (0 == strncmp (path, pfx, pfx_len));
      if (rval)
        break;
    }

  return rval;
}
/* EXact matching function to compare node-path with set of prefixes. */
static svn_boolean_t
ary_exact_match (apr_array_header_t *estlist, const char *path)
{
  int i, path_len, est_len;
  const char *est;
  svn_boolean_t rval = FALSE;

  path_len = strlen (path);

  for (i= 0; i < estlist->nelts; i++)
    {
      est = APR_ARRAY_IDX (estlist , i, const char *);
      est_len = strlen (est);
      if (path_len != est_len)
        continue;
      rval = (0 == strncmp (path, est, est_len));
      if (rval)
        break;
    }

  return rval;
}
/* Note: the input stream parser calls us up with events.
   Output of filtered dump should take place at the close-events.
   Before that we just save supplied data in corresponding batons.
*/



/* Filtering batons */

struct parse_baton_t {
  svn_boolean_t       do_exclude;
  svn_boolean_t       drop_empty_revs;
  svn_boolean_t       do_renumber_revs;
  svn_stream_t       *in_stream;
  svn_stream_t       *out_stream;
  apr_int32_t         rev_drop_count;
  apr_int32_t         node_drop_count;
  apr_array_header_t *prefixes;
  apr_array_header_t *dropped_nodes;
  apr_hash_t         *renumber_history;
};

struct revision_baton_t {
  struct parse_baton_t *pb;

  svn_boolean_t    has_nodes;
  svn_boolean_t    has_props;
  svn_boolean_t    had_dropped_nodes;
  svn_revnum_t     rev_orig;
  svn_revnum_t     rev_actual;
  svn_stringbuf_t *header;
  svn_stringbuf_t *props;
  svn_stringbuf_t *body;
  svn_stream_t    *body_stream;
};

struct node_baton_t {
  struct revision_baton_t *rb;

  svn_boolean_t    do_skip;
  svn_boolean_t    has_props;
  svn_boolean_t    has_text;
  svn_boolean_t    remove_props;
  svn_stringbuf_t *header;
  svn_stringbuf_t *props;
  svn_stringbuf_t *body;
  svn_stringbuf_t *node_path;
  svn_stringbuf_t *copyfrom_path;
  svn_stream_t    *body_stream;
};



/* Filtering vtable members */

/* New revision: set up revision_baton, decide if we skip it. */
static svn_error_t *
new_revision_record (void **revision_baton,
                     apr_hash_t *headers,
                     void *parse_baton,
                     apr_pool_t *pool)
{
  struct revision_baton_t *rb;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  svn_stream_t *header_stream;

  apr_pool_t *revhistory_pool;
  svn_revnum_t *rr_key, *rr_value;

  *revision_baton = apr_palloc (pool, sizeof (struct revision_baton_t));
  rb = *revision_baton;
  rb->has_nodes = FALSE;
  rb->has_props = FALSE;
  rb->had_dropped_nodes = FALSE;
  rb->pb     = parse_baton;
  rb->header = svn_stringbuf_create ("", pool);
  rb->props  = svn_stringbuf_create ("", pool);
  rb->body   = svn_stringbuf_create ("", pool);
  rb->body_stream = svn_stream_from_stringbuf (rb->body, pool);

  header_stream = svn_stream_from_stringbuf (rb->header, pool);

  val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                      APR_HASH_KEY_STRING);
  rb->rev_orig = SVN_STR_TO_REV(val);

  if (rb->pb->do_renumber_revs)
    {
      rb->rev_actual = rb->rev_orig - rb->pb->rev_drop_count;

      revhistory_pool = apr_hash_pool_get (rb->pb->renumber_history);
      rr_key = apr_palloc (revhistory_pool, sizeof (svn_revnum_t));
      rr_value = apr_palloc (revhistory_pool, sizeof (svn_revnum_t));

      *rr_key = rb->rev_orig;
      *rr_value = rb->rev_actual;

      apr_hash_set (rb->pb->renumber_history, rr_key,
                    sizeof (svn_revnum_t), rr_value);
    }
  else
    {
      rb->rev_actual = rb->rev_orig;
    }


  SVN_ERR (svn_stream_printf (header_stream, pool,
                              SVN_REPOS_DUMPFILE_REVISION_NUMBER ": %"
                              SVN_REVNUM_T_FMT "\n", rb->rev_actual));


  for (hi = apr_hash_first (pool, headers); hi; hi = apr_hash_next (hi))
    {
      apr_hash_this (hi, &key, NULL, &val);
      if ((!strcmp (key, SVN_REPOS_DUMPFILE_CONTENT_LENGTH))
          || (!strcmp (key, SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH))
          || (!strcmp (key, SVN_REPOS_DUMPFILE_REVISION_NUMBER)))
        continue;

      /* passthru: put header into header stringbuf. */

      SVN_ERR (svn_stream_printf (header_stream, pool, "%s: %s\n",
                                  (const char *)key,
                                  (const char *)val));

    }

  SVN_ERR (svn_stream_close (header_stream));

  return SVN_NO_ERROR;
}

/* UUID record here: dump it, as we do not filter them. */
static svn_error_t *
uuid_record (const char *uuid, void *parse_baton, apr_pool_t *pool)
{
  struct parse_baton_t *pb = parse_baton;
  SVN_ERR (svn_stream_printf (pb->out_stream, pool,
                              SVN_REPOS_DUMPFILE_UUID ": %s\n\n", uuid));
  return SVN_NO_ERROR;
}

/* New node here. Set up node_baton by copying headers. */
static svn_error_t *
new_node_record (void **node_baton,
                 apr_hash_t *headers,
                 void *rev_baton,
                 apr_pool_t *pool)
{
  struct parse_baton_t *pb;
  struct node_baton_t *nb;
  char *node_path, *copyfrom_path;
  svn_revnum_t cf_orig_rev, *cf_renum_rev;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  svn_stream_t *header_stream;

  *node_baton = apr_palloc (pool, sizeof (struct node_baton_t));
  nb          = *node_baton;
  nb->rb      = rev_baton;
  pb          = nb->rb->pb;

  node_path = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_PATH,
                            APR_HASH_KEY_STRING);

  /* shame, shame, shame ... this is NXOR. */
  nb->do_skip = (ary_prefix_match (pb->prefixes, node_path)
                 ? pb->do_exclude : (!pb->do_exclude));

  copyfrom_path = apr_hash_get (headers,
                                SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH,
                                APR_HASH_KEY_STRING);

  /* See if this node was copied from dropped source (drop it too, then.) */
  if ((copyfrom_path != NULL) && (!nb->do_skip))
    {
      nb->do_skip = ary_exact_match (pb->dropped_nodes, copyfrom_path);
    }

  /* if we're skipping the node, take note of path, discarding the rest
     This is ugly :-(
  */
  if (nb->do_skip)
    {
      *((const char **) apr_array_push (pb->dropped_nodes))
        = apr_pstrdup (pb->dropped_nodes->pool, node_path);
      
      nb->rb->had_dropped_nodes = TRUE;
      pb->node_drop_count ++;
    }
  else
    {
      nb->has_props = FALSE;
      nb->has_text  = FALSE;
      nb->remove_props = FALSE;
      nb->header    = svn_stringbuf_create ("", pool);
      nb->props     = svn_stringbuf_create ("", pool);
      nb->body      = svn_stringbuf_create ("", pool);
      nb->body_stream = svn_stream_from_stringbuf (nb->body, pool);
      header_stream  = svn_stream_from_stringbuf (nb->header, pool);

      for (hi = apr_hash_first (pool, headers); hi; hi = apr_hash_next (hi))
        {
          apr_hash_this (hi, (const void **) &key, NULL, &val);
          if ((!strcmp (key, SVN_REPOS_DUMPFILE_CONTENT_LENGTH))
              || (!strcmp (key, SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH))
              || (!strcmp (key, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH)))
            continue;

          /* Rewrite Node-Copyfrom-Rev if we are renumbering revisions.
             The number points to some revision in the past. We keep track
             of revision renumbering in a apr_hash, which maps original
             revisions to new ones. Dropped revision are mapped to -1.
             This should never happen here.
          */
          if (pb->do_renumber_revs
              && (!strcmp (key, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV)))
            {
              cf_orig_rev = SVN_STR_TO_REV(val);
              cf_renum_rev = apr_hash_get (pb->renumber_history,
                                           &cf_orig_rev,
                                           sizeof (svn_revnum_t));
              if ((cf_renum_rev == NULL) || (*cf_renum_rev == -1))
                {
                  /* bail out with an error */
                  return svn_error_createf
                    (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                     "Node with dropped parent sneaked in.");
                }
              SVN_ERR (svn_stream_printf
                       (header_stream, pool,
                        SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV ": %"
                        SVN_REVNUM_T_FMT "\n", *cf_renum_rev));
              continue;
            }

          /* passthru: put header into header stringbuf  */

          SVN_ERR (svn_stream_printf (header_stream, pool, "%s: %s\n",
                                      (const char *)key,
                                      (const char *)val));
        }

      SVN_ERR (svn_stream_close (header_stream));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property (void *revision_baton,
                       const char *name,
                       const svn_string_t *value)
{
  struct revision_baton_t *rb = revision_baton;

  write_prop_to_stringbuf (&(rb->props), name, value);
  rb->has_props = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property (void *node_baton,
                   const char *name,
                   const svn_string_t *value)
{
  struct node_baton_t *nb = node_baton;

  if (!nb->do_skip)
    {
      write_prop_to_stringbuf (&(nb->props), name, value);
      nb->has_props = TRUE;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props (void *node_baton)
{
  struct node_baton_t *nb = node_baton;

  nb->remove_props = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext (svn_stream_t **stream, void *node_baton)
{
  struct node_baton_t *nb = node_baton;

  if (!nb->do_skip)
    {
      *stream = nb->body_stream;
      nb->has_text = TRUE;
    }

  return SVN_NO_ERROR;
}



/* Finalize node */
static svn_error_t *
close_node (void *node_baton)
{
  struct node_baton_t *nb = node_baton;
  int bytes_used;
  char buf[SVN_KEYLINE_MAXLEN];

  /* Get out of here if we can. */
  if (nb->do_skip)
    {
      return SVN_NO_ERROR;
    }

  /* when there are no props nb->props->len would be zero and won't mess up
     Content-Length. */
  if (nb->has_props)
    svn_stringbuf_appendcstr (nb->props, "PROPS-END\n");

  /* 1. recalculate & check text-md5 if present. Passed through right now. */

  /* 2. recalculate and add content-lengths */

  if (nb->has_props)
    {
      svn_stringbuf_appendcstr (nb->header,
                                SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH);
      sprintf (buf, ": %" APR_SIZE_T_FMT "%n", nb->props->len, &bytes_used);
      svn_stringbuf_appendbytes (nb->header, buf, bytes_used);
      svn_stringbuf_appendbytes (nb->header, "\n", 1);
    }
  if (nb->has_text)
    {
      svn_stringbuf_appendcstr (nb->header,
                                SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH);
      sprintf (buf, ": %" APR_SIZE_T_FMT "%n", nb->body->len, &bytes_used);
      svn_stringbuf_appendbytes (nb->header, buf, bytes_used);
      svn_stringbuf_appendbytes (nb->header, "\n", 1);
    }
  svn_stringbuf_appendcstr (nb->header, SVN_REPOS_DUMPFILE_CONTENT_LENGTH);
  sprintf (buf, ": %" APR_SIZE_T_FMT "%n", (nb->props->len + nb->body->len),
           &bytes_used);
  svn_stringbuf_appendbytes (nb->header, buf, bytes_used);
  svn_stringbuf_appendbytes (nb->header, "\n", 1);

  /* put an end to headers */
  svn_stringbuf_appendbytes (nb->header, "\n", 1);

  /* put an end to node. */
  svn_stringbuf_appendbytes (nb->body,   "\n\n", 2);

  /* 3. add all stuff to the parent revision */

  svn_stringbuf_appendstr (nb->rb->body, nb->header);
  svn_stringbuf_appendstr (nb->rb->body, nb->props);
  svn_stringbuf_appendstr (nb->rb->body, nb->body);
  nb->rb->has_nodes = TRUE;

  return SVN_NO_ERROR;
}

/* Finalize revision */
static svn_error_t *
close_revision (void *revision_baton)
{
  struct revision_baton_t *rb = revision_baton;
  int bytes_used;
  char buf[SVN_KEYLINE_MAXLEN];

  if (rb->has_props)
    svn_stringbuf_appendcstr (rb->props, "PROPS-END\n");

  if (rb->has_props)
    {
      svn_stringbuf_appendcstr (rb->header,
                                SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH);
      sprintf (buf, ": %" APR_SIZE_T_FMT "%n", rb->props->len, &bytes_used);
      svn_stringbuf_appendbytes (rb->header, buf, bytes_used);
      svn_stringbuf_appendbytes (rb->header, "\n", 1);
    }

  svn_stringbuf_appendcstr (rb->header, SVN_REPOS_DUMPFILE_CONTENT_LENGTH);
  sprintf (buf, ": %" APR_SIZE_T_FMT "%n", rb->props->len, &bytes_used);
  svn_stringbuf_appendbytes (rb->header, buf, bytes_used);
  svn_stringbuf_appendbytes (rb->header, "\n", 1);

  /* put an end to headers */
  svn_stringbuf_appendbytes (rb->header, "\n", 1);

  /* put an end to revision */
  svn_stringbuf_appendbytes (rb->props,  "\n", 1);

  /* write out the revision */
  /* Revision is written out in the following cases:
     1. No --drop-empty-revs has been supplied.
     2. --drop-empty-revs has been supplied,
     but revision has not all nodes dropped
     3. Revision had no nodes to begin with.
  */
  if (rb->has_nodes
      || (! rb->pb->drop_empty_revs)
      || (! rb->had_dropped_nodes))
    {
      SVN_ERR (svn_stream_write (rb->pb->out_stream,
                                 rb->header->data , &(rb->header->len)));
      SVN_ERR (svn_stream_write (rb->pb->out_stream,
                                 rb->props->data  , &(rb->props->len)));
      SVN_ERR (svn_stream_write (rb->pb->out_stream,
                                 rb->body->data   , &(rb->body->len)));
      fprintf (stderr, "Revision %" SVN_REVNUM_T_FMT " committed as %"
               SVN_REVNUM_T_FMT ".\n", rb->rev_orig, rb->rev_actual);
    }
  else
    {
      rb->pb->rev_drop_count ++;
      fprintf (stderr, "Revision %" SVN_REVNUM_T_FMT " skipped.\n",
               rb->rev_orig);
    }
  return SVN_NO_ERROR;
}


/* Filtering vtable */

svn_repos_parser_fns_t filtering_vtable =
  {
    new_revision_record,
    uuid_record,
    new_node_record,
    set_revision_property,
    set_node_property,
    remove_node_props,
    set_fulltext,
    close_node,
    close_revision
  };




/** Subcommands. **/

static svn_opt_subcommand_t
subcommand_help,
  subcommand_exclude,
  subcommand_include;

enum
  {
    svndumpfilter__drop_empty_revs = SVN_OPT_FIRST_LONGOPT_ID,
    svndumpfilter__renumber_revs
  };

/* Option codes and descriptions.
 *
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

    {"drop-empty-revs",   svndumpfilter__drop_empty_revs, 0,
     "Remove revisions emptied by filtering."},
    {"renumber-revs",     svndumpfilter__renumber_revs, 0,
     "Renumber revisions left after filtering." },

    {NULL}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc_t cmd_table[] =
  {
    {"exclude", subcommand_exclude, {0},
     "usage: svndumpfilter exclude PATH_PREFIX...\n\n"
     "Filter out nodes with given prefixes from dumpstream.\n",
     {svndumpfilter__drop_empty_revs, svndumpfilter__renumber_revs} },

    {"include", subcommand_include, {0},
     "usage: svndumpfilter include PATH_PREFIX...\n\n"
     "Filter out nodes without given prefixes from dumpstream.\n",
     {svndumpfilter__drop_empty_revs, svndumpfilter__renumber_revs} },

    {"help", subcommand_help, {"?", "h"},
     "usage: svndumpfilter help [SUBCOMMAND...]\n\n"
     "Display this usage message.\n",
     {0} },

    { NULL, NULL, {0}, NULL, {0} }
  };


/* Baton for passing option/argument state to a subcommand function. */
struct svndumpfilter_opt_state
{
  svn_opt_revision_t start_revision;     /* -r X[:Y] is       */
  svn_opt_revision_t end_revision;       /* not implemented.  */
  svn_boolean_t drop_empty_revs;         /* --drop-empty-revs */
  svn_boolean_t help;                    /* --help or -?      */
  svn_boolean_t renumber_revs;           /* --renumber_revs   */
  apr_array_header_t *prefixes;          /* mainargs.         */
};


static void
dump_opt_state (struct svndumpfilter_opt_state *opt_state,
                svn_boolean_t do_exclude)
{
  int i;

  fprintf (stderr,"%s %s drop called for prefixes:\n",
           do_exclude ? "Exclude" : "Include",
           opt_state->drop_empty_revs ? "with" : "without");

  for (i= 0; i < opt_state->prefixes->nelts; i++)
    {
      fprintf (stderr,"'%s'\n",
               APR_ARRAY_IDX (opt_state->prefixes , i, const char *));
    }
}

static svn_error_t *
parse_baton_initialize (struct parse_baton_t **pb,
                        struct svndumpfilter_opt_state *opt_state,
                        svn_boolean_t do_exclude,
                        apr_pool_t *pool)
{
  dump_opt_state (opt_state, do_exclude);

  *pb = apr_palloc (pool, sizeof (struct parse_baton_t));

  /* Read the stream from STDIN.  Users can redirect a file. */
  SVN_ERR (create_stdio_stream (&((*pb)->in_stream),
                                apr_file_open_stdin, pool));

  /* Have the parser dump results to STDOUT. Users can redirect a file. */
  SVN_ERR (create_stdio_stream (&((*pb)->out_stream),
                                apr_file_open_stdout, pool));

  (*pb)->do_exclude = do_exclude;
  (*pb)->do_renumber_revs = opt_state->renumber_revs;
  (*pb)->drop_empty_revs = opt_state->drop_empty_revs;
  (*pb)->prefixes = opt_state->prefixes;
  /* this may be used to shift revision numbers while filtering */
  (*pb)->rev_drop_count = 0;
  (*pb)->node_drop_count = 0;

  (*pb)->dropped_nodes = apr_array_make (pool, REASONABLE_GUESS,
                                         sizeof (const char *));
  (*pb)->renumber_history = apr_hash_make (pool);

  SVN_ERR (svn_stream_printf ((*pb)->out_stream, pool,
                              SVN_REPOS_DUMPFILE_MAGIC_HEADER ": %d\n\n",
                              SVN_REPOS_DUMPFILE_FORMAT_VERSION));

  return SVN_NO_ERROR;
}

/* This implements `help` subcommand. */
static svn_error_t *
subcommand_help (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  const char *header =
    "general usage: svndumpfilter SUBCOMMAND [ARGS & OPTIONS ...]\n"
    "Type \"svndumpfilter help <subcommand>\" for help on a "
    "specific subcommand.\n"
    "\n"
    "Available subcommands:\n";

  SVN_ERR (svn_opt_print_help (os, "svndumpfilter", FALSE, FALSE, NULL,
                               header, cmd_table, options_table, NULL,
                               pool));

  return SVN_NO_ERROR;
}

/* This implements `exclude' subcommand. */
static svn_error_t *
subcommand_exclude (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svndumpfilter_opt_state *opt_state = baton;
  struct parse_baton_t *pb;

  SVN_ERR (parse_baton_initialize (&pb, opt_state, TRUE, pool));

  SVN_ERR (svn_repos_parse_dumpstream (pb->in_stream,
                                       &filtering_vtable, pb, pool));

  fprintf (stderr, "\n Dropped %d revisions, %d nodes\n",
           pb->rev_drop_count, pb->node_drop_count);

  if (pb->do_renumber_revs)
    {
      fprintf (stderr, "\nRenumber history:\n");
      lx_dump_2revnum_ht (pool, pb->renumber_history);
    }

  if (pb->node_drop_count > 0)
    {
      fprintf (stderr, "\n\nDropped nodes list:\n");
      lx_dump_cstring_ary (pb->dropped_nodes);
    }
  else
    {
      fprintf (stderr, "\n\nNo nodes dropped.\n");
    }
  return SVN_NO_ERROR;
}


/* This implements `include` subcommand. */
static svn_error_t *
subcommand_include (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svndumpfilter_opt_state *opt_state = baton;
  struct parse_baton_t *pb;

  SVN_ERR (parse_baton_initialize (&pb, opt_state, FALSE, pool));

  SVN_ERR (svn_repos_parse_dumpstream (pb->in_stream,
                                       &filtering_vtable, pb, pool));

  fprintf (stderr, "\n Dropped %d revisions, %d nodes\n",
           pb->rev_drop_count, pb->node_drop_count);

  if (pb->do_renumber_revs)
    {
      fprintf (stderr, "\nRenumber history:\n");
      lx_dump_2revnum_ht (pool, pb->renumber_history);
    }

  if (pb->node_drop_count > 0)
    {
      fprintf (stderr, "\n\nDropped nodes list:\n");
      lx_dump_cstring_ary (pb->dropped_nodes);
    }
  else
    {
      fprintf (stderr, "\n\nNo nodes dropped.\n");
    }

  return SVN_NO_ERROR;
}



/** Main. **/

int
main (int argc, const char * const *argv)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_pool_t *pool;

  const svn_opt_subcommand_desc_t *subcommand = NULL;
  struct svndumpfilter_opt_state opt_state;
  apr_getopt_t *os;
  int opt_id;
  int received_opts[SVN_OPT_MAX_OPTIONS];
  int i, num_opts = 0;


  /* Initialize the app. */
  if (svn_cmdline_init ("svndumpfilter", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool. */
  pool = svn_pool_create (NULL);

  if (argc <= 1)
    {
      subcommand_help (NULL, NULL, pool);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* Initialize opt_state. */
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;

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
        case 'h':
        case '?':
          opt_state.help = TRUE;
          break;
        case svndumpfilter__drop_empty_revs:
          opt_state.drop_empty_revs = TRUE;
          break;
        case svndumpfilter__renumber_revs:
          opt_state.renumber_revs = TRUE;
          break;
        default:
          {
            subcommand_help (NULL, NULL, pool);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        }  /* close `switch' */
    }  /* close `while' */

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

  /* If there's a second argument, it's probably [one of] prefixes.
     Every subcommand except `help' requires at least one, so we parse
     them out here and store in opt_state. */

  if (subcommand->cmd_func != subcommand_help)
    {
      if (os->ind < os->argc)
        {
          opt_state.prefixes = apr_array_make (pool,
                                               os->argc - os->ind,
                                               sizeof (const char *));
          for (i = os->ind ; i< os->argc; i++)
            {
              const char *prefix_utf8;
              SVN_INT_ERR (svn_utf_cstring_to_utf8 (&prefix_utf8,
                                                    os->argv[i],
                                                    NULL,  pool));
              *(const char **)apr_array_push (opt_state.prefixes)
                = svn_path_internal_style (prefix_utf8, pool);
            }
        }
      else
        {
          fprintf (stderr,
                   "\nError: no prefixes supplied.\n");
          svn_opt_subcommand_help (subcommand->name,
                                   cmd_table,
                                   options_table,
                                   pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
    }


  /* Check that the subcommand wasn't passed any inappropriate options.*/
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
