/*
* Copyright 2021 NVIDIA Corporation.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#if !defined(__NV_VULKANVIDEOPARSERPARAMS_H__)
#define __NV_VULKANVIDEOPARSERPARAMS_H__

#include "vulkan_interfaces.h"
#include <bitset>
#include <assert.h>
#include <atomic>

typedef int64_t VkVideotimestamp;

struct VkParserSourceDataPacket;
struct VkParserDetectedVideoFormat;
class StdVideoPictureParametersSet;

struct VkParserPerFrameDecodeParameters {
    enum {
        MAX_DPB_REF_SLOTS = (16 + 1), // max 16 reference pictures plus 1 for the current picture.
    };
    int currPicIdx; /** Output index of the current picture                       */
    StdVideoPictureParametersSet* pCurrentPictureParameters;
    // Bitstream data
    unsigned int bitstreamDataLen; /** Number of bytes in bitstream data buffer                  */
    const unsigned char* pBitstreamData; /** ptr to bitstream data for this picture (slice-layer)      */
    VkVideoDecodeInfoKHR decodeFrameInfo;
    int32_t numGopReferenceSlots;
    int8_t pGopReferenceImagesIndexes[MAX_DPB_REF_SLOTS];
    VkVideoPictureResourceKHR pictureResources[MAX_DPB_REF_SLOTS];
};

struct VkParserFrameSyncinfo {
    uint32_t unpairedField : 1; // Generate a semaphore reference, do not return the semaphore.
    uint32_t syncToFirstField : 1; // Use the semaphore from the unpared field to wait on.
    void* pDebugInterface;
};

union VkParserFieldFlags {
    struct {
        uint32_t progressiveFrame : 1; // Frame is progressive
        uint32_t fieldPic : 1; // 0 = frame picture, 1 = field picture
        uint32_t bottomField : 1; // 0 = top field, 1 = bottom field (ignored if field_pic_flag=0)
        uint32_t secondField : 1; // Second field of a complementary field pair
        uint32_t topFieldFirst : 1; // Frame pictures only
        uint32_t unpairedField : 1; // Incomplete (half) frame.
        uint32_t syncFirstReady : 1; // Synchronize the second field to the first one.
        uint32_t syncToFirstField : 1; // Synchronize the second field to the first one.
        uint32_t repeatFirstField : 3; // For 3:2 pulldown (number of additional fields, 2 = frame doubling, 4 = frame tripling)
        uint32_t refPic : 1; // Frame is a reference frame
    };
    uint32_t fieldFlags;
};

struct VkParserDecodePictureInfo {
    int32_t pictureIndex; // Index of the current picture
    VkParserFieldFlags flags;
    VkVideotimestamp timestamp; // decode time
    VkParserFrameSyncinfo frameSyncinfo;
    uint16_t videoFrameType; // VideoFrameType - use Vulkan codec specific type pd->CodecSpecific.h264.slice_type.
    uint16_t viewId; // from pictureInfoH264->ext.mvcext.view_id
};

struct VulkanVideoDisplayPictureInfo {
    VkVideotimestamp timestamp; // Presentation time stamp
};

struct VkParserDetectedVideoFormat {
    VkVideoCodecOperationFlagBitsKHR codec; // Compression format
    /**
     * OUT: frame rate = numerator / denominator (for example: 30000/1001)
     */
    struct {
        /** frame rate numerator   (0 = unspecified or variable frame rate) */
        uint32_t numerator;
        /** frame rate denominator (0 = unspecified or variable frame rate) */
        uint32_t denominator;
    } frame_rate;
    uint8_t progressive_sequence; /** 0=interlaced, 1=progressive                                      */
    uint8_t bit_depth_luma_minus8; /** high bit depth luma. E.g, 2 for 10-bitdepth, 4 for 12-bitdepth   */
    uint8_t bit_depth_chroma_minus8; /** high bit depth chroma. E.g, 2 for 10-bitdepth, 4 for 12-bitdepth */
    uint8_t reserved1; /**< Reserved for future use                                               */
    uint32_t coded_width; /** coded frame width in pixels                                      */
    uint32_t coded_height; /** coded frame height in pixels                                     */
    /**
     * area of the frame that should be displayed
     * typical example:
     * coded_width = 1920, coded_height = 1088
     * display_area = { 0,0,1920,1080 }
     */
    struct {
        int32_t left; /** left position of display rect    */
        int32_t top; /** top position of display rect     */
        int32_t right; /** right position of display rect   */
        int32_t bottom; /** bottom position of display rect  */
    } display_area;
    VkVideoChromaSubsamplingFlagBitsKHR chromaSubsampling; /**  Chroma format                   */
    uint32_t bitrate; /** video bitrate (bps, 0=unknown)   */
    /**
     * OUT: Display Aspect Ratio = x:y (4:3, 16:9, etc)
     */
    struct {
        int32_t x;
        int32_t y;
    } display_aspect_ratio;

    uint32_t minNumDecodeSurfaces; // Minimum number of decode surfaces for correct decoding (NumRefFrames + 2;)
    uint32_t maxNumDpbSlots; // Can't be more than 16 + 1

    /**
    * Video Signal Description
    * Refer section E.2.1 (VUI parameters semantics) of H264 spec file
    */
    struct {
        uint8_t video_format : 3; /** 0-Component, 1-PAL, 2-NTSC, 3-SECAM, 4-MAC, 5-Unspecified     */
        uint8_t video_full_range_flag : 1; /** indicates the black level and luma and chroma range           */
        uint8_t reserved_zero_bits : 4; /**< Reserved bits                                                      */
        uint8_t color_primaries; /** chromaticity coordinates of source primaries                  */
        uint8_t transfer_characteristics; /** opto-electronic transfer characteristic of the source picture */
        uint8_t matrix_coefficients; /** used in deriving luma and chroma signals from RGB primaries   */
    } video_signal_description;
    uint32_t seqhdr_data_length; /** Additional bytes following (NVVIDEOFORMATEX)                  */
};

typedef enum {
    VK_PARSER_PKT_ENDOFSTREAM = 0x01, /**< Set when this is the last packet for this stream  */
    VK_PARSER_PKT_TIMESTAMP = 0x02, /**< Timestamp is valid                                */
    VK_PARSER_PKT_DISCONTINUITY = 0x04, /**< Set when a discontinuity has to be signalled      */
    VK_PARSER_PKT_ENDOFPICTURE = 0x08, /**< Set when the packet contains exactly one frame    */
} VkVideopacketflags;

struct VkParserSourceDataPacket {
    uint32_t flags; /** Combination of VK_PARSER_PKT_XXX flags                              */
    uint32_t payload_size; /** number of bytes in the payload (may be zero if EOS flag is set) */
    const uint8_t* payload; /** Pointer to packet payload data (may be NULL if EOS flag is set) */
    VkVideotimestamp timestamp; /** Presentation time stamp (10MHz clock), only valid if
                                             VK_PARSER_PKT_TIMESTAMP flag is set                                 */
};

#endif // __NV_VULKANVIDEOPARSERPARAMS_H__
