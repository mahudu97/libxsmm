/******************************************************************************
** Copyright (c) 2015-2019, Intel Corporation                                **
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
/* Alexander Heinecke (Intel Corp.)
******************************************************************************/
#include "generator_spgemm_csr_asparse_reg.h"
#include "generator_x86_instructions.h"
#include "generator_gemm_common.h"
#include "libxsmm_main.h"

#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(push,target(LIBXSMM_OFFLOAD_TARGET))
#endif
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(pop)
#endif

#define LN_BLOCKING 2
#define LM_BLOCKING 3
#define BCAST_REG 23
#define ACC_REG 26
#define MAX_UNIQUE_DP 184
#define MAX_UNIQUE_SP 368

LIBXSMM_API_INTERN
void libxsmm_mmfunction_signature_asparse_reg( libxsmm_generated_code*        io_generated_code,
                                               const char*                    i_routine_name,
                                               const libxsmm_gemm_descriptor* i_xgemm_desc ) {
  char l_new_code[512];
  int l_max_code_length = 511;
  int l_code_length = 0;

  if ( io_generated_code->code_type > 1 ) {
    return;
  } else if ( io_generated_code->code_type == 1 ) {
    l_code_length = LIBXSMM_SNPRINTF(l_new_code, l_max_code_length, ".global %s\n.type %s, @function\n%s:\n", i_routine_name, i_routine_name, i_routine_name);
  } else {
    /* selecting the correct signature */
    if (LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
      if (LIBXSMM_GEMM_PREFETCH_NONE == i_xgemm_desc->prefetch) {
        l_code_length = LIBXSMM_SNPRINTF(l_new_code, l_max_code_length, "void %s(const float* A, const float* B, float* C) {\n", i_routine_name);
      } else {
        l_code_length = LIBXSMM_SNPRINTF(l_new_code, l_max_code_length, "void %s(const float* A, const float* B, float* C, const float* A_prefetch, const float* B_prefetch, const float* C_prefetch) {\n", i_routine_name);
      }
    } else {
      if (LIBXSMM_GEMM_PREFETCH_NONE == i_xgemm_desc->prefetch) {
        l_code_length = LIBXSMM_SNPRINTF(l_new_code, l_max_code_length, "void %s(const double* A, const double* B, double* C) {\n", i_routine_name);
      } else {
        l_code_length = LIBXSMM_SNPRINTF(l_new_code, l_max_code_length, "void %s(const double* A, const double* B, double* C, const double* A_prefetch, const double* B_prefetch, const double* C_prefetch) {\n", i_routine_name);
      }
    }
  }

  libxsmm_append_code_as_string( io_generated_code, l_new_code, l_code_length );
}

