;;;; Emacs Lisp help for writing Subversion books. ;;;;

(if (not (boundp 'visited-svnbook-el))
    (progn
      (autoload 'xml-mode "psgml" "Major mode to edit XML files." t)))
(setq visited-svnbook-el t)

(xml-mode)

;; We need to eval the local variables (specifically
;; sgml-parent-document) after setting the mode to xml-mode so that we
;; can get all the xml goodness that C-c C-t (sgml-list-valid-tags)
;; gives us.
(hack-local-variables)

;; *lots* of great stuff here: http://www.snee.com/bob/sgmlfree/emcspsgm.html
;;
;; For possible use. -Fitz
;; (add-hook 'sgml-mode-hook   ; make all this stuff SGML-specific
;; (function (lambda()
;;               ; everything in here will only apply in SGML mode
;; )))

(defun sgml-emdash ()
  "Insert ISO entity reference for em dash."
  (interactive)
  (insert "&mdash;"))

(define-key sgml-mode-map "_" 'sgml-emdash)

;; Stole this from kfogel's .emacs, and now from my .emacs -Fitz
(defun svn-htmlegalize-region (b e)
  "Just replaces < and > for now."
  (interactive "r")
  (save-excursion
    (let ((em (copy-marker e)))
      (goto-char b)
      (while (search-forward "<" em t)
      (replace-match "&lt;" nil t))
      (goto-char b)
      (while (search-forward ">" em t)
        (replace-match "&gt;" nil t))
      )))


(setq sgml-omittag-transparent t
      sgml-trace-entity-lookup nil
      sgml-live-element-indicator t
      ;; sgml-declaration "declaration/xml.dcl"
      sgml-xml-validate-command "SP_CHARSET_FIXED=YES SP_ENCODING=XML nsgmls -wxml -mdeclaration/xml.soc -ges %s %s"
      sgml-validate-command "SP_CHARSET_FIXED=YES SP_ENCODING=XML nsgmls -wxml -mdeclaration/xml.soc -ges %s %s"
      ;; NOTE: I removed the -u from sgml-validate-command to get rid of stupid errors in the DTDs. -Fitz
      sgml-set-face t
      sgml-mode-hook (quote (auto-fill-mode))
      sgml-catalog-files (append sgml-catalog-files '("/usr/local/prod/sgml/dblite/catalog"))
      sgml-auto-activate-dtd t)


(message "loaded svnbook.el")


