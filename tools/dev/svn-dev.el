;;;; Emacs Lisp help for writing Subversion code. ;;;;

;;; In C files, put something like this to load this file automatically:
;;
;;   /* -----------------------------------------------------------------
;;    * local variables:
;;    * eval: (load-file "../svn-dev.el")
;;    * end:
;;    */
;;
;; (note: make sure to get the path right in the argument to load-file).



;; Later on, there will be auto-detection of svn files, modeline
;; status, and a whole library of routines to interface with the
;; command-line client.  For now, there's this, at Ben's request.
(defun svn-revert ()
  "Revert the current buffer and its file to its svn base revision."
  (interactive)
  (let ((obuf (current-buffer))
        (fname (buffer-file-name))
        (outbuf (get-buffer-create "*svn output*")))
    (set-buffer outbuf)
    (delete-region (point-min) (point-max))
    (call-process "svn" nil outbuf nil "status" fname)
    (goto-char (point-min))
    (search-forward fname)
    (beginning-of-line)
    (if (looking-at "^?")
        (error "\"%s\" is not a Subversion-controlled file" fname))
    (call-process "svn" nil outbuf nil "revert" fname)
    (set-buffer obuf)
    ;; todo: make a backup~ file?
    (save-excursion
      (revert-buffer nil t)
      (save-buffer))
    (message "Reverted \"%s\"." fname)))



;; Helper for referring to issue numbers in a user-friendly way.
(defun svn-bug-url (n)
  "Insert the url for Subversion issue number N.  Interactively, prompt for N."
  (interactive "nSubversion issue number: ")
  (insert (format "http://subversion.tigris.org/issues/show_bug.cgi?id=%d" n)))



