Project 2 README

Juan Vega

Files:
produce_consume.c - bounded-buffer producer/consumer 
mother_hubbard.c - two-thread sequencing with semaphores
airline.c - staged passenger processing with counting semaphores
Makefile - builds all programs
Prj2README - this file

Compilation:
run make to build all programs

Usage:
./prod_cons
./mh <number_of_days>
./airline <passengers> <handlers> <screeners> <attendants>

Observations:

Producer/Consumer:

Prints a stream of characters and then a newline. As N productions increase so execution time



Mother Hubbard:

Each day starts with a day header and “Mother wakes up.”

Mother processes all 12 children in order for: breakfast ->school -> dinner -> bath.

Father wakes up after baths begin, then reads and tucks each child (in order).


Airline:

Passengers announce arrival, then wait at each stage baggage, screening, boarding.

With limited workers, passengers block until capacity is free (program runs slower)

When all passengers are seated, a final “plane takes off” message appears.

