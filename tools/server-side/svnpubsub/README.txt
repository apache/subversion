### write a README


TODO:
- bulk update at startup time to avoid backlog warnings
- switch to host:port format in config file
- fold BDEC into Daemon
- fold WorkingCopy._get_match() into __init__
- remove wc_ready(). assume all WorkingCopy instances are usable.
  place the instances into .watch at creation. the .update_applies()
  just returns if the wc is disabled (eg. could not find wc dir)
- figure out way to avoid the ASF-specific PRODUCTION_RE_FILTER
  (a base path exclusion list should work for the ASF)
- add support for SIGHUP to reread the config and reinitialize working copies
- joes will write documentation for svnpubsub as these items become fulfilled
- make LOGLEVEL configurable
