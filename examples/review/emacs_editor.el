;;; -*- lexical-binding: t; -*-
(require 'ert)
(load-file (expand-file-name "ide-plugins/emacs-dtt/dtt.el"))

(ert-deftest dtt-installer-entry-points-are-defined ()
  (dolist (name '(dtt--download-plugin dtt--download-llvm dtt--ensure-binaries))
    (should (fboundp name))))

(ert-deftest dtt-llvm-published-assets ()
  (dolist (variant '((gnu/linux "x86_64-linux" "x86_64-linux-gnu-ubuntu-18.04")
                     (gnu/linux "aarch64-linux" "aarch64-linux-gnu")
                     (darwin "aarch64-apple" "arm64-apple-macos11")
                     (windows-nt "x86_64-windows" "x86_64-pc-windows-msvc")
                     (darwin "x86_64-apple" nil)
                     (windows-nt "aarch64-windows" nil)))
    (let ((system-type (nth 0 variant)) (system-configuration (nth 1 variant))
          (expected (nth 2 variant)) url called)
      (cl-letf (((symbol-function 'dtt--download-file)
                 (lambda (address _dest callback) (setq url address) (funcall callback nil))))
        (dtt--download-llvm (lambda (_) (setq called t))))
      (should called)
      (if expected
          (should (string-suffix-p (concat expected ".tar.xz") url))
        (should-not url)))))

(ert-run-tests-batch-and-exit)
