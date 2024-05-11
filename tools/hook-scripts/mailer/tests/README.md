# Setup

Prepare a repository for use with testing:

```
$ ./mailer-init.sh
```

(make sure the subversion bindings are installed; this is not normal; see Ubuntu package "python3-subversion")

The above creates a test repository named `mailer-init.12345` (whatever PID).
You can now run the test using:

```
$ ./mailer-t1.sh mailer-init.12345/repos/ ../mailer.py
```

----

TBD: discuss mailer-t2.sh

----

TBD: discuss mailer-t3.py

----
