/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>

#include "./vpx_config.h"
#include "vp9_rtcd.h"
#include "vp9/common/vp9_reconintra.h"
#include "vpx_mem/vpx_mem.h"

// Using multiplication and shifting instead of division in diagonal prediction.
// iscale table is calculated from ((1 << 16) + (i + 2) / 2) / (i+2) and used as
// ((A + B) * iscale[i] + (1 << 15)) >> 16;
// where A and B are weighted pixel values.
static const unsigned int iscale[64] = {
  32768, 21845, 16384, 13107, 10923,  9362,  8192,  7282,
   6554,  5958,  5461,  5041,  4681,  4369,  4096,  3855,
   3641,  3449,  3277,  3121,  2979,  2849,  2731,  2621,
   2521,  2427,  2341,  2260,  2185,  2114,  2048,  1986,
   1928,  1872,  1820,  1771,  1725,  1680,  1638,  1598,
   1560,  1524,  1489,  1456,  1425,  1394,  1365,  1337,
   1311,  1285,  1260,  1237,  1214,  1192,  1170,  1150,
   1130,  1111,  1092,  1074,  1057,  1040,  1024,  1008,
};

static INLINE int iscale_round(int value, int i) {
  return ROUND_POWER_OF_TWO(value * iscale[i], 16);
}

static void d27_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;

  r = 0;
  for (c = 0; c < bw - 2; c++) {
    int a = c & 1 ? yleft_col[r + 1]
                  : ROUND_POWER_OF_TWO(yleft_col[r] + yleft_col[r + 1], 1);
    int b = yabove_row[c + 2];
    ypred_ptr[c] = iscale_round(2 * a + (c + 1) * b, 1 + c);
  }

  for (r = 1; r < bh / 2 - 1; r++) {
    for (c = 0; c < bw - 2 - 2 * r; c++) {
      int a = c & 1 ? yleft_col[r + 1]
                    : ROUND_POWER_OF_TWO(yleft_col[r] + yleft_col[r + 1], 1);
      int b = ypred_ptr[(r - 1) * y_stride + c + 2];
      ypred_ptr[r * y_stride + c] = iscale_round(2 * a + (c + 1) * b, 1 + c);
    }
  }

  for (; r < bh - 1; r++) {
    for (c = 0; c < bw; c++) {
      int v = c & 1 ? yleft_col[r + 1]
                    : ROUND_POWER_OF_TWO(yleft_col[r] + yleft_col[r + 1], 1);
      int h = r - c / 2;
      ypred_ptr[h * y_stride + c] = v;
    }
  }

  c = 0;
  r = bh - 1;
  ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(ypred_ptr[(r - 1) * y_stride] +
                                               yleft_col[r], 1);
  for (r = bh - 2; r >= bh / 2; --r) {
    const int w = c + (bh - 1 - r) * 2;
    ypred_ptr[r * y_stride + w] =
        ROUND_POWER_OF_TWO(ypred_ptr[(r - 1) * y_stride + w] +
                           ypred_ptr[r * y_stride + w - 1], 1);
  }

  for (c = 1; c < bw; c++) {
    for (r = bh - 1; r >= bh / 2 + c / 2; --r) {
      const int w = c + (bh - 1 - r) * 2;
      ypred_ptr[r * y_stride + w] =
          ROUND_POWER_OF_TWO(ypred_ptr[(r - 1) * y_stride + w] +
                             ypred_ptr[r * y_stride + w - 1], 1);
    }
  }
}

