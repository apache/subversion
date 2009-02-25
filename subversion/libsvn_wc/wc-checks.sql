/* wc-checks.sql -- trigger-based checks for the wc-metadata database.
 *     This is intended for use with SQLite 3
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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


/* ------------------------------------------------------------------------- */

CREATE TRIGGER no_repository_updates BEFORE UPDATE ON REPOSITORY
BEGIN
  SELECT RAISE(FAIL, 'Updates to REPOSITORY are not allowed.');
END;


/* ------------------------------------------------------------------------- */

/* no triggers for WCROOT yet */


/* ------------------------------------------------------------------------- */

CREATE TRIGGER valid_repos_id_insert BEFORE INSERT ON BASE_NODE
WHEN new.repos_id is not null
BEGIN
  SELECT * FROM REPOSITORY WHERE id = new.repos_id;
END;


/* ------------------------------------------------------------------------- */
