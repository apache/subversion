/*
 * config_file.c :  parsing configuration files
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



#define APR_WANT_STDIO
#include <apr_want.h>

#include <apr_lib.h>
#include "config_impl.h"



/* File parsing context */
typedef struct parse_context_t
{
  /* This config struct and file */
  svn_config_t *cfg;
  const char *file;

  /* The file descriptor */
  FILE *fd;

  /* The current line in the file */
  int line;

  /* Temporary strings, allocated from the temp pool */
  svn_stringbuf_t *section;
  svn_stringbuf_t *option;
  svn_stringbuf_t *value;

  /* Temporary pool parsing */
  apr_pool_t *pool;
} parse_context_t;


/* Skip and count spaces */
static APR_INLINE int
skip_whitespace (FILE* fd, int *pcount)
{
  int ch = getc (fd);
  int count = 0;
  while (ch != EOF && ch != '\n' && apr_isspace (ch))
    {
      ++count;
      ch = getc (fd);
    }
  *pcount = count;
  return ch;
}


/* Skip to the end of the line (or file).  Returns the char that ended
   the line; the char is either EOF or newline. */
static APR_INLINE int
skip_to_eoln (FILE *fd)
{
  int ch = getc (fd);
  while (ch != EOF && ch != '\n')
    ch = getc (fd);
  return ch;
}


/* Parse a single option value */
static svn_error_t *
parse_value (int *pch, parse_context_t *ctx)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t end_of_val = FALSE;
  int ch;

  /* Read the first line of the value */
  svn_stringbuf_setempty (ctx->value);
  for (ch = getc (ctx->fd);   /* ### kff: huh, not gotten first char yet? */
       ch != EOF && ch != '\n';
       ch = getc (ctx->fd))
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes (ctx->value, &char_from_int, 1);
    }
  /* Leading and trailing whitespace is ignored. */
  svn_stringbuf_strip_whitespace (ctx->value);

  /* Look for any continuation lines. */
  for (;;)
    {
      if (ch == EOF || end_of_val)
        {
          if (!ferror (ctx->fd))
            {
              /* At end of file. The value is complete, there can't be
                 any continuation lines. */
              svn_config_set (ctx->cfg, ctx->section->data,
                              ctx->option->data, ctx->value->data);
            }
          break;
        }
      else
        {
          int count;
          ++ctx->line;
          getc (ctx->fd);       /* Eat the eoln */
          ch = skip_whitespace (ctx->fd, &count);

          switch (ch)
            {
            case '\n':
              /* The next line was empty. Ergo, it can't be a
                 continuation line. */
              ++ctx->line;
              ch = getc(ctx->fd);
              end_of_val = TRUE;
              continue;

            case EOF:
              /* This is also an empty line. */
              end_of_val = TRUE;
              continue;

            default:
              if (count == 0)
                {
                  /* This line starts in the first column.  That means
                     it's either a section, option or comment.  Put
                     the char back into the stream, because it doesn't
                     belong to us. */
                  ungetc (ch, ctx->fd);
                  end_of_val = TRUE;
                }
              else
                {
                  /* This is a continuation line. Read it. */
                  svn_stringbuf_appendbytes (ctx->value, " ", 1);

                  for (;
                       ch != EOF && ch != '\n';
                       ch = getc (ctx->fd))
                    {
                      const char char_from_int = ch;
                      svn_stringbuf_appendbytes (ctx->value,
                                                 &char_from_int, 1);
                    }
                  /* Trailing whitespace is ignored. */
                  svn_stringbuf_strip_whitespace (ctx->value);
                }
            }
        }
    }

  *pch = ch;
  return err;
}


/* Parse a single option */
static svn_error_t *
parse_option (int *pch, parse_context_t *ctx)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;

  svn_stringbuf_setempty (ctx->option);
  for (ch = *pch;               /* Yes, the first char is relevant. */
       ch != EOF && ch != ':' && ch != '=' && ch != '\n';
       ch = getc (ctx->fd))
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes (ctx->option, &char_from_int, 1);
    }

  if (ch != ':' && ch != '=')
    {
      ch = EOF;
      err = svn_error_createf (SVN_ERR_MALFORMED_FILE,
                               0, NULL, ctx->pool,
                               "%s:%d: Option must end with ':' or '='",
                               ctx->file, ctx->line);
    }
  else
    {
      /* Whitespace around the name separator is ignored. */
      svn_stringbuf_strip_whitespace (ctx->option);
      err = parse_value (&ch, ctx);
    }

  *pch = ch;
  return err;
}


