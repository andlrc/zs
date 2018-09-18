/*
 * zs - work with, and move objects from one AS/400 to another.
 * file is used for testing zs
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */

#ifndef UTIL_H
#define UTIL_H 1
int
runcmd(int *exit_status, char **stdout, char **stderr, char *const *cmd);
#endif
