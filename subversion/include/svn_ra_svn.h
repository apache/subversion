/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_ra_svn.h
 * @brief libsvn_ra_svn functions used by the server
 */




#ifndef SVN_RA_SVN_H
#define SVN_RA_SVN_H

#include <apr_network_io.h>
#include <svn_delta.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** The well-known svn port number. */
#define SVN_RA_SVN_PORT 3690

/** A specialized form of @c SVN_ERR to deal with errors which occur in an
 * @c svn_ra_svn_command_handler.
 *
 * An error returned with this macro will be passed back to the other side 
 * of the connection.  Use this macro when performing the requested operation; 
 * use the regular @c SVN_ERR when performing I/O with the client.
 */
#define SVN_CMD_ERR(expr)                                     \
  do {                                                        \
    svn_error_t *svn_err__temp = (expr);                      \
    if (svn_err__temp)                                        \
      return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR,         \
                              svn_err__temp, NULL);           \
  } while (0)

/** an ra_svn connection. */
typedef struct svn_ra_svn_conn_st svn_ra_svn_conn_t;

/** Command handler, used by @c svn_ra_svn_handle_commands. */
typedef svn_error_t *(*svn_ra_svn_command_handler)(svn_ra_svn_conn_t *conn,
                                                   apr_pool_t *pool,
                                                   apr_array_header_t *params,
                                                   void *baton);

/** Command table, used by @c svn_ra_svn_handle_commands.
 *
 * If TERMINATE is set, command-handling will cease after command is 
 * processed.
 */
typedef struct {
  const char *cmdname;
  svn_ra_svn_command_handler handler;
  svn_boolean_t terminate;
} svn_ra_svn_cmd_entry_t;

/** Memory representation of an on-the-wire data item. */
typedef struct {
  enum {
    SVN_RA_SVN_NUMBER,
    SVN_RA_SVN_STRING,
    SVN_RA_SVN_WORD,
    SVN_RA_SVN_LIST
  } kind;
  union {
    apr_uint64_t number;
    svn_string_t *string;
    const char *word;

    /** Contains @c svn_ra_svn_item_t's. */
    apr_array_header_t *list;
  } u;
} svn_ra_svn_item_t;

typedef svn_error_t *(*svn_ra_svn_edit_callback)(void *baton);

/** Initialize a connection structure for the given socket or
 * input/output files.
 *
 * Either @c sock or @c in_file/@c out_file must be set, not both.
 */
svn_ra_svn_conn_t *svn_ra_svn_create_conn(apr_socket_t *sock,
                                          apr_file_t *in_file,
                                          apr_file_t *out_file,
                                          apr_pool_t *pool);

/** Write a number over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *svn_ra_svn_write_number(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     apr_uint64_t number);

/** Write a string over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *svn_ra_svn_write_string(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_string_t *str);

/** Write a cstring over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *svn_ra_svn_write_cstring(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool, const char *s);

/** Write a word over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *svn_ra_svn_write_word(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *word);

/** Begin a list.  Writes will be buffered until the next read or flush. */
svn_error_t *svn_ra_svn_start_list(svn_ra_svn_conn_t *conn, apr_pool_t *pool);

/** End a list.  Writes will be buffered until the next read or flush. */
svn_error_t *svn_ra_svn_end_list(svn_ra_svn_conn_t *conn, apr_pool_t *pool);

/** Flush the write buffer.
 *
 * Normally this shouldn't be necessary, since the write buffer is flushed
 * when a read is attempted.
 */
svn_error_t *svn_ra_svn_flush(svn_ra_svn_conn_t *conn, apr_pool_t *pool);

/** Write a tuple, using a printf-like interface.
 *
 * The format string @a fmt may contain:
 *
 *<pre>
 *   Spec  Argument type         Item type
 *   ----  --------------------  ---------
 *   n     apr_uint64_t          Number
 *   r     svn_revnum_t          Number
 *   s     const svn_string_t *  String
 *   c     const char *          String
 *   w     const char *          Word
 *   b     svn_boolean_t         Word ("true" or "false")
 *   (                           Begin tuple
 *   )                           End tuple
 *   ?                           Remaining elements optional
 * </pre>
 *
 * Inside the optional part of a tuple, 'r' values may be @c
 * SVN_INVALID_REVNUM and 's', 'c', and 'w' values may be @c NULL; in
 * these cases no data will be written.  'n', 'b', and '(' may not
 * appear in the optional part of a tuple.  Either all or none of the
 * optional values should be valid.
 */
