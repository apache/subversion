/* load.c --- parsing a 'dumpfile'-formatted stream.
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


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_hash.h"
#include "svn_path.h"

#include "apr_lib.h"

/*----------------------------------------------------------------------*/

/** The parser and related helper funcs **/


/* Allocate a new hash *HEADERS in POOL, and read a series of
   RFC822-style headers from STREAM.  Duplicate each header's name and
   value into POOL and store in hash as a const char * ==> const char *.

   The headers are assumed to be terminated by a single blank line,
   which will be permanently sucked from the stream and tossed.

   If the caller has already read in the first header line, it should
   be passed in as FIRST_HEADER.  If not, pass NULL instead.
 */
static svn_error_t *
read_header_block (svn_stream_t *stream,
                   svn_stringbuf_t *first_header,
                   apr_hash_t **headers,
                   apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  *headers = apr_hash_make (pool);  

  while (1)
    {
      svn_stringbuf_t *header_str;
      const char *name, *value; 
      apr_size_t old_i = 0, i = 0;

      if (first_header != NULL)
        {
          header_str = first_header;
          first_header = NULL;  /* so we never visit this block again. */
        }

      else
        /* Read the next line into a stringbuf in subpool. */
        SVN_ERR (svn_stream_readline (stream, &header_str, subpool));
      
      if ((header_str == NULL) || (svn_stringbuf_isempty (header_str)))
        break;    /* end of header block */

      /* Find the next colon in the stringbuf. */
      while (header_str->data[i] != ':')
        {
          if (header_str->data[i] == '\0')
            return svn_error_create (SVN_ERR_MALFORMED_STREAM_DATA,
                                     0, NULL, pool,
                                     "Found malformed header block "
                                     "in dumpfile stream.");
          i++;
        }
      /* Allocate the header name in the original pool. */
      name = apr_pstrmemdup (pool, header_str->data, i);

      /* Skip over the colon and the space following it.  */
      i += 2;
      old_i = i;

      /* Find the end of the stringbuf. */
      while (header_str->data[i] != '\0')
        i++;
      /* Allocate the header value in the original pool. */
      value = apr_pstrmemdup (pool, header_str->data + old_i, (i - old_i));
      
      apr_hash_set (*headers, name, APR_HASH_KEY_STRING, value);

      svn_pool_clear (subpool); /* free the stringbuf */
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}



/* Read CONTENT_LENGTH bytes from STREAM.  Look for encoded properties
   at the start of the content block, and make multiple calls to
   PARSE_FNS->set_*_property on RECORD_BATON (depending on the value
   of IS_NODE.)  PACK_FUNC is used to decode the property values.

   If IS_NODE is true and content exists beyond the properties, push
   the remaining content at a write-stream obtained from
   PARSE_FNS->set_fulltext, and then close the write-stream.
   Use BUFFER/BUFLEN to push the fulltext in "chunks".

   Use pool for all allocations.
*/
static svn_error_t *
parse_content_block (svn_stream_t *stream,
                     apr_size_t content_length,
                     const svn_repos_parser_fns_t *parse_fns,
                     void *record_baton,
                     void *(*pack_func) (size_t len,
                                         const char *val,
                                         apr_pool_t *pool),
                     svn_boolean_t is_node,
                     char *buffer,
                     apr_size_t buflen,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *strbuf;
  apr_pool_t *subpool = svn_pool_create (pool);
  
  /* A running tally of how many bytes we've sucked from the stream. */
  apr_size_t remaining_bytes, bytes_sucked = 0;
  
  /* Step 1:  parse properties out of the stream.  This code is a
     variant of the hash-reading routine in libsvn_subr. */
  while (1)
    {
      void *package;
      svn_string_t *propstring;
      char *buf;  /* a pointer into the stringbuf's data */

      /* Read a key length line.  (Actually, it might be PROPS_END). */
      SVN_ERR (svn_stream_readline (stream, &strbuf, subpool));
      bytes_sucked += (strbuf->len + 1); /* +1 because we read a \n too. */
      buf = strbuf->data;

      if (! strcmp (buf, "PROPS-END"))
        break; /* no more properties. */

      else if ((buf[0] == 'K') && (buf[1] == ' '))
        {
          apr_size_t numread;
          char *keybuf;
          char c;
          
          /* Get the length of the key */
          size_t keylen = (size_t) atoi (buf + 2);

          /* Now read that much into a buffer, + 1 byte for null terminator */
          keybuf = apr_pcalloc (subpool, keylen + 1);
          numread = keylen;
          SVN_ERR (svn_stream_read (stream, keybuf, &numread));
          bytes_sucked += numread;
          if (numread != keylen)
            goto stream_ran_dry;
          ((char *) keybuf)[keylen] = '\0';

          /* Suck up extra newline after key data */
          numread = 1;
          SVN_ERR (svn_stream_read (stream, &c, &numread));
          bytes_sucked += numread;
          if (numread != 1)
            goto stream_ran_dry;
          if (c != '\n') 
            goto stream_malformed;

          /* Read a val length line */
          SVN_ERR (svn_stream_readline (stream, &strbuf, subpool));
          bytes_sucked += (strbuf->len + 1); /* +1 because we read \n too */
          buf = strbuf->data;

          if ((buf[0] == 'V') && (buf[1] == ' '))
            {
              /* Get the length of the value */
              int vallen = atoi (buf + 2);

              /* Again, 1 extra byte for the null termination. */
              char *valbuf = apr_palloc (subpool, vallen + 1);
              numread = vallen;
              SVN_ERR (svn_stream_read (stream, valbuf, &numread));
              bytes_sucked += numread;
              if (numread != vallen)
                goto stream_ran_dry;
              ((char *) valbuf)[vallen] = '\0';

              /* Suck up extra newline after val data */
              numread = 1;
              SVN_ERR (svn_stream_read (stream, &c, &numread));
              bytes_sucked += numread;
              if (numread != 1)
                goto stream_ran_dry;
              if (c != '\n') 
                goto stream_malformed;

              /* Send the val data for packaging... */
              package = (void *) (*pack_func) (vallen, valbuf, subpool);
              propstring = (svn_string_t *) package;

              /* Now send the property pair to the vtable! */
              if (is_node)
                SVN_ERR (parse_fns->set_node_property (record_baton,
                                                       keybuf,
                                                       propstring));
              else
                SVN_ERR (parse_fns->set_revision_property (record_baton,
                                                           keybuf,
                                                           propstring));
            }
          else
            goto stream_malformed; /* didn't find expected 'V' line */
        }
      else
        goto stream_malformed; /* didn't find expected 'K' line */
      
      svn_pool_clear (subpool);
    } /* while (1) */


  /* Step 2:  if we've not yet read CONTENT_LENGTH bytes of data, push
     the remaining bytes as fulltext. */
  remaining_bytes = content_length - bytes_sucked;
  if (remaining_bytes > 0) 
    {
      apr_size_t rlen, wlen, i, iterations, remainder;
      svn_stream_t *text_stream;

      if (! is_node)
        goto stream_malformed;  /* revisions don't have text! */
      
      SVN_ERR (parse_fns->set_fulltext (&text_stream, record_baton));

      remainder = remaining_bytes % buflen;
      iterations = remaining_bytes / buflen;

      for (i = 0; i < iterations; i++)
        {
          /* read a maximum number of bytes from the stream. */
          rlen = buflen; 
          SVN_ERR (svn_stream_read (stream, buffer, &rlen));
          
          if (rlen != buflen)
            /* Uh oh, didn't read all buflen bytes. */
            goto stream_ran_dry;
          
          if (text_stream != NULL)
            {
              /* write however many bytes you read. */
              wlen = rlen;
              SVN_ERR (svn_stream_write (text_stream, buffer, &wlen));
              if (wlen != rlen)
                /* Uh oh, didn't write as many bytes as we read. */
                return
                  svn_error_create (SVN_ERR_UNEXPECTED_EOF, 0, NULL, pool,
                                    "Error pushing textual contents.");
            }
        }
      
      /* push 'remainder' bytes */
      rlen = remainder;
      SVN_ERR (svn_stream_read (stream, buffer, &rlen));
      
      if (rlen != remainder)
        /* Uh oh, didn't read all remainder bytes. */
        goto stream_ran_dry;
      
          /* write however many bytes you read. */
      if (text_stream != NULL)
        {
          wlen = rlen;
          SVN_ERR (svn_stream_write (text_stream, buffer, &wlen));
          if (wlen != rlen)
            /* Uh oh, didn't write as many bytes as we read. */
            return
              svn_error_create (SVN_ERR_UNEXPECTED_EOF, 0, NULL, pool,
                                "Error pushing textual contents.");

          /* done pushing text, close the stream. */
          SVN_ERR (svn_stream_close (text_stream));
        }   
   
    } /* done slurping all the fulltext */
    
  /* Everything good, mission complete. */
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
 
 stream_ran_dry:
  return 
    svn_error_create (SVN_ERR_INCOMPLETE_DATA, 0, NULL, pool,
                      "Premature end of content data in dumpstream.");

 stream_malformed:
  return
    svn_error_create (SVN_ERR_MALFORMED_STREAM_DATA, 0, NULL, pool,
                      "Dumpstream data appears to be malformed.");
}



static svn_error_t *
validate_format_version (const char *versionstring)
{
  /* ### parse string and verify that we support the dumpfile format
         version number. */
  
  return SVN_NO_ERROR;
}



/* The Main Parser Logic */
svn_error_t *
svn_repos_parse_dumpstream (svn_stream_t *stream,
                            const svn_repos_parser_fns_t *parse_fns,
                            void *parse_baton,
                            apr_pool_t *pool)
{
  svn_stringbuf_t *linebuf;
  void *current_rev_baton = NULL;
  char *buffer = apr_pcalloc (pool, SVN_STREAM_CHUNK_SIZE);
  apr_size_t buflen = SVN_STREAM_CHUNK_SIZE;
  apr_pool_t *linepool = svn_pool_create (pool);
  apr_pool_t *revpool = svn_pool_create (pool);
  apr_pool_t *nodepool = svn_pool_create (pool);

  /* The first two lines of the stream are the dumpfile-format version
     number, and a blank line. */
  SVN_ERR (svn_stream_readline (stream, &linebuf, pool));
  SVN_ERR (validate_format_version (linebuf->data));

  /* A dumpfile "record" is defined to be a header-block of
     rfc822-style headers, possibly followed by a content-block.

       - A header-block is always terminated by a single blank line (\n\n)

       - We know whether the record has a content-block by looking for
         a 'Content-length:' header.  The content-block will always be
         of a specific length, plus an extra newline.

     Once a record is fully sucked from the stream, an indeterminate
     number of blank lines (or lines that begin with whitespace) may
     follow before the next record (or the end of the stream.)
  */
  
  while (1)
    {
      /* Keep reading blank lines until we discover a new record, or until
         the stream runs out. */
      SVN_ERR (svn_stream_readline (stream, &linebuf, pool));
      
      if (linebuf == NULL)
        break;   /* end of stream, go home. */

      if ((linebuf->len > 0) && (! apr_isspace (linebuf->data[0])))
        {
          /* Found the beginning of a new record. */ 
          apr_hash_t *headers;
          void *node_baton;
          const char *valstr;
          svn_boolean_t found_node = FALSE;

          /* The last line we read better be a header of some sort.
             Read the whole header-block into a hash. */
          SVN_ERR (read_header_block (stream, linebuf, &headers, linepool));

          /* Create some kind of new record object. */
          if (apr_hash_get (headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                            APR_HASH_KEY_STRING))
            {
              /* Found a new revision record. */
              if (current_rev_baton != NULL)
                {
                  SVN_ERR (parse_fns->close_revision (current_rev_baton));
                  svn_pool_clear (revpool);
                }

              SVN_ERR (parse_fns->new_revision_record (&current_rev_baton,
                                                       headers, parse_baton,
                                                       revpool));
            }
          else if (apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_PATH,
                                 APR_HASH_KEY_STRING))
            {
              /* Found a new node record. */
              SVN_ERR (parse_fns->new_node_record (&node_baton,
                                                   headers,
                                                   current_rev_baton,
                                                   nodepool));
              found_node = TRUE;
            }
          else            
            /* What the heck is this record?!? */
            return svn_error_create (SVN_ERR_MALFORMED_STREAM_DATA,
                                     0, NULL, pool,
                                     "Unrecognized record type in stream.");


          /* Is there a content-block to parse? */
          if ((valstr = apr_hash_get (headers,
                                      SVN_REPOS_DUMPFILE_CONTENT_LENGTH,
                                      APR_HASH_KEY_STRING)))
            {
              apr_size_t content_length = (apr_size_t) atoi (valstr);

              SVN_ERR (parse_content_block (stream, content_length,
                                            parse_fns,
                                            found_node ? 
                                              node_baton : current_rev_baton,
                                            svn_pack_bytestring,
                                            found_node,
                                            buffer, buflen,
                                            found_node ?
                                              nodepool : revpool));
            }
          
          /* Done processing this node-record. */
          if (found_node)
            {
              SVN_ERR (parse_fns->close_node (node_baton));
              svn_pool_clear (nodepool);
            }

        } /* end of processing for one record. */
      
      svn_pool_clear (linepool);

    } /* end of stream */

  /* Close out whatever revision we're in. */
  if (current_rev_baton != NULL)
    SVN_ERR (parse_fns->close_revision (current_rev_baton));

  svn_pool_destroy (linepool);
  svn_pool_destroy (revpool);
  svn_pool_destroy (nodepool);
  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------------*/

/** vtable for doing commits to a fs **/

/* ### right now, this vtable does nothing but stupid printf's. */

struct parse_baton
{
  svn_fs_t *fs;
  
};

struct revision_baton
{
  svn_revnum_t rev;

  svn_fs_txn_t *txn;

  struct parse_baton *pb;
  apr_pool_t *pool;
};

struct node_baton
{
  const char *path;
  enum svn_node_kind kind;
  enum svn_node_action action;

  struct revision_baton *rb;
  apr_pool_t *pool;
};


static struct node_baton *
make_node_baton (apr_hash_t *headers,
                 struct revision_baton *rb,
                 apr_pool_t *pool)
{
  struct node_baton *nb = apr_pcalloc (pool, sizeof(*nb));
  const char *val;

  /* Start with sensible defaults. */
  nb->rb = rb;
  nb->pool = pool;
  nb->kind = svn_node_unknown;

  /* Then add info from the headers.  */
  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_PATH,
                           APR_HASH_KEY_STRING)))
    nb->path = apr_pstrdup (pool, val);

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_KIND,
                           APR_HASH_KEY_STRING)))
    {
      if (! strcmp (val, "file"))
        nb->kind = svn_node_file;
      else if (! strcmp (val, "dir"))
        nb->kind = svn_node_dir;
    }

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_ACTION,
                           APR_HASH_KEY_STRING)))
    {
      if (! strcmp (val, "change"))
        nb->kind = svn_node_action_change;
      else if (! strcmp (val, "add"))
        nb->kind = svn_node_action_add;
      else if (! strcmp (val, "delete"))
        nb->kind = svn_node_action_delete;
      else if (! strcmp (val, "replace"))
        nb->kind = svn_node_action_replace;
    }

  return nb;
}

