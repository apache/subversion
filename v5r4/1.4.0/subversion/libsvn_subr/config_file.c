/*
 * config_file.c :  parsing configuration files
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include <apr_lib.h>
#include <apr_md5.h>
#include <apr_env.h>
#include "config_impl.h"
#include "svn_io.h"
#include "svn_types.h"
#include "svn_path.h"
#include "svn_auth.h"
#include "svn_subst.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_user.h"

#include "svn_private_config.h"


/* File parsing context */
typedef struct parse_context_t
{
  /* This config struct and file */
  svn_config_t *cfg;
  const char *file;

  /* The file descriptor */
  svn_stream_t *stream;

  /* The current line in the file */
  int line;

  /* Cached ungotten character  - streams don't support ungetc()
     [emulate it] */
  int ungotten_char;
  svn_boolean_t have_ungotten_char;

  /* Temporary strings, allocated from the temp pool */
  svn_stringbuf_t *section;
  svn_stringbuf_t *option;
  svn_stringbuf_t *value;
} parse_context_t;



/* Emulate getc() because streams don't support it.
 *
 * In order to be able to ungetc(), use the CXT instead of the stream
 * to be able to store the 'ungotton' character.
 *
 */
static APR_INLINE svn_error_t *
parser_getc(parse_context_t *ctx, int *c)
{
  if (ctx->have_ungotten_char)
    {
      *c = ctx->ungotten_char;
      ctx->have_ungotten_char = FALSE;
    }
  else
    {
      char char_buf;
      apr_size_t readlen = 1;

      SVN_ERR(svn_stream_read(ctx->stream, &char_buf, &readlen));

      if (readlen == 1)
        *c = char_buf;
      else
        *c = EOF;
    }

  return SVN_NO_ERROR;
}

/* Emulate ungetc() because streams don't support it.
 *
 * Use CTX to store the ungotten character C.
 */
static APR_INLINE svn_error_t *
parser_ungetc(parse_context_t *ctx, int c)
{
  ctx->ungotten_char = c;
  ctx->have_ungotten_char = TRUE;

  return SVN_NO_ERROR;
}

/* Eat chars from STREAM until encounter non-whitespace, newline, or EOF.
   Set *PCOUNT to the number of characters eaten, not counting the
   last one, and return the last char read (the one that caused the
   break).  */
static APR_INLINE svn_error_t *
skip_whitespace(parse_context_t *ctx, int *c, int *pcount)
{
  int ch;
  int count = 0;

  SVN_ERR(parser_getc(ctx, &ch));
  while (ch != EOF && ch != '\n' && apr_isspace(ch))
    {
      ++count;
      SVN_ERR(parser_getc(ctx, &ch));
    }
  *pcount = count;
  *c = ch;
  return SVN_NO_ERROR;
}


/* Skip to the end of the line (or file).  Returns the char that ended
   the line; the char is either EOF or newline. */
static APR_INLINE svn_error_t *
skip_to_eoln(parse_context_t *ctx, int *c)
{
  int ch;

  SVN_ERR(parser_getc(ctx, &ch));
  while (ch != EOF && ch != '\n')
    SVN_ERR(parser_getc(ctx, &ch));

  *c = ch;
  return SVN_NO_ERROR;
}


/* Parse a single option value */
static svn_error_t *
parse_value(int *pch, parse_context_t *ctx)
{
  svn_boolean_t end_of_val = FALSE;
  int ch;

  /* Read the first line of the value */
  svn_stringbuf_setempty(ctx->value);
  SVN_ERR(parser_getc(ctx, &ch));
  while (ch != EOF && ch != '\n')
    /* last ch seen was ':' or '=' in parse_option. */
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes(ctx->value, &char_from_int, 1);
      SVN_ERR(parser_getc(ctx, &ch));
    }
  /* Leading and trailing whitespace is ignored. */
  svn_stringbuf_strip_whitespace(ctx->value);

  /* Look for any continuation lines. */
  for (;;)
    {

      if (ch == EOF || end_of_val)
        {
          /* At end of file. The value is complete, there can't be
             any continuation lines. */
          svn_config_set(ctx->cfg, ctx->section->data,
                         ctx->option->data, ctx->value->data);
          break;
        }
      else
        {
          int count;
          ++ctx->line;
          SVN_ERR(skip_whitespace(ctx, &ch, &count));

          switch (ch)
            {
            case '\n':
              /* The next line was empty. Ergo, it can't be a
                 continuation line. */
              ++ctx->line;
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
                  SVN_ERR(parser_ungetc(ctx, ch));
                  end_of_val = TRUE;
                }
              else
                {
                  /* This is a continuation line. Read it. */
                  svn_stringbuf_appendbytes(ctx->value, " ", 1);

                  while (ch != EOF && ch != '\n')
                    {
                      const char char_from_int = ch;
                      svn_stringbuf_appendbytes(ctx->value,
                                                &char_from_int, 1);
                      SVN_ERR(parser_getc(ctx, &ch));
                    }
                  /* Trailing whitespace is ignored. */
                  svn_stringbuf_strip_whitespace(ctx->value);
                }
            }
        }
    }

  *pch = ch;
  return SVN_NO_ERROR;
}


