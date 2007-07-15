(require (planet "test.ss" ("schematics" "schemeunit.plt" 2 )))
(require (planet "text-ui.ss" ("schematics" "schemeunit.plt" 2)))
(define subversion-tests
	(test-suite
         "Tests for subversion bindings"
         (test-suite
          "Enviroment sanity"
         (test-equal? "Sanity check (1==1)" (+ 0 1) 1)
         )
	))
(test/text-ui subversion-tests)