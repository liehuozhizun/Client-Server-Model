asgn0 grade report for cmpe105/winter19/hyuan3: commit 4b361c0da4fd28ddff775ce2d496fe8fa84e6ee5
# DESIGN DOCUMENT

Some over-complication with error message handling, and special casing of error conditions.

# WRITEUP

We were looking for "abstraction". You're right that there's little code difference between the two
cases. This is because the internals are abstracted behind the file interface.

# AUTOMATED TESTER RESULTS
Test "cat with no arguments" (./mycat ): 4
Test "simple, single file" (./mycat tf-simple-1): 5
Test "simple, multiple files" (./mycat tf-simple-1 tf-simple-2 tf-simple-3): 4
Test "big file" (./mycat words.txt): 2
Test "multiple big file" (./mycat words.txt words.txt words.txt): 2
Test "random file" (./mycat tf-random): 3
Test "zero-containing file" (./mycat tf-zero): 3
Test "no-newline files" (./mycat tf-nnl-1 tf-nnl-2): 3
Test "err directory" (./mycat .): 1
Test "err permission" (./mycat /etc/shadow): 1
Test "err no-exist" (./mycat fowenfinweofnowefn): 1
Test "err read" (./mycat Makefile): 1
Test "err read stdin" (./mycat ): 1
Test "err write" (./mycat Makefile): 0

=> TOTAL POINTS: 31

# Code Review

for (int32_t i = 1; i < argc; i++) {
match type of argc


Assignment allows use of fprintf for error messages.

Error messages should go to stderr, not stdout.


stdin handling and file handling for read and write are similar; could be combined.


Whenever you have a loop with an if-else inside with several breaks, you can usually redesign the
loop to get rid of the breaks.



# Adjustments
submission time score adjustment (submitted: 1/17/2019 0:55:22)EARLY by 40 for 5 extra points.
