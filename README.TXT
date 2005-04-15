About this directory.


This subdirectory has two branches.
They are copies of /trunk:13253 and are merged from trunk from time 
to time.
- text-time
  This branch is dedicated for the versioning of the text-time
  attribute for files. See below for details.
  This part has beta-quality.
- owner-group-mode
  Here the versioning of owner, group, and unix-filemode is implemented.
  This part is more like an early alpha.
- patches
  Here I will store patches of the functionalities above; this is meant
  for people running of /trunk, who want to test this parts.
  The patches for owner-group-mode will not depend on the text-time-patches;
  they will include them.


Why meta-data versioning?

There has been much discussion whether, and to a smaller extent, how
meta-data should be versioned.
Some argue that this is not a requirement of subversion itself, but 
should be done by wrapper scripts; IMO it makes much sense to do that
inside subversion, as only one run through a wc is required on commit.


How does this work?

The general approach is as follows:
- A file about to be committed is checked for the properties
  svn:text-time, svn:owner, svn:group, svn:unix-mode
- Each of these properties gets set to the current value of the file,
  if it exists.
  So the mtime of a file is only versioned, if the file has the property
  svn:text-time set (to any value)
- The initial property setting can be achieved by auto-props.
- If the configuration value "use-commit-times" is set to "yes", then
  on checkout/update/export the stored values should [1] be used
  to restore the files' meta-data.

Example of ~/.subversion/config:
  [miscellany]
  use-commit-times = yes
  enable-auto-props = yes

  [auto-props]
  * = svn:text-time=yes;svn:owner=yes;svn:group=yes;svn:unix-mode=yes


Known problems:
- The functionality is incomplete/has bugs, the meta-data is not restored 
  on every occasion.
  Whereas text-time is mostly complete, the other properties need work.
- As mentioned by Philip Martin in 
  http://marc.theaimsgroup.com/?l=subversion-dev&m=110729651620515&w=2,
  the way conflicts are treated has to be specified.
  

Last updated: 04 Mar 2005.



References

1. Issue 1256. Discussion about svn:text-time.
   http://subversion.tigris.org/issues/show_bug.cgi?id=1256

2. Feb. 2005 - discussion about mtime versioning on the mailing list.
   Fwd: Feature request: Keep file dates
   http://marc.theaimsgroup.com/?l=subversion-dev&m=110917936530930&w=2

3. Feb. 2005 - Announce of first o-g-m patch on ml
   [PATCH] svn now stores unix-owner, -group, and -mode too   
   http://marc.theaimsgroup.com/?l=subversion-dev&m=110724160930812&w=2

4. May 2003 - Thread with first patches for mtime versioning on ml
   saving time stamps of files
   http://marc.theaimsgroup.com/?l=subversion-dev&m=105238644900470&w=2


Notes

[1] "Should". If they *would* be used in every case, the branches would 
be finished (and not alpha or beta quality).


