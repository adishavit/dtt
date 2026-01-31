;;; dtt.el --- DTT memory safety checker for C/C++ -*- lexical-binding: t; -*-

;; Author: DTT Contributors
;; Version: 0.1.0
;; Package-Requires: ((emacs "25.1") (flycheck "32"))
;; Keywords: c, c++, tools, convenience
;; URL: https://github.com/adishavit/dtt

;;; Commentary:

;; This package provides Flycheck integration for DTT (Dangling Pointer and
;; Type Safety Checker).  DTT detects null pointer dereferences, use-after-move,
;; dangling pointers, and buffer overflows in C/C++ code.

;; Usage:
;;   (require 'dtt)
;;   (add-hook 'c-mode-hook #'dtt-mode)
;;   (add-hook 'c++-mode-hook #'dtt-mode)

;;; Code:

(require 'flycheck)

(defgroup dtt nil
  "DTT memory safety checker."
  :group 'flycheck
  :prefix "dtt-")

(defcustom dtt-clang-path "clang++"
  "Path to clang++ executable."
  :type 'string
  :group 'dtt)

(defcustom dtt-plugin-path nil
  "Path to DTT plugin.  If nil, searches common locations."
  :type '(choice (const nil) string)
  :group 'dtt)

(defcustom dtt-standard "c++20"
  "C++ standard to use."
  :type 'string
  :group 'dtt)

(defcustom dtt-mode-arg "inform"
  "DTT analysis mode: inform, enforce, or quiet."
  :type '(choice (const "inform")
                 (const "enforce")
                 (const "quiet"))
  :group 'dtt)

(defun dtt--windows-p ()
  "Return non-nil if running on Windows."
  (memq system-type '(windows-nt ms-dos cygwin)))

(defun dtt--find-plugin ()
  "Find DTT plugin in common locations."
  (or dtt-plugin-path
      (cl-find-if #'file-exists-p
                  (if (dtt--windows-p)
                      (list (expand-file-name "~/.dtt/bin/DTTPlugin.dll")
                            (expand-file-name (concat (getenv "LOCALAPPDATA") "/DTT/bin/DTTPlugin.dll"))
                            (expand-file-name (concat (or (getenv "ProgramFiles") "C:/Program Files") "/DTT/bin/DTTPlugin.dll"))
                            (expand-file-name (concat (or (getenv "ProgramFiles") "C:/Program Files") "/LLVM/lib/DTTPlugin.dll")))
                    (list (expand-file-name "~/.dtt/bin/libDTTPlugin.so")
                          (expand-file-name "~/.dtt/bin/libDTTPlugin.dylib")
                          "/usr/local/lib/libDTTPlugin.so"
                          "/usr/lib/libDTTPlugin.so"
                          "/opt/homebrew/lib/libDTTPlugin.dylib"
                          "/usr/local/lib/libDTTPlugin.dylib")))))

(defun dtt--plugin-args ()
  "Return plugin arguments for clang based on platform."
  (let ((plugin (dtt--find-plugin)))
    (if (dtt--windows-p)
        ;; Windows requires -Xclang syntax for plugins
        (list "-Xclang" "-load" "-Xclang" plugin
              "-Xclang" "-add-plugin" "-Xclang" "dtt"
              "-Xclang" "-plugin-arg-dtt" "-Xclang" (concat "--dtt-mode=" dtt-mode-arg))
      ;; Unix-like systems use -fplugin
      (list (concat "-fplugin=" plugin)
            (concat "-fplugin-arg-dtt---dtt-mode=" dtt-mode-arg)))))

(flycheck-define-checker dtt
  "DTT memory safety checker for C/C++.

See URL `https://github.com/adishavit/dtt'."
  :command ("clang++"
            (eval (dtt--plugin-args))
            (eval (concat "-std=" dtt-standard))
            "-fsyntax-only"
            source)
  :error-patterns
  ((error line-start (file-name) ":" line ":" column ": error: " (message) line-end)
   (warning line-start (file-name) ":" line ":" column ": warning: " (message) line-end)
   (info line-start (file-name) ":" line ":" column ": note: " (message) line-end))
  :error-filter
  (lambda (errors)
    (flycheck-sanitize-errors
     (seq-filter (lambda (err)
                   (string-match-p "DTT:" (flycheck-error-message err)))
                 errors)))
  :modes (c-mode c++-mode)
  :predicate (lambda () (dtt--find-plugin)))

;;;###autoload
(define-minor-mode dtt-mode
  "Minor mode for DTT memory safety checking."
  :lighter " DTT"
  :group 'dtt
  (if dtt-mode
      (progn
        (add-to-list 'flycheck-checkers 'dtt)
        (flycheck-mode 1))
    (setq flycheck-checkers (delq 'dtt flycheck-checkers))))

(provide 'dtt)
;;; dtt.el ends here
