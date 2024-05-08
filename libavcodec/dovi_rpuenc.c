/*
 * Dolby Vision RPU encoder
 *
 * Copyright (C) 2024 Niklas Haas
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "dovi_rpu.h"
#include "itut35.h"
#include "put_bits.h"
#include "put_golomb.h"
#include "refstruct.h"

static struct {
    uint64_t pps; // maximum pixels per second
    int width; // maximum width
    int main; // maximum bitrate in main tier
    int high; // maximum bitrate in high tier
} dv_levels[] = {
     [1] = {1280*720*24,    1280,  20,  50},
     [2] = {1280*720*30,    1280,  20,  50},
     [3] = {1920*1080*24,   1920,  20,  70},
     [4] = {1920*1080*30,   2560,  20,  70},
     [5] = {1920*1080*60,   3840,  20,  70},
     [6] = {3840*2160*24,   3840,  25, 130},
     [7] = {3840*2160*30,   3840,  25, 130},
     [8] = {3840*2160*48,   3840,  40, 130},
     [9] = {3840*2160*60,   3840,  40, 130},
    [10] = {3840*2160*120,  3840,  60, 240},
    [11] = {3840*2160*120,  7680,  60, 240},
    [12] = {7680*4320*60,   7680, 120, 450},
    [13] = {7680*4320*120u, 7680, 240, 800},
};

int ff_dovi_configure(DOVIContext *s, AVCodecContext *avctx)
{
    AVDOVIDecoderConfigurationRecord *cfg;
    const AVDOVIRpuDataHeader *hdr = NULL;
    const AVFrameSideData *sd;
    int dv_profile, dv_level, bl_compat_id = -1;
    size_t cfg_size;
    uint64_t pps;

    if (!s->enable)
        goto skip;

    sd = av_frame_side_data_get(avctx->decoded_side_data,
                                avctx->nb_decoded_side_data, AV_FRAME_DATA_DOVI_METADATA);

    if (sd)
        hdr = av_dovi_get_header((const AVDOVIMetadata *) sd->data);

    if (s->enable == FF_DOVI_AUTOMATIC && !hdr)
        goto skip;

    switch (avctx->codec_id) {
    case AV_CODEC_ID_AV1:  dv_profile = 10; break;
    case AV_CODEC_ID_H264: dv_profile = 9;  break;
    case AV_CODEC_ID_HEVC: dv_profile = hdr ? ff_dovi_guess_profile_hevc(hdr) : 8; break;
    default:
        /* No other encoder should be calling this! */
        av_assert0(0);
        return AVERROR_BUG;
    }

    if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        if (dv_profile == 9) {
            if (avctx->pix_fmt != AV_PIX_FMT_YUV420P)
                dv_profile = 0;
        } else {
            if (avctx->pix_fmt != AV_PIX_FMT_YUV420P10)
                dv_profile = 0;
        }
    }

    switch (dv_profile) {
    case 4: /* HEVC with enhancement layer */
    case 7:
        if (s->enable > 0) {
            av_log(s->logctx, AV_LOG_ERROR, "Coding of Dolby Vision enhancement "
                   "layers is currently unsupported.");
            return AVERROR_PATCHWELCOME;
        } else {
            goto skip;
        }
    case 5: /* HEVC with proprietary IPTPQc2 */
        bl_compat_id = 0;
        break;
    case 10:
        /* FIXME: check for proper H.273 tags once those are added */
        if (hdr && hdr->bl_video_full_range_flag) {
            /* AV1 with proprietary IPTPQc2 */
            bl_compat_id = 0;
            break;
        }
        /* fall through */
    case 8: /* HEVC (or AV1) with BL compatibility */
        if (avctx->colorspace == AVCOL_SPC_BT2020_NCL &&
            avctx->color_primaries == AVCOL_PRI_BT2020 &&
            avctx->color_trc == AVCOL_TRC_SMPTE2084) {
            bl_compat_id = 1;
        } else if (avctx->colorspace == AVCOL_SPC_BT2020_NCL &&
                   avctx->color_primaries == AVCOL_PRI_BT2020 &&
                   avctx->color_trc == AVCOL_TRC_ARIB_STD_B67) {
            bl_compat_id = 4;
        } else if (avctx->colorspace == AVCOL_SPC_BT709 &&
                   avctx->color_primaries == AVCOL_PRI_BT709 &&
                   avctx->color_trc == AVCOL_TRC_BT709) {
            bl_compat_id = 2;
        }
    }

    if (!dv_profile || bl_compat_id < 0) {
        if (s->enable > 0) {
            av_log(s->logctx, AV_LOG_ERROR, "Dolby Vision enabled, but could "
                   "not determine profile and compaatibility mode. Double-check "
                   "colorspace and format settings for compatibility?\n");
            return AVERROR(EINVAL);
        }
        goto skip;
    }

    pps = avctx->width * avctx->height;
    if (avctx->framerate.num) {
        pps = pps * avctx->framerate.num / avctx->framerate.den;
    } else {
        pps *= 25; /* sanity fallback */
    }

    dv_level = 0;
    for (int i = 1; i < FF_ARRAY_ELEMS(dv_levels); i++) {
        if (pps > dv_levels[i].pps)
            continue;
        if (avctx->width > dv_levels[i].width)
            continue;
        /* In theory, we should also test the bitrate when known, and
         * distinguish between main and high tier. In practice, just ignore
         * the bitrate constraints and hope they work out. This would ideally
         * be handled by either the encoder or muxer directly. */
        dv_level = i;
        break;
    }

    if (!dv_level) {
        if (avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT) {
            av_log(s->logctx, AV_LOG_ERROR, "Coded PPS (%"PRIu64") and width (%d) "
                   "exceed Dolby Vision limitations\n", pps, avctx->width);
            return AVERROR(EINVAL);
        } else {
            av_log(s->logctx, AV_LOG_WARNING, "Coded PPS (%"PRIu64") and width (%d) "
                   "exceed Dolby Vision limitations. Ignoring, resulting file "
                   "may be non-conforming.\n", pps, avctx->width);
            dv_level = FF_ARRAY_ELEMS(dv_levels) - 1;
        }
    }

    cfg = av_dovi_alloc(&cfg_size);
    if (!cfg)
        return AVERROR(ENOMEM);

    if (!av_packet_side_data_add(&avctx->coded_side_data, &avctx->nb_coded_side_data,
                                 AV_PKT_DATA_DOVI_CONF, cfg, cfg_size, 0)) {
        av_free(cfg);
        return AVERROR(ENOMEM);
    }

    cfg->dv_version_major = 1;
    cfg->dv_version_minor = 0;
    cfg->dv_profile = dv_profile;
    cfg->dv_level = dv_level;
    cfg->rpu_present_flag = 1;
    cfg->el_present_flag = 0;
    cfg->bl_present_flag = 1;
    cfg->dv_bl_signal_compatibility_id = bl_compat_id;

    s->cfg = *cfg;
    return 0;

