(import (tests r6rs arithmetic fixnums)
        (tests scheme test)
        (scheme write))
(display "Running tests for (rnrs arithmetic fixnums)\n")
(run-arithmetic-fixnums-tests)
(report-test-results)
