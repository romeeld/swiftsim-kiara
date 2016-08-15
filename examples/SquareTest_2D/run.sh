#!/bin/bash

 # Generate the initial conditions if they are not present.
if [ ! -e square.hdf5 ]
then
    echo "Generating initial conditions for the square test ..."
    python makeIC.py
fi

# Run SWIFT
../swift -s -t 1 square.yml

# Plot the solution
python plotSolution.py 40