;;; Subversion C conventions
(if (eq major-mode 'c-mode)
    (progn
      (c-add-style "svn" '("gnu" (c-offsets-alist . ((inextern-lang . 0)))))
      (c-set-style "svn")))
(setq indent-tabs-mode nil)
(setq angry-mob-with-torches-and-pitchforks t)



;; Subversion Python conventions, plus some harmless helpers for
;; people who don't have python mode set up by default.
(autoload 'python-mode "python-mode" nil t)
(or (assoc "\\.py$" auto-mode-alist)
    (setq auto-mode-alist
          (cons '("\\.py$" . python-mode) auto-mode-alist)))

(defun svn-python-mode-hook ()
  "Set up the Subversion python conventions.  The effect of this is
local to the current buffer, which is presumably visiting a file in
the Subversion project.  Python setup in other buffers will not be
affected."
  (when (string-match "/subversion/" (buffer-file-name))
    (make-local-variable 'py-indent-offset)
    (setq py-indent-offset 2)
    (make-local-variable 'py-smart-indentation)
    (setq py-smart-indentation nil)))

(add-hook 'python-mode-hook 'svn-python-mode-hook)



;; Much of the APR documentation is embedded perldoc format.  The
;; perldoc program itself sucks, however.  If you're the author of
;; perldoc, I'm sorry, but what were you thinking?  Don't you know
;; that there are people in the world who don't work in vt100
;; terminals?  If I want to view a perldoc page in my Emacs shell
;; buffer, I have to run the ridiculous command
;;
;;   $ PAGER=cat perldoc -t target_file
;;
;; (Not that this was documented anywhere, I had to figure it out for
;; myself by reading /usr/bin/perldoc).
;;
;; Non-paging behavior should be a standard command-line option.  No
;; program that can output text should *ever* insist on invoking the
;; pager.
;;
;; Anyway, these Emacs commands will solve the problem for us.
;;
;; Acknowledgements:
;; Much of this code is copied from man.el in the FSF Emacs 21.x
;; sources.

(defcustom svn-perldoc-overstrike-face 'bold
  "*Face to use when fontifying overstrike."
  :type 'face
  :group 'svn-dev)

(defcustom svn-perldoc-underline-face 'underline
  "*Face to use when fontifying underlining."
  :type 'face
  :group 'svn-dev)


(defun svn-perldoc-softhyphen-to-minus ()
  ;; \255 is some kind of dash in Latin-N.  Versions of Debian man, at
  ;; least, emit it even when not in a Latin-N locale.
  (unless (eq t (compare-strings "latin-" 0 nil
				 current-language-environment 0 6 t))
    (goto-char (point-min))
    (let ((str "\255"))
      (if enable-multibyte-characters
	  (setq str (string-as-multibyte str)))
      (while (search-forward str nil t) (replace-match "-")))))


(defun svn-perldoc-fontify-buffer ()
  "Convert overstriking and underlining to the correct fonts.
Same for the ANSI bold and normal escape sequences."
  (interactive)
  (message "Please wait, making up the page...")
  (goto-char (point-min))
  (while (search-forward "\e[1m" nil t)
    (delete-backward-char 4)
    (put-text-property (point)
		       (progn (if (search-forward "\e[0m" nil 'move)
				  (delete-backward-char 4))
			      (point))
		       'face svn-perldoc-overstrike-face))
  (goto-char (point-min))
  (while (search-forward "_\b" nil t)
    (backward-delete-char 2)
    (put-text-property (point) (1+ (point)) 'face svn-perldoc-underline-face))
  (goto-char (point-min))
  (while (search-forward "\b_" nil t)
    (backward-delete-char 2)
    (put-text-property (1- (point)) (point) 'face svn-perldoc-underline-face))
  (goto-char (point-min))
  (while (re-search-forward "\\(.\\)\\(\b\\1\\)+" nil t)
    (replace-match "\\1")
    (put-text-property (1- (point)) (point) 'face svn-perldoc-overstrike-face))
  (goto-char (point-min))
  (while (re-search-forward "o\b\\+\\|\\+\bo" nil t)
    (replace-match "o")
    (put-text-property (1- (point)) (point) 'face 'bold))
  (goto-char (point-min))
  (while (re-search-forward "[-|]\\(\b[-|]\\)+" nil t)
    (replace-match "+")
    (put-text-property (1- (point)) (point) 'face 'bold))
  (svn-perldoc-softhyphen-to-minus)
  (message "Please wait, making up the page...done"))


(defun svn-perldoc-cleanup-buffer ()
  "Remove overstriking and underlining from the current buffer."
  (interactive)
  (message "Please wait, cleaning up the page...")
  (progn
    (goto-char (point-min))
    (while (search-forward "_\b" nil t) (backward-delete-char 2))
    (goto-char (point-min))
    (while (search-forward "\b_" nil t) (backward-delete-char 2))
    (goto-char (point-min))
    (while (re-search-forward "\\(.\\)\\(\b\\1\\)+" nil t) 
      (replace-match "\\1"))
    (goto-char (point-min))
    (while (re-search-forward "\e\\[[0-9]+m" nil t) (replace-match ""))
    (goto-char (point-min))
    (while (re-search-forward "o\b\\+\\|\\+\bo" nil t) (replace-match "o"))
    (goto-char (point-min))
    (while (re-search-forward "" nil t) (replace-match " ")))
  (goto-char (point-min))
  (while (re-search-forward "[-|]\\(\b[-|]\\)+" nil t) (replace-match "+"))
  (svn-perldoc-softhyphen-to-minus)
  (message "Please wait, cleaning up the page...done"))


;; Entry point to svn-perldoc functionality.
(defun svn-perldoc (file)
  "Run perldoc on FILE, display the output in a buffer."
  (interactive "fRun perldoc on file: ")
  (let ((outbuf (get-buffer-create 
                 (format "*%s PerlDoc*" (file-name-nondirectory file))))
        (savepg (getenv "PAGER")))
    (setenv "PAGER" "cat")  ;; for perldoc
    (save-excursion
      (set-buffer outbuf)
      (delete-region (point-min) (point-max))
      (call-process "perldoc" nil outbuf nil (expand-file-name file))
      (svn-perldoc-fontify-buffer)      
      (svn-perldoc-cleanup-buffer)
      ;; Clean out the inevitable leading dead space.
      (goto-char (point-min))
      (re-search-forward "[^ \i\n]")
      (beginning-of-line)
      (delete-region (point-min) (point)))
    (setenv "PAGER" savepg)
    (display-buffer outbuf)))



(message "loaded svn-dev.el")
