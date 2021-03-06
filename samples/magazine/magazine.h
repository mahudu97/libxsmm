/******************************************************************************
* Copyright (c) Intel Corporation - All rights reserved.                      *
* This file is part of the LIBXSMM library.                                   *
*                                                                             *
* For information on the license, see the LICENSE file.                       *
* Further information: https://github.com/hfp/libxsmm/                        *
* SPDX-License-Identifier: BSD-3-Clause                                       *
******************************************************************************/
/* Hans Pabst (Intel Corp.)
******************************************************************************/
#ifndef MAGAZINE_H
#define MAGAZINE_H

#include <stdio.h>

#if !defined(TYPE)
# define TYPE double
#endif

#if 1
# define STREAM_A(EXPR) (EXPR)
#else
# define STREAM_A(EXPR) 0
#endif
#if 1
# define STREAM_B(EXPR) (EXPR)
#else
# define STREAM_B(EXPR) 0
#endif
#if 0
# define STREAM_C(EXPR) (EXPR)
#else
# define STREAM_C(EXPR) 0
/* synchronization among C matrices */
# define SYNC
#endif

#if 1 /* PAD (alignment) must be power of two */
# define PAD 64
#else
# define PAD 1
#endif


static void init(int seed, TYPE* dst, int nrows, int ncols, int ld, double scale) {
  const double seed1 = scale * seed + scale;
  int i;
  for (i = 0; i < ncols; ++i) {
    int j = 0;
    for (; j < nrows; ++j) {
      const int k = i * ld + j;
      dst[k] = (TYPE)(seed1 / (1.0 + k));
    }
    for (; j < ld; ++j) {
      const int k = i * ld + j;
      dst[k] = (TYPE)(seed);
    }
  }
}


static double norm(const TYPE* src, int nrows, int ncols, int ld) {
  int i, j;
  double result = 0, comp = 0;
  for (i = 0; i < ncols; ++i) {
    for (j = 0; j < nrows; ++j) {
      const int k = i * ld + j;
      const double v = src[k], a = (0 <= v ? v : -v) - comp, b = result + a;
      comp = (b - result) - a;
      result = b;
    }
  }
  return result;
}

#endif /*MAGAZINE_H*/