/* Parse a single section */
static svn_error_t *
parse_section (int *pch, parse_context_t *ctx)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;

  svn_stringbuf_setempty (ctx->section);
  for (ch = getc (ctx->fd);
       ch != EOF && ch != ']' && ch != '\n';
       ch = getc (ctx->fd))
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes (ctx->section, &char_from_int, 1);
    }

  if (ch != ']')
    {
      ch = EOF;
      err = svn_error_createf (SVN_ERR_MALFORMED_FILE,
                               0, NULL, ctx->pool,
                               "%s:%d: Section header must end with ']'",
                               ctx->file, ctx->line);
    }
  else
    {
      /* Everything from the ']' to the end of the line is ignored. */
      ch = skip_to_eoln (ctx->fd);
      if (ch != EOF)
        ++ctx->line;
    }

  *pch = ch;
  return err;
}



/*** Exported interface. ***/

svn_error_t *
svn_config__parse_file (svn_config_t *cfg, const char *file,
                        svn_boolean_t must_exist)
{
  svn_error_t *err = SVN_NO_ERROR;
  parse_context_t ctx;
  int ch, count;
  /* "Why," you ask yourself, "is he using stdio FILE's instead of
     apr_file_t's?"  The answer is simple: newline translation.  For
     all that it has an APR_BINARY flag, APR doesn't do newline
     translation in files.  The only portable way I know to get
     translated text files is to use the standard stdio library. */

  FILE *fd = fopen (file, "rt");
  if (fd == NULL)
    {
      if (errno != ENOENT)
        return svn_error_createf (SVN_ERR_BAD_FILENAME,
                                  errno, NULL, cfg->pool,
                                  "Can't open config file \"%s\"", file);
      else if (must_exist && errno == ENOENT)
        return svn_error_createf (SVN_ERR_BAD_FILENAME,
                                  errno, NULL, cfg->pool,
                                  "Can't find config file \"%s\"", file);
      else
        return SVN_NO_ERROR;
    }

  ctx.cfg = cfg;
  ctx.file = file;
  ctx.fd = fd;
  ctx.line = 1;
  ctx.pool = svn_pool_create (cfg->pool);
  ctx.section = svn_stringbuf_create("", ctx.pool);
  ctx.option = svn_stringbuf_create("", ctx.pool);
  ctx.value = svn_stringbuf_create("", ctx.pool);

  do
    {
      ch = skip_whitespace (fd, &count);
      switch (ch)
        {
        case '[':               /* Start of section header */
          if (count == 0)
            err = parse_section (&ch, &ctx);
          else
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE,
                                       0, NULL, ctx.pool,
                                       "%s:%d: Section header"
                                       " must start in the first column",
                                       file, ctx.line);
            }
          break;

        case '#':               /* Comment */
          if (count == 0)
            ch = skip_to_eoln(fd);
          else
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE,
                                       0, NULL, ctx.pool,
                                       "%s:%d: Comment"
                                       " must start in the first column",
                                       file, ctx.line);
            }
          break;

        case '\n':              /* Empty line */
          ++ctx.line;
          ch = getc(fd);
          break;

        case EOF:               /* End of file or read error */
          break;

        default:
          if (svn_stringbuf_isempty (ctx.section))
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE,
                                       0, NULL, ctx.pool,
                                       "%s:%d: Section header expected",
                                       file, ctx.line);
            }
          else if (count != 0)
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE,
                                       0, NULL, ctx.pool,
                                       "%s:%d: Option expected",
                                       file, ctx.line);
            }
          else
            err = parse_option (&ch, &ctx);
          break;
        }
    }
  while (ch != EOF);

  if (ferror (fd))
    {
      err = svn_error_createf (-1, /* FIXME: Wrong error code. */
                               errno, NULL, ctx.pool,
                               "%s:%d: Read error", file, ctx.line);
    }

  svn_pool_destroy (ctx.pool);
  fclose (fd);
  return err;
}



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
