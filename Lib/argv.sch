; Lib/argv.sch
; Larceny library -- command line arguments
;
; $Id: argv.sch,v 1.2 1997/07/18 13:55:49 lth Exp $
;
; The argument vector is a vector of strings, one for each command line 
; argument.  What constitutes an argument is up to the user's shell or
; the C language startup.

($$trace "argv")

(define *argv* '#())

(define (command-line-arguments . rest)
  (cond ((null? rest)
	 *argv*)
	((null? (cdr rest))
	 (let ((old *argv*))
	   (set! *argv* (car rest))
	   old))
	(else
	 (error "Too many arguments to procedure command-line-arguments.")
	 #t)))

; eof
