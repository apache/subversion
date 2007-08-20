(require (planet "test.ss" ("schematics" "schemeunit.plt" 2 )))
(require (planet "text-ui.ss" ("schematics" "schemeunit.plt" 2)))
(require "../libsvn_swig_mzscheme/svn.scm")
(define subversion-tests
	(test-suite
         "Tests for subversion bindings"
         (test-suite
          "Enviroment sanity"
         (test-equal? "Sanity check (1==1)" (+ 0 1) 1)
         )
	 (test-suite
	  "Core tests"
	  (test-suite "mime types"
		      (test-equal? "image/png" (svn-mime-type-is-binary "image/png")  1 )
		      (test-equal? "image/cgm" (svn-mime-type-is-binary "image/cgm")  1 )
		      (test-equal? "image/jpeg" (svn-mime-type-is-binary "image/jpeg")  1 )
		      (test-equal? "audio/mpeg" (svn-mime-type-is-binary "audio/mpeg") 1)
		      (test-equal? "text/plain" (svn-mime-type-is-binary "text/plain")  0 )
		      (test-equal? "text/xml" (svn-mime-type-is-binary "text/xml")  0 )
		      (test-equal? "text/css" (svn-mime-type-is-binary "text/css")  0 )
	  )
	  (test-suite "version"
		      (let
		       ;Create versions
		       (( myver1 (new-svn-version-t) )
		       (myver2 (new-svn-version-t) )
		       (myver3 (new-svn-version-t) ))
		       (begin
			 ;Set the version numbers
			 (svn-version-t-major-set myver1 1)
			 (svn-version-t-major-set myver2 1)
			 (svn-version-t-major-set myver3 1)
			 (svn-version-t-minor-set myver1 2)
			 (svn-version-t-minor-set myver2 2)
			 (svn-version-t-minor-set myver3 2)
			 (svn-version-t-patch-set myver1 3)
			 (svn-version-t-patch-set myver2 5)
			 (svn-version-t-patch-set myver3 4) 
			 (test-suite "version numbers" 
			 (test-equal? "majour version number" (svn-version-t-major-get myver1) 1)
			 (test-equal? "majour version number" (svn-version-t-major-get myver2) 1)
			 (test-equal? "majour version number" (svn-version-t-major-get myver3) 1)
			 (test-equal? "minor version number" (svn-version-t-minor-get myver1) 2)
			 (test-equal? "minor version number" (svn-version-t-minor-get myver2) 2)
			 (test-equal? "minor version number" (svn-version-t-minor-get myver3) 2)
			 (test-equal? "patch version number" (svn-version-t-patch-get myver1) 3)
			 (test-equal? "match version number" (svn-version-t-patch-get myver2) 5)
			 (test-equal? "patch version number" (svn-version-t-patch-get myver3) 4))
			 )
		       )
		       )
		     )
	))
(test/text-ui subversion-tests)
(exit)