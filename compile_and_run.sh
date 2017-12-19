#bin/bash

echo "Compiling BST execution program"
g++ -std=c++11 helper.cpp BST.cpp sharing.cpp -pthread -o bst -mrtm -mhle -mrdrnd


echo "Launching.."
./bst
