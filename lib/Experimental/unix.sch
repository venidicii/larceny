; $Id$
;
; Some Solaris 2.6 and Linux 2.4 system and library calls, and some
; essential constants, alpha by name.
;
; This is handcoded and ad-hock.  Except for hair like execl, these could
; and should all have been generated by FFIGEN -- at least by some other
; mechanism than hand-coding!

(require 'std-ffi)
(require 'srfi-0)

(require 'foreign-ctools)

(cond-expand
 (macosx
  (foreign-file "/usr/lib/libresolv.dylib"))
 (linux
  (foreign-file "/usr/lib/libresolv.so"))
 (solaris
  (foreign-file "/lib/libsocket.so")
  (foreign-file "/lib/libxnet.so")))

; accept(3XN)
; int accept( int s, struct sockaddr *addr, int *addrlen )

(define unix/accept (foreign-procedure "accept" '(int boxed boxed) 'int))


; bind(3XN)
; int bind( int s, struct sockaddr *name, int namelen )

(define unix/bind (foreign-procedure "bind" '(int boxed int) 'int))


; close(2)
; int close( int fd )

(define unix/close (foreign-procedure "close" '(int) 'int))


; connect(3XN)
; int connect( int s, struct sockaddr *name, int namelen )

(define unix/connect (foreign-procedure "connect" '(int boxed int) 'int))


; dup2(3C)
; int dup2( int fildes, inf fildes2 )

(define unix/dup2 (foreign-procedure "dup2" '(int int) 'int))


; execl(2)
;
; execl( char *path, char *arg0, ..., char *argn, char * /* NULL */ )
;
; Example: (unix/execl "/usr/bin/ksh" "ksh" "-c" "echo Hello, sailor!")
;
; The cache is a hack that is necessary only because foreign functions
; are not (yet) weakly held, thus repeated use of vfork(2) will cause 
; foreign functions to be accumulated in the parent's address space.

(define unix/execl
  (let ((cache '#()))
    (lambda (path . args)
      (let ((l (length args)))
	(if (>= l (vector-length cache))
	    (let ((new-cache (make-vector (+ l 1) #f)))
	      (do ((i 0 (+ i 1)))
		  ((= i (vector-length cache)))
		(vector-set! new-cache i (vector-ref cache i)))
	      (set! cache new-cache)))
	(if (not (vector-ref cache l))
	    (vector-set! cache l
			 (foreign-procedure
			  "execl" (make-list (+ l 2) 'string) 'int)))
	(apply (vector-ref cache l) path (append! args '(#f)))))))


; fork(2)
; pid_t fork( void )

(define unix/fork (foreign-procedure "fork" '() 'int))


; listen(3XN)
; int listen( int s, int backlog )

(define unix/listen (foreign-procedure "listen" '(int int) 'int))


; open(2)
; int open( const char *path, int oflag )
; int open( const char *path, int oflag, mode_t mode )

(define unix/open
  (let ((open (foreign-procedure "open" '(string int int) 'int)))
    (lambda (path oflag . rest)
      (if (null? rest)
          (open path oflag #o644)
          (open path oflag (car rest))))))

; Flags for oflag parameter.

(define-c-info (include<> "fcntl.h")
  (const unix/O_RDONLY int "O_RDONLY")
  (const unix/O_WRONLY int "O_WRONLY")
  (const unix/O_RDWR   int "O_RDWR")
  (const unix/O_APPEND int "O_APPEND")
  (const unix/O_CREAT  int "O_CREAT")
  (const unix/O_TRUNC  int "O_TRUNC"))


; perror(3c)
; void perror( const char * )

(define unix/perror (foreign-procedure "perror" '(string) 'void))


; pipe(2)
; int pipe( int fildes[2] )

(define unix/pipe 
  (let ((pipe (foreign-procedure "pipe" '(boxed) 'int)))
    (lambda ()
      (let ((array (make-bytevector (* sizeof:int 2))))
        (let ((r (pipe array)))
          (if (= r -1)
              (values -1 -1 -1)
              (values r (%get-int array 0) (%get-int array sizeof:int))))))))


; poll(2)
; int poll( struct pollfd fds[], unsigned long nfds, int timeout )

(define unix/poll (foreign-procedure "poll" '(boxed ulong int) 'int))

(define-c-info (include<> "poll.h")
  (const unix/POLLIN     int "POLLIN")
  (const unix/POLLPRI    int "POLLPRI")
  (const unix/POLLOUT    int "POLLOUT")
  (const unix/POLLRDNORM int "POLLRDNORM")
  (const unix/POLLWRNORM int "POLLWRNORM")
  (const unix/POLLRDBAND int "POLLRDBAND")
  (const unix/POLLWRBAND int "POLLWRBAND")
  (const unix/POLLERR    int "POLLERR")
  (const unix/POLLNVAL   int "POLLNVAL"))

; read(2)
; int read( int fd, void *buf, int n )

(define unix/read (foreign-procedure "read" '(int boxed int) 'int))


; setsockopt(3XN)
; int setsockopt( int s, int level, int option_name, void *value, unsigned len)

(define unix/setsockopt
  (foreign-procedure "setsockopt" '(int int int boxed unsigned) 'int))


; socket(3XN)
; int socket( int domain, int type, int protocol )

(define unix/socket (foreign-procedure "socket" '(int int int) 'int))


; stat(2)
; int stat( const char *path, struct stat *buf );

'(cond-expand
 (linux

  (define unix/stat 
    (let ((_stat (foreign-procedure "__xstat" '(int string boxed) 'int)))
      (lambda (name buf)
	(_stat 0 name buf)))) )

 (solaris

  (define unix/stat
    (foreign-procedure "stat" '(string boxed) 'int)) ))

(define unix/stat
  (foreign-procedure "stat" '(string boxed) 'int))

; Symbolic constants from sys/stat.h on Solaris 2.6; they are the
; same on Linux 2.4 (but there written in octal :-)

(define-c-info (include<> "sys/stat.h")
  (const unix/S_IFIFO    int "S_IFIFO")  ; fifo
  (const unix/S_IFCHR    int "S_IFCHR")  ; character special
  (const unix/S_IFDIR    int "S_IFDIR")  ; directory
  (const unix/S_IFBLK    int "S_IFBLK")  ; block special
  (const unix/S_IFREG    int "S_IFREG")) ; regular

; strerror(3c)
; char *strerror( int )

(define unix/strerror (foreign-procedure "strerror" '(int) 'string))


; wait(2)
; pid_t wait( int *stat_loc )
;
; Returns { return value, *stat_loc }

(define unix/wait
  (let ((wait (foreign-procedure "wait" '(boxed) 'int)))
    (lambda ()
      (let ((buffer (make-bytevector sizeof:int)))
        (let ((r (wait buffer)))
          (values r (%get-int buffer 0)))))))


; waitpid(2)
; pid_t waitpid(pid_t pid, int *stat_loc, int options)
;
; Options defaults to 0.
; Returns { return value, *stat_loc }

(define unix/waitpid
  (let ((waitpid (foreign-procedure "waitpid" '(int boxed int) 'int)))
    (lambda (pid . rest)
      (let ((options (if (null? rest) 0 (car rest))))
        (let ((buffer (make-bytevector sizeof:int)))
          (let ((r (waitpid pid buffer options)))
            (values r (%get-int buffer 0))))))))


; Flags to pass as options to waitpid()
; sys/wait.h & bits/waitflags.h

(define-c-info (include<> "sys/wait.h")
  (const unix/WNOHANG    int "WNOHANG")
  (const unix/WUNTRACED  int "WUNTRACED")
  (const unix/WCONTINUED int "WCONTINUED")
  (const unix/WNOWAIT    int "WNOWAIT"))

; Macros to process the exit status of wait, waitpid
; sys/wait.h & bits/waitflags.h

(define (unix/WEXITSTATUS stat) 
  (fxlogand (fxrsha stat 8) #xFF))

(define (unix/WTERMSIG stat) 
  (not (zero? (fxlogand stat #x07F))))

(define (unix/WSTOPSIG stat) 
  (fxlogand (fxrsha stat 8) #xFF))

(cond-expand 
 (linux
  (define (unix/WIFEXITED stat)
    (zero? stat))

  (define (unix/WIFSIGNALED stat)
    (and (not (unix/WIFSTOPPED stat))
	 (not (unix/WIFEXITED stat))))

  (define (unix/WIFSTOPPED stat)
    (= #x7F (fxlogand stat #xFF))) )

 (macosx
  (define (unix/WIFEXITED stat)
    (zero? (fxlogand stat #o177)))

  (define (unix/WIFSIGNALED stat)
    (and (not (= (fxlogand stat #o177) #o177))
	 (not (= (fxlogand stat #o177) 0))))

  (define (unix/WIFSTOPPED stat)
    (= (fxlogand stat #o177) #o177))

  (define (unix/WIFCONTINUED stat)
    (= stat #x13)))

 (solaris
  (define (unix/WIFEXITED stat) 
    (zero? (fxlogand stat #xFF))) 

  (define (unix/WIFSIGNALED stat) 
    (and (positive? (fxlogand stat #xFF)) 
	 (zero? (fxlogand stat #xFF00)))) 

  (define (unix/WIFSTOPPED stat) 
    (and (= (fxlogand stat #xFF) #o177)
	 (not (zero? (fxlogand stat #xFF00))))) 

  (define (unix/WIFCONTINUED stat) 
    (= (fxlogand stat #o177777) #o177777)) ))

(define (unix/WCOREDUMP stat) 
  (not (zero? (fxlogand stat #o200))))

  
; write(2)
; int write( int fd, void *buf, int n )

(define unix/write (foreign-procedure "write" '(int boxed int) 'int))


; Helpers

(define (c-variable-getter varname locname)
  (cond ((equal? "Linux" (cdr (assq 'os-name (system-features))))
         ;; On Linux, varname is a macro that expands into a procedure
         ;; that returns a ptr to thread-local errno-value.
         (let ((get-loc (foreign-procedure locname '() 'uint)))
           (lambda ()
             (%peek-pointer (get-loc)))))
        (else
         (foreign-variable varname 'int))))

; int get_errno( void )
; Returns errno.

(define get-errno
  (c-variable-getter "errno" "__errno_location"))

; int get_h_errno( void )
; Returns h_errno.

(define get-h-errno
  (c-variable-getter "h_errno" "__h_errno_location"))

; eof
