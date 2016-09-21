/******************************************************************************
** Copyright (c) 2016, Intel Corporation                                     **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Alexander Heinecke (Intel Corp.), Hans Pabst (Intel Corp.)
******************************************************************************/

/* use for-loops to potentially leverage NUMA in the future */
int i1, i2, i3, i4, i5, i6, i7, i8;
int splits = filter->splits;
int ifmb = filter->ifmb;
int bifm = filter->bifm;
int ofmb = filter->ofmb;
int bofm = filter->bofm;
int R = filter->R;
int S = filter->S;
int lpb = filter->lpb;

LIBXSMM_VLA_DECL(8, element_type, handle_data, filter->data, ofmb, ifmb, R, S, bifm, bofm, lpb);
LIBXSMM_VLA_DECL(5, const element_type, user_data, (element_type*)data, ofmb * bofm, ifmb * bifm * lpb, R, S);

for (i1 = 0; i1 < splits; ++i1) {
  for (i2 = 0; i2 < ofmb; ++i2) {
    for (i3 = 0; i3 < ifmb; ++i3) {
      for (i4 = 0; i4 < R; ++i4) {
        for (i5 = 0; i5 < S; ++i5) {
          for (i6 = 0; i6 < bifm; ++i6) {
            for (i7 = 0; i7 < bofm; ++i7) {
              for (i8 = 0; i8 < lpb; ++i8) {
                LIBXSMM_VLA_ACCESS(8, handle_data, i1, i2, i3, i4, i5, i6, i7, i8, ofmb, ifmb, R, S, bifm, bofm, lpb) =
                LIBXSMM_VLA_ACCESS(5, user_data, i1, i2 * bofm + i7, (i3*bifm*lpb) + (i6*lpb) + i8, i4, i5, ofmb * bofm, ifmb * bifm * lpb, R, S);
              }
            }
          }
        }
      }
    }
  }
}