/* Parse a single option */
static svn_error_t *
parse_option(int *pch, parse_context_t *ctx, apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;

  svn_stringbuf_setempty(ctx->option);
  ch = *pch;   /* Yes, the first char is relevant. */
  while (ch != EOF && ch != ':' && ch != '=' && ch != '\n')
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes(ctx->option, &char_from_int, 1);
      SVN_ERR(parser_getc(ctx, &ch));
    }

  if (ch != ':' && ch != '=')
    {
      ch = EOF;
      err = svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                              "%s:%d: Option must end with ':' or '='",
                              svn_path_local_style(ctx->file, pool),
                              ctx->line);
    }
  else
    {
      /* Whitespace around the name separator is ignored. */
      svn_stringbuf_strip_whitespace(ctx->option);
      err = parse_value(&ch, ctx);
    }

  *pch = ch;
  return err;
}


/* Read chars until enounter ']', then skip everything to the end of
 * the line.  Set *PCH to the character that ended the line (either
 * newline or EOF), and set CTX->section to the string of characters
 * seen before ']'.
 * 
 * This is meant to be called immediately after reading the '[' that
 * starts a section name.
 */
static svn_error_t *
parse_section_name(int *pch, parse_context_t *ctx, apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;

  svn_stringbuf_setempty(ctx->section);
  SVN_ERR(parser_getc(ctx, &ch));
  while (ch != EOF && ch != ']' && ch != '\n')
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes(ctx->section, &char_from_int, 1);
      SVN_ERR(parser_getc(ctx, &ch));
    }

  if (ch != ']')
    {
      ch = EOF;
      err = svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                              "%s:%d: Section header must end with ']'",
                              svn_path_local_style(ctx->file, pool),
                              ctx->line);
    }
  else
    {
      /* Everything from the ']' to the end of the line is ignored. */
      SVN_ERR(skip_to_eoln(ctx, &ch));
      if (ch != EOF)
        ++ctx->line;
    }

  *pch = ch;
  return err;
}


svn_error_t *
svn_config__sys_config_path(const char **path_p,
                            const char *fname,
                            apr_pool_t *pool)
{
  /* ### This never actually returns error in practice.  Perhaps the
     prototype should change? */

  *path_p = NULL;

  /* Note that even if fname is null, svn_path_join_many will DTRT. */

#ifdef WIN32
  {
    const char *folder;
    SVN_ERR(svn_config__win_config_path(&folder, TRUE, pool));
    *path_p = svn_path_join_many(pool, folder,
                                 SVN_CONFIG__SUBDIRECTORY, fname, NULL);
  }

#else  /* ! WIN32 */

  *path_p = svn_path_join_many(pool, SVN_CONFIG__SYS_DIRECTORY, fname, NULL);

#endif /* WIN32 */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_config__user_config_path(const char *config_dir,
                             const char **path_p,
                             const char *fname,
                             apr_pool_t *pool)
{
  /* ### This never actually returns error in practice.  Perhaps the
     prototype should change? */

  *path_p = NULL;

  /* Note that even if fname is null, svn_path_join_many will DTRT. */

  if (config_dir)
    {
      *path_p = svn_path_join_many(pool, config_dir, fname, NULL);
      return SVN_NO_ERROR;
    }
  
#ifdef WIN32
  {
    const char *folder;
    SVN_ERR(svn_config__win_config_path(&folder, FALSE, pool));
    *path_p = svn_path_join_many(pool, folder,
                                 SVN_CONFIG__SUBDIRECTORY, fname, NULL);
  }

#else  /* ! WIN32 */
  {
    const char *homedir = svn_user_get_homedir(pool); 
    if (! homedir)
      return SVN_NO_ERROR;
    *path_p = svn_path_join_many(pool,
                                 svn_path_canonicalize(homedir, pool),
                                 SVN_CONFIG__USR_DIRECTORY, fname, NULL);
  }
#endif /* WIN32 */

  return SVN_NO_ERROR;
}



/*** Exported interfaces. ***/