skip:
    s->cfg = (AVDOVIDecoderConfigurationRecord) {0};
    return 0;
}

static inline void put_ue_coef(PutBitContext *pb, const AVDOVIRpuDataHeader *hdr,
                               uint64_t coef)
{
    union { uint32_t u32; float f32; } fpart;

    switch (hdr->coef_data_type) {
    case RPU_COEFF_FIXED:
        set_ue_golomb(pb, coef >> hdr->coef_log2_denom);
        put_bits64(pb, hdr->coef_log2_denom,
                   coef & ((1LL << hdr->coef_log2_denom) - 1));
        break;
    case RPU_COEFF_FLOAT:
        fpart.f32 = coef / (float) (1LL << hdr->coef_log2_denom);
        put_bits64(pb, hdr->coef_log2_denom, fpart.u32);
        break;
    }
}

static inline void put_se_coef(PutBitContext *pb, const AVDOVIRpuDataHeader *hdr,
                               uint64_t coef)
{
    union { uint32_t u32; float f32; } fpart;

    switch (hdr->coef_data_type) {
    case RPU_COEFF_FIXED:
        set_se_golomb(pb, coef >> hdr->coef_log2_denom);
        put_bits64(pb, hdr->coef_log2_denom,
                   coef & ((1LL << hdr->coef_log2_denom) - 1));
        break;
    case RPU_COEFF_FLOAT:
        fpart.f32 = coef / (float) (1LL << hdr->coef_log2_denom);
        put_bits64(pb, hdr->coef_log2_denom, fpart.u32);
        break;
    }
}

static int av_q2den(AVRational q, int den)
{
    if (!q.den || q.den == den)
        return q.num;
    q = av_mul_q(q, av_make_q(den, 1));
    return (q.num + (q.den >> 1)) / q.den;
}

