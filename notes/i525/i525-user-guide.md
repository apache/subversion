
This is a detailed user guide to the "i525pod" feature.


# Terminology

Place-holders:

  - "i525pod" stands for the name of the feature documented here, that is
    as implemented on branch 'pristines-on-demand-on-mwf' on 2022-03-08.
    (Not any other interpretation of what Issue #525 discusses.)
  - "bare" stands for the state of a WC in which the feature "i525pod" is
    enabled, and so contains only some of the pristine copies.

Terminology:

  - "pristine copy" or "pristine" or "text base": a copy of a file's content
    matching the corresponding base revision in the repository. For any file
    type, not necessarily text format. Enables e.g. local diff and revert,
    and delta update and commit. Stored in the WC metadata area. The term
    herein refers only to file content, although Subversion also stores the
    pristine copy of properties and of tree structure.
  - "hydrate" a pristine copy: to fetch the pristine copy from the
    repository and store it in the WC metadata area.
  - "dehydrate" a pristine copy: to remove the pristine copy from the WC
    metadata area, while remembering that it may be needed again.
  - "sync scope": the set of WC paths in which a Subversion operation will
    check for pristines that need to be hydrated or dehydrated. This is a
    superset of the pristines that the operation will actually need.
  - "operation": a high level Subversion operation, such as "diff" or
    "merge" or "update"; e.g. a subcommand of the 'svn' program.


# "i525pod" User Guide

## Functional and Timing Differences

This section details the functional and timing differences when the
"i525pod" feature is used.

In a WC where "i525pod" is enabled (see other sections for how), basic usage
differs from that found in previous versions of Subversion (1.14 in case of
doubt) in the following ways.

Each of the following operations, that previously were *offline* operations,
will now contact the repository to "hydrate" pristine copies before
beginning its function, if (and only if) any pristines within the "sync scope"
are found to be locally modified and currently "dehydrated".

  - `svn cat` (default case: base version)
  - `svn diff` (default case: base against working)
  - `svn resolve` (also conflicts resolver in `merge`, `update`)
  - `svn revert`

Notes on previously *offline* operations,:

  - Contacting the repository may require authentication.
  - If contact or authorization fails, the operation will error out and not
    be available.
  - This contact may be needed as a result of a file being modified that is
    not of interest in the current operation, as the "sync scope" is a
    superset of the pristines that this operation will actually need.
  - The "hydrating" phase may take a long time, and (currently) gives no
    progress feedback, before the operation begins its usual (previous)
    behaviour.

[TODO: update that if we add progress feedback]

Each of the following operations, that previously were *online* operations,
also will now require the same.

  - `svn diff` (comparing repository to WC)
  - `svn merge`
  - `svn switch`
  - `svn update`
  - `svn checkout --force` (similar to update)

Additional notes on previously *online* operations:

  - The "hydrating" phase requires its own connection and authentication to
    the repository, which is not [currently] shared with the main part of
    the operation. This may mean a password would have to be entered an
    additional time, for example, depending on the configuration.
  - In some edge cases, there may be some difference in the outcome of the
    operation. A possible example is if repository path authorization has
    been withdrawn from a path that now needs to be hydrated; this
    particular case is still under discussion in issue #????.

[TODO: check/eliminate the additional authentication? Issue filed?]

## Disk Space Usage Differences

This section details the disk space usage differences when the feature is
used.

Summary:

  - Without "i525pod" feature, a Subversion WC typically occupies
    approximately twice the size of the working files, because there is a
    pristine copy of every unique working file, plus some other metadata.
  - The "i525pod" feature enables a WC to occupy a disk space little greater
    than the size of the working files, in cases where only a small
    proportion (size wise) of the files are locally modified at any one
    time.
  - Cases where the feature will not provide much disk space benefit
    include:
    - where a large proportion of WC files are duplicates (because there was
      only one pristine copy in the first place for each set of duplicates);
    - where the WC is arranged to contain only (predominantly) the files
      that are to be modified in a given work flow.

Initial WC size, from checkout/upgrade:

  - on a fresh checkout, no pristines are stored;
  - on enabling the feature in an existing WC (by WC upgrade), pristines are
    removed for unmodified files;
  - [TODO] check: are pristines removed in fact for all files not just
    unmodified files?

Size increase and decrease:

  - the operations listed in the 'Functional differences' section may grow
    and/or shrink the disk space used, as detailed there;
  - operations such as editing a file (without Subversion's control), and
    editing its Subversion properties, will not cause any change to the
    pristine storage;
  - 'commit' will remove the pristines for files it commits that had locally
    modified content;

Disk Full:

Failure modes when disk becomes full during an operation...

  - [TODO] Investigate.


## Support, Compatibility, Enabling

In brief:

"i525pod" is an optional feature in Subversion 1.15. It can be enabled
separately for each WC. By default "i525pod" is not enabled and WCs remain
compatible with Subversion 1.8 through 1.14. To enable "i525pod" for a given
WC, check out or upgrade the WC to 1.15's format. A WC in
1.15's format is no longer compatible with older Subversion
clients.

For details: see "Working Copy Format Upgrade and Compatibility" section.

[TODO: update if/when we use a specific feature-enable flag.]

## User Guide: Principle Of Operation

Quoting the original 'BRANCH-README':

  "The core idea is that we start to maintain the following invariant: only
  the modified files have their pristine text-base files available on the
  disk."

When "i525pod" is enabled in a given WC, Subversion stores pristine copies
according to the following principle:

  - It stores the pristine copy of each file that is currently in a locally
    modified state.
  - It does not store the pristine copy of each file that is not currently
    in a locally modified state.
  - At certain points in its operations, Subversion checks the modified
    state of certain files, and fetches (from the repository) or removes
    their pristine copy accordingly if the state has changed.

Subversion updates the pristine storage to match this principle at certain
times.
  - To get into the appropriate state at the beginning of the operation, we
    walk through the current text-base info in the db and check if the
    corresponding working files are modified.  The missing text-bases are
    fetched using the 'svn_ra' layer.  The operations also include a final
    step during which the no longer required text-bases are removed from
    disk.

  - The operations that don't need to access the text-bases (such as "svn
    ls" or the updated "svn st") do not perform this walk and do not
    synchronize the text-base state.


Subversion updates the pristine storage to match this principle at certain
times.

  - At the beginning of any high level operation that may need to access
    pristine copies [1], Subversion first checks for files [1] that are now
    locally modified and whose pristine copies are not already present, and
    connects to the repository and fetches them.
  - At the beginning and/or the end of those operations, Subversion checks
    for files that are now unmodified, and removes their pristine copies.
    That includes files that became unmodified before the operation, and,
    for some operations (such as commit) also files that became unmodified
    because of the action of the operation.
  - It does not matter how a file became modified or unmodified: whether a
    Subversion operation caused that to be the case, or whether the user had
    externally edited the file into that state some time before.
  - [1] See "Which operations?"
  - [2] See "Which files?" about the "sync scope".

The pristine storage state does not immediately change to match the
principle:

  - NOT whenever the user edits a file outside of Subversion's control,
  - NOR for files outside the "sync scope" of a given operation,
  - NOR during any Subversion operation other than those listed.

The definition of "locally modified" takes into account Subversion's local
"translation" options such as "keywords" and "eol-style". A working file
that differs from the repository copy only in keywords and/or EOL-style is
not regarded as locally modified. When Subversion needs to access a pristine
copy of such a file, Subversion makes a temporary pristine copy by
"detranslating" the working file. It may store this in temporary disk space
for the duration of the operation, but does not keep this indefinitely.

### Which operations will "hydrate" or "dehydrate" pristines?

The operations deemed to need the pristine copies of locally modified files,
and which therefore "hydrate" (and "dehydrate") the pristine copies of
locally modified files, are:

  - `H` `D` `svn cat` (default case: base version)
  - `-` `D` `svn commit` (dehydrate only)
  - `H` `D` `svn diff` (default case: base against working)
  - `H` `D` `svn resolve` (also conflicts resolver in `merge`, `update`)
  - `H` `D` `svn revert`
  - `H` `D` `svn switch`
  - `H` `D` `svn update`
  - `-` `D` `svn upgrade --compatible-version=1.15` (upgrade to 1.15's WC format enables "i525pod")

### Which files does Subversion "hydrate" or "dehydrate" ("sync scope")?

Which files does Subversion "hydrate" or "dehydrate", whenever an eligible
operation has the chance to do so?

The files that a given operation "hydrates" and/or "dehydrates" are neither
all possible files in the working copy, nor the minimal subset necessary for
the particular operation.

Not maximal:

  - It considers files to "hydrate" or "dehydrate" within one or more
    sub-trees that encompass all the target paths passed to the operation.
    Depending on the operation, this sub-tree may span anything from the
    whole WC (typical for "update", for example), right down to one specific
    file of interest, or even a directory containing no files at all.

Not minimal:

  - The operation in the end may not look at all the files in "sync scope":
    for example, because of filtering options (such as `--depth`,
    `--changelist`), or because the operation terminated early (for
    example, `svn resolve`... and choose `quit`).
  - The operation in the end may not read the pristine copy of every file it
    processes: for example, `svn diff --properties-only`.



# Working Copy Format Upgrade and Compatibility

## Summary

  - "i525pod" is active when a given WC is in 1.15-compatible format.
  - To use "i525pod", use `svn checkout --compatible-version=1.15`
    or `svn upgrade --compatible-version=1.15`.
  - Subversion 1.15 by default uses 1.8-compatible WC format, with "i525pod"
    inactive in those WCs, the same as Subversion 1.8 through 1.14.
  - Subversion 1.14 and older cannot read or write a 1.15-compatible WC.
  - Working copies cannot be downgraded

## Details

To use "i525pod" in a given working copy (WC), that WC's metadata needs to be in a new 1.15-compatible format (also called WC format 32). You need a Subversion 1.15 client to create or use a WC in this format.

You can either check out a working copy in this format:

    svn checkout --compatible-version=1.15

or upgrade an existing working copy from a 1.8-compatible or older format to this format:

    svn upgrade --compatible-version=1.15

That working copy:

  - becomes "bare" right away;
  - is no longer accessible by Subversion clients below version 1.15;
  - cannot be converted back to 1.8-compatible format.

[TODO] We might change this so that upgrading to 1.15-compatible format and enabling "i525pod" are separate steps and the latter is optional.

Subversion 1.15 also supports, and uses by default, the working copy format that has been in use since Subversion 1.8 (format 31). A working copy in that format does not support "i525pod" and is never "bare".

    svn checkout --compatible-version=1.8
    svn checkout  # the same, as 1.8 is the default

You do not need to use the new format for all your working copies. Subversion 1.15 can work with some working copies in 1.8-compatible format and others in 1.15-compatible format.

## Upgrade / Downgrade

You can upgrade to 1.15-compatible format (WC format 32) with:

    svn upgrade --compatible-version=1.15

from either

  - 1.8-compatible format, which Subversion 1.15 can use; or
  - 1.7-compatible or older format, which Subversion 1.15 cannot use but can upgrade.

By default "svn upgrade" upgrades a working copy to 1.8-compatible format. This is useful for upgrading a WC from the 1.7 and older formats so Subversion 1.8 and newer can use it.

    svn upgrade
    svn upgrade --compatible-version=1.8  # the same

You CANNOT downgrade any working copy to an older format.

(If you need a working copy in an older format than your current Subversion client supports, you would have to check out a working copy using an older Subversion client that supports the format you want to use.)

## [TODO] Enable/disable "i525pod" in a 1.15-compatible WC

[TODO] Not yet implemented (2022-03-08).