svn_error_t *
svn_config__parse_file(svn_config_t *cfg, const char *file,
                       svn_boolean_t must_exist, apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  parse_context_t ctx;
  int ch, count;
  apr_file_t *f;

  /* No need for buffering; a translated stream buffers */
  err = svn_io_file_open(&f, file, APR_BINARY | APR_READ,
                         APR_OS_DEFAULT, pool);

  if (! must_exist && err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  ctx.cfg = cfg;
  ctx.file = file;
  ctx.stream = svn_subst_stream_translated(svn_stream_from_aprfile(f, pool),
                                           "\n", TRUE, NULL, FALSE, pool);
  ctx.line = 1;
  ctx.have_ungotten_char = FALSE;
  ctx.section = svn_stringbuf_create("", pool);
  ctx.option = svn_stringbuf_create("", pool);
  ctx.value = svn_stringbuf_create("", pool);

  do
    {
      SVN_ERR(skip_whitespace(&ctx, &ch, &count));

      switch (ch)
        {
        case '[':               /* Start of section header */
          if (count == 0)
            SVN_ERR(parse_section_name(&ch, &ctx, pool));
          else
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     "%s:%d: Section header"
                                     " must start in the first column",
                                     svn_path_local_style(file, pool),
                                     ctx.line);
          break;

        case '#':               /* Comment */
          if (count == 0)
            {
              SVN_ERR(skip_to_eoln(&ctx, &ch));
              ++ctx.line;
            }
          else
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     "%s:%d: Comment"
                                     " must start in the first column",
                                     svn_path_local_style(file, pool),
                                     ctx.line);
          break;

        case '\n':              /* Empty line */
          ++ctx.line;
          break;

        case EOF:               /* End of file or read error */
          break;

        default:
          if (svn_stringbuf_isempty(ctx.section))
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     "%s:%d: Section header expected",
                                     svn_path_local_style(file, pool),
                                     ctx.line);
          else if (count != 0)
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     "%s:%d: Option expected",
                                     svn_path_local_style(file, pool),
                                     ctx.line);
          else
            SVN_ERR(parse_option(&ch, &ctx, pool));
          break;
        }
    }
  while (ch != EOF);

  /* Close the file and streams (and other cleanup): */
  SVN_ERR(svn_stream_close(ctx.stream));
  SVN_ERR(svn_io_file_close(f, pool));

  return SVN_NO_ERROR;
}


/* Helper for svn_config_ensure:  see if ~/.subversion/auth/ and its
   subdirs exist, try to create them, but don't throw errors on
   failure.  PATH is assumed to be a path to the user's private config
   directory. */
static void
ensure_auth_dirs(const char *path,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *auth_dir, *auth_subdir;
  svn_error_t *err;

  /* Ensure ~/.subversion/auth/ */
  auth_dir = svn_path_join_many(pool, path, SVN_CONFIG__AUTH_SUBDIR, NULL);
  err = svn_io_check_path(auth_dir, &kind, pool);
  if (err || kind == svn_node_none)
    {
      svn_error_clear(err);
      /* 'chmod 700' permissions: */
      err = svn_io_dir_make(auth_dir,
                            (APR_UREAD | APR_UWRITE | APR_UEXECUTE),
                            pool);
      if (err)
        {
          /* Don't try making subdirs if we can't make the top-level dir. */
          svn_error_clear(err);
          return;
        }
    }

  /* If a provider exists that wants to store credentials in
     ~/.subversion, a subdirectory for the cred_kind must exist. */

  auth_subdir = svn_path_join_many(pool, auth_dir,
                                   SVN_AUTH_CRED_SIMPLE, NULL);
  err = svn_io_check_path(auth_subdir, &kind, pool);
  if (err || kind == svn_node_none)
    {
      svn_error_clear(err);
      svn_error_clear(svn_io_dir_make(auth_subdir, APR_OS_DEFAULT, pool));
    }
      
  auth_subdir = svn_path_join_many(pool, auth_dir,
                                   SVN_AUTH_CRED_USERNAME, NULL);
  err = svn_io_check_path(auth_subdir, &kind, pool);
  if (err || kind == svn_node_none)
    {
      svn_error_clear(err);
      svn_error_clear(svn_io_dir_make(auth_subdir, APR_OS_DEFAULT, pool));
    }

  auth_subdir = svn_path_join_many(pool, auth_dir,
                                   SVN_AUTH_CRED_SSL_SERVER_TRUST, NULL);
  err = svn_io_check_path(auth_subdir, &kind, pool);
  if (err || kind == svn_node_none)
    {
      svn_error_clear(err);
      svn_error_clear(svn_io_dir_make(auth_subdir, APR_OS_DEFAULT, pool));
    }
}