static void generate_ext_v1(PutBitContext *pb, const AVDOVIDmData *dm)
{
    int ext_block_length, start_pos, pad_bits;

    switch (dm->level) {
    case 1:   ext_block_length = 5;  break;
    case 2:   ext_block_length = 11; break;
    case 4:   ext_block_length = 3;  break;
    case 5:   ext_block_length = 7;  break;
    case 6:   ext_block_length = 8;  break;
    case 255: ext_block_length = 6;  break;
    default: return;
    }

    set_ue_golomb(pb, ext_block_length);
    put_bits(pb, 8, dm->level);
    start_pos = put_bits_count(pb);

    switch (dm->level) {
    case 1:
        put_bits(pb, 12, dm->l1.min_pq);
        put_bits(pb, 12, dm->l1.max_pq);
        put_bits(pb, 12, dm->l1.avg_pq);
        break;
    case 2:
        put_bits(pb, 12, dm->l2.target_max_pq);
        put_bits(pb, 12, dm->l2.trim_slope);
        put_bits(pb, 12, dm->l2.trim_offset);
        put_bits(pb, 12, dm->l2.trim_power);
        put_bits(pb, 12, dm->l2.trim_chroma_weight);
        put_bits(pb, 12, dm->l2.trim_saturation_gain);
        put_bits(pb, 13, dm->l2.ms_weight + 8192);
        break;
    case 4:
        put_bits(pb, 12, dm->l4.anchor_pq);
        put_bits(pb, 12, dm->l4.anchor_power);
        break;
    case 5:
        put_bits(pb, 13, dm->l5.left_offset);
        put_bits(pb, 13, dm->l5.right_offset);
        put_bits(pb, 13, dm->l5.top_offset);
        put_bits(pb, 13, dm->l5.bottom_offset);
        break;
    case 6:
        put_bits(pb, 16, dm->l6.max_luminance);
        put_bits(pb, 16, dm->l6.min_luminance);
        put_bits(pb, 16, dm->l6.max_cll);
        put_bits(pb, 16, dm->l6.max_fall);
        break;
    case 255:
        put_bits(pb, 8, dm->l255.dm_run_mode);
        put_bits(pb, 8, dm->l255.dm_run_version);
        for (int i = 0; i < 4; i++)
            put_bits(pb, 8, dm->l255.dm_debug[i]);
        break;
    }

    pad_bits = ext_block_length * 8 - (put_bits_count(pb) - start_pos);
    av_assert1(pad_bits >= 0);
    put_bits(pb, pad_bits, 0);
}

static void put_cie_xy(PutBitContext *pb, AVCIExy xy)
{
    const int denom = 32767;
    put_sbits(pb, 16, av_q2den(xy.x, denom));
    put_sbits(pb, 16, av_q2den(xy.y, denom));
}

#define ANY6(arr) (arr[0] || arr[1] || arr[2] || arr[3] || arr[4] || arr[5])
#define ANY_XY(xy) (xy.x.num || xy.y.num)
#define ANY_CSP(csp) (ANY_XY(csp.prim.r) || ANY_XY(csp.prim.g) || \
                      ANY_XY(csp.prim.b) || ANY_XY(csp.wp))

