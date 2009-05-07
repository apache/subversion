/* rep-cache-db.sql -- schema for use in rep-caching
 *   This is intentd for use with SQLite 3
 *
 * ====================================================================
 * Copyright (c) 2008-2009 CollabNet.  All rights reserved.
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

pragma auto_vacuum = 1;

/* A table mapping representation hashes to locations in a rev file. */
create table rep_cache (hash text not null primary key,
                        revision integer not null,
                        offset integer not null,
                        size integer not null,
                        expanded_size integer not null);
