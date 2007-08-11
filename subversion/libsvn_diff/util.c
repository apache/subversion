/*
 * util.c :  routines for doing diffs
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


#include <apr.h>
#include <apr_general.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"
#include "svn_ctype.h"

#include "diff.h"

/**
 * An Adler-32 implementation per RFC1950.
 *
 * "The Adler-32 algorithm is much faster than the CRC32 algorithm yet
 * still provides an extremely low probability of undetected errors"
 */
 
/*
 * 65521 is the largest prime less than 65536.
 * "That 65521 is prime is important to avoid a possible large class of
 *  two-byte errors that leave the check unchanged."
 */
#define ADLER_MOD_BASE 65521

/*
 * "The modulo on unsigned long accumulators can be delayed for 5552 bytes,
 *  so the modulo operation time is negligible."
 */
#define ADLER_MOD_BLOCK_SIZE 5552


/*
 * Start with CHECKSUM and update the checksum by processing a chunk
 * of DATA sized LEN.
 */
apr_uint32_t
svn_diff__adler32(apr_uint32_t checksum, const char *data, apr_size_t len)
{
  const unsigned char *input = (const unsigned char *)data;
  apr_uint32_t s1 = checksum & 0xFFFF;
  apr_uint32_t s2 = checksum >> 16;
  apr_uint32_t b;
  apr_size_t blocks = len / ADLER_MOD_BLOCK_SIZE;
  
  len %= ADLER_MOD_BLOCK_SIZE;

  while (blocks--)
    {
      int count = ADLER_MOD_BLOCK_SIZE;
      while (count--)
        {
          b = *input++;
          s1 += b;
          s2 += s1;
        }

      s1 %= ADLER_MOD_BASE;
      s2 %= ADLER_MOD_BASE;
    }

  while (len--)
    {
      b = *input++;
      s1 += b;
      s2 += s1;
    }

  return ((s2 % ADLER_MOD_BASE) << 16) | (s1 % ADLER_MOD_BASE);
}


svn_boolean_t
svn_diff_contains_conflicts(svn_diff_t *diff)
{
  while (diff != NULL)
    {
      if (diff->type == svn_diff__type_conflict)
        {
          return TRUE;
        }

      diff = diff->next;
    }

  return FALSE;
}

svn_boolean_t
svn_diff_contains_diffs(svn_diff_t *diff)
{
  while (diff != NULL)
    {
      if (diff->type != svn_diff__type_common)
        {
          return TRUE;
        }

      diff = diff->next;
    }

  return FALSE;
}

svn_error_t *
svn_diff_output(svn_diff_t *diff,
                void *output_baton,
                const svn_diff_output_fns_t *vtable)
{
  svn_error_t *(*output_fn)(void *,
                            apr_off_t, apr_off_t,
                            apr_off_t, apr_off_t,
                            apr_off_t, apr_off_t);

  while (diff != NULL)
    {
      switch (diff->type)
        {
        case svn_diff__type_common:
          output_fn = vtable->output_common;
          break;

        case svn_diff__type_diff_common:
          output_fn = vtable->output_diff_common;
          break;

        case svn_diff__type_diff_modified:
          output_fn = vtable->output_diff_modified;
          break;

        case svn_diff__type_diff_latest:
          output_fn = vtable->output_diff_latest;
          break;

        case svn_diff__type_conflict:
          output_fn = NULL;
          if (vtable->output_conflict != NULL)
            {
              SVN_ERR(vtable->output_conflict(output_baton,
                               diff->original_start, diff->original_length,
                               diff->modified_start, diff->modified_length,
                               diff->latest_start, diff->latest_length,
                               diff->resolved_diff));
            }
          break;

        default:
          output_fn = NULL;
          break;
        }

      if (output_fn != NULL)
        {
          SVN_ERR(output_fn(output_baton,
                            diff->original_start, diff->original_length,
                            diff->modified_start, diff->modified_length,
                            diff->latest_start, diff->latest_length));
        }

      diff = diff->next;
    }

  return SVN_NO_ERROR;
}


void
svn_diff__normalize_buffer(char *buf,
                           apr_off_t *lengthp,
                           svn_diff__normalize_state_t *statep,
                           const svn_diff_file_options_t *opts)
{
  char *curp, *endp;
  /* Start of next chunk to copy. */
  char *start = buf;
  /* The current end of the normalized buffer. */
  char *newend = buf;
  svn_diff__normalize_state_t state = *statep;

  /* If this is a noop, then just get out of here. */
  if (! opts->ignore_space && ! opts->ignore_eol_style)
    return;

  for (curp = buf, endp = buf + *lengthp; curp != endp; ++curp)
    {
      switch (state)
        {
        case svn_diff__normalize_state_cr:
          state = svn_diff__normalize_state_normal;
          if (*curp == '\n' && opts->ignore_eol_style)
            {
              start = curp + 1;
              break;
            }
          /* Else, fall through. */
        case svn_diff__normalize_state_normal:
          if (svn_ctype_isspace(*curp))
            {
              /* Flush non-ws characters. */
              if (newend != start)
                memmove(newend, start, curp - start);
              newend += curp - start;
              start = curp;
              switch (*curp)
                {
                case '\r':
                  state = svn_diff__normalize_state_cr;
                  if (opts->ignore_eol_style)
                    {
                      /* Replace this CR with an LF; if we're followed by an
                         LF, that will be ignored. */
                      *newend++ = '\n';
                      ++start;
                    }
                  break;
                case '\n':
                  break;
                default:
                  /* Some other whitespace character. */
                  if (opts->ignore_space)
                    {
                      state = svn_diff__normalize_state_whitespace;
                      if (opts->ignore_space
                          == svn_diff_file_ignore_space_change)
                        *newend++ = ' ';
                    }
                  break;
                }
            }
          break;
        case svn_diff__normalize_state_whitespace:
          /* This is only entered if we're ignoring whitespace. */
          if (svn_ctype_isspace(*curp))
            switch (*curp)
              {
              case '\r':
                state = svn_diff__normalize_state_cr;
                if (opts->ignore_eol_style)
                  {
                    *newend++ = '\n';
                    start = curp + 1;
                  }
                else
                  start = curp;
                break;
              case '\n':
                state = svn_diff__normalize_state_normal;
                start = curp;
                break;
              default:
                break;
              }
          else
            {
              /* Non-whitespace character. */
              start = curp;
              state = svn_diff__normalize_state_normal;
            }
          break;
        }
    }

  /* If we're not in whitespace, flush the last chunk of data.
   * Note that this will work correctly when this is the last chunk of the
   * file:
   * * If there is an eol, it will either have been output when we entered
   *   the state_cr, or it will be output now.
   * * If there is no eol and we're not in whitespace, then we just output
   *   everything below.
   * * If there's no eol and we are in whitespace, we want to ignore
   *   whitespace unconditionally. */
  if (state != svn_diff__normalize_state_whitespace)
    {
      if (start != newend)
        memmove(newend, start, curp - start);
      newend += curp - start;
    }
  *lengthp = newend - buf;
  *statep = state;
}


/* Return the library version number. */
const svn_version_t *
svn_diff_version(void)
{
  SVN_VERSION_BODY;
}