static void d63_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;

  c = 0;
  for (r = 0; r < bh - 2; r++) {
    int a = r & 1 ? yabove_row[c + 1]
                  : ROUND_POWER_OF_TWO(yabove_row[c] + yabove_row[c + 1], 1);
    int b = yleft_col[r + 2];
    ypred_ptr[r * y_stride] = iscale_round(2 * a + (r + 1) * b, 1 + r);
  }

  for (c = 1; c < bw / 2 - 1; c++) {
    for (r = 0; r < bh - 2 - 2 * c; r++) {
      int a = r & 1 ? yabove_row[c + 1]
                    : ROUND_POWER_OF_TWO(yabove_row[c] + yabove_row[c + 1], 1);
      int b = ypred_ptr[(r + 2) * y_stride + c - 1];
      ypred_ptr[r * y_stride + c] = iscale_round(2 * a + (c + 1) * b, 1 + c);
    }
  }

  for (; c < bw - 1; ++c) {
    for (r = 0; r < bh; r++) {
      int v = r & 1 ? yabove_row[c + 1]
                    : ROUND_POWER_OF_TWO(yabove_row[c] + yabove_row[c + 1], 1);
      int w = c - r / 2;
      ypred_ptr[r * y_stride + w] = v;
    }
  }

  r = 0;
  c = bw - 1;
  ypred_ptr[c] = ROUND_POWER_OF_TWO(ypred_ptr[(c - 1)] + yabove_row[c], 1);
  for (c = bw - 2; c >= bw / 2; --c) {
    const int h = r + (bw - 1 - c) * 2;
    ypred_ptr[h * y_stride + c] =
         ROUND_POWER_OF_TWO(ypred_ptr[h * y_stride + c - 1] +
                            ypred_ptr[(h - 1) * y_stride + c], 1);
  }

  for (r = 1; r < bh; r++) {
    for (c = bw - 1; c >= bw / 2 + r / 2; --c) {
      const int h = r + (bw - 1 - c) * 2;
      ypred_ptr[h * y_stride + c] =
          ROUND_POWER_OF_TWO(ypred_ptr[h * y_stride + c - 1] +
                             ypred_ptr[(h - 1) * y_stride + c], 1);
    }
  }
}

static void d45_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;

  for (r = 0; r < bh - 1; ++r) {
    for (c = 0; c <= r; ++c) {
      ypred_ptr[(r - c) * y_stride + c] = iscale_round(
          yabove_row[r + 1] * (c + 1) + yleft_col[r + 1] * (r - c + 1), r);
    }
  }

  for (c = 0; c <= r; ++c) {
    int yabove_ext = yabove_row[r];  // clip_pixel(2 * yabove_row[r] -
                                     //            yabove_row[r - 1]);
    int yleft_ext = yleft_col[r];  // clip_pixel(2 * yleft_col[r] -
                                   //            yleft_col[r-1]);
    ypred_ptr[(r - c) * y_stride + c] =
         iscale_round(yabove_ext * (c + 1) + yleft_ext * (r - c + 1), r);
  }
  for (r = 1; r < bh; ++r) {
    for (c = bw - r; c < bw; ++c) {
      const int yabove_ext = ypred_ptr[(r - 1) * y_stride + c];
      const int yleft_ext = ypred_ptr[r * y_stride + c - 1];
      ypred_ptr[r * y_stride + c] =
          ROUND_POWER_OF_TWO(yabove_ext + yleft_ext, 1);
    }
  }
}

static void d117_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  for (c = 0; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 1] + yabove_row[c], 1);
  ypred_ptr += y_stride;
  for (c = 0; c < bw; c++)
    ypred_ptr[c] = yabove_row[c - 1];
  ypred_ptr += y_stride;
  for (r = 2; r < bh; ++r) {
    ypred_ptr[0] = yleft_col[r - 2];
    for (c = 1; c < bw; c++)
      ypred_ptr[c] = ypred_ptr[-2 * y_stride + c - 1];
    ypred_ptr += y_stride;
  }
}

static void d135_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  ypred_ptr[0] = yabove_row[-1];
  for (c = 1; c < bw; c++)
    ypred_ptr[c] = yabove_row[c - 1];
  for (r = 1; r < bh; ++r)
    ypred_ptr[r * y_stride] = yleft_col[r - 1];

  ypred_ptr += y_stride;
  for (r = 1; r < bh; ++r) {
    for (c = 1; c < bw; c++)
      ypred_ptr[c] = ypred_ptr[-y_stride + c - 1];
    ypred_ptr += y_stride;
  }
}

static void d153_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yabove_row[-1] + yleft_col[0], 1);
  for (r = 1; r < bh; r++)
    ypred_ptr[r * y_stride] =
        ROUND_POWER_OF_TWO(yleft_col[r - 1] + yleft_col[r], 1);
  ypred_ptr++;
  ypred_ptr[0] = yabove_row[-1];
  for (r = 1; r < bh; r++)
    ypred_ptr[r * y_stride] = yleft_col[r - 1];
  ypred_ptr++;

  for (c = 0; c < bw - 2; c++)
    ypred_ptr[c] = yabove_row[c];
  ypred_ptr += y_stride;
  for (r = 1; r < bh; ++r) {
    for (c = 0; c < bw - 2; c++)
      ypred_ptr[c] = ypred_ptr[-y_stride + c - 2];
    ypred_ptr += y_stride;
  }
}

