; Larceny/compat.sch
; Larceny -- compatibility library for Twobit running under Larceny.
;
; $Id: compat.sch,v 1.2 1997/08/22 21:04:25 lth Exp $

(define host-system 'larceny)

(define (compat:initialize)
  #t)

(define (compat:initialize2)
;  (peephole-optimization #f)
  #t
  )

(define (with-optimization level thunk) 
  (thunk))

; Calls thunk1, and if thunk1 causes an error to be signalled, calls thunk2.

(define (call-with-error-control thunk1 thunk2) 
  (let ((eh (error-handler)))
    (error-handler (lambda args
		     (error-handler eh)
		     (thunk2)
		     (apply eh args)))
    (thunk1)
    (error-handler eh)))

(define (larc-new-extension fn ext)
  (let* ((l (string-length fn))
	 (x (let loop ((i (- l 1)))
	      (cond ((< i 0) #f)
		    ((char=? (string-ref fn i) #\.) (+ i 1))
		    (else (loop (- i 1)))))))
    (if (not x)
	(string-append fn "." ext)
	(string-append (substring fn 0 x) ext))))

(define (compat:load filename)
  (define (loadit fn)
    (if *verbose-load*
	(format #t "~a~%" fn))
    (load fn))
  (if *always-load-scheme-code*
      (loadit filename)
      (let ((fn (larc-new-extension filename "fasl")))
	(if (and (file-exists? fn)
		 (compat:file-newer? fn filename))
	    (loadit fn)
	    (loadit filename)))))

(define (compat:file-newer? a b)
  (let* ((ta    (file-modification-time a))
	 (tb    (file-modification-time b))
	 (limit (vector-length ta)))
    (let loop ((i 0))
      (cond ((= i limit)
	     #f)
	    ((= (vector-ref ta i) (vector-ref tb i))
	     (loop (+ i 1)))
	    (else
	     (> (vector-ref ta i) (vector-ref tb i)))))))

; I have split the file here so that the second part can be compiled; 
; compat:load will load the fasl file when appropriate.

(compat:load "Larceny/compat2.sch")
(compat:load "Auxlib/misc.sch")
(compat:load "Auxlib/sort.sch")
(compat:load "Auxlib/pp.sch")

; eof
