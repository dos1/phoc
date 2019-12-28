/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

char *phoc_generate_random_id (GRand *rand, int    length);

G_END_DECLS
