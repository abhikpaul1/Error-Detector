# C Error Detector & Correction Suggester
Compiler Design Lab - Project Based Learning (PBL) for 5th Semester

Finds lexical, syntax and semantic errors in a C program and suggests how to fix them.
The whole tool is one standard-C file: error_detector.c

## Run on Online GDB or any other online compiler or on device
1. Open https://www.onlinegdb.com/online_c_compiler and choose C
2. Paste the contents of error_detector.c and click Run
3. Type 1-4 for the sample programs, or 5 to check your own program (finish it with a line END)

## Run with GCC
`gcc -o error_detector error_detector.c` then `./error_detector`

## What it checks
- Lexical: invalid characters, bad names and numbers, unclosed strings and comments, wrong #directives
- Syntax: unbalanced brackets, missing ;, = inside conditions, wrong for-loop header
- Semantic: undeclared or repeated names, misspelled keywords, scanf without &, printf/scanf format mismatch, division by zero

## Example
Line 7: Call to undeclared function 'prinf'
        -> Suggestion: Did you mean 'printf'?
