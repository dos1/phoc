/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "util.h"

#include <glib.h>

/* 
 * phoc_generate_random_id:: 
 *  
 * Generate a random string of printable ASCII characters.
 */
char *
phoc_generate_random_id (GRand *rand,
                         int    length)
{
  char *id;
  int i;

  id = g_new0 (char, length + 1);
  for (i = 0; i < length; i++)
    id[i] = (char) g_rand_int_range (rand, 32, 127);

  return id;
}