static void generate_ext_v2(PutBitContext *pb, const AVDOVIDmData *dm)
{
    int ext_block_length, start_pos, pad_bits;

    switch (dm->level) {
    case 3: ext_block_length = 5; break;
    case 8:
        if (ANY6(dm->l8.hue_vector_field)) {
            ext_block_length = 25;
        } else if (ANY6(dm->l8.saturation_vector_field)) {
            ext_block_length = 19;
        } else if (dm->l8.clip_trim) {
            ext_block_length = 13;
        } else if (dm->l8.target_mid_contrast) {
            ext_block_length = 12;
        } else {
            ext_block_length = 10;
        }
        break;
    case 9:
        if (ANY_CSP(dm->l9.source_display_primaries)) {
            ext_block_length = 17;
        } else {
            ext_block_length = 1;
        }
        break;
    case 10:
        if (ANY_CSP(dm->l10.target_display_primaries)) {
            ext_block_length = 21;
        } else {
            ext_block_length = 5;
        }
        break;
    case 11:  ext_block_length = 4; break;
    case 254: ext_block_length = 2; break;
    default: return;
    }

    set_ue_golomb(pb, ext_block_length);
    put_bits(pb, 8, dm->level);
    start_pos = put_bits_count(pb);

    switch (dm->level) {
    case 3:
        put_bits(pb, 12, dm->l3.min_pq_offset);
        put_bits(pb, 12, dm->l3.max_pq_offset);
        put_bits(pb, 12, dm->l3.avg_pq_offset);
        break;
    case 8:
        put_bits(pb, 8, dm->l8.target_display_index);
        put_bits(pb, 12, dm->l8.trim_slope);
        put_bits(pb, 12, dm->l8.trim_offset);
        put_bits(pb, 12, dm->l8.trim_power);
        put_bits(pb, 12, dm->l8.trim_chroma_weight);
        put_bits(pb, 12, dm->l8.trim_saturation_gain);
        put_bits(pb, 12, dm->l8.ms_weight + 8192);
        if (ext_block_length < 12)
            break;
        put_bits(pb, 12, dm->l8.target_mid_contrast);
        if (ext_block_length < 13)
            break;
        put_bits(pb, 12, dm->l8.clip_trim);
        if (ext_block_length < 19)
            break;
        for (int i = 0; i < 6; i++)
            put_bits(pb, 8, dm->l8.saturation_vector_field[i]);
        if (ext_block_length < 25)
            break;
        for (int i = 0; i < 6; i++)
            put_bits(pb, 8, dm->l8.hue_vector_field[i]);
        break;
    case 9:
        put_bits(pb, 8, dm->l9.source_primary_index);
        if (ext_block_length < 17)
            break;
        put_cie_xy(pb, dm->l9.source_display_primaries.prim.r);
        put_cie_xy(pb, dm->l9.source_display_primaries.prim.g);
        put_cie_xy(pb, dm->l9.source_display_primaries.prim.b);
        put_cie_xy(pb, dm->l9.source_display_primaries.wp);
        break;
    case 10:
        put_bits(pb, 8, dm->l10.target_display_index);
        put_bits(pb, 12, dm->l10.target_max_pq);
        put_bits(pb, 12, dm->l10.target_min_pq);
        put_bits(pb, 8, dm->l10.target_primary_index);
        if (ext_block_length < 21)
            break;
        put_cie_xy(pb, dm->l10.target_display_primaries.prim.r);
        put_cie_xy(pb, dm->l10.target_display_primaries.prim.g);
        put_cie_xy(pb, dm->l10.target_display_primaries.prim.b);
        put_cie_xy(pb, dm->l10.target_display_primaries.wp);
        break;
    case 11:
        put_bits(pb, 8, dm->l11.content_type);
        put_bits(pb, 4, dm->l11.whitepoint);
        put_bits(pb, 1, dm->l11.reference_mode_flag);
        put_bits(pb, 3, 0); /* reserved */
        put_bits(pb, 2, dm->l11.sharpness);
        put_bits(pb, 2, dm->l11.noise_reduction);
        put_bits(pb, 2, dm->l11.mpeg_noise_reduction);
        put_bits(pb, 2, dm->l11.frame_rate_conversion);
        put_bits(pb, 2, dm->l11.brightness);
        put_bits(pb, 2, dm->l11.color);
        break;
    case 254:
        put_bits(pb, 8, dm->l254.dm_mode);
        put_bits(pb, 8, dm->l254.dm_version_index);
        break;
    }

    pad_bits = ext_block_length * 8 - (put_bits_count(pb) - start_pos);
    av_assert1(pad_bits >= 0);
    put_bits(pb, pad_bits, 0);
}