void vp9_build_intra_predictors(uint8_t *src, int src_stride,
                                uint8_t *ypred_ptr,
                                int y_stride, int mode,
                                int bw, int bh,
                                int up_available, int left_available,
                                int right_available) {
  int r, c, i;
  uint8_t yleft_col[64], yabove_data[65], ytop_left;
  uint8_t *yabove_row = yabove_data + 1;

  // 127 127 127 .. 127 127 127 127 127 127
  // 129  A   B  ..  Y   Z
  // 129  C   D  ..  W   X
  // 129  E   F  ..  U   V
  // 129  G   H  ..  S   T   T   T   T   T
  // ..

  if (left_available) {
    for (i = 0; i < bh; i++)
      yleft_col[i] = src[i * src_stride - 1];
  } else {
    vpx_memset(yleft_col, 129, bh);
  }

  if (up_available) {
    uint8_t *yabove_ptr = src - src_stride;
    vpx_memcpy(yabove_row, yabove_ptr, bw);
    ytop_left = left_available ? yabove_ptr[-1] : 127;
  } else {
    vpx_memset(yabove_row, 127, bw);
    ytop_left = 127;
  }
  yabove_row[-1] = ytop_left;


  switch (mode) {
    case DC_PRED: {
      int i;
      int expected_dc = 128;
      int average = 0;
      int count = 0;

      if (up_available || left_available) {
        if (up_available) {
          for (i = 0; i < bw; i++)
            average += yabove_row[i];
          count += bw;
        }

        if (left_available) {
          for (i = 0; i < bh; i++)
            average += yleft_col[i];
          count += bh;
        }
        expected_dc = (average + (count >> 1)) / count;
      }

      for (r = 0; r < bh; r++) {
        vpx_memset(ypred_ptr, expected_dc, bw);
        ypred_ptr += y_stride;
      }
    }
    break;
    case V_PRED:
      for (r = 0; r < bh; r++) {
        memcpy(ypred_ptr, yabove_row, bw);
        ypred_ptr += y_stride;
      }
      break;
    case H_PRED:
      for (r = 0; r < bh; r++) {
        vpx_memset(ypred_ptr, yleft_col[r], bw);
        ypred_ptr += y_stride;
      }
      break;
    case TM_PRED:
      for (r = 0; r < bh; r++) {
        for (c = 0; c < bw; c++)
          ypred_ptr[c] = clip_pixel(yleft_col[r] + yabove_row[c] - ytop_left);

        ypred_ptr += y_stride;
      }
      break;
    case D45_PRED:
    case D135_PRED:
    case D117_PRED:
    case D153_PRED:
    case D27_PRED:
    case D63_PRED:
      if (bw == bh) {
        switch (mode) {
          case D45_PRED:
            d45_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
      } else if (bw > bh) {
        uint8_t pred[64*64];
        memset(yleft_col + bh, yleft_col[bh - 1], bw - bh);
        switch (mode) {
          case D45_PRED:
            d45_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
        for (i = 0; i < bh; i++)
          memcpy(ypred_ptr + y_stride * i, pred + i * 64, bw);
      } else {
        uint8_t pred[64 * 64];
        memset(yabove_row + bw, yabove_row[bw - 1], bh - bw);
        switch (mode) {
          case D45_PRED:
            d45_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
        for (i = 0; i < bh; i++)
          memcpy(ypred_ptr + y_stride * i, pred + i * 64, bw);
      }
      break;
    default:
      break;
  }
}

#if CONFIG_COMP_INTERINTRA_PRED
static void combine_interintra(MB_PREDICTION_MODE mode,
                               uint8_t *interpred,
                               int interstride,
                               uint8_t *intrapred,
                               int intrastride,
                               int bw, int bh) {
  // TODO(debargha): Explore different ways of combining predictors
  //                 or designing the tables below
  static const int scale_bits = 8;
  static const int scale_max = 256;     // 1 << scale_bits;
  static const int scale_round = 127;   // (1 << (scale_bits - 1));
  // This table is a function A + B*exp(-kx), where x is hor. index
  static const int weights1d[64] = {
    128, 125, 122, 119, 116, 114, 111, 109,
    107, 105, 103, 101,  99,  97,  96,  94,
     93,  91,  90,  89,  88,  86,  85,  84,
     83,  82,  81,  81,  80,  79,  78,  78,
     77,  76,  76,  75,  75,  74,  74,  73,
     73,  72,  72,  71,  71,  71,  70,  70,
     70,  70,  69,  69,  69,  69,  68,  68,
     68,  68,  68,  67,  67,  67,  67,  67,
  };

  int size = MAX(bw, bh);
  int size_scale = (size >= 64 ? 1:
                    size == 32 ? 2 :
                    size == 16 ? 4 :
                    size == 8  ? 8 : 16);
  int i, j;
  switch (mode) {
    case V_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          int scale = weights1d[i * size_scale];
          interpred[k] =
              ((scale_max - scale) * interpred[k] +
               scale * intrapred[i * intrastride + j] + scale_round)
              >> scale_bits;
        }
      }
      break;

    case H_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          int scale = weights1d[j * size_scale];
          interpred[k] =
              ((scale_max - scale) * interpred[k] +
               scale * intrapred[i * intrastride + j] + scale_round)
              >> scale_bits;
        }
      }
      break;

    case D63_PRED:
    case D117_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          int scale = (weights1d[i * size_scale] * 3 +
                       weights1d[j * size_scale]) >> 2;
          interpred[k] =
              ((scale_max - scale) * interpred[k] +
               scale * intrapred[i * intrastride + j] + scale_round)
              >> scale_bits;
        }
      }
      break;

    case D27_PRED:
    case D153_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          int scale = (weights1d[j * size_scale] * 3 +
                       weights1d[i * size_scale]) >> 2;
          interpred[k] =
              ((scale_max - scale) * interpred[k] +
               scale * intrapred[i * intrastride + j] + scale_round)
              >> scale_bits;
        }
      }
      break;

    case D135_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          int scale = weights1d[(i < j ? i : j) * size_scale];
          interpred[k] =
              ((scale_max - scale) * interpred[k] +
               scale * intrapred[i * intrastride + j] + scale_round)
              >> scale_bits;
        }
      }
      break;

    case D45_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          int scale = (weights1d[i * size_scale] +
                       weights1d[j * size_scale]) >> 1;
          interpred[k] =
              ((scale_max - scale) * interpred[k] +
               scale * intrapred[i * intrastride + j] + scale_round)
              >> scale_bits;
        }
      }
      break;

    case TM_PRED:
    case DC_PRED:
    default:
      // simple average
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) {
          int k = i * interstride + j;
          interpred[k] = (interpred[k] + intrapred[i * intrastride + j]) >> 1;
        }
      }
      break;
  }
}