LIBXSMM_API_INTERN
void libxsmm_generator_spgemm_csr_asparse_reg( libxsmm_generated_code*         io_generated_code,
                                               const libxsmm_gemm_descriptor*  i_xgemm_desc,
                                               const char*                     i_arch,
                                               const unsigned int*             i_row_idx,
                                               const unsigned int*             i_column_idx,
                                               const double*                   i_values ) {
  unsigned int l_m;
  unsigned int l_mm;
  unsigned int l_n;
  unsigned int l_z;
  unsigned int largest_lz;
  unsigned int l_row_elements[LM_BLOCKING];
  unsigned int l_unique;
  unsigned int l_reg_num;
  unsigned int l_hit;
  unsigned int l_n_blocking = LN_BLOCKING;
  unsigned int l_n_row_idx = i_row_idx[i_xgemm_desc->m];
  double *const l_unique_values = (double*)(0 != l_n_row_idx ? malloc(sizeof(double) * l_n_row_idx) : NULL);
  unsigned int *const l_unique_pos = (unsigned int*)(0 != l_n_row_idx ? malloc(sizeof(unsigned int) * l_n_row_idx) : NULL);
  double l_code_const_dp[8];
  float l_code_const_fp[16];

  libxsmm_micro_kernel_config l_micro_kernel_config;
  libxsmm_loop_label_tracker l_loop_label_tracker;
  libxsmm_gp_reg_mapping l_gp_reg_mapping;

  /* check if mallocs were successful */
  if ( 0 == l_unique_values || 0 == l_unique_pos ) {
    free(l_unique_values); free(l_unique_pos);
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_CSR_ALLOC_DATA );
    return;
  }

  /* check that we build for AVX512 */
  if ( (strcmp(i_arch, "knl") != 0) &&
       (strcmp(i_arch, "knm") != 0) &&
       (strcmp(i_arch, "skx") != 0) &&
       (strcmp(i_arch, "clx") != 0) &&
       (strcmp(i_arch, "cpx") != 0) ) {
    free(l_unique_values); free(l_unique_pos);
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_ARCH );
    return;
  } else {
    if ( strcmp(i_arch, "knl") == 0 ) {
      io_generated_code->arch = LIBXSMM_X86_AVX512_MIC;
    } else if ( strcmp(i_arch, "knm") == 0 ) {
      io_generated_code->arch = LIBXSMM_X86_AVX512_KNM;
    } else if ( strcmp(i_arch, "skx") == 0 ) {
      io_generated_code->arch = LIBXSMM_X86_AVX512_CORE;
    } else if ( strcmp(i_arch, "clx") == 0 ) {
      io_generated_code->arch = LIBXSMM_X86_AVX512_CLX;
    } else if ( strcmp(i_arch, "cpx") == 0 ) {
      io_generated_code->arch = LIBXSMM_X86_AVX512_CPX;
    } else {
      /* cannot happen */
    }
  }

  /* prerequisite */
  assert(0 != i_values);

  /* Let's figure out how many unique values we have */
  l_unique = 1;
  l_unique_values[0] = i_values[0];
  l_unique_pos[0] = 0;
  for ( l_m = 1; l_m < l_n_row_idx; l_m++ ) {
    l_hit = 0;
    /* search for the value */
    for ( l_z = 0; l_z < l_unique; l_z++) {
      if ( /*l_unique_values[l_z] == i_values[l_m]*/!(l_unique_values[l_z] < i_values[l_m]) && !(l_unique_values[l_z] > i_values[l_m]) ) {
        l_unique_pos[l_m] = l_z;
        l_hit = 1;
      }
    }
    /* values was not found */
    if ( l_hit == 0 ) {
      l_unique_values[l_unique] = i_values[l_m];
      l_unique_pos[l_m] = l_unique;
      l_unique++;
    }
  }

  /* check that we have enough registers for the datatype */
  if ( (LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) && l_unique > MAX_UNIQUE_DP) ||
       (LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) && l_unique > MAX_UNIQUE_SP) ) {
    free(l_unique_values); free(l_unique_pos);
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_UNIQUE_VAL );
    return;
  }

  /* define gp register mapping */
  libxsmm_reset_x86_gp_reg_mapping( &l_gp_reg_mapping );
#if defined(_WIN32) || defined(__CYGWIN__)
  l_gp_reg_mapping.gp_reg_a = LIBXSMM_X86_GP_REG_RCX;
  l_gp_reg_mapping.gp_reg_b = LIBXSMM_X86_GP_REG_RDX;
  l_gp_reg_mapping.gp_reg_c = LIBXSMM_X86_GP_REG_R8;
  /* TODO: full support for Windows calling convention */
  l_gp_reg_mapping.gp_reg_a_prefetch = LIBXSMM_X86_GP_REG_R9;
  l_gp_reg_mapping.gp_reg_b_prefetch = LIBXSMM_X86_GP_REG_RDI;
  l_gp_reg_mapping.gp_reg_c_prefetch = LIBXSMM_X86_GP_REG_RSI;
#else /* match calling convention on Linux */
  l_gp_reg_mapping.gp_reg_a = LIBXSMM_X86_GP_REG_RDI;
  l_gp_reg_mapping.gp_reg_b = LIBXSMM_X86_GP_REG_RSI;
  l_gp_reg_mapping.gp_reg_c = LIBXSMM_X86_GP_REG_RDX;
  l_gp_reg_mapping.gp_reg_a_prefetch = LIBXSMM_X86_GP_REG_RCX;
  l_gp_reg_mapping.gp_reg_b_prefetch = LIBXSMM_X86_GP_REG_R8;
  l_gp_reg_mapping.gp_reg_c_prefetch = LIBXSMM_X86_GP_REG_R9;
#endif
  l_gp_reg_mapping.gp_reg_mloop = LIBXSMM_X86_GP_REG_R12;
  l_gp_reg_mapping.gp_reg_nloop = LIBXSMM_X86_GP_REG_R13;
  l_gp_reg_mapping.gp_reg_kloop = LIBXSMM_X86_GP_REG_R14;
  l_gp_reg_mapping.gp_reg_help_0 = LIBXSMM_X86_GP_REG_UNDEF;
  l_gp_reg_mapping.gp_reg_help_1 = LIBXSMM_X86_GP_REG_UNDEF;
  l_gp_reg_mapping.gp_reg_help_2 = LIBXSMM_X86_GP_REG_UNDEF;
  l_gp_reg_mapping.gp_reg_help_3 = LIBXSMM_X86_GP_REG_UNDEF;
  l_gp_reg_mapping.gp_reg_help_4 = LIBXSMM_X86_GP_REG_UNDEF;
  l_gp_reg_mapping.gp_reg_help_5 = LIBXSMM_X86_GP_REG_UNDEF;

  /* define loop_label_tracker */
  libxsmm_reset_loop_label_tracker( &l_loop_label_tracker );

  /* define the micro kernel code gen properties */
  libxsmm_generator_gemm_init_micro_kernel_config_fullvector( &l_micro_kernel_config, io_generated_code->arch, i_xgemm_desc, 0 );

  /* inner chunk size */
  if ( i_xgemm_desc->n != l_micro_kernel_config.vector_length ) {
    free(l_unique_values); free(l_unique_pos);
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_N_BLOCK );
    return;
  }

  /* open asm */
  libxsmm_x86_instruction_open_stream( io_generated_code, &l_gp_reg_mapping, i_xgemm_desc->prefetch );

  /* load A into registers */
  l_z = 0;
  l_reg_num = 0;
  while (l_z < l_unique) {
    char l_id[65];
    LIBXSMM_SNPRINTF(l_id, 64, "%u", l_reg_num);
    l_m = 0;

    if ( LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
      while (l_z < l_unique && l_m < 8) {
        l_code_const_dp[l_m++] = l_unique_values[l_z++];
      }
      libxsmm_x86_instruction_full_vec_load_of_constants( io_generated_code,
                                                          (unsigned char*)l_code_const_dp,
                                                          l_id,
                                                          l_micro_kernel_config.vector_name,
                                                          l_reg_num++ );
    } else {
      while (l_z < l_unique && l_m < 16) {
        l_code_const_fp[l_m++] = (float)l_unique_values[l_z++];
      }
      libxsmm_x86_instruction_full_vec_load_of_constants( io_generated_code,
                                                          (unsigned char*)l_code_const_fp,
                                                          l_id,
                                                          l_micro_kernel_config.vector_name,
                                                          l_reg_num++ );
    }
  }

  /* n loop */
#if 0
  libxsmm_x86_instruction_register_jump_back_label( io_generated_code, &l_loop_label_tracker );
  libxsmm_x86_instruction_alu_imm( io_generated_code, l_micro_kernel_config.alu_add_instruction, l_gp_reg_mapping.gp_reg_nloop, l_n_blocking );
#endif

  for ( l_m = 0; l_m < (unsigned int)i_xgemm_desc->m; l_m+=LM_BLOCKING ) {
    largest_lz = 0;
    for ( l_mm=l_m; l_mm < l_m+LM_BLOCKING; l_mm++) {
      if ( l_mm < (unsigned int)i_xgemm_desc->m ) {
        l_row_elements[l_mm-l_m] = i_row_idx[l_mm+1] - i_row_idx[l_mm];
        if (l_row_elements[l_mm-l_m] > 0) {
          largest_lz = (l_row_elements[l_mm-l_m] > largest_lz) ? l_row_elements[l_mm-l_m] : largest_lz;
          for ( l_n = 0; l_n < l_n_blocking; l_n++ ) {
            /* load C or reset to 0 depending on beta */
            if (0 == (LIBXSMM_GEMM_FLAG_BETA_0 & i_xgemm_desc->flags)) { /* Beta=1 */
              libxsmm_x86_instruction_vec_move( io_generated_code,
                                                l_micro_kernel_config.instruction_set,
                                                l_micro_kernel_config.c_vmove_instruction,
                                                l_gp_reg_mapping.gp_reg_c,
                                                LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                l_mm*i_xgemm_desc->ldc*l_micro_kernel_config.datatype_size +
                                                  l_n*l_micro_kernel_config.datatype_size*l_micro_kernel_config.vector_length,
                                                l_micro_kernel_config.vector_name,
                                                ACC_REG+(l_n*LM_BLOCKING)+(l_mm-l_m), 0, 1, 0 );
            } else {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                                      l_micro_kernel_config.instruction_set,
                                                      l_micro_kernel_config.vxor_instruction,
                                                      l_micro_kernel_config.vector_name,
                                                      ACC_REG+(l_n*LM_BLOCKING)+(l_mm-l_m),
                                                      ACC_REG+(l_n*LM_BLOCKING)+(l_mm-l_m),
                                                      ACC_REG+(l_n*LM_BLOCKING)+(l_mm-l_m) );
            }

            /* only prefetch if we do temporal stores */
            if ((LIBXSMM_GEMM_FLAG_ALIGN_C_NTS_HINT & i_xgemm_desc->flags) == 0) {
              libxsmm_x86_instruction_prefetch( io_generated_code,
                                                LIBXSMM_X86_INSTR_PREFETCHT2,
                                                l_gp_reg_mapping.gp_reg_c,
                                                LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                l_mm*i_xgemm_desc->ldc*l_micro_kernel_config.datatype_size +
                                                  (l_n+1)*l_micro_kernel_config.datatype_size*l_micro_kernel_config.vector_length );
            }
          }
        }
      }
    }
    for ( l_z = 0; l_z < largest_lz; l_z++ ) {
      for ( l_mm=l_m; l_mm < l_m+LM_BLOCKING; l_mm++) {
        if ( (l_mm < (unsigned int)i_xgemm_desc->m) && (l_z < l_row_elements[l_mm-l_m]) ) {
          /* check k such that we just use columns which actually need to be multiplied */
          const unsigned int u = i_row_idx[l_mm] + l_z;
          LIBXSMM_ASSERT(u < l_n_row_idx);

          if ( LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              l_micro_kernel_config.instruction_set,
                                              l_micro_kernel_config.a_vmove_instruction,
                                              l_gp_reg_mapping.gp_reg_a,
                                              LIBXSMM_X86_GP_REG_UNDEF, 0,
                                              (l_unique_pos[u] % 8)*64,
                                              l_micro_kernel_config.vector_name,
                                              BCAST_REG+(l_mm-l_m), 0, 1, 0 );

            libxsmm_x86_instruction_vec_compute_reg(io_generated_code,
                                                    l_micro_kernel_config.instruction_set,
                                                    LIBXSMM_X86_INSTR_VPERMD,
                                                    l_micro_kernel_config.vector_name,
                                                    l_unique_pos[u] / 8,
                                                    BCAST_REG+(l_mm-l_m),
                                                    BCAST_REG+(l_mm-l_m));
          } else {
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              l_micro_kernel_config.instruction_set,
                                              l_micro_kernel_config.a_vmove_instruction,
                                              l_gp_reg_mapping.gp_reg_a,
                                              LIBXSMM_X86_GP_REG_UNDEF, 0,
                                              (l_unique_pos[u] % 16)*64,
                                              l_micro_kernel_config.vector_name,
                                              BCAST_REG+(l_mm-l_m), 0, 1, 0 );

            libxsmm_x86_instruction_vec_compute_reg(io_generated_code,
                                                    l_micro_kernel_config.instruction_set,
                                                    LIBXSMM_X86_INSTR_VPERMD,
                                                    l_micro_kernel_config.vector_name,
                                                    l_unique_pos[u] / 16,
                                                    BCAST_REG+(l_mm-l_m),
                                                    BCAST_REG+(l_mm-l_m));
          }

          for ( l_n = 0; l_n < l_n_blocking; l_n++ ) {

            libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                    l_micro_kernel_config.instruction_set,
                                                    l_micro_kernel_config.vmul_instruction,
                                                    0,
                                                    l_gp_reg_mapping.gp_reg_b,
                                                    LIBXSMM_X86_GP_REG_UNDEF,
                                                    0,
                                                    i_column_idx[u]*i_xgemm_desc->ldb*l_micro_kernel_config.datatype_size +
                                                      l_n*l_micro_kernel_config.datatype_size*l_micro_kernel_config.vector_length,
                                                    l_micro_kernel_config.vector_name,
                                                    BCAST_REG+(l_mm-l_m),
                                                    ACC_REG+(l_n*LM_BLOCKING)+(l_mm-l_m) );

            libxsmm_x86_instruction_prefetch( io_generated_code,
                                              LIBXSMM_X86_INSTR_PREFETCHT2,
                                              l_gp_reg_mapping.gp_reg_b,
                                              LIBXSMM_X86_GP_REG_UNDEF,
                                              0,
                                              i_column_idx[u]*i_xgemm_desc->ldb*l_micro_kernel_config.datatype_size +
                                                (l_n+1)*l_micro_kernel_config.datatype_size*l_micro_kernel_config.vector_length );
          }
        }
      }
    }
    for ( l_mm=l_m; l_mm < l_m+LM_BLOCKING; l_mm++) {
      if ( l_mm < (unsigned int)i_xgemm_desc->m ) {
        if (l_row_elements[l_mm-l_m] > 0) {
          for ( l_n = 0; l_n < l_n_blocking; l_n++ ) {
            unsigned int l_store_instruction = 0;
            if ((LIBXSMM_GEMM_FLAG_ALIGN_C_NTS_HINT & i_xgemm_desc->flags) > 0) {
              if ( LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )  ) {
                l_store_instruction = LIBXSMM_X86_INSTR_VMOVNTPD;
              } else {
                l_store_instruction = LIBXSMM_X86_INSTR_VMOVNTPS;
              }
            } else {
              l_store_instruction = l_micro_kernel_config.c_vmove_instruction;
            }
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              l_micro_kernel_config.instruction_set,
                                              l_store_instruction,
                                              l_gp_reg_mapping.gp_reg_c,
                                              LIBXSMM_X86_GP_REG_UNDEF, 0,
                                              l_mm*i_xgemm_desc->ldc*l_micro_kernel_config.datatype_size +
                                                l_n*l_micro_kernel_config.datatype_size*l_micro_kernel_config.vector_length,
                                              l_micro_kernel_config.vector_name,
                                              ACC_REG+(l_n*LM_BLOCKING)+(l_mm-l_m), 0, 0, 1 );
          }
        }
      }
    }
  }

  /* close n loop */
#if 0
  libxsmm_x86_instruction_alu_imm( io_generated_code, l_micro_kernel_config.alu_cmp_instruction, l_gp_reg_mapping.gp_reg_nloop, l_n_blocking );
  libxsmm_x86_instruction_jump_back_to_label( io_generated_code, l_micro_kernel_config.alu_jmp_instruction, &l_loop_label_tracker );
#endif

  /* close asm */
  libxsmm_x86_instruction_close_stream( io_generated_code, &l_gp_reg_mapping, i_xgemm_desc->prefetch );

  free(l_unique_values);
  free(l_unique_pos);
}