int ff_dovi_rpu_generate(DOVIContext *s, const AVDOVIMetadata *metadata,
                         uint8_t **out_rpu, int *out_size)
{
    PutBitContext *pb = &(PutBitContext){0};
    const AVDOVIRpuDataHeader *hdr;
    const AVDOVIDataMapping *mapping;
    const AVDOVIColorMetadata *color;
    int vdr_dm_metadata_changed, vdr_rpu_id, use_prev_vdr_rpu, profile,
        buffer_size, rpu_size, pad, zero_run;
    int num_ext_blocks_v1, num_ext_blocks_v2;
    uint32_t crc;
    uint8_t *dst;
    if (!metadata) {
        *out_rpu = NULL;
        *out_size = 0;
        return 0;
    }

    hdr = av_dovi_get_header(metadata);
    mapping = av_dovi_get_mapping(metadata);
    color = av_dovi_get_color(metadata);
    av_assert0(s->cfg.dv_profile);

    if (hdr->rpu_type != 2) {
        av_log(s->logctx, AV_LOG_ERROR, "Unhandled RPU type %"PRIu8"\n",
               hdr->rpu_type);
        return AVERROR_INVALIDDATA;
    }

    vdr_rpu_id = -1;
    for (int i = 0; i <= DOVI_MAX_DM_ID; i++) {
        if (s->vdr[i] && !memcmp(&s->vdr[i]->mapping, mapping, sizeof(*mapping))) {
            vdr_rpu_id = i;
            break;
        } else if (vdr_rpu_id < 0 && (!s->vdr[i] || i == DOVI_MAX_DM_ID)) {
            vdr_rpu_id = i;
        }
    }

    if (!s->vdr[vdr_rpu_id]) {
        s->vdr[vdr_rpu_id] = ff_refstruct_allocz(sizeof(DOVIVdr));
        if (!s->vdr[vdr_rpu_id])
            return AVERROR(ENOMEM);
    }

    if (!s->vdr[color->dm_metadata_id]) {
        s->vdr[color->dm_metadata_id] = ff_refstruct_allocz(sizeof(DOVIVdr));
        if (!s->vdr[color->dm_metadata_id])
            return AVERROR(ENOMEM);
    }

    num_ext_blocks_v1 = num_ext_blocks_v2 = 0;
    for (int i = 0; i < metadata->num_ext_blocks; i++) {
        const AVDOVIDmData *dm = av_dovi_get_ext(metadata, i);
        switch (dm->level) {
        case 1:
        case 2:
        case 4:
        case 5:
        case 6:
        case 255:
            num_ext_blocks_v1++;
            break;
        case 3:
        case 8:
        case 9:
        case 10:
        case 11:
        case 254:
            num_ext_blocks_v2++;
            break;
        default:
            av_log(s->logctx, AV_LOG_ERROR, "Invalid ext block level %d\n",
                   dm->level);
            return AVERROR_INVALIDDATA;
        }
    }

    vdr_dm_metadata_changed = !s->color || memcmp(s->color, color, sizeof(*color));
    use_prev_vdr_rpu = !memcmp(&s->vdr[vdr_rpu_id]->mapping, mapping, sizeof(*mapping));

    buffer_size = 12 /* vdr seq info */ + 5 /* CRC32 + terminator */;
    buffer_size += num_ext_blocks_v1 * 13;
    buffer_size += num_ext_blocks_v2 * 28;
    if (!use_prev_vdr_rpu) {
        buffer_size += 160;
        for (int c = 0; c < 3; c++) {
            for (int i = 0; i < mapping->curves[c].num_pivots - 1; i++) {
                switch (mapping->curves[c].mapping_idc[i]) {
                case AV_DOVI_MAPPING_POLYNOMIAL: buffer_size += 26;  break;
                case AV_DOVI_MAPPING_MMR:        buffer_size += 177; break;
                }
            }
        }
    }
    if (vdr_dm_metadata_changed)
        buffer_size += 67;

    av_fast_padded_malloc(&s->rpu_buf, &s->rpu_buf_sz, buffer_size);
    if (!s->rpu_buf)
        return AVERROR(ENOMEM);
    init_put_bits(pb, s->rpu_buf, s->rpu_buf_sz);

    /* RPU header */
    put_bits(pb, 6, hdr->rpu_type);
    put_bits(pb, 11, hdr->rpu_format);
    put_bits(pb, 4, hdr->vdr_rpu_profile);
    put_bits(pb, 4, hdr->vdr_rpu_level);
    put_bits(pb, 1, 1); /* vdr_seq_info_present */
    put_bits(pb, 1, hdr->chroma_resampling_explicit_filter_flag);
    put_bits(pb, 2, hdr->coef_data_type);
    if (hdr->coef_data_type == RPU_COEFF_FIXED)
        set_ue_golomb(pb, hdr->coef_log2_denom);
    put_bits(pb, 2, hdr->vdr_rpu_normalized_idc);
    put_bits(pb, 1, hdr->bl_video_full_range_flag);
    if ((hdr->rpu_format & 0x700) == 0) {
        set_ue_golomb(pb, hdr->bl_bit_depth - 8);
        set_ue_golomb(pb, hdr->el_bit_depth - 8);
        set_ue_golomb(pb, hdr->vdr_bit_depth - 8);
        put_bits(pb, 1, hdr->spatial_resampling_filter_flag);
        put_bits(pb, 3, 0); /* reserved_zero_3bits */
        put_bits(pb, 1, hdr->el_spatial_resampling_filter_flag);
        put_bits(pb, 1, hdr->disable_residual_flag);
    }
    s->header = *hdr;

    put_bits(pb, 1, vdr_dm_metadata_changed);
    put_bits(pb, 1, use_prev_vdr_rpu);
    set_ue_golomb(pb, vdr_rpu_id);
    s->mapping = &s->vdr[vdr_rpu_id]->mapping;

    if (!use_prev_vdr_rpu) {
        set_ue_golomb(pb, mapping->mapping_color_space);
        set_ue_golomb(pb, mapping->mapping_chroma_format_idc);
        for (int c = 0; c < 3; c++) {
            const AVDOVIReshapingCurve *curve = &mapping->curves[c];
            int prev = 0;
            set_ue_golomb(pb, curve->num_pivots - 2);
            for (int i = 0; i < curve->num_pivots; i++) {
                put_bits(pb, hdr->bl_bit_depth, curve->pivots[i] - prev);
                prev = curve->pivots[i];
            }
        }

        if (mapping->nlq_method_idc != AV_DOVI_NLQ_NONE) {
            put_bits(pb, 3, mapping->nlq_method_idc);
            put_bits(pb, hdr->bl_bit_depth, mapping->nlq_pivots[0]);
            put_bits(pb, hdr->bl_bit_depth, mapping->nlq_pivots[1] - mapping->nlq_pivots[0]);
        }

        set_ue_golomb(pb, mapping->num_x_partitions - 1);
        set_ue_golomb(pb, mapping->num_y_partitions - 1);

        for (int c = 0; c < 3; c++) {
            const AVDOVIReshapingCurve *curve = &mapping->curves[c];
            for (int i = 0; i < curve->num_pivots - 1; i++) {
                set_ue_golomb(pb, curve->mapping_idc[i]);
                switch (curve->mapping_idc[i]) {
                case AV_DOVI_MAPPING_POLYNOMIAL: {
                    set_ue_golomb(pb, curve->poly_order[i] - 1);
                    if (curve->poly_order[i] == 1)
                        put_bits(pb, 1, 0); /* linear_interp_flag */
                    for (int k = 0; k <= curve->poly_order[i]; k++)
                        put_se_coef(pb, hdr, curve->poly_coef[i][k]);
                    break;
                }
                case AV_DOVI_MAPPING_MMR: {
                    put_bits(pb, 2, curve->mmr_order[i] - 1);
                    put_se_coef(pb, hdr, curve->mmr_constant[i]);
                    for (int j = 0; j < curve->mmr_order[i]; j++) {
                        for (int k = 0; k < 7; k++)
                            put_se_coef(pb, hdr, curve->mmr_coef[i][j][k]);
                    }
                    break;
                }
                }
            }
        }

        if (mapping->nlq_method_idc != AV_DOVI_NLQ_NONE) {
            for (int c = 0; c < 3; c++) {
                const AVDOVINLQParams *nlq = &mapping->nlq[c];
                put_bits(pb, hdr->el_bit_depth, nlq->nlq_offset);
                put_ue_coef(pb, hdr, nlq->vdr_in_max);
                switch (mapping->nlq_method_idc) {
                case AV_DOVI_NLQ_LINEAR_DZ:
                    put_ue_coef(pb, hdr, nlq->linear_deadzone_slope);
                    put_ue_coef(pb, hdr, nlq->linear_deadzone_threshold);
                    break;
                }
            }
        }

        memcpy(&s->vdr[vdr_rpu_id]->mapping, mapping, sizeof(*mapping));
    }

    if (vdr_dm_metadata_changed) {
        const int denom = profile == 4 ? (1 << 30) : (1 << 28);
        set_ue_golomb(pb, color->dm_metadata_id); /* affected_dm_id */
        set_ue_golomb(pb, color->dm_metadata_id); /* current_dm_id */
        set_ue_golomb(pb, color->scene_refresh_flag);
        for (int i = 0; i < 9; i++)
            put_sbits(pb, 16, av_q2den(color->ycc_to_rgb_matrix[i], 1 << 13));
        for (int i = 0; i < 3; i++)
            put_bits32(pb, av_q2den(color->ycc_to_rgb_offset[i], denom));
        for (int i = 0; i < 9; i++)
            put_sbits(pb, 16, av_q2den(color->rgb_to_lms_matrix[i], 1 << 14));
        put_bits(pb, 16, color->signal_eotf);
        put_bits(pb, 16, color->signal_eotf_param0);
        put_bits(pb, 16, color->signal_eotf_param1);
        put_bits32(pb, color->signal_eotf_param2);
        put_bits(pb, 5, color->signal_bit_depth);
        put_bits(pb, 2, color->signal_color_space);
        put_bits(pb, 2, color->signal_chroma_format);
        put_bits(pb, 2, color->signal_full_range_flag);
        put_bits(pb, 12, color->source_min_pq);
        put_bits(pb, 12, color->source_max_pq);
        put_bits(pb, 10, color->source_diagonal);

        memcpy(&s->vdr[color->dm_metadata_id]->color, color, sizeof(*color));
        s->color = &s->vdr[color->dm_metadata_id]->color;
    }

    set_ue_golomb(pb, num_ext_blocks_v1);
    align_put_bits(pb);
    for (int i = 0; i < metadata->num_ext_blocks; i++)
        generate_ext_v1(pb, av_dovi_get_ext(metadata, i));

    if (num_ext_blocks_v2) {
        set_ue_golomb(pb, num_ext_blocks_v2);
        align_put_bits(pb);
        for (int i = 0; i < metadata->num_ext_blocks; i++)
            generate_ext_v2(pb, av_dovi_get_ext(metadata, i));
    }

    flush_put_bits(pb);
    crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1,
                            s->rpu_buf, put_bytes_output(pb)));
    put_bits32(pb, crc);
    put_bits(pb, 8, 0x80); /* terminator */
    flush_put_bits(pb);

    rpu_size = put_bytes_output(pb);
    switch (s->cfg.dv_profile) {
    case 10:
        /* AV1 uses T.35 OBU with EMDF header */
        *out_rpu = av_malloc(rpu_size + 15);
        if (!*out_rpu)
            return AVERROR(ENOMEM);
        init_put_bits(pb, *out_rpu, rpu_size + 15);
        put_bits(pb,  8, ITU_T_T35_COUNTRY_CODE_US);
        put_bits(pb, 16, ITU_T_T35_PROVIDER_CODE_DOLBY);
        put_bits32(pb, 0x800); /* provider_oriented_code */
        put_bits(pb, 27, 0x01be6841u); /* fixed EMDF header, see above */
        if (rpu_size > 0xFF) {
            av_assert2(rpu_size <= 0x10000);
            put_bits(pb, 8, (rpu_size >> 8) - 1);
            put_bits(pb, 1, 1); /* read_more */
            put_bits(pb, 8, rpu_size & 0xFF);
            put_bits(pb, 1, 0);
        } else {
            put_bits(pb, 8, rpu_size);
            put_bits(pb, 1, 0);
        }
        ff_copy_bits(pb, s->rpu_buf, rpu_size * 8);
        put_bits(pb, 17, 0x400); /* emdf payload id + emdf_protection */

        pad = pb->bit_left & 7;
        put_bits(pb, pad, (1 << pad) - 1); /* pad to next byte with 1 bits */
        flush_put_bits(pb);
        *out_size = put_bytes_output(pb);
        return 0;

    case 5:
    case 8:
        *out_rpu = dst = av_malloc(1 + rpu_size * 3 / 2); /* worst case */
        if (!*out_rpu)
            return AVERROR(ENOMEM);
        *dst++ = 25; /* NAL prefix */
        zero_run = 0;
        for (int i = 0; i < rpu_size; i++) {
            if (zero_run < 2) {
                if (s->rpu_buf[i] == 0) {
                    zero_run++;
                } else {
                    zero_run = 0;
                }
            } else {
                if ((s->rpu_buf[i] & ~3) == 0) {
                    /* emulation prevention */
                    *dst++ = 3;
                }
                zero_run = s->rpu_buf[i] == 0;
            }
            *dst++ = s->rpu_buf[i];
        }
        *out_size = dst - *out_rpu;
        return 0;

    default:
        /* Should be unreachable */
        av_assert0(0);
        return AVERROR_BUG;
    }
}