void vp9_build_interintra_predictors(MACROBLOCKD *xd,
                                              uint8_t *ypred,
                                              uint8_t *upred,
                                              uint8_t *vpred,
                                              int ystride, int uvstride,
                                              BLOCK_SIZE_TYPE bsize) {
  vp9_build_interintra_predictors_sby(xd, ypred, ystride, bsize);
  vp9_build_interintra_predictors_sbuv(xd, upred, vpred, uvstride, bsize);
}

void vp9_build_interintra_predictors_sby(MACROBLOCKD *xd,
                                               uint8_t *ypred,
                                               int ystride,
                                               BLOCK_SIZE_TYPE bsize) {
  int bwl = mi_width_log2(bsize),  bw = MI_SIZE << bwl;
  int bhl = mi_height_log2(bsize), bh = MI_SIZE << bhl;
  uint8_t intrapredictor[4096];
  vp9_build_intra_predictors(
      xd->plane[0].dst.buf, xd->plane[0].dst.stride,
      intrapredictor, bw,
      xd->mode_info_context->mbmi.interintra_mode, bw, bh,
      xd->up_available, xd->left_available, xd->right_available);
  combine_interintra(xd->mode_info_context->mbmi.interintra_mode,
                     ypred, ystride, intrapredictor, bw, bw, bh);
}

