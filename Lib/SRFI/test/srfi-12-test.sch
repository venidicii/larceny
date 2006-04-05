(define (test-composite)
  (let* ((cs-key (list 'color-scheme))
         (bg-key (list 'background))
         (color-scheme? (condition-predicate cs-key))
         (color-scheme-background 
          (condition-property-accessor cs-key bg-key))
         (condition1 (make-property-condition cs-key bg-key 'green))
         (condition2 (make-property-condition cs-key bg-key 'blue))
         (condition3 (make-composite-condition condition1 condition2)))
    (and (color-scheme? condition1)
         (color-scheme? condition2)
         (color-scheme? condition3)
         (color-scheme-background condition3))))