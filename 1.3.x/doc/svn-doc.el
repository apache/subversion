;;;; Emacs Lisp help for writing the Subversion Documentation. ;;;;

(defun svn-doc-visit-info-file ()
  "Visit the local info file in progress.
Right now there's only one place to go, so no prompting is done."
  (interactive)
  (Info-goto-node (concat "(" default-directory "svn-design.info)Top")))


(defun svn-doc-texinfo-new-node (name)
  "Insert a new node (defaults to `section' heading).
Includes the commented separator line we like to use."
  ;; todo: search backwards for menu and build a completion table?
  (interactive "sNode name: ")
  (insert "@c ")
  (insert-char ?- 71)
  (insert "\n")
  (insert "@node " name "\n")
  (insert "@section " name "\n\n"))

