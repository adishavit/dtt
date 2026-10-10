;;; dtt.el --- DTT memory safety checker for C/C++ -*- lexical-binding: t; -*-

;; Author: DTT Contributors
;; Maintainer: Adi Shavit <adishavit@gmail.com>
;; Version: 0.1.0
;; Package-Requires: ((emacs "27.1") (flycheck "35"))
;; Keywords: c, c++, tools, convenience
;; URL: https://github.com/adishavit/dtt
;; Homepage: https://github.com/adishavit/dtt

;; This file is free software; you can redistribute it and/or modify
;; it under the terms of the BSD 2-Clause License. See LICENSE file.

;;; Commentary:

;; Zero-config DTT integration for Emacs with Flycheck.
;; Auto-downloads clang and DTT plugin if not found.
;;
;; Usage:
;;   (require 'dtt)
;;   (add-hook 'c-mode-hook #'dtt-mode)
;;   (add-hook 'c++-mode-hook #'dtt-mode)

;;; Code:

(require 'flycheck)
(require 'cl-lib)
(require 'seq)
(require 'url)

(defgroup dtt nil
  "DTT memory safety checker."
  :group 'flycheck
  :prefix "dtt-")

(defcustom dtt-clang-path nil
  "Path to clang++ executable.  If nil, auto-detected."
  :type '(choice (const nil) string)
  :group 'dtt)

(defcustom dtt-plugin-path nil
  "Path to DTT plugin.  If nil, auto-detected or downloaded."
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

(defcustom dtt-auto-download t
  "Automatically download missing binaries."
  :type 'boolean
  :group 'dtt)

(defconst dtt--llvm-version "18.1.8")
(defconst dtt--dtt-version "latest")

(defvar dtt--setup-done nil)
(defvar dtt--setup-in-progress nil)
(defvar dtt--checksums nil "Cached checksums from release.")

(defun dtt--compute-file-hash (file)
  "Compute SHA256 hash of FILE."
  (with-temp-buffer
    (set-buffer-multibyte nil)
    (insert-file-contents-literally file)
    (secure-hash 'sha256 (current-buffer))))

(defun dtt--fetch-checksums (callback)
  "Fetch checksums from release, then call CALLBACK with hash table."
  (let ((url (format "https://github.com/adishavit/dtt/releases/%s/download/checksums.txt" dtt--dtt-version)))
    (url-retrieve
     url
     (lambda (status)
       (if (plist-get status :error)
           (funcall callback (make-hash-table :test 'equal))
         (goto-char (point-min))
         (when (re-search-forward "\r?\n\r?\n" nil t)
           (let ((checksums (make-hash-table :test 'equal)))
             (while (re-search-forward "^\\([a-f0-9]\\{64\\}\\)\\s-+\\(.+\\)$" nil t)
               (puthash (string-trim (match-string 2)) (match-string 1) checksums))
             (funcall callback checksums)))))
     nil t t)))

(defun dtt--verify-checksum (file expected-hash filename callback)
  "Verify FILE has EXPECTED-HASH, call CALLBACK with result."
  (if (or (null expected-hash) (string-empty-p expected-hash))
      (progn
        (message "DTT: Warning - No checksum available for %s. Skipping verification." filename)
        (funcall callback t))
    (let ((actual-hash (dtt--compute-file-hash file)))
      (if (string-equal-ignore-case actual-hash expected-hash)
          (funcall callback t)
        (message "DTT: Checksum verification failed for %s!" filename)
        (message "  Expected: %s" expected-hash)
        (message "  Got: %s" actual-hash)
        (when (file-exists-p file) (delete-file file))
        (funcall callback nil)))))

(defun dtt--windows-p ()
  "Return non-nil if running on Windows."
  (memq system-type '(windows-nt ms-dos cygwin)))

(defun dtt--macos-p ()
  "Return non-nil if running on macOS."
  (eq system-type 'darwin))

(defun dtt--storage-dir ()
  "Return DTT storage directory."
  (expand-file-name "~/.dtt/bin"))

(defun dtt--find-clang ()
  "Find clang++ in PATH and common locations."
  (or dtt-clang-path
      (executable-find "clang++")
      (executable-find "clang++-18")
      (cl-find-if #'file-executable-p
                  (if (dtt--windows-p)
                      (list "C:/Program Files/LLVM/bin/clang++.exe"
                            "C:/LLVM/bin/clang++.exe"
                            (expand-file-name "~/.dtt/bin/llvm/bin/clang++.exe"))
                    (if (dtt--macos-p)
                        (list "/opt/homebrew/opt/llvm/bin/clang++"
                              "/usr/local/opt/llvm/bin/clang++"
                              "/opt/homebrew/bin/clang++"
                              (expand-file-name "~/.dtt/bin/llvm/bin/clang++"))
                      (list "/usr/bin/clang++-18"
                            "/usr/bin/clang++"
                            "/usr/local/bin/clang++"
                            (expand-file-name "~/.dtt/bin/llvm/bin/clang++")))))))

(defun dtt--find-plugin ()
  "Find DTT plugin in common locations."
  (or dtt-plugin-path
      (let ((ext (if (dtt--windows-p) ".dll" (if (dtt--macos-p) ".dylib" ".so")))
            (name (if (dtt--windows-p) "DTTPlugin" "libDTTPlugin")))
        (cl-find-if #'file-exists-p
                    (if (dtt--windows-p)
                        (list (expand-file-name (concat "~/.dtt/bin/" name ext))
                              (expand-file-name (concat (getenv "LOCALAPPDATA") "/DTT/bin/" name ext))
                              (expand-file-name (concat (or (getenv "ProgramFiles") "C:/Program Files") "/DTT/bin/" name ext)))
                      (list (expand-file-name (concat "~/.dtt/bin/" name ext))
                            (concat "/usr/local/lib/" name ext)
                            (concat "/usr/lib/" name ext)
                            (concat "/opt/homebrew/lib/" name ext)))))))

(defun dtt--download-file (url dest callback)
  "Download URL to DEST, then call CALLBACK with success status."
  (make-directory (file-name-directory dest) t)
  (message "DTT: Downloading %s..." (file-name-nondirectory dest))
  (url-retrieve
   url
   (lambda (status)
     (if (plist-get status :error)
         (progn
           (message "DTT: Download failed: %s" (plist-get status :error))
           (funcall callback nil))
       (goto-char (point-min))
       (re-search-forward "\r?\n\r?\n")
       (let ((data (buffer-substring (point) (point-max))))
         (with-temp-file dest
           (set-buffer-multibyte nil)
           (insert data))
         (message "DTT: Downloaded %s" (file-name-nondirectory dest))
         (funcall callback t))))
   nil t t))

(defun dtt--extract-archive (archive dest callback)
  "Extract ARCHIVE to DEST, then call CALLBACK."
  (make-directory dest t)
  (message "DTT: Extracting...")
  (let ((default-directory dest)
        (exit-code 0))
    (setq exit-code
          (if (string-match-p "\\.tar\\.xz$" archive)
              ;; tar with safe argument passing
              (call-process "tar" nil nil nil "-xf" archive "-C" dest "--strip-components=1" "--no-same-owner")
            (if (dtt--windows-p)
                ;; PowerShell: use -args to safely pass paths without injection risk
                (call-process "powershell" nil nil nil
                              "-NoProfile" "-NonInteractive"
                              "-ExecutionPolicy" "Bypass"
                              "-Command"
                              "Expand-Archive -LiteralPath $args[0] -DestinationPath $args[1] -Force"
                              "-args" archive dest)
              (call-process "unzip" nil nil nil "-q" "-o" archive "-d" dest))))
    (if (= exit-code 0)
        (progn
          (delete-file archive)
          (funcall callback t))
      (message "DTT: Extraction failed with code %d" exit-code)
      (when (file-exists-p archive) (delete-file archive))
      (funcall callback nil))))

(defun dtt--download-plugin (callback)
  "Download DTT plugin with checksum verification, then call CALLBACK with path or nil."
  (let* ((ext (if (dtt--windows-p) ".dll" (if (dtt--macos-p) ".dylib" ".so")))
         (name (if (dtt--windows-p) "DTTPlugin" "libDTTPlugin"))
         (asset (if (dtt--windows-p) "DTTPlugin-win64.dll"
                  (if (dtt--macos-p) "libDTTPlugin-darwin.dylib" "libDTTPlugin-linux.so")))
         (url (format "https://github.com/adishavit/dtt/releases/%s/download/%s" dtt--dtt-version asset))
         (dest (expand-file-name (concat (dtt--storage-dir) "/" name ext))))
    ;; Fetch checksums first, then download
    (dtt--fetch-checksums
     (lambda (checksums)
       (let ((expected-hash (gethash asset checksums)))
         (dtt--download-file
          url dest
          (lambda (ok)
            (if ok
                ;; Verify checksum before marking as successful
                (dtt--verify-checksum dest expected-hash asset
                  (lambda (verified)
                    (if verified
                        (progn
                          (unless (dtt--windows-p)
                            (set-file-modes dest #o755))
                          (message "DTT: Plugin installed!")
                          (funcall callback dest))
                      (funcall callback nil))))
              ;; Download failed
              (when (dtt--macos-p)
                (message "DTT: macOS binary not available. Build from source:")
                (message "  brew install llvm@18 z3")
                (message "  git clone https://github.com/adishavit/dtt && cd dtt")
                (message "  cmake -B build && cmake --build build")
                (message "  cp build/libDTTPlugin.dylib ~/.dtt/bin/"))
              (funcall callback nil)))))))))

(defun dtt--download-llvm (callback)
  "Download a published LLVM archive, or explain manual installation."
  (let* ((arm (string-match-p "arm\\|aarch" system-configuration))
         (suffix (cond
                  ((dtt--windows-p) (unless arm "x86_64-pc-windows-msvc"))
                  ((dtt--macos-p) (when arm "arm64-apple-macos11"))
                  (arm "aarch64-linux-gnu")
                  (t "x86_64-linux-gnu-ubuntu-18.04"))))
    (if (not suffix)
        (progn
          (message "DTT: Install a matching Clang 18 toolchain manually (macOS: brew install llvm@18)")
          (funcall callback nil))
      (let* ((filename (format "clang+llvm-%s-%s.tar.xz" dtt--llvm-version suffix))
             (url (format "https://github.com/llvm/llvm-project/releases/download/llvmorg-%s/%s"
                          dtt--llvm-version filename))
             (archive (expand-file-name (concat (dtt--storage-dir) "/llvm.tar.xz")))
             (llvm-dir (expand-file-name (concat (dtt--storage-dir) "/llvm"))))
        (message "DTT: Downloading LLVM %s..." dtt--llvm-version)
        (dtt--download-file
         url archive
         (lambda (ok)
           (if (not ok) (funcall callback nil)
             (dtt--extract-archive
              archive llvm-dir
              (lambda (extracted)
                (let ((clang-path (concat llvm-dir "/bin/clang++" (if (dtt--windows-p) ".exe" ""))))
                  (funcall callback (when (and extracted (file-executable-p clang-path)) clang-path))))))))))))

(defun dtt--ensure-binaries (callback)
  "Ensure clang and plugin are available, download if needed."
  (cond
   ;; Already in progress
   (dtt--setup-in-progress
    (funcall callback nil))

   ;; Both found - ready to go
   ((let ((clang (dtt--find-clang))
          (plugin (dtt--find-plugin)))
      (when (and clang plugin)
        (setq dtt-clang-path clang
              dtt-plugin-path plugin
              dtt--setup-done t)
        (funcall callback t)
        t)))

   ;; Auto-download disabled
   ((not dtt-auto-download)
    (unless (dtt--find-clang)
      (message "DTT: Clang not found. Install LLVM 18+ or set dtt-clang-path"))
    (unless (dtt--find-plugin)
      (message "DTT: Plugin not found. Set dtt-plugin-path"))
    (funcall callback nil))

   ;; Need to download
   (t
    (setq dtt--setup-in-progress t)
    (let* ((clang (dtt--find-clang))
           (plugin (dtt--find-plugin))
           (finish (lambda ()
                     (setq dtt-clang-path (or dtt-clang-path (dtt--find-clang))
                           dtt-plugin-path (or dtt-plugin-path (dtt--find-plugin))
                           dtt--setup-in-progress nil)
                     (if (and dtt-clang-path dtt-plugin-path)
                         (progn
                           (setq dtt--setup-done t)
                           (message "DTT: Ready!")
                           (funcall callback t))
                       (funcall callback nil)))))
      (if (not plugin)
          (dtt--download-plugin
           (lambda (plugin-path)
             (when plugin-path (setq dtt-plugin-path plugin-path))
             (if (not clang)
                 (dtt--download-llvm
                  (lambda (clang-path)
                    (when clang-path (setq dtt-clang-path clang-path))
                    (funcall finish)))
               (setq dtt-clang-path clang)
               (funcall finish))))
        (setq dtt-plugin-path plugin)
        (dtt--download-llvm
         (lambda (clang-path)
           (when clang-path (setq dtt-clang-path clang-path))
           (funcall finish))))))))

(defun dtt--plugin-args ()
  "Return plugin arguments for clang based on platform."
  (if (dtt--windows-p)
      (list "-Xclang" "-load" "-Xclang" dtt-plugin-path
            "-Xclang" "-add-plugin" "-Xclang" "dtt"
            "-Xclang" "-plugin-arg-dtt" "-Xclang" (concat "--dtt-mode=" dtt-mode-arg))
    (list (concat "-fplugin=" dtt-plugin-path)
          (concat "-fplugin-arg-dtt---dtt-mode=" dtt-mode-arg))))

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
  :predicate (lambda () dtt--setup-done))

;;;###autoload
(define-minor-mode dtt-mode
  "Minor mode for DTT memory safety checking."
  :lighter " DTT"
  :group 'dtt
  (if dtt-mode
      (progn
        (unless dtt--setup-done
          (dtt--ensure-binaries (lambda (_) (when dtt--setup-done
                                               (setq-local flycheck-dtt-executable dtt-clang-path)
                                               (flycheck-buffer)))))
        (setq-local flycheck-dtt-executable dtt-clang-path)
        (add-to-list 'flycheck-checkers 'dtt)
        (flycheck-mode 1))
    (setq flycheck-checkers (delq 'dtt flycheck-checkers))))

(provide 'dtt)
;;; dtt.el ends here
