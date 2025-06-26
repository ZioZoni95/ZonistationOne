# Add this to your Makefile for the log splitter utility
split_log: split_log.c
	gcc -o split_log split_log.c 