static struct revision_baton *
make_revision_baton (apr_hash_t *headers,
                     struct parse_baton *pb,
                     apr_pool_t *pool)
{
  struct revision_baton *rb = apr_pcalloc (pool, sizeof(*rb));
  const char *val;

  rb->pb = pb;
  rb->pool = pool;
  rb->rev = SVN_INVALID_REVNUM;

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                           APR_HASH_KEY_STRING)))
    rb->rev = (svn_revnum_t) atoi (val);


  return rb;
}


static svn_error_t *
new_revision_record (void **revision_baton,
                     apr_hash_t *headers,
                     void *parse_baton,
                     apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  struct revision_baton *rb =  make_revision_baton (headers, pb, pool);

  printf ("<<< Got new record for revision %" SVN_REVNUM_T_FMT "\n", rb->rev);

  /* ### Create a new rb->txn someday, using pb->fs. */

  *revision_baton = rb;
  return SVN_NO_ERROR;
}


static svn_error_t *
new_node_record (void **node_baton,
                 apr_hash_t *headers,
                 void *revision_baton,
                 apr_pool_t *pool)
{
  struct revision_baton *rb = revision_baton;
  struct node_baton *nb =  make_node_baton (headers, rb, pool);

  printf ("   [[[ Got new record for node path : %s\n", nb->path);

  *node_baton = nb;
  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property (void *baton,
                       const char *name,
                       const svn_string_t *value)
{
  printf (" Set revision property '%s' to '%s'\n", name, value->data);

  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property (void *baton,
                   const char *name,
                   const svn_string_t *value)
{
  printf ("        Set node property '%s' to '%s'\n", name, value->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext (svn_stream_t **stream,
              void *node_baton)
{
  printf ("        Sorry, not interested in node's fulltext.\n");
  *stream = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_node (void *baton)
{
  struct node_baton *nb = baton;
  printf ("       End of node path : %s ]]]\n\n", nb->path);
  return SVN_NO_ERROR;
}


static svn_error_t *
close_revision (void *baton)
{
  struct revision_baton *rb = baton;
  printf ("End of revision %" SVN_REVNUM_T_FMT " >>>\n\n", rb->rev);

  /* ### someday commit a txn here. */

  return SVN_NO_ERROR;
}



static svn_error_t *
get_parser (const svn_repos_parser_fns_t **parser_callbacks,
            void **parse_baton,
            svn_repos_t *repos,
            apr_pool_t *pool)
{
  svn_repos_parser_fns_t *parser = apr_pcalloc (pool, sizeof(*parser));
  struct parse_baton *pb = apr_pcalloc (pool, sizeof(*pb));

  parser->new_revision_record = new_revision_record;
  parser->new_node_record = new_node_record;
  parser->set_revision_property = set_revision_property;
  parser->set_node_property = set_node_property;
  parser->set_fulltext = set_fulltext;
  parser->close_node = close_node;
  parser->close_revision = close_revision;

  pb->fs = svn_repos_fs (repos);

  *parser_callbacks = parser;
  *parse_baton = pb;
  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/** The main loader routine. **/


svn_error_t *
svn_repos_load_fs (svn_repos_t *repos,
                   svn_stream_t *stream,
                   apr_pool_t *pool)
{
  const svn_repos_parser_fns_t *parser;
  void *parse_baton;
  
  /* This is really simple. */  

  SVN_ERR (get_parser (&parser, &parse_baton, repos, pool));

  SVN_ERR (svn_repos_parse_dumpstream (stream, parser, parse_baton, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
