;;; psvn.el --- Subversion interface for emacs
;; Copyright (C) 2002 by Stefan Reichoer

;; Author: Stefan Reichoer, <reichoer@web.de>
;; Version: 0.2b

;; $Id$

;; psvn.el is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; psvn.el is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs; see the file COPYING.  If not, write to
;; the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
;; Boston, MA 02111-1307, USA.

;;; Commentary

;; psvn.el is tested with GNU Emacs 21.1.1 on windows, debian linux
;; with svn 0.14.3

;; psvn.el is an interface for the revision control tool subversion
;; (see http://subversion.tigris.org)
;; psvn.el provides a similar interface for subversion as pcl-cvs for cvs.
;; At the moment are the following commands implemented:
;; M-x svn-status: run 'svn -status -v'
;; and show the result in the *svn-status* buffer, this buffer uses the
;; svn-status mode. In this mode are the following keys defined:
;; g     - svn-status-update:               run 'svn status -v'
;; C-u g - svn-status-update:               run 'svn status -vu'
;; =     - svn-status-show-svn-diff         run 'svn diff'
;; l     - svn-status-show-svn-log          run 'svn log'
;; i     - svn-status-info                  run 'svn info'
;; r     - svn-status-revert-file           run 'svn revert'
;; U     - svn-status-update-cmd            run 'svn update'
;; c     - svn-status-commit-file           run 'svn commit'
;; a     - svn-status-add-file              run 'svn add'
;; M-c   - svn-status-cleanup               run 'svn cleanup'
;; s     - svn-status-show-process-buffer
;; e     - svn-status-toggle-edit-cmd-flag
;; ?     - svn-status-toggle-hide-unknown
;; _     - svn-status-toggle-hide-unmodified
;; m     - svn-status-set-user-mark
;; u     - svn-status-unset-user-mark
;; DEL   - svn-status-unset-user-mark-backwards
;; .     - svn-status-goto-root-or-return
;; o     - svn-status-find-file-other-window
;; P l   - svn-status-property-list
;; P s   - svn-status-property-set
;; P d   - svn-status-property-delete
;; P e   - svn-status-property-edit-one-entry
;; P i   - svn-status-property-ignore-file
;; P I   - svn-status-property-ignore-file-extension
;; P C-i - svn-status-property-edit-svn-ignore
;; P k   - svn-status-property-set-keyword-list
;; h     - svn-status-use-history
;; q     - svn-status-bury-buffer

;; To use psvn.el put the following line in your .emacs:
;; (require 'psvn)
;; Start the svn interface with M-x svn-status

;; The latest version of psvn.el can be found at:
;;   http://xsteve.nit.at/prg/emacs/psvn.el
;; Or you can check it out from the subversion repository:
;;   svn co http://svn.collab.net/repos/svn/trunk/tools/client-side/psvn psvn

;; TODO:
;; * shortcut for svn propset svn:keywords "Date" psvn.el
;; * docstrings for the functions
;; * perhaps shortcuts for ranges, dates
;; * when editing the command line - offer help from the svn client
;; * finish svn-status-property-set
;; * eventually use the customize interface
;; * make it xemacs compatible:
;;   - make the menu entry working
;; * add support for svn rename, svn delete
;; * Parse the following line correct:
;;  A  +            -       ?          ?    ./psvn.el -- show the + also
;;

;; Overview over the implemented/not (yet) implemented svn sub-commands:
;; * add                       implemented
;; * checkout (co)
;; * cleanup                   implemented
;; * commit (ci)               implemented
;; * copy (cp)
;; * delete (del, remove, rm)
;; * diff (di)                 implemented
;; * export
;; * help (?, h)
;; * import
;; * info                      implemented
;; * log                       implemented
;; * merge
;; * mkdir
;; * move (mv, rename, ren)
;; * propdel (pdel)            implemented
;; * propedit (pedit, pe)      not needed
;; * propget (pget, pg)        used
;; * proplist (plist, pl)      implemented
;; * propset (pset, ps)        used
;; * revert                    implemented
;; * resolve
;; * status (stat, st)         implemented
;; * switch (sw)
;; * update (up)               implemented

;; For the not yet implemented commands you should use the command line
;; svn client. If there are user requests for any missing commands I will
;; probably implement them.

;; Comments / suggestions and bug reports are welcome!

;;; Code:

; user setable variables
(defvar svn-status-hide-unknown nil "Hide unknown files in *cvs-status* buffer.")
(defvar svn-status-hide-unmodified nil "Hide unmodified files in *cvs-status* buffer.")
(defvar svn-status-directory-history nil "List of visited svn working directories.")

(require 'overlay nil t)

;; internal variables
(defvar svn-process-cmd nil)
(defvar svn-status-info nil)
(defvar svn-status-default-column 23)
(defvar svn-status-files-to-commit nil)
(defvar svn-status-pre-commit-window-configuration nil)
(defvar svn-status-pre-propedit-window-configuration nil)
(defvar svn-status-head-revision nil)
(defvar svn-status-root-return-info nil)
(defvar svn-status-property-edit-must-match-flag nil)
(defvar svn-status-propedit-property-name nil)
(defvar svn-status-propedit-file-list nil)
(defvar svn-status-mode-line-process "")
(defvar svn-status-mode-line-process-status "")
(defvar svn-status-mode-line-process-edit-flag "")
(defvar svn-status-edit-svn-command nil)
(defvar svn-status-temp-dir
  (or
   (when (boundp 'temporary-file-directory) temporary-file-directory) ;emacs
   (when (boundp 'temp-directory) temp-directory)                     ;xemacs
   "/tmp/"))
(defvar svn-status-temp-arg-file (concat svn-status-temp-dir "svn.arg"))
(defconst svn-xemacsp (featurep 'xemacs))

;; faces
(defface svn-status-marked-face
  '((((type tty) (class color)) (:foreground "green" :weight light))
    (((class color) (background light)) (:foreground "green3"))
    (t (:weight bold)))
  "Face to highlight the mark for user marked files in svn status buffers")

(defface svn-status-modified-external-face
  '((((type tty) (class color)) (:foreground "magenta" :weight light))
    (((class color) (background light)) (:foreground "magenta"))
    (t (:weight bold)))
  "Face to highlight the externaly modified phrase in svn status buffers")

(defvar svn-highlight t)
;; stolen from PCL-CVS
(defun svn-add-face (str face &optional keymap)
  (when svn-highlight
    (add-text-properties 0 (length str)
		 (list* 'face face
			(when keymap
			  (list 'mouse-face 'highlight
				'local-map keymap)))
		 str))
  str)

; compatibility
; emacs 20
(unless (fboundp 'point-at-eol) (defalias 'point-at-eol 'line-end-position))
(unless (fboundp 'point-at-bol) (defalias 'point-at-bol 'line-beginning-position))


(defun svn-status (dir &optional arg)
  (interactive (list (read-file-name "SVN status directory: "
                                     nil default-directory nil)))
  (setq svn-status-directory-history (delete dir svn-status-directory-history))
  (add-to-list 'svn-status-directory-history dir)
  (let* ((status-buf (get-buffer-create "*svn-status*"))
         (proc-buf (get-buffer-create "*svn-process*")))
    (save-excursion
      (set-buffer status-buf)
      (setq default-directory dir)
      (set-buffer proc-buf)
      (setq default-directory dir)
      (if arg
          (svn-run-svn t t 'status "status" "-vu")
        (svn-run-svn t t 'status "status" "-v")))))

(defun svn-status-use-history ()
  (interactive)
  (let* ((hist svn-status-directory-history)
         (dir (read-from-minibuffer "svn-status on directory: "
                              (cdr svn-status-directory-history)
                              nil nil 'hist)))
    (when (file-directory-p dir)
      (svn-status dir))))

(defun svn-run-svn (run-asynchron clear-process-buffer cmdtype &rest arglist)
  (if (eq (process-status "svn") nil)
      (progn
        (when svn-status-edit-svn-command
          (setq arglist (append arglist
                                (split-string
                                 (read-from-minibuffer
                                  (format "svn %s %S " cmdtype arglist)))))
          (when (eq svn-status-edit-svn-command t)
            (svn-status-toggle-edit-cmd-flag t))
          (message "svn-run-svn %s: %S" cmdtype arglist))
        (let* ((proc-buf (get-buffer-create "*svn-process*"))
               (svn-proc))
          (when (listp (car arglist))
            (setq arglist (car arglist)))
          (save-excursion
            (set-buffer proc-buf)
            (setq buffer-read-only nil)
            (fundamental-mode)
            (if clear-process-buffer
                (delete-region (point-min) (point-max))
              (goto-char (point-max)))
            (setq svn-process-cmd cmdtype)
            (setq svn-status-mode-line-process-status (format " running %s" cmdtype))
            (svn-status-update-mode-line)
            (sit-for 0.1)
            (if run-asynchron
                (progn
                  (setq svn-proc (apply 'start-process "svn" proc-buf "svn" arglist))
                  (set-process-sentinel svn-proc 'svn-process-sentinel))
              ;;(message "running synchron: svn %S" arglist)
              (apply 'call-process "svn" nil proc-buf nil arglist)
              (setq svn-status-mode-line-process-status "")
              (svn-status-update-mode-line)))))
    (error "You can only run one svn process at once!")))

(defun svn-process-sentinel (process event)
  ;(princ (format "Process: %s had the event `%s'" process event)))
  ;(save-excursion
    (set-buffer (process-buffer process))
    (setq svn-status-mode-line-process-status "")
    (svn-status-update-mode-line)
    (if (string= event "finished\n")
        (progn
          (cond ((eq svn-process-cmd 'status)
                 ;;(message "svn status finished")
                 (svn-parse-status-result)
                 (svn-status-update-buffer))
                ((eq svn-process-cmd 'log)
                 (svn-status-show-process-buffer-internal t)
                 (message "svn log finished"))
                ((eq svn-process-cmd 'info)
                 (svn-status-show-process-buffer-internal t)
                 (message "svn info finished"))
;;                ((eq svn-process-cmd 'diff)
;;                 (svn-status-show-process-buffer-internal t)
;;                 (save-excursion
;;                   (set-buffer "*svn-process*")
;;                   (diff-mode)
;;                   (font-lock-fontify-buffer))
;;                 (message "svn diff finished"))
                ((eq svn-process-cmd 'commit)
                 (svn-status-show-process-buffer-internal t)
                 (svn-status-update)
                 (message "svn commit finished"))
                ((eq svn-process-cmd 'update)
                 (svn-status-show-process-buffer-internal t)
                 (svn-status-update)
                 (message "svn update finished"))
                ((eq svn-process-cmd 'add)
                 (svn-status-update)
                 (message "svn add finished"))
                ((eq svn-process-cmd 'revert)
                 (svn-status-update)
                 (message "svn revert finished"))
                ((eq svn-process-cmd 'cleanup)
                 ;;(svn-status-show-process-buffer-internal t)
                 (message "svn cleanup finished"))
                ((eq svn-process-cmd 'proplist)
                 (svn-status-show-process-buffer-internal t)
                 (message "svn proplist finished"))
                ((eq svn-process-cmd 'proplist-parse)
                 (svn-status-property-parse-property-names))
;;              ((eq svn-process-cmd 'propget-parse)
;;               t)
                ((eq svn-process-cmd 'propset)
                 (svn-status-update))
                ((eq svn-process-cmd 'propdel)
                 (svn-status-update))))
      ;;(message (format "SVN Error: :%s:" event))
      (svn-status-show-process-buffer-internal t)))

(defun svn-parse-status-result ()
  (setq svn-status-head-revision nil)
  (save-excursion
    (let ((old-marked-files (svn-status-marked-file-names))
          (line-string)
          (user-mark)
          (file-svn-info)
          (svn-file-mark)
          (svn-property-mark)
          (local-rev)
          (last-change-rev)
          (modified-external)
          (author)
          (path))
      (set-buffer "*svn-process*")
      (setq svn-status-info nil)
      (goto-char (point-min))
      (while (> (- (point-at-eol) (point-at-bol)) 0)
        (setq modified-external nil)
        (setq line-string (buffer-substring-no-properties
                           (point-at-bol)
                           (point-at-eol)))
        (if (string-match "Head revision:[ ]+\\([0-9]+\\)" line-string)
            (setq svn-status-head-revision (match-string 1 line-string))
          (setq file-svn-info (append (list (substring line-string 0 4))
                                      (if svn-xemacsp
                                          (cdr (split-string (substring line-string 5)))
                                        (split-string (substring line-string 5)))))
          (when (string= (nth 1 file-svn-info) "*")
            (setq modified-external t)
            ; remove the (nth 1) entry
            (setq file-svn-info (append
                                 (list (car file-svn-info))
                                 ; nth 1 removed
                                 (cddr file-svn-info))))
          (setq svn-file-mark (string-to-char (car file-svn-info)))
          (setq svn-property-mark (string-to-char (substring (car file-svn-info) 1)))
          (when (eq svn-property-mark 0) (setq svn-property-mark nil))
          ;is this necessary?
          (when (eq svn-property-mark ?\ ) (setq svn-property-mark nil))
          (cond ((eq svn-file-mark ??)
                 (setq path (nth 1 file-svn-info)
                       local-rev -1
                       last-change-rev -1
                       author "?"))
                (t
                 (setq path (nth 4 file-svn-info)
                       local-rev (string-to-number (nth 1 file-svn-info))
                       last-change-rev (string-to-number (nth 2 file-svn-info))
                       author (nth 3 file-svn-info))))
          (unless path (setq path "."))
          (setq user-mark (not (not (member path old-marked-files))))
          (setq svn-status-info (append svn-status-info
                                        (list
                                         (list user-mark
                                               svn-file-mark
                                               svn-property-mark
                                               path
                                               local-rev
                                               last-change-rev
                                               author
                                               modified-external)))))
                                               ;;file-svn-info
          (next-line 1)))))

(condition-case nil
    (easy-menu-add-item nil '("tools") ["SVN Status" svn-status t] "PCL-CVS")
  (error (message "psvn: could not install menu")))

(defvar svn-status-mode-map () "Keymap used in svn-status-mode buffers.")
(defvar svn-status-mode-property-map ()
  "Subkeymap used in svn-status-mode for property commands.")

(when (not svn-status-mode-map)
  (setq svn-status-mode-map (make-sparse-keymap))
  (define-key svn-status-mode-map [return] 'svn-status-select-line)
  (define-key svn-status-mode-map [?s] 'svn-status-show-process-buffer)
  (define-key svn-status-mode-map [?o] 'svn-status-find-file-other-window)
  (define-key svn-status-mode-map [?e] 'svn-status-toggle-edit-cmd-flag)
  (define-key svn-status-mode-map [?g] 'svn-status-update)
  (define-key svn-status-mode-map [?q] 'svn-status-bury-buffer)
  (define-key svn-status-mode-map [?h] 'svn-status-use-history)
  (define-key svn-status-mode-map [?m] 'svn-status-set-user-mark)
  (define-key svn-status-mode-map [?u] 'svn-status-unset-user-mark)
  (define-key svn-status-mode-map [(backspace)] 'svn-status-unset-user-mark-backwards)
  (define-key svn-status-mode-map [?.] 'svn-status-goto-root-or-return)
  (define-key svn-status-mode-map [??] 'svn-status-toggle-hide-unknown)
  (define-key svn-status-mode-map [?_] 'svn-status-toggle-hide-unmodified)
  (define-key svn-status-mode-map [?a] 'svn-status-add-file)
  (define-key svn-status-mode-map [?c] 'svn-status-commit-file)
  (define-key svn-status-mode-map [(meta ?c)] 'svn-status-cleanup)
  (define-key svn-status-mode-map [?U] 'svn-status-update-cmd)
  (define-key svn-status-mode-map [?r] 'svn-status-revert-file)
  (define-key svn-status-mode-map [?l] 'svn-status-show-svn-log)
  (define-key svn-status-mode-map [?i] 'svn-status-info)
  (define-key svn-status-mode-map [?=] 'svn-status-show-svn-diff))

(when (not svn-status-mode-property-map)
  (setq svn-status-mode-property-map (make-sparse-keymap))
  (define-key svn-status-mode-property-map [?l] 'svn-status-property-list)
  (define-key svn-status-mode-property-map [?s] 'svn-status-property-set)
  (define-key svn-status-mode-property-map [?d] 'svn-status-property-delete)
  (define-key svn-status-mode-property-map [?e] 'svn-status-property-edit-one-entry)
  (define-key svn-status-mode-property-map [?i] 'svn-status-property-ignore-file)
  (define-key svn-status-mode-property-map [?I] 'svn-status-property-ignore-file-extension)
  (define-key svn-status-mode-property-map [(control ?i)] 'svn-status-property-edit-svn-ignore)
  (define-key svn-status-mode-property-map [?k] 'svn-status-property-set-keyword-list)
  (define-key svn-status-mode-property-map [?p] 'svn-status-property-parse)
  (define-key svn-status-mode-property-map [return] 'svn-status-select-line)
  (define-key svn-status-mode-map [?P] svn-status-mode-property-map))


(easy-menu-define svn-status-mode-menu svn-status-mode-map
"'svn-status-mode' menu"
                  '("Svn"
                    ["svn status" svn-status-update t]
                    ["svn update" svn-status-update-cmd t]
                    ["svn commit" svn-status-commit-file t]
                    ["svn log" svn-status-show-svn-log t]
                    ["svn info" svn-status-info t]
                    ["svn diff" svn-status-show-svn-diff t]
                    ["svn add" svn-status-add-file t]
                    ["svn revert" svn-status-revert-file t]
                    ["svn cleanup" svn-status-cleanup t]
                    ["Show process buffer" svn-status-show-process-buffer t]
                    ("Property"
                     ["svn proplist" svn-status-property-list t]
                     ["set multiple properties" svn-status-property-set t]
                     ["edit one property" svn-status-property-edit-one-entry t]
                     ["svn:ignore file" svn-status-property-ignore-file t]
                     ["svn:ignore file extension" svn-status-property-ignore-file-extension t]
                     ["edit svn:ignore file" svn-status-property-edit-svn-ignore t]
                     ["set svn:keyword list" svn-status-property-set-keyword-list t]
                     ["svn propdel" svn-status-property-delete t]
                     )
                    "---"
                    ["edit next svn cmd line" svn-status-toggle-edit-cmd-flag t]
                    ["work directory history" svn-status-use-history t]
                    ["mark" svn-status-set-user-mark t]
                    ["unmark" svn-status-unset-user-mark t]
                    ["toggle hide unknown" svn-status-toggle-hide-unknown t]
                    ["toggle hide unmodified" svn-status-toggle-hide-unmodified t]
                    ))

(defun svn-status-mode ()
  "Major mode for processing the svn status

  psvn.el is an interface for the revision control tool subversion
  (see http://subversion.tigris.org)
  psvn.el provides a similar interface for subversion as pcl-cvs for cvs.
  At the moment are the following commands implemented:
  M-x svn-status: run 'svn -status -v'
  and show the result in the *svn-status* buffer, this buffer uses the
  svn-status mode. In this mode are the following keys defined:
  g     - svn-status-update:               run 'svn status -v'
  C-u g - svn-status-update:               run 'svn status -vu'
  =     - svn-status-show-svn-diff         run 'svn diff'
  l     - svn-status-show-svn-log          run 'svn log'
  i     - svn-status-info                  run 'svn info'
  r     - svn-status-revert-file           run 'svn revert'
  U     - svn-status-update-cmd            run 'svn update'
  c     - svn-status-commit-file           run 'svn commit'
  a     - svn-status-add-file              run 'svn add'
  M-c   - svn-status-cleanup               run 'svn cleanup'
  s     - svn-status-show-process-buffer
  e     - svn-status-toggle-edit-cmd-flag
  ?     - svn-status-toggle-hide-unknown
  _     - svn-status-toggle-hide-unmodified
  m     - svn-status-set-user-mark
  u     - svn-status-unset-user-mark
  DEL   - svn-status-unset-user-mark-backwards
  .     - svn-status-goto-root-or-return
  o     - svn-status-find-file-other-window
  P l   - svn-status-property-list
  P s   - svn-status-property-set
  P d   - svn-status-property-delete
  P e   - svn-status-property-edit-one-entry
  P i   - svn-status-property-ignore-file
  P I   - svn-status-property-ignore-file-extension
  P C-i - svn-status-property-edit-svn-ignore
  P k   - svn-status-property-set-keyword-list
  h     - svn-status-use-history
  q     - svn-status-bury-buffer"
  (interactive)
  (kill-all-local-variables)

  (use-local-map svn-status-mode-map)
  (setq major-mode 'svn-status-mode)
  (setq mode-name "svn-status")
  (setq mode-line-process 'svn-status-mode-line-process)
  (toggle-read-only 1))

(defun svn-status-update-mode-line ()
  (setq svn-status-mode-line-process
        (concat svn-status-mode-line-process-edit-flag svn-status-mode-line-process-status))
  (force-mode-line-update))

(defun svn-status-bury-buffer ()
  (interactive)
  (bury-buffer))

(defun svn-status-find-file-other-window ()
  (interactive)
  (find-file-other-window (svn-status-line-info->filename
                           (svn-status-get-line-information))))

(defun svn-status-line-info->has-usermark (line-info) (nth 0 line-info))
(defun svn-status-line-info->filemark (line-info) (nth 1 line-info))
(defun svn-status-line-info->propmark (line-info) (nth 2 line-info))
(defun svn-status-line-info->filename (line-info) (nth 3 line-info))
(defun svn-status-line-info->filename-nondirectory (line-info)
  (file-name-nondirectory (svn-status-line-info->filename line-info)))
(defun svn-status-line-info->localrev (line-info)
  (if (>= (nth 4 line-info) 0)
      (nth 4 line-info)
    nil))
(defun svn-status-line-info->lastchangerev (line-info)
  (if (>= (nth 5 line-info) 0)
      (nth 5 line-info)
    nil))
(defun svn-status-line-info->author (line-info) (nth 6 line-info))
(defun svn-status-line-info->modified-external (line-info) (nth 7 line-info))

(defun svn-status-line-info->is-visiblep (line-info)
  (not (or (svn-status-line-info->hide-because-unknown line-info)
           (svn-status-line-info->hide-because-unmodified line-info))))

(defun svn-status-line-info->hide-because-unknown (line-info)
  (and svn-status-hide-unknown
       (eq (svn-status-line-info->filemark line-info) ??)))

(defun svn-status-line-info->hide-because-unmodified (line-info)
  ;;(message " %S %S %S %S - %s" svn-status-hide-unmodified (svn-status-line-info->propmark line-info) ?_
  ;;         (svn-status-line-info->filemark line-info) (svn-status-line-info->filename line-info))
  (and svn-status-hide-unmodified
       (and (or (eq (svn-status-line-info->filemark line-info) ?_)
                (eq (svn-status-line-info->filemark line-info) ? ))
            (or (eq (svn-status-line-info->propmark line-info) ?_)
                (eq (svn-status-line-info->propmark line-info) ? )
                (eq (svn-status-line-info->propmark line-info) nil)))))


(defun svn-insert-line-in-status-buffer (line-info)
  (let ((s "*"))
    (let ((usermark (if (svn-status-line-info->has-usermark line-info)
                        (svn-add-face "*" 'svn-status-marked-face)
                      " "))
          (external (if (svn-status-line-info->modified-external line-info)
                        (svn-add-face " (modified external)" 'svn-status-modified-external-face)
                      "")))
      (insert (concat usermark
                      (format " %c%c %3s %3s %-9s %s"
                              (svn-status-line-info->filemark line-info)
                              (or (svn-status-line-info->propmark line-info) ? )
                              (or (svn-status-line-info->localrev line-info) "")
                              (or (svn-status-line-info->lastchangerev line-info) "")
                              (svn-status-line-info->author line-info)
                              (svn-status-line-info->filename line-info))
                      external
                      "\n")))))

(defun svn-status-update-buffer ()
  (interactive)
  (delete-other-windows)
  (pop-to-buffer "*svn-status*")
  (svn-status-mode)
  (let ((st-info svn-status-info)
        (buffer-read-only nil)
        (start-pos)
        (overlay)
        (unmodified-count 0)
        (unknown-count 0)
        (fname (svn-status-line-info->filename (svn-status-get-line-information)))
        (column (current-column)))
    (delete-region (point-min) (point-max))
    (insert "\n")
    ;(insert (format "%S\n\n" svn-status-info))
    (while st-info
      (setq start-pos (point))
      (cond ((svn-status-line-info->hide-because-unknown (car st-info))
             (setq unknown-count (+ unknown-count 1)))
            ((svn-status-line-info->hide-because-unmodified (car st-info))
             (setq unmodified-count (+ unmodified-count 1)))
            (t
             (svn-insert-line-in-status-buffer (car st-info))))
      (setq overlay (make-overlay start-pos (point)))
      (overlay-put overlay 'svn-info (car st-info))
      (setq st-info (cdr st-info)))
    (goto-char (point-min))
    (insert (format "svn status for directory %s%s\n"
                    default-directory
                    (if svn-status-head-revision (format " (head revision: %s)"
                                                         svn-status-head-revision)
                      "")))
    (when svn-status-hide-unknown
      (insert
       (format "%d Unknown files are hidden - press ? to toggle hiding\n"
               unknown-count)))
    (when svn-status-hide-unmodified
      (insert
       (format "%d Unmodified files are hidden - press _ to toggle hiding\n"
               unmodified-count)))
    (if fname
        (progn
          (svn-status-goto-file-name fname)
          (goto-char (+ column (point-at-bol))))
      (goto-char (+ (next-overlay-change (point-min)) svn-status-default-column)))))

(defun svn-status-toggle-edit-cmd-flag (&optional reset)
  (interactive)
  (cond ((or reset (eq svn-status-edit-svn-command 'sticky))
         (setq svn-status-edit-svn-command nil))
        ((eq svn-status-edit-svn-command nil)
         (setq svn-status-edit-svn-command t))
        ((eq svn-status-edit-svn-command t)
         (setq svn-status-edit-svn-command 'sticky)))
  (cond ((eq svn-status-edit-svn-command t)
         (setq svn-status-mode-line-process-edit-flag " EditCmd"))
        ((eq svn-status-edit-svn-command 'sticky)
         (setq svn-status-mode-line-process-edit-flag " EditCmd#"))
        (t
         (setq svn-status-mode-line-process-edit-flag "")))
  (svn-status-update-mode-line))

(defun svn-status-goto-root-or-return ()
  (interactive)
  (if (string= (svn-status-line-info->filename (svn-status-get-line-information)) ".")
      (when svn-status-root-return-info
        (svn-status-goto-file-name
         (svn-status-line-info->filename svn-status-root-return-info)))
    (setq svn-status-root-return-info (svn-status-get-line-information))
    (svn-status-goto-file-name ".")))

(defun svn-status-next-line (nr-of-lines)
  (next-line nr-of-lines)
  (goto-char (+ (point-at-bol) svn-status-default-column)))

(defun svn-status-update (&optional arg)
  (interactive "P")
  (pop-to-buffer "*svn-status*")
  (svn-status default-directory arg))

(defun svn-status-get-line-information ()
  (let ((overlay (car (overlays-at (point)))))
    (when overlay
      (overlay-get overlay 'svn-info))))

(defun svn-status-select-line ()
  (interactive)
  (let ((info (svn-status-get-line-information)))
    (if info
        (message "%S %S %S" info (svn-status-line-info->hide-because-unknown info)
                                 (svn-status-line-info->hide-because-unmodified info))
      (message "No file on this line"))))

(defun svn-status-set-user-mark (arg)
  "Set a user mark on the current file or directory.
If the cursor is on a file this file is marked and the cursor advances to the next line.
If the cursor is on a directory all files in this directory are marked.

If this function is called with a prefix argument, only the actual line is
marked - no matter if it is a directory or a file."
  (interactive "P")
  (let ((info (svn-status-get-line-information)))
    (if info
        (progn
          (svn-status-apply-usermark t arg)
          (svn-status-next-line 1))
      (message "No file on this line - cannot set a mark"))))

(defun svn-status-unset-user-mark (arg)
  "Remove a user mark on the current file or directory.
If the cursor is on a file this file is unmarked and the cursor advances to the next line.
If the cursor is on a directory all files in this directory are unmarked.

If this function is called with a prefix argument, only the actual line is
unmarked - no matter if it is a directory or a file."
  (interactive "P")
  (let ((info (svn-status-get-line-information)))
    (if info
        (progn
          (svn-status-apply-usermark nil arg)
          (svn-status-next-line 1))
      (message "No file on this line - cannot unset a mark"))))

(defun svn-status-unset-user-mark-backwards ()
  "Remove a user mark from the current file.
Afterwards move one line up."
  (interactive)
  (let ((info (svn-status-get-line-information)))
    (if info
        (progn
          (svn-status-apply-usermark nil t)
          (svn-status-next-line -1))
      (message "No file on this line - cannot unset a mark"))))

(defun svn-status-apply-usermark (set-mark only-this-line)
  (let* ((st-info svn-status-info)
         (line-info (svn-status-get-line-information))
         (file-name (svn-status-line-info->filename line-info))
         (newcursorpos-fname)
         (i-fname))
    (while st-info
      (setq i-fname (svn-status-line-info->filename (car st-info)))
      (when (and (>= (length i-fname) (length file-name))
                 (string= file-name (substring i-fname 0 (length file-name))))
        (when (svn-status-line-info->is-visiblep (car st-info))
          (when (or (not only-this-line) (string= file-name i-fname))
            (setq newcursorpos-fname i-fname)
            (if set-mark
                (message "marking: %s" i-fname)
              (message "unmarking: %s" i-fname))
            (setcar (car st-info) set-mark))))
      (setq st-info (cdr st-info)))
    (svn-status-update-buffer)
    (svn-status-goto-file-name newcursorpos-fname)))

(defun svn-status-toggle-hide-unknown ()
  (interactive)
  (setq svn-status-hide-unknown (not svn-status-hide-unknown))
  (svn-status-update-buffer))

(defun svn-status-toggle-hide-unmodified ()
  (interactive)
  (setq svn-status-hide-unmodified (not svn-status-hide-unmodified))
  (svn-status-update-buffer))

(defun svn-status-goto-file-name (name)
  (let ((start-pos (point)))
    (goto-char (point-min))
    (while (< (point) (point-max))
      (goto-char (next-overlay-change (point)))
      (when (string= name (svn-status-line-info->filename
                           (svn-status-get-line-information)))
        (setq start-pos (+ (point) svn-status-default-column))))
    (goto-char start-pos)))

(defun svn-status-find-info-for-file-name (name)
  (save-excursion
    (let ((info nil))
      (goto-char (point-min))
      (while (< (point) (point-max))
        (goto-char (next-overlay-change (point)))
        (when (string= name (svn-status-line-info->filename
                             (svn-status-get-line-information)))
          (setq info (svn-status-get-line-information))))
      info)))

(defun svn-status-marked-files ()
  (let* ((st-info svn-status-info)
         (file-list))
    (while st-info
      (when (svn-status-line-info->has-usermark (car st-info))
        (setq file-list (append file-list (list (car st-info)))))
      (setq st-info (cdr st-info)))
    (or file-list
        (if (svn-status-get-line-information)
            (list (svn-status-get-line-information))
          nil))))

(defun svn-status-marked-file-names ()
  (mapcar 'svn-status-line-info->filename (svn-status-marked-files)))

(defun svn-status-create-arg-file (file-name prefix file-info-list postfix)
  (with-temp-file file-name
    (insert prefix)
    (let ((st-info file-info-list))
      (while st-info
        (insert (svn-status-line-info->filename (car st-info)))
        (insert "\n")
        (setq st-info (cdr st-info)))

    (insert postfix))))

(defun svn-status-show-process-buffer-internal (&optional scroll-to-top)
  (when (eq (current-buffer) "*svn-status*")
    (delete-other-windows))
  (pop-to-buffer "*svn-process*")
  (when scroll-to-top
    (goto-char (point-min)))
  (other-window 1))

(defun svn-status-show-svn-log ()
  (interactive)
  ;(message "show log info for: %S" (svn-status-marked-files))
  (svn-status-create-arg-file svn-status-temp-arg-file "" (svn-status-marked-files) "")
  (svn-run-svn t t 'log "log" "--targets" svn-status-temp-arg-file))

(defun svn-status-info ()
  (interactive)
  (svn-status-create-arg-file svn-status-temp-arg-file "" (svn-status-marked-files) "")
  (svn-run-svn t t 'info "info" "--targets" svn-status-temp-arg-file))

(defun svn-status-show-svn-diff ()
  (interactive)
  (let ((fl (svn-status-marked-files))
        (clear-buf t))
    (while fl
      (svn-run-svn nil clear-buf 'diff "diff" (svn-status-line-info->filename (car fl)))
      (setq clear-buf nil)
      (setq fl (cdr fl))))
  (svn-status-show-process-buffer-internal t)
  (save-excursion
    (set-buffer "*svn-process*")
    (diff-mode)
    (font-lock-fontify-buffer)))

(defun svn-status-show-process-buffer ()
  (interactive)
  (svn-status-show-process-buffer-internal))

(defun svn-status-add-file ()
  (interactive)
  (message "adding: %S" (svn-status-marked-file-names))
  (svn-status-create-arg-file svn-status-temp-arg-file "" (svn-status-marked-files) "")
  (svn-run-svn t t 'add "add" "--targets" svn-status-temp-arg-file))

(defun svn-status-revert-file ()
  (interactive)
  (let* ((marked-files (svn-status-marked-files))
         (num-of-files (length marked-files)))
    (if (= 0 num-of-files)
        (message "No file selected for reverting!")
      (when (yes-or-no-p (if (= 1 num-of-files)
                             (format "Revert %s? " (svn-status-line-info->filename
                                                    (car marked-files)))
                           (format "Revert %d files? " num-of-files)))
        (message "reverting: %S" (svn-status-marked-files))
        (svn-status-create-arg-file svn-status-temp-arg-file ""
                                    (svn-status-marked-files) "")
        (svn-run-svn t t 'revert "revert" "--targets" svn-status-temp-arg-file)))))

(defun svn-status-update-cmd ()
  (interactive)
  ;TODO: use file names also
  (svn-run-svn t t 'update "update"))

(defun svn-status-commit-file ()
  (interactive)
  (let* ((marked-files (svn-status-marked-files)))
    (message "Commiting %S" (svn-status-marked-file-names))
    (setq svn-status-files-to-commit marked-files)
    (svn-status-pop-to-commit-buffer)))

(defun svn-status-pop-to-commit-buffer ()
  (interactive)
  (setq svn-status-pre-commit-window-configuration (current-window-configuration))
  (let* ((commit-buffer (get-buffer-create "*svn-log-edit*"))
         (dir default-directory))
    (pop-to-buffer commit-buffer)
    (setq default-directory dir)
    (svn-log-edit-mode)))

(defun svn-status-cleanup ()
  (interactive)
  (let ((file-names (svn-status-marked-file-names)))
    (if file-names
        (progn
          ;(message "svn-status-cleanup %S" file-names))
          (svn-run-svn t t 'cleanup (append (list "cleanup") file-names)))
      (message "No valid file selected - No status cleanup possible"))))
;; --------------------------------------------------------------------------------
;; Property List stuff
;; --------------------------------------------------------------------------------

(defun svn-status-property-list ()
  (interactive)
  (let ((file-names (svn-status-marked-file-names)))
    (if file-names
        (progn
          (svn-run-svn t t 'proplist (append (list "proplist" "-v") file-names)))
      (message "No valid file selected - No property listing possible"))))

(defun svn-status-proplist-start ()
  (svn-run-svn t t 'proplist-parse "proplist" (svn-status-line-info->filename
                                               (svn-status-get-line-information))))

(defun svn-status-property-parse ()
  (interactive)
  (svn-status-proplist-start))

(defun svn-status-property-edit-one-entry (arg)
  (interactive "P")
  (setq svn-status-property-edit-must-match-flag (not arg))
  (svn-status-proplist-start))

(defun svn-status-property-set ()
  (interactive)
  (message "svn-status-property-set")
  (svn-status-proplist-start))

(defun svn-status-property-delete ()
  (interactive)
  (setq svn-status-property-edit-must-match-flag t)
  (svn-status-proplist-start))

(defun svn-status-property-parse-property-names ()
  ;(svn-status-show-process-buffer-internal t)
  (message "svn-status-property-parse-property-names")
  (let ((pl)
        (pfl)
        (prop-name)
        (prop-value))
    (save-excursion
      (set-buffer "*svn-process*")
      (goto-char (point-min))
      (forward-line 1)
      (while (looking-at "  \\(.+\\)")
        (setq pl (append pl (list (match-string 1))))
        (forward-line 1)))
    ;(cond last-command: svn-status-property-set, svn-status-property-edit-one-entry
    ;svn-status-property-parse:
    (cond ((eq last-command 'svn-status-property-parse)
           ;(message "%S %S" pl last-command)
           (while pl
             (svn-run-svn nil t 'propget-parse "propget" (car pl)
                          (svn-status-line-info->filename
                           (svn-status-get-line-information)))
             (save-excursion
               (set-buffer "*svn-process*")
               (setq pfl (append pfl (list
                                      (list
                                       (car pl)
                                       (buffer-substring
                                        (point-min) (- (point-max) 1)))))))
             (setq pl (cdr pl))
             (message "%S" pfl)))
          ((eq last-command 'svn-status-property-edit-one-entry)
           ;;(message "svn-status-property-edit-one-entry")
           (setq prop-name
                 (completing-read "Set Property - Name: " (mapcar 'list pl)
                                  nil svn-status-property-edit-must-match-flag))
           (unless (string= prop-name "")
             (save-excursion
               (set-buffer "*svn-status*")
               (svn-status-property-edit (list (svn-status-get-line-information))
                                         prop-name))))
          ((eq last-command 'svn-status-property-set)
           (message "svn-status-property-set")
           (setq prop-name
                 (completing-read "Set Property - Name: " (mapcar 'list pl) nil nil))
           (setq prop-value (read-from-minibuffer "Property value: "))
           (unless (string= prop-name "")
             (save-excursion
               (set-buffer "*svn-status*")
               (message "setting property %s := %s for %S" prop-name prop-value
                        (svn-status-marked-files)))))
          ((eq last-command 'svn-status-property-delete)
           (setq prop-name
                 (completing-read "Delete Property - Name: " (mapcar 'list pl) nil t))
           (unless (string= prop-name "")
             (let ((file-names (svn-status-marked-file-names)))
               (when file-names
                 (svn-run-svn t t 'propdel
                              (append (list "propdel" prop-name) file-names)))))))))

(defun svn-status-property-edit (file-info-list prop-name &optional new-prop-value)
  (let* ((commit-buffer (get-buffer-create "*svn-property-edit*"))
         (dir default-directory)
         ;; now only one file is implemented ...
         (file-name (svn-status-line-info->filename (car file-info-list)))
         (prop-value))
    (message "Edit property %s for file %s" prop-name file-name)
    (svn-run-svn nil t 'propget-parse "propget" prop-name file-name)
    (save-excursion
      (set-buffer "*svn-process*")
      (setq prop-value (if (> (point-max) 1)
                           (buffer-substring (point-min) (- (point-max) 1))
                         "")))
    (setq svn-status-propedit-property-name prop-name)
    (setq svn-status-propedit-file-list file-info-list)
    (setq svn-status-pre-propedit-window-configuration (current-window-configuration))
    (pop-to-buffer commit-buffer)
    (delete-region (point-min) (point-max))
    (setq default-directory dir)
    (insert prop-value)
    (when new-prop-value
      (when (listp new-prop-value)
        (message "Adding new prop values %S " new-prop-value)
        (while new-prop-value
          (goto-char (point-min))
          (unless (re-search-forward
                   (concat "^" (regexp-quote (car new-prop-value)) "$") nil t)
            (goto-char (point-max))
            (when (> (current-column) 0) (insert "\n"))
            (insert (car new-prop-value)))
          (setq new-prop-value (cdr new-prop-value)))))
    (svn-prop-edit-mode)))

(defun svn-status-get-directory (line-info)
  (let* ((file-name (svn-status-line-info->filename line-info))
         (file-dir (file-name-directory file-name)))
    ;;(message "file-dir: %S" file-dir)
    (if file-dir
        (substring file-dir 0 (- (length file-dir) 1))
      ".")))

(defun svn-status-get-file-list-per-directory (files)
  ;;(message "%S" files)
  (let ((dir-list nil)
        (i files)
        (j)
        (dir))
    (while i
      (setq dir (svn-status-get-directory (car i)))
      (setq j (assoc dir dir-list))
      (if j
          (progn
            ;;(message "dir already present %S %s" j dir)
            (setcdr j (append (cdr j) (list (car i)))))
        (setq dir-list (append dir-list (list (list dir (car i))))))
      (setq i (cdr i)))
    ;;(message "svn-status-get-file-list-per-directory: %S" dir-list)
    dir-list))

(defun svn-status-property-ignore-file ()
  (interactive)
  (let ((d-list (svn-status-get-file-list-per-directory (svn-status-marked-files)))
        (dir)
        (f-info)
        (ext-list))
    (while d-list
      (setq dir (caar d-list))
      (setq f-info (cdar d-list))
      (setq ext-list (mapcar '(lambda (i)
                                (svn-status-line-info->filename-nondirectory i)) f-info))
      ;;(message "ignore in dir %s: %S" dir f-info)
      (save-window-excursion
        (when (y-or-n-p (format "Ignore %S for %s? " ext-list dir))
          (svn-status-property-edit
           (list (svn-status-find-info-for-file-name dir)) "svn:ignore" ext-list)
          (svn-prop-edit-done)))
      (setq d-list (cdr d-list)))))

(defun svn-status-property-ignore-file-extension ()
  (interactive)
  (let ((d-list (svn-status-get-file-list-per-directory (svn-status-marked-files)))
        (dir)
        (f-info)
        (ext-list))
    (while d-list
      (setq dir (caar d-list))
      (setq f-info (cdar d-list))
      ;;(message "ignore in dir %s: %S" dir f-info)
      (setq ext-list nil)
      (while f-info
        (add-to-list 'ext-list (concat "*."
                                       (file-name-extension
                                        (svn-status-line-info->filename (car f-info)))))
        (setq f-info (cdr f-info)))
      ;;(message "%S" ext-list)
      (save-window-excursion
        (when (y-or-n-p (format "Ignore %S for %s? " ext-list dir))
          (svn-status-property-edit
           (list (svn-status-find-info-for-file-name dir)) "svn:ignore"
           ext-list)
          (svn-prop-edit-done)))
      (setq d-list (cdr d-list)))))

(defun svn-status-property-edit-svn-ignore ()
  (interactive)
  (let ((dir (svn-status-get-directory (svn-status-get-line-information))))
    (svn-status-property-edit
     (list (svn-status-find-info-for-file-name dir)) "svn:ignore")
    (message "Edit svn:ignore on %s" dir)))


(defun svn-status-property-set-keyword-list ()
  (interactive)
  (message "svn-status-property-set-keyword-list"))

;; --------------------------------------------------------------------------------
;; svn-prop-edit-mode:
;; --------------------------------------------------------------------------------

(defvar svn-prop-edit-mode-map () "Keymap used in svn-prop-edit-mode buffers.")

(when (not svn-prop-edit-mode-map)
  (setq svn-prop-edit-mode-map (make-sparse-keymap))
  (define-key svn-prop-edit-mode-map [(control ?c) (control ?c)] 'svn-prop-edit-done)
  (define-key svn-prop-edit-mode-map [(control ?c) ?=] 'svn-prop-edit-svn-diff)
  (define-key svn-prop-edit-mode-map [(control ?c) ?s] 'svn-prop-edit-svn-status)
  (define-key svn-prop-edit-mode-map [(control ?c) ?q] 'svn-prop-edit-abort))

(easy-menu-define svn-prop-edit-mode-menu svn-prop-edit-mode-map
"'svn-prop-edit-mode' menu"
                  '("Svn-PropEdit"
                    ["commit" svn-prop-edit-done t]
                    ["show diff" svn-prop-edit-svn-diff t]
                    ["show status" svn-prop-edit-svn-status t]
                    ["abort" svn-prop-edit-abort t]))

(defun svn-prop-edit-mode ()
  (interactive)
  (kill-all-local-variables)
  (use-local-map svn-prop-edit-mode-map)
  (setq major-mode 'svn-prop-edit-mode)
  (setq mode-name "svn-prop-edit"))

(defun svn-prop-edit-abort ()
  (interactive)
  (bury-buffer)
  (set-window-configuration svn-status-pre-propedit-window-configuration))

(defun svn-prop-edit-done ()
  (interactive)
  (message "svn propset %s on %s"
           svn-status-propedit-property-name
           svn-status-propedit-file-list)
  (save-excursion
    (set-buffer (get-buffer "*svn-property-edit*"))
    (set-buffer-file-coding-system 'undecided-unix nil)
    (write-region (point-min) (point-max)
                  (concat svn-status-temp-dir "svn-prop-edit.txt")) nil 1)
  (when svn-status-propedit-file-list ; there are files to change properties
    (svn-status-create-arg-file svn-status-temp-arg-file ""
                                svn-status-propedit-file-list "")
    (setq svn-status-propedit-file-list nil)
    (svn-run-svn t t 'propset "propset" svn-status-propedit-property-name
                 "--targets" svn-status-temp-arg-file
                 "-F" (concat svn-status-temp-dir "svn-prop-edit.txt")))
  (set-window-configuration svn-status-pre-propedit-window-configuration))

(defun svn-prop-edit-svn-diff ()
  (interactive)
  (set-buffer "*svn-status*")
  (svn-status-show-svn-diff))

(defun svn-prop-edit-svn-status ()
  (interactive)
  (pop-to-buffer "*svn-status*")
  (other-window 1))

;; --------------------------------------------------------------------------------
;; svn-log-edit-mode:
;; --------------------------------------------------------------------------------

(defvar svn-log-edit-mode-map () "Keymap used in svn-log-edit-mode buffers.")

(when (not svn-log-edit-mode-map)
  (setq svn-log-edit-mode-map (make-sparse-keymap))
  (define-key svn-log-edit-mode-map [(control ?c) (control ?c)] 'svn-log-edit-done)
  (define-key svn-log-edit-mode-map [(control ?c) ?=] 'svn-log-edit-svn-diff)
  (define-key svn-log-edit-mode-map [(control ?c) ?s] 'svn-log-edit-svn-status)
  (define-key svn-log-edit-mode-map [(control ?c) ?q] 'svn-log-edit-abort))

(easy-menu-define svn-log-edit-mode-menu svn-log-edit-mode-map
"'svn-log-edit-mode' menu"
                  '("Svn-Log"
                    ["commit" svn-log-edit-done t]
                    ["show diff" svn-log-edit-svn-diff t]
                    ["show status" svn-log-edit-svn-status t]
                    ["abort" svn-log-edit-abort t]))

(defun svn-log-edit-mode ()
  (interactive)
  (kill-all-local-variables)
  (use-local-map svn-log-edit-mode-map)
  (setq major-mode 'svn-log-edit-mode)
  (setq mode-name "svn-log-edit"))

(defun svn-log-edit-abort ()
  (interactive)
  (bury-buffer)
  (set-window-configuration svn-status-pre-commit-window-configuration))

(defun svn-log-edit-done ()
  (interactive)
  (message "svn-log editing done")
  (save-excursion
    (set-buffer (get-buffer "*svn-log-edit*"))
    (set-buffer-file-coding-system 'undecided-unix nil)
    (write-region (point-min) (point-max)
                  (concat svn-status-temp-dir "svn-log-edit.txt") nil 1))
  (when svn-status-files-to-commit ; there are files to commit
    (svn-status-create-arg-file svn-status-temp-arg-file ""
                                svn-status-files-to-commit "")
    (setq svn-status-files-to-commit nil)
    (svn-run-svn t t 'commit "commit" "--targets" svn-status-temp-arg-file
                 "-F" (concat svn-status-temp-dir "svn-log-edit.txt")))
  (set-window-configuration svn-status-pre-commit-window-configuration))

(defun svn-log-edit-svn-diff ()
  (interactive)
  (set-buffer "*svn-status*")
  (svn-status-show-svn-diff))

(defun svn-log-edit-svn-status ()
  (interactive)
  (pop-to-buffer "*svn-status*")
  (other-window 1))

(provide 'psvn)