void vp9_build_interintra_predictors_sbuv(MACROBLOCKD *xd,
                                                uint8_t *upred,
                                                uint8_t *vpred,
                                                int uvstride,
                                                BLOCK_SIZE_TYPE bsize) {
  int bwl = mi_width_log2(bsize),  bw = MI_UV_SIZE << bwl;
  int bhl = mi_height_log2(bsize), bh = MI_UV_SIZE << bhl;
  uint8_t uintrapredictor[1024];
  uint8_t vintrapredictor[1024];
  vp9_build_intra_predictors(
      xd->plane[1].dst.buf, xd->plane[1].dst.stride,
      uintrapredictor, bw,
      xd->mode_info_context->mbmi.interintra_uv_mode, bw, bh,
      xd->up_available, xd->left_available, xd->right_available);
  vp9_build_intra_predictors(
      xd->plane[2].dst.buf, xd->plane[1].dst.stride,
      vintrapredictor, bw,
      xd->mode_info_context->mbmi.interintra_uv_mode, bw, bh,
      xd->up_available, xd->left_available, xd->right_available);
  combine_interintra(xd->mode_info_context->mbmi.interintra_uv_mode,
                     upred, uvstride, uintrapredictor, bw, bw, bh);
  combine_interintra(xd->mode_info_context->mbmi.interintra_uv_mode,
                     vpred, uvstride, vintrapredictor, bw, bw, bh);
}
#endif  // CONFIG_COMP_INTERINTRA_PRED

void vp9_build_intra_predictors_sby_s(MACROBLOCKD *xd,
                                      BLOCK_SIZE_TYPE bsize) {
  const int bwl = mi_width_log2(bsize),  bw = MI_SIZE << bwl;
  const int bhl = mi_height_log2(bsize), bh = MI_SIZE << bhl;

  vp9_build_intra_predictors(xd->plane[0].dst.buf, xd->plane[0].dst.stride,
                             xd->plane[0].dst.buf, xd->plane[0].dst.stride,
                             xd->mode_info_context->mbmi.mode,
                             bw, bh,
                             xd->up_available, xd->left_available,
                             xd->right_available);
}

void vp9_build_intra_predictors_sbuv_s(MACROBLOCKD *xd,
                                       BLOCK_SIZE_TYPE bsize) {
  const int bwl = mi_width_log2(bsize), bw = MI_UV_SIZE << bwl;
  const int bhl = mi_height_log2(bsize), bh = MI_UV_SIZE << bhl;

  vp9_build_intra_predictors(xd->plane[1].dst.buf, xd->plane[1].dst.stride,
                             xd->plane[1].dst.buf, xd->plane[1].dst.stride,
                             xd->mode_info_context->mbmi.uv_mode,
                             bw, bh, xd->up_available,
                             xd->left_available, xd->right_available);
  vp9_build_intra_predictors(xd->plane[2].dst.buf, xd->plane[1].dst.stride,
                             xd->plane[2].dst.buf, xd->plane[1].dst.stride,
                             xd->mode_info_context->mbmi.uv_mode,
                             bw, bh, xd->up_available,
                             xd->left_available, xd->right_available);
}

void vp9_intra8x8_predict(MACROBLOCKD *xd,
                          int block4x4_idx,
                          int mode,
                          uint8_t *predictor, int pre_stride) {
  const int block_idx = (block4x4_idx >> 2) | !!(block4x4_idx & 2);
  const int have_top = (block_idx >> 1) || xd->up_available;
  const int have_left = (block_idx & 1) || xd->left_available;
  const int have_right = !(block_idx & 1) || xd->right_available;

  vp9_build_intra_predictors(predictor, pre_stride,
                             predictor, pre_stride,
                             mode, 8, 8, have_top, have_left,
                             have_right);
}

void vp9_intra_uv4x4_predict(MACROBLOCKD *xd,
                             int block4x4_idx,
                             int mode,
                             uint8_t *predictor, int pre_stride) {
  const int block_idx = block4x4_idx & 3;
  const int have_top = (block_idx >> 1) || xd->up_available;
  const int have_left = (block_idx & 1) || xd->left_available;
  const int have_right = !(block_idx & 1) || xd->right_available;

  vp9_build_intra_predictors(predictor, pre_stride,
                             predictor, pre_stride,
                             mode, 4, 4, have_top, have_left,
                             have_right);
}