svn_error_t *
svn_config_ensure(const char *config_dir, apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;
  svn_error_t *err;

  /* Ensure that the user-specific config directory exists.  */
  SVN_ERR(svn_config__user_config_path(config_dir, &path, NULL, pool));

  if (! path)
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      /* Don't throw an error, but don't continue. */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (kind == svn_node_none)
    {
      err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);
      if (err)
        {
          /* Don't throw an error, but don't continue. */
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
    }
  else
    {
      /* ### config directory already exists, but for the sake of
         smooth upgrades, try to ensure that the auth/ subdirs exist
         as well.  we can remove this check someday in the future. */
      ensure_auth_dirs(path, pool);

      return SVN_NO_ERROR;
    }

  /* Else, there's a configuration directory. */

  /* If we get errors trying to do things below, just stop and return
     success.  There's no _need_ to init a config directory if
     something's preventing it. */

  /** If non-existent, try to create a number of auth/ subdirectories. */
  ensure_auth_dirs(path, pool);

  /** Ensure that the `README.txt' file exists. **/
  SVN_ERR(svn_config__user_config_path
          (config_dir, &path, SVN_CONFIG__USR_README_FILE, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
   "This directory holds run-time configuration information for Subversion"
   APR_EOL_STR
   "clients.  The configuration files all share the same syntax, but you"
   APR_EOL_STR
   "should examine a particular file to learn what configuration"
   APR_EOL_STR
   "directives are valid for that file."
   APR_EOL_STR
   APR_EOL_STR
   "The syntax is standard INI format:"
   APR_EOL_STR
   APR_EOL_STR
   "   - Empty lines, and lines starting with '#', are ignored."
   APR_EOL_STR
   "     The first significant line in a file must be a section header."
   APR_EOL_STR
   APR_EOL_STR
   "   - A section starts with a section header, which must start in"
   APR_EOL_STR
   "     the first column:"
   APR_EOL_STR
   APR_EOL_STR
   "       [section-name]"
   APR_EOL_STR
   APR_EOL_STR
   "   - An option, which must always appear within a section, is a pair"
   APR_EOL_STR
   "     (name, value).  There are two valid forms for defining an"
   APR_EOL_STR
   "     option, both of which must start in the first column:"
   APR_EOL_STR
   APR_EOL_STR
   "       name: value"
   APR_EOL_STR
   "       name = value"
   APR_EOL_STR
   APR_EOL_STR
   "     Whitespace around the separator (:, =) is optional."
   APR_EOL_STR
   APR_EOL_STR
   "   - Section and option names are case-insensitive, but case is"
   APR_EOL_STR
   "     preserved."
   APR_EOL_STR
   APR_EOL_STR
   "   - An option's value may be broken into several lines.  The value"
   APR_EOL_STR
   "     continuation lines must start with at least one whitespace."
   APR_EOL_STR
   "     Trailing whitespace in the previous line, the newline character"
   APR_EOL_STR
   "     and the leading whitespace in the continuation line is compressed"
   APR_EOL_STR
   "     into a single space character."
   APR_EOL_STR
   APR_EOL_STR
   "   - All leading and trailing whitespace around a value is trimmed,"
   APR_EOL_STR
   "     but the whitespace within a value is preserved, with the"
   APR_EOL_STR
   "     exception of whitespace around line continuations, as"
   APR_EOL_STR
   "     described above."
   APR_EOL_STR
   APR_EOL_STR
   "   - When a value is a boolean, any of the following strings are"
   APR_EOL_STR
   "     recognised as truth values (case does not matter):"
   APR_EOL_STR
   APR_EOL_STR
   "       true      false"
   APR_EOL_STR
   "       yes       no"
   APR_EOL_STR
   "       on        off"
   APR_EOL_STR
   "       1         0"
   APR_EOL_STR
   APR_EOL_STR
   "   - When a value is a list, it is comma-separated.  Again, the"
   APR_EOL_STR
   "     whitespace around each element of the list is trimmed."
   APR_EOL_STR
   APR_EOL_STR
   "   - Option values may be expanded within a value by enclosing the"
   APR_EOL_STR
   "     option name in parentheses, preceded by a percent sign and"
   APR_EOL_STR
   "     followed by an 's':"
   APR_EOL_STR
   APR_EOL_STR
   "       %(name)s"
   APR_EOL_STR
   APR_EOL_STR
   "     The expansion is performed recursively and on demand, during"
   APR_EOL_STR
   "     svn_option_get.  The name is first searched for in the same"
   APR_EOL_STR
   "     section, then in the special [DEFAULT] section. If the name"
   APR_EOL_STR
   "     is not found, the whole '%(name)s' placeholder is left"
   APR_EOL_STR
   "     unchanged."
   APR_EOL_STR
   APR_EOL_STR
   "     Any modifications to the configuration data invalidate all"
   APR_EOL_STR
   "     previously expanded values, so that the next svn_option_get"
   APR_EOL_STR
   "     will take the modifications into account."
   APR_EOL_STR
   APR_EOL_STR
   "The syntax of the configuration files is a subset of the one used by"
   APR_EOL_STR
   "Python's ConfigParser module; see"
   APR_EOL_STR
   APR_EOL_STR
   "   http://www.python.org/doc/current/lib/module-ConfigParser.html"
   APR_EOL_STR
   APR_EOL_STR
   "Configuration data in the Windows registry"
   APR_EOL_STR
   "=========================================="
   APR_EOL_STR
   APR_EOL_STR
   "On Windows, configuration data may also be stored in the registry.  The"
   APR_EOL_STR
   "functions svn_config_read and svn_config_merge will read from the"
   APR_EOL_STR
   "registry when passed file names of the form:"
   APR_EOL_STR
   APR_EOL_STR
   "   REGISTRY:<hive>/path/to/config-key"
   APR_EOL_STR
   APR_EOL_STR
   "The REGISTRY: prefix must be in upper case. The <hive> part must be"
   APR_EOL_STR
   "one of:"
   APR_EOL_STR
   APR_EOL_STR
   "   HKLM for HKEY_LOCAL_MACHINE"
   APR_EOL_STR
   "   HKCU for HKEY_CURRENT_USER"
   APR_EOL_STR
   APR_EOL_STR
   "The values in config-key represent the options in the [DEFAULT] section."
   APR_EOL_STR
   "The keys below config-key represent other sections, and their values"
   APR_EOL_STR
   "represent the options. Only values of type REG_SZ whose name doesn't"
   APR_EOL_STR
   "start with a '#' will be used; other values, as well as the keys'"
   APR_EOL_STR
   "default values, will be ignored."
   APR_EOL_STR
   APR_EOL_STR
   APR_EOL_STR
   "File locations"
   APR_EOL_STR
   "=============="
   APR_EOL_STR
   APR_EOL_STR
   "Typically, Subversion uses two config directories, one for site-wide"
   APR_EOL_STR
   "configuration,"
   APR_EOL_STR
   APR_EOL_STR
   "  Unix:"
   APR_EOL_STR
   "    /etc/subversion/servers"
   APR_EOL_STR
   "    /etc/subversion/config"
   APR_EOL_STR
   "    /etc/subversion/hairstyles"
   APR_EOL_STR
   "  Windows:"
   APR_EOL_STR
   "    %ALLUSERSPROFILE%\\Application Data\\Subversion\\servers"
   APR_EOL_STR
   "    %ALLUSERSPROFILE%\\Application Data\\Subversion\\config"
   APR_EOL_STR
   "    %ALLUSERSPROFILE%\\Application Data\\Subversion\\hairstyles"
   APR_EOL_STR
   "    REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Servers"
   APR_EOL_STR
   "    REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Config"
   APR_EOL_STR
   "    REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Hairstyles"
   APR_EOL_STR
   APR_EOL_STR
   "and one for per-user configuration:"
   APR_EOL_STR
   APR_EOL_STR
   "  Unix:"
   APR_EOL_STR
   "    ~/.subversion/servers"
   APR_EOL_STR
   "    ~/.subversion/config"
   APR_EOL_STR
   "    ~/.subversion/hairstyles"
   APR_EOL_STR
   "  Windows:"
   APR_EOL_STR
   "    %APPDATA%\\Subversion\\servers"
   APR_EOL_STR
   "    %APPDATA%\\Subversion\\config"
   APR_EOL_STR
   "    %APPDATA%\\Subversion\\hairstyles"
   APR_EOL_STR
   "    REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Servers"
   APR_EOL_STR
   "    REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Config"
   APR_EOL_STR
   "    REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Hairstyles"
   APR_EOL_STR
   APR_EOL_STR;

      err = svn_io_file_open(&f, path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);

      if (! err)
        {
          SVN_ERR(svn_io_file_write_full(f, contents, 
                                         strlen(contents), NULL, pool));
          SVN_ERR(svn_io_file_close(f, pool));
        }

      svn_error_clear(err);
    }

  /** Ensure that the `servers' file exists. **/
  SVN_ERR(svn_config__user_config_path
          (config_dir, &path, SVN_CONFIG_CATEGORY_SERVERS, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  
  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
        "### This file specifies server-specific protocol parameters,"
        APR_EOL_STR
        "### including HTTP proxy information, and HTTP timeout settings."
        APR_EOL_STR
        "###"
        APR_EOL_STR
        "### The currently defined server options are:"
        APR_EOL_STR
        "###   http-proxy-host            Proxy host for HTTP connection"
        APR_EOL_STR
        "###   http-proxy-port            Port number of proxy host service"
        APR_EOL_STR
        "###   http-proxy-username        Username for auth to proxy service"
        APR_EOL_STR
        "###   http-proxy-password        Password for auth to proxy service"
        APR_EOL_STR
        "###   http-proxy-exceptions      List of sites that do not use proxy"
        APR_EOL_STR
        "###   http-timeout               Timeout for HTTP requests in seconds"
        APR_EOL_STR
        "###   http-compression           Whether to compress HTTP requests"
        APR_EOL_STR
        "###   neon-debug-mask            Debug mask for Neon HTTP library"
        APR_EOL_STR
        "###   ssl-authority-files        List of files, each of a trusted CAs"
        APR_EOL_STR
        "###   ssl-trust-default-ca       Trust the system 'default' CAs" 
        APR_EOL_STR
        "###   ssl-client-cert-file       PKCS#12 format client "
        "certificate file"
        APR_EOL_STR
        "###   ssl-client-cert-password   Client Key password, if needed."
        APR_EOL_STR
        "###"
        APR_EOL_STR
        "### HTTP timeouts, if given, are specified in seconds.  A timeout"
        APR_EOL_STR
        "### of 0, i.e. zero, causes a builtin default to be used."
        APR_EOL_STR
        "###"
        APR_EOL_STR
        "### The commented-out examples below are intended only to"
        APR_EOL_STR
        "### demonstrate how to use this file; any resemblance to actual"
        APR_EOL_STR
        "### servers, living or dead, is entirely coincidental."
        APR_EOL_STR
        APR_EOL_STR
        "### In this section, the URL of the repository you're trying to"
        APR_EOL_STR
        "### access is matched against the patterns on the right.  If a"
        APR_EOL_STR
        "### match is found, the server info is from the section with the"
        APR_EOL_STR
        "### corresponding name."
        APR_EOL_STR
        APR_EOL_STR
        "[groups]"
        APR_EOL_STR
        "# group1 = *.collab.net"
        APR_EOL_STR
        "# othergroup = repository.blarggitywhoomph.com"
        APR_EOL_STR
        "# thirdgroup = *.example.com"
        APR_EOL_STR
        APR_EOL_STR
        "### Information for the first group:"
        APR_EOL_STR
        "# [group1]"
        APR_EOL_STR
"# http-proxy-host = proxy1.some-domain-name.com"
        APR_EOL_STR
        "# http-proxy-port = 80"
        APR_EOL_STR
        "# http-proxy-username = blah"
        APR_EOL_STR
        "# http-proxy-password = doubleblah"
        APR_EOL_STR
        "# http-timeout = 60"
        APR_EOL_STR
        "# neon-debug-mask = 130"
        APR_EOL_STR
        ""
        APR_EOL_STR
        "### Information for the second group:"
        APR_EOL_STR
        "# [othergroup]"
        APR_EOL_STR
        "# http-proxy-host = proxy2.some-domain-name.com"
        APR_EOL_STR
        "# http-proxy-port = 9000"
        APR_EOL_STR
        "# No username and password, so use the defaults below."
        APR_EOL_STR
        ""
        APR_EOL_STR
        "### You can set default parameters in the 'global' section."
        APR_EOL_STR
        "### These parameters apply if no corresponding parameter is set in"
        APR_EOL_STR
        "### a specifically matched group as shown above.  Thus, if you go"
        APR_EOL_STR
        "### through the same proxy server to reach every site on the"
        APR_EOL_STR
        "### Internet, you probably just want to put that server's"
        APR_EOL_STR
        "### information in the 'global' section and not bother with"
        APR_EOL_STR
        "### 'groups' or any other sections."
        APR_EOL_STR
        "###"
        APR_EOL_STR
        "### If you go through a proxy for all but a few sites, you can"
        APR_EOL_STR
        "### list those exceptions under 'http-proxy-exceptions'.  This only"
        APR_EOL_STR
        "### overrides defaults, not explicitly matched server names."
        APR_EOL_STR
        "###"
        APR_EOL_STR
        "### 'ssl-authority-files' is a semicolon-delimited list of files,"
        APR_EOL_STR
        "### each pointing to a PEM-encoded Certificate Authority (CA) "
        APR_EOL_STR
        "### SSL certificate.  See details above for overriding security "
        APR_EOL_STR
        "### due to SSL."
        APR_EOL_STR
        "[global]"
        APR_EOL_STR
        "# http-proxy-exceptions = *.exception.com, www.internal-site.org"
        APR_EOL_STR
        "# http-proxy-host = defaultproxy.whatever.com"
        APR_EOL_STR
        "# http-proxy-port = 7000"
        APR_EOL_STR
        "# http-proxy-username = defaultusername"
        APR_EOL_STR
        "# http-proxy-password = defaultpassword"
        APR_EOL_STR
        "# http-compression = no"
        APR_EOL_STR
        "# No http-timeout, so just use the builtin default."
        APR_EOL_STR
        "# No neon-debug-mask, so neon debugging is disabled."
        APR_EOL_STR
        "# ssl-authority-files = /path/to/CAcert.pem;/path/to/CAcert2.pem"
        APR_EOL_STR;

      err = svn_io_file_open(&f, path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);

      if (! err)
        {
          SVN_ERR(svn_io_file_write_full(f, contents, 
                                         strlen(contents), NULL, pool));
          SVN_ERR(svn_io_file_close(f, pool));
        }

      svn_error_clear(err);
    }

  /** Ensure that the `config' file exists. **/
  SVN_ERR(svn_config__user_config_path
          (config_dir, &path, SVN_CONFIG_CATEGORY_CONFIG, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  
  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
        "### This file configures various client-side behaviors."
        APR_EOL_STR
        "###"
        APR_EOL_STR
        "### The commented-out examples below are intended to demonstrate"
        APR_EOL_STR
        "### how to use this file."
        APR_EOL_STR
        APR_EOL_STR
        "### Section for authentication and authorization customizations."
        APR_EOL_STR
        "[auth]"
        APR_EOL_STR
        "### Set store-passwords to 'no' to avoid storing passwords in the"
        APR_EOL_STR
        "### auth/ area of your config directory.  It defaults to 'yes'."
        APR_EOL_STR
        "### Note that this option only prevents saving of *new* passwords;"
        APR_EOL_STR
        "### it doesn't invalidate existing passwords.  (To do that, remove"
        APR_EOL_STR
        "### the cache files by hand as described in the Subversion book.)"
        APR_EOL_STR
        "# store-passwords = no"
        APR_EOL_STR
        "### Set store-auth-creds to 'no' to avoid storing any subversion"
        APR_EOL_STR
        "### credentials in the auth/ area of your config directory."
        APR_EOL_STR
        "### It defaults to 'yes'.  Note that this option only prevents"
        APR_EOL_STR
        "### saving of *new* credentials;  it doesn't invalidate existing"
        APR_EOL_STR
        "### caches.  (To do that, remove the cache files by hand.)"
        APR_EOL_STR
        "# store-auth-creds = no"
        APR_EOL_STR
        APR_EOL_STR
        "### Section for configuring external helper applications."
        APR_EOL_STR
        "[helpers]"
        APR_EOL_STR
        "### Set editor to the command used to invoke your text editor."
        APR_EOL_STR
        "###   This will override the environment variables that Subversion"
        APR_EOL_STR
        "###   examines by default to find this information ($EDITOR, "
        APR_EOL_STR
        "###   et al)."
        APR_EOL_STR
        "# editor-cmd = editor (vi, emacs, notepad, etc.)"
        APR_EOL_STR
        "### Set diff-cmd to the absolute path of your 'diff' program."
        APR_EOL_STR
        "###   This will override the compile-time default, which is to use"
        APR_EOL_STR
        "###   Subversion's internal diff implementation."
        APR_EOL_STR
        "# diff-cmd = diff_program (diff, gdiff, etc.)"
        APR_EOL_STR
        "### Set diff3-cmd to the absolute path of your 'diff3' program."
        APR_EOL_STR
        "###   This will override the compile-time default, which is to use"
        APR_EOL_STR
        "###   Subversion's internal diff3 implementation."
        APR_EOL_STR
        "# diff3-cmd = diff3_program (diff3, gdiff3, etc.)"
        APR_EOL_STR
        "### Set diff3-has-program-arg to 'true' or 'yes' if your 'diff3'"
        APR_EOL_STR
        "###   program accepts the '--diff-program' option."
        APR_EOL_STR
        "# diff3-has-program-arg = [true | false]"
        APR_EOL_STR
        APR_EOL_STR
        "### Section for configuring tunnel agents."
        APR_EOL_STR
        "[tunnels]"
        APR_EOL_STR
        "### Configure svn protocol tunnel schemes here.  By default, only"
        APR_EOL_STR
        "### the 'ssh' scheme is defined.  You can define other schemes to"
        APR_EOL_STR
        "### be used with 'svn+scheme://hostname/path' URLs.  A scheme"
        APR_EOL_STR
        "### definition is simply a command, optionally prefixed by an"
        APR_EOL_STR
        "### environment variable name which can override the command if it"
        APR_EOL_STR
        "### is defined.  The command (or environment variable) may contain"
        APR_EOL_STR
        "### arguments, using standard shell quoting for arguments with"
        APR_EOL_STR
        "### spaces.  The command will be invoked as:"
        APR_EOL_STR
        "###   <command> <hostname> svnserve -t"
        APR_EOL_STR
        "### (If the URL includes a username, then the hostname will be"
        APR_EOL_STR
        "### passed to the tunnel agent as <user>@<hostname>.)  If the"
        APR_EOL_STR
        "### built-in ssh scheme were not predefined, it could be defined"
        APR_EOL_STR
        "### as:"
        APR_EOL_STR
        "# ssh = $SVN_SSH ssh"
        APR_EOL_STR
        "### If you wanted to define a new 'rsh' scheme, to be used with"
        APR_EOL_STR
        "### 'svn+rsh:' URLs, you could do so as follows:"
        APR_EOL_STR
        "# rsh = rsh"
        APR_EOL_STR
        "### Or, if you wanted to specify a full path and arguments:"
        APR_EOL_STR
        "# rsh = /path/to/rsh -l myusername"
        APR_EOL_STR
        "### On Windows, if you are specifying a full path to a command,"
        APR_EOL_STR
        "### use a forward slash (/) or a paired backslash (\\\\) as the"
        APR_EOL_STR
        "### path separator.  A single backslash will be treated as an"
        APR_EOL_STR
        "### escape for the following character." 
        APR_EOL_STR
        APR_EOL_STR
        "### Section for configuring miscelleneous Subversion options."
        APR_EOL_STR
        "[miscellany]"
        APR_EOL_STR
        "### Set global-ignores to a set of whitespace-delimited globs"
        APR_EOL_STR
        "### which Subversion will ignore in its 'status' output, and"
        APR_EOL_STR
        "### while importing or adding files and directories."
        APR_EOL_STR
        "# global-ignores = " SVN_CONFIG_DEFAULT_GLOBAL_IGNORES ""
        APR_EOL_STR
        "### Set log-encoding to the default encoding for log messages"
        APR_EOL_STR
        "# log-encoding = latin1"
        APR_EOL_STR
        "### Set use-commit-times to make checkout/update/switch/revert"
        APR_EOL_STR
        "### put last-committed timestamps on every file touched."
        APR_EOL_STR
        "# use-commit-times = yes"
        APR_EOL_STR
        "### Set no-unlock to prevent 'svn commit' from automatically"
        APR_EOL_STR
        "### releasing locks on files."
        APR_EOL_STR
        "# no-unlock = yes"
        APR_EOL_STR
        "### Set enable-auto-props to 'yes' to enable automatic properties"
        APR_EOL_STR
        "### for 'svn add' and 'svn import', it defaults to 'no'."
        APR_EOL_STR
        "### Automatic properties are defined in the section 'auto-props'."
        APR_EOL_STR
        "# enable-auto-props = yes"
        APR_EOL_STR
        APR_EOL_STR
        "### Section for configuring automatic properties."
        APR_EOL_STR
        "[auto-props]"
        APR_EOL_STR
        "### The format of the entries is:"
        APR_EOL_STR
        "###   file-name-pattern = propname[=value][;propname[=value]...]"
        APR_EOL_STR
        "### The file-name-pattern can contain wildcards (such as '*' and"
        APR_EOL_STR
        "### '?').  All entries which match will be applied to the file."
        APR_EOL_STR
        "### Note that auto-props functionality must be enabled, which"
        APR_EOL_STR
        "### is typically done by setting the 'enable-auto-props' option."
        APR_EOL_STR
        "# *.c = svn:eol-style=native"
        APR_EOL_STR
        "# *.cpp = svn:eol-style=native"
        APR_EOL_STR
        "# *.h = svn:eol-style=native"
        APR_EOL_STR
        "# *.dsp = svn:eol-style=CRLF"
        APR_EOL_STR
        "# *.dsw = svn:eol-style=CRLF"
        APR_EOL_STR
        "# *.sh = svn:eol-style=native;svn:executable"
        APR_EOL_STR
        "# *.txt = svn:eol-style=native"
        APR_EOL_STR
        "# *.png = svn:mime-type=image/png"
        APR_EOL_STR
        "# *.jpg = svn:mime-type=image/jpeg"
        APR_EOL_STR
        "# Makefile = svn:eol-style=native"
        APR_EOL_STR
        APR_EOL_STR;
        
      err = svn_io_file_open(&f, path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);

      if (! err)
        {
          SVN_ERR(svn_io_file_write_full(f, contents, 
                                         strlen(contents), NULL, pool));
          SVN_ERR(svn_io_file_close(f, pool));
        }

      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}