svn_error_t *svn_ra_svn_write_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    const char *fmt, ...);

/** Read an item from the network into @c item. */
svn_error_t *svn_ra_svn_read_item(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  svn_ra_svn_item_t **item);

/** Parse an array of @c svn_item_t structures as a tuple, using a
 * printf-like interface.  The format string @a fmt may contain:
 *
 *<pre>
 *   Spec  Argument type          Item type
 *   ----  --------------------   ---------
 *   n     apr_uint64_t *         Number
 *   r     svn_revnum_t *         Number
 *   s     svn_string_t **        String
 *   c     const char **          String
 *   w     const char **          Word
 *   b     svn_boolean_t *        Word ("true" or "false")
 *   l     apr_array_header_t **  List
 *   (                            Begin tuple
 *   )                            End tuple
 *   ?                            Tuple is allowed to end here
 *</pre>
 *
 * Note that a tuple is only allowed to end precisely at a '?', or at
 * the end of the specification, of course.  So if @a fmt is "c?cc"
 * and @a list contains two elements, an error will result.
 *
 * If an optional part of a tuple contains no data, 'r' values will be
 * set to @c SVN_INVALID_REVNUM and 's', 'c', 'w', and 'l' values will
 * be set to @c NULL.  'n' and 'b' may not appear inside an optional
 * tuple specification.
 */
svn_error_t *svn_ra_svn_parse_tuple(apr_array_header_t *list,
                                    apr_pool_t *pool,
                                    const char *fmt, ...);

/** Read a tuple from the network and parse it as a tuple, using the
 * format string notation from @c svn_ra_svn_parse_typle.
 */
svn_error_t *svn_ra_svn_read_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *fmt, ...);

/** Read a command response from the network and parse it as a tuple, using 
 * the format string notation from @c svn_ra_svn_parse_typle.
 */
svn_error_t *svn_ra_svn_read_cmd_response(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool,
                                          const char *fmt, ...);

/** Accept commands over the network and handle them according to
 * @a commands.
 *
 * Accept commands over the network and handle them according to
 * @a commands.  Command handlers will be passed @a conn, a subpool of 
 * @a pool (cleared after each command is handled), the parameters of the
 * command, and @a baton.  Commands will be accepted until a terminating
 * command is received (a command with "terminate" set in the command
 * table).  Normally, this function will only halt and return an error
 * when a communications failure occurs, and will send other errors to
 * the remote connection as command failures.  If @a pass_through_errors
 * is set, all errors will be returned (after being sent to the remote
 * connection if appropriate).
 */
svn_error_t *svn_ra_svn_handle_commands(svn_ra_svn_conn_t *conn,
                                        apr_pool_t *pool,
                                        svn_ra_svn_cmd_entry_t *commands,
                                        void *baton,
                                        svn_boolean_t pass_through_errors);

/** Write a command over the network, using the same format string notation 
 * as svn_ra_svn_write_tuple.
 */
svn_error_t *svn_ra_svn_write_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  const char *cmdname, const char *fmt, ...);

/** Write a successful command response over the network, using the same 
 * format string notation as svn_ra_svn_write_tuple.
 */
svn_error_t *svn_ra_svn_write_cmd_response(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           const char *fmt, ...);

/** Write an unsuccessful command response over the network. */
svn_error_t *svn_ra_svn_write_cmd_failure(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool, svn_error_t *err);

/** Set @a *editor and @a *edit_baton to an editor which will pass editing
 * operations over the network, using @a conn and @a pool.
 *
 * Upon successful completion of the edit, the editor will invoke @a callback 
 * with @a callback_baton as an argument.
 */
void svn_ra_svn_get_editor(const svn_delta_editor_t **editor,
                           void **edit_baton, svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool, svn_ra_svn_edit_callback callback,
                           void *callback_baton);

/** Receive edit commands over the network and use them to drive @a editor
 * with @a edit_baton.
 *
 * On return, @a *aborted will be set if the edit was aborted.  See the 
 * @c svn_ra_svn_handle_commands description for the meaning of 
 * @a pass_through_errors.
 */
svn_error_t *svn_ra_svn_drive_editor(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_delta_editor_t *editor,
                                     void *edit_baton,
                                     svn_boolean_t pass_through_errors,
                                     svn_boolean_t *aborted);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_SVN_H */
