;;;; Emacs Lisp help for writing the Subversion Documentation. ;;;;

(defun svn-doc-visit-info-file ()
  "Visit the local info file in progress.
Right now there's only one place to go, so no prompting is done."
  (interactive)
  (Info-goto-node (concat "(" default-directory "svn-design.info)Top")))
