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


/*----------------------------------------------------------------------*/

/** The parser and related helper funcs **/


/* Allocate a new hash *HEADERS in POOL, and read a series of
   RFC822-style headers from STREAM.  Duplicate each header's name and
   value into POOL and store in hash as a const char * ==> const char *.

   The headers are assumed to be terminated by a blank line ("\n\n").
 */
static svn_error_t *
read_header_block (svn_stream_t *stream,
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

      /* Read the next line into a stringbuf in subpool. */
      SVN_ERR (svn_stream_readline (stream, &header_str, subpool));
      
      if (svn_stringbuf_isempty (header_str))
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
      svn_stream_t *text_stream;

      if (! is_node)
        goto stream_malformed;
      
      SVN_ERR (parse_fns->set_fulltext (&text_stream, record_baton));
      if (text_stream != NULL)
        {
          char buffer[SVN_STREAM_CHUNK_SIZE];
          apr_size_t buflen = SVN_STREAM_CHUNK_SIZE;
          apr_size_t rlen, wlen, i, iterations, remainder;

          iterations = remaining_bytes % buflen;
          remainder = remaining_bytes - (buflen * iterations);

          for (i = 0; i < iterations; i++)
            {
              /* read a maximum number of bytes from the stream. */
              rlen = buflen; 
              SVN_ERR (svn_stream_read (stream, buffer, &rlen));

              if (rlen != buflen)
                /* Uh oh, didn't read all buflen bytes. */
                goto stream_ran_dry;

              /* write however many bytes you read. */
              wlen = rlen;
              SVN_ERR (svn_stream_write (text_stream, buffer, &wlen));
              if (wlen != rlen)
                /* Uh oh, didn't write as many bytes as we read. */
                return
                  svn_error_create (SVN_ERR_UNEXPECTED_EOF, 0, NULL, pool,
                                    "Error pushing textual contents.");
            }

          /* push 'remainder' bytes */
          rlen = remainder;
          SVN_ERR (svn_stream_read (stream, buffer, &rlen));

          if (rlen != buflen)
            /* Uh oh, didn't read all remainder bytes. */
            goto stream_ran_dry;
          
          /* write however many bytes you read. */
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
    }
  
  
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



/* The Main Parser Logic */
svn_error_t *
svn_repos_parse_dumpstream (svn_stream_t *stream,
                            const svn_repos_parser_fns_t *parse_fns,
                            void *parse_baton,
                            apr_pool_t *pool)
{
  apr_hash_t *headers;

  /* ### verify that we support the dumpfile format version number. */

  /* ### outline:

  while (stream):
  {
    read group of headers into a hash

    if hash contains revision-number,
       possibly close_revision() on the old revision baton
       new_revision_record()
    else if hash contains node-path,
       new_node_record()
     
    if hash contains content-length,
       read & parse a content block.

    if in a node,
        close_node()
   }
   if in a revision,
     close_revision()
 */

  /* shut up compiler warnings about unused functions. */
  SVN_ERR (read_header_block (stream, &headers, pool));
  SVN_ERR (parse_content_block (stream, 42,
                                parse_fns, NULL,
                                svn_pack_bytestring, 
                                TRUE, pool));

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/** vtable for doing commits to a fs **/

/* ### right now, this vtable will do nothing but stupid printf's */

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

  nb->rb = rb;
  nb->pool = pool;

  /* ### parse the headers into a node_baton struct */

  return NULL;
}

static struct revision_baton *
make_revision_baton (apr_hash_t *headers,
                     struct parse_baton *pb,
                     apr_pool_t *pool)
{
  struct revision_baton *rb = apr_pcalloc (pool, sizeof(*rb));
  
  rb->pb = pb;
  rb->pool = pool;

  /* ### parse the headers into a revision_baton struct */

  return NULL;
}


static svn_error_t *
new_revision_record (void **revision_baton,
                     apr_hash_t *headers,
                     void *parse_baton,
                     apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  *revision_baton = make_revision_baton (headers, pb, pool);

  printf ("Got a new revision record.\n");

  /* ### Create a new *revision_baton->txn, using pb->fs. */

  return SVN_NO_ERROR;
}


static svn_error_t *
new_node_record (void **node_baton,
                 apr_hash_t *headers,
                 void *revision_baton,
                 apr_pool_t *pool)
{
  struct revision_baton *rb = revision_baton;
  printf ("Got a new node record.\n");

  *node_baton = make_node_baton (headers, rb, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property (void *baton,
                       const char *name,
                       const svn_string_t *value)
{
  printf("Got a revision prop.\n");

  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property (void *baton,
                   const char *name,
                   const svn_string_t *value)
{
  printf("Got a node prop.\n");

  return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext (svn_stream_t **stream,
              void *node_baton)
{
  printf ("Not interested in fulltext.\n");
  *stream = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_node (void *baton)
{
  printf ("End of node\n");
  return SVN_NO_ERROR;
}


static svn_error_t *
close_revision (void *baton)
{
  /* ### someday commit a txn here. */

  printf ("End of revision\n");
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
