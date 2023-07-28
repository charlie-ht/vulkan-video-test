#if HAVE_OPENH264
#include "codec_api.h"
#endif

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util.hpp"

int debuglevel = 10;

void ParseNAL(const u8* bufptr, int len);
void ParseNAL(const u8* bufptr, int len)
{
    assert (len < 1024*512);
    int i;
    int padding;

    for (i = 0; i < len; i++) {
        if (i % 16 == 0) {
            putc('\n', stderr);
            fprintf(stderr, "%08x: ", i);
        }
        fprintf(stderr, "%02x ", bufptr[i]);
    }

    padding = util::RoundUp32(i, 16);
    for (; i < padding; i++)
        fputs("   ", stderr);
    putc('\n', stderr);
}

// FIXME: Don't think this parser works correctly...
int ParseBitstream(char** args, int numArgs)
{
#if HAVE_OPENH264
    int ret = -1;

    char* inFilename = args[0];

    u8 uiStartCode[4] = { 0, 0, 0, 1 };

    util::sized_buffer bitstream = util::ReadWholeBinaryFileIntoMemory(inFilename);
    u8* pBuf = new u8[bitstream.len + 4];
    memcpy(pBuf, bitstream.bytes, bitstream.len);
    memcpy(pBuf + bitstream.len, &uiStartCode[0], 4);

    ISVCDecoder* h264Decoder;
    SParserBsInfo sDstParseInfo;
    util::ZeroMemory((u8*)&sDstParseInfo, sizeof(SParserBsInfo));

    // Managed by OpenH264
    sDstParseInfo.pDstBuff = new u8[32 * 1024];

    WelsCreateDecoder(&h264Decoder);

    SDecodingParam sDecParam = { 0 };
    sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    sDecParam.bParseOnly = true;

    h264Decoder->Initialize(&sDecParam);

    int r = h264Decoder->DecodeParser(pBuf, bitstream.len, &sDstParseInfo);
    if (r != 0) XERROR(0, "Could not parse the bitstream\n");

    DEBUG(1, "Found %d NALs\n", sDstParseInfo.iNalNum);

    u8* bufptr = sDstParseInfo.pDstBuff;

    for (int nalNdx = 0; nalNdx < sDstParseInfo.iNalNum; nalNdx++) {
        int nalNumBytes = sDstParseInfo.pNalLenInByte[nalNdx];
        fprintf(stderr, "\nNal #%d (num bytes %d):", nalNdx, nalNumBytes);
        ParseNAL(bufptr, nalNumBytes);
        bufptr += nalNumBytes;
    }

    h264Decoder->Uninitialize();
    WelsDestroyDecoder(h264Decoder);
    FreeSizedBuffer(&bitstream);
    delete[] pBuf;
    return ret;
#else
    (void)args; (void)numArgs;
    return 1;
#endif
}

int EncodeBitstream(char **args, int numArgs)
{
#if HAVE_OPENH264
    // ffmpeg -f lavfi -i testsrc=size=640x480:rate=1 -vframes 1 -pix_fmt yuv420p output.yuv

    if (numArgs != 5) XERROR(0, "-encode <I420 yuv filename> <width> <height> <numFramesToDecode> <output H264 filename>\n");

    const char* yuvDataI420 = args[0];

    const int width = atoi(args[1]);
    const int height = atoi(args[2]);
    const int numFrames = atoi(args[3]);

    const char* outputH264Filename = args[4];

    ISVCEncoder* encoder_;
    int rv = WelsCreateSVCEncoder(&encoder_);
    assert(rv == 0);
    assert(encoder_ != NULL);

    SEncParamBase param;
    util::ZeroMemory(&param, sizeof(SEncParamBase));

    param.iUsageType = SCREEN_CONTENT_REAL_TIME; // from EUsageType enum
    param.fMaxFrameRate = 1;
    param.iPicWidth = width;
    param.iPicHeight = height;
    param.iTargetBitrate = 50000000;
    encoder_->Initialize(&param);

    int g_LevelSetting = 1;
    encoder_->SetOption(ENCODER_OPTION_TRACE_LEVEL, &g_LevelSetting);
    int videoFormat = videoFormatI420;
    encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);

    int frameSize = std::ceil(float(width) * float(height) * 1.5f);

    SFrameBSInfo info;
    memset(&info, 0, sizeof(SFrameBSInfo));
    SSourcePicture pic;
    memset(&pic, 0, sizeof(SSourcePicture));

    util::sized_buffer Bitstream = util::ReadWholeBinaryFileIntoMemory(yuvDataI420);

    assert(!Bitstream.has_error);
    printf("bitstream bytes=%d num frames=%d frame bytes=%d\n", Bitstream.len, numFrames, frameSize);
    assert(Bitstream.len == numFrames * frameSize);

    u8* pData = Bitstream.bytes;

    pic.iPicWidth = width;
    pic.iPicHeight = height;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = pic.iPicWidth;
    pic.iStride[1] = pic.iPicWidth / 2;
    pic.iStride[2] = pic.iPicWidth / 2;
    pic.pData[0] = pData;
    pic.pData[1] = pic.pData[0] + (width * height);
    pic.pData[2] = pic.pData[1] + ((width * height) / 4);

    FILE* output = fopen(outputH264Filename, "wb");

    for (int num = 0; num < numFrames; num++) {
        // prepare input data
        rv = encoder_->EncodeFrame(&pic, &info);
        assert(rv == cmResultSuccess);
        if (info.eFrameType != videoFrameTypeSkip) {
            // output bitstream handling
            fprintf(stderr, "%d: encoded %d layers %d encoded bytes\n", num, info.iLayerNum, info.iFrameSizeInBytes);
            fflush(stderr);
            for (int layer = 0; layer < info.iLayerNum; layer++) {
                SLayerBSInfo layerInfo = info.sLayerInfo[layer];
                switch (layerInfo.eFrameType) {
                case videoFrameTypeInvalid: // ,    ///< encoder not ready or parameters are invalidate
                    break;
                case videoFrameTypeIDR: //,        ///< IDR frame in H.264
                    printf("IDR frame\n");
                    break;
                case videoFrameTypeI: //,          ///< I frame type
                    printf("I frame\n");
                    break;
                case videoFrameTypeP: //,          ///< P frame type
                    printf("P frame\n");
                    break;
                case videoFrameTypeSkip: //,       ///< skip the frame based encoder kernel
                    printf("SKIP");
                    break;
                case videoFrameTypeIPMixed: ///< a frame where I and P slices are mixing, not supported yet
                    printf("IP Mixed\n");
                    break;
                }
                printf("temporal id=%d  spatial id=%d quality id=%d\n", layerInfo.uiTemporalId, layerInfo.uiSpatialId, layerInfo.uiQualityId);
                printf("Sub seq id=%d\n", layerInfo.iSubSeqId);

                size_t bitstreamChunkSize = 0;
                for (int nalUnitIdx = 0; nalUnitIdx < layerInfo.iNalCount; nalUnitIdx++) {
                    bitstreamChunkSize += layerInfo.pNalLengthInByte[nalUnitIdx];
                }

                fwrite(layerInfo.pBsBuf, 1, bitstreamChunkSize, output);
            }
        }
    }

    if (encoder_) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder(encoder_);
    }

    if (output)
        fclose(output);
    return 0;

#else
    (void)args; (void)numArgs;
    return 0;
#endif
}

#if HAVE_OPENH264
void logDecodingState(DECODING_STATE st)
{
    switch (st) {
        /**
         * Errors derived from bitstream parsing
         */
    case dsErrorFree: //  = 0x00,   ///< bit stream error-free
        fprintf(stderr, "DecodeFrameNoDelay -> dsErrorFree\n");
        fflush(stderr);
        break;
    case dsFramePending: //  = 0x01,   ///< need more throughput to generate a frame output,
        fprintf(stderr, "DecodeFrameNoDelay -> dsFramePending\n");
        fflush(stderr);
        break;
    case dsRefLost: //  = 0x02,   ///< layer lost at reference frame with temporal id 0
        fprintf(stderr, "DecodeFrameNoDelay -> dsRefLost\n");
        fflush(stderr);
        break;
    case dsBitstreamError: //  = 0x04,   ///< error bitstreams(maybe broken internal frame) the decoder cared
        fprintf(stderr, "DecodeFrameNoDelay -> dsBitstreamError\n");
        fflush(stderr);
        break;
    case dsDepLayerLost: //  = 0x08,   ///< dependented layer is ever lost
        fprintf(stderr, "DecodeFrameNoDelay -> dsDepLayerLost\n");
        fflush(stderr);
        break;
    case dsNoParamSets: //  = 0x10,   ///< no parameter set NALs involved
        fprintf(stderr, "DecodeFrameNoDelay -> dsNoParamSets\n");
        fflush(stderr);
        break;
    case dsDataErrorConcealed: //  = 0x20,   ///< current data error concealed specified
        fprintf(stderr, "DecodeFrameNoDelay -> dsDataErrorConcealed\n");
        fflush(stderr);
        break;
    case dsRefListNullPtrs: //  = 0x40, ///<ref picure list contains null ptrs within uiRefCount range
        fprintf(stderr, "DecodeFrameNoDelay -> dsRefListNullPtrs\n");
        fflush(stderr);
        break;
        /**
         * Errors derived from logic level
         */
    case dsInvalidArgument: //      = 0x1000, ///< invalid argument specified
        fprintf(stderr, "DecodeFrameNoDelay -> dsInvalidArgument\n");
        fflush(stderr);
        break;
    case dsInitialOptExpected: //   = 0x2000, ///< initializing operation is expected
        fprintf(stderr, "DecodeFrameNoDelay -> dsInitialOptExpected\n");
        fflush(stderr);
        break;
    case dsOutOfMemory: //          = 0x4000, ///< out of memory due to new request
        fprintf(stderr, "DecodeFrameNoDelay -> dsOutOfMemory\n");
        fflush(stderr);
        break;
        /**
         * ANY OTHERS?
         */
    case dsDstBufNeedExpan: //      = 0x8000  ///< actual picture size exceeds size of dst pBuffer feed in decoder, so need expand its size
        fprintf(stderr, "DecodeFrameNoDelay -> dsDstBufNeedExpan\n");
        fflush(stderr);
        break;
    }
}
#endif

void Write2File(FILE* pFp, unsigned char* pData[3], int iStride[2], u32 iWidth, u32 iHeight)
{
    u32 i;
    unsigned char* pPtr = nullptr;

    pPtr = pData[0];
    for (i = 0; i < iHeight; i++) {
        fwrite(pPtr, 1, iWidth, pFp);
        pPtr += iStride[0];
    }
    fprintf(stderr, "WelsXXX:iHeight=%d iWidth=%d iStride[0]=%d %ld\n", iHeight, iWidth, iStride[0], (pPtr - pData[0]));
    fflush(stderr);

    iHeight = iHeight / 2;
    iWidth = iWidth / 2;
    pPtr = pData[1];
    for (i = 0; i < iHeight; i++) {
        fwrite(pPtr, 1, iWidth, pFp);
        pPtr += iStride[1];
    }

    pPtr = pData[2];
    for (i = 0; i < iHeight; i++) {
        fwrite(pPtr, 1, iWidth, pFp);
        pPtr += iStride[1];
    }
}

int DecodeH264(char **args, int numArgs)
{
#if HAVE_OPENH264
    int ret = -1;

    if (numArgs != 2) XERROR(0, "-decode <input H264 bitstream> <output I420 YUV filename>\n");
    const char* h264BitstreamFilename = args[0];
    const char* yuvOutputFilename = args[1];

    u8 uiStartCode[4] = { 0, 0, 0, 1 };

    ISVCDecoder* h264Decoder;

    SBufferInfo sDstBufInfo;
    util::ZeroMemory((u8*)&sDstBufInfo, sizeof(SBufferInfo));

    WelsCreateDecoder(&h264Decoder);

    SDecodingParam sDecParam = { 0 };
    sDecParam.uiTargetDqLayer = (u8)-1;
    sDecParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;
    sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

    int levelSetting = 4;
    int32_t iThreadCount = 0;
    h264Decoder->SetOption(DECODER_OPTION_NUM_OF_THREADS, &iThreadCount);
    h264Decoder->SetOption(DECODER_OPTION_TRACE_LEVEL, &levelSetting);
    h264Decoder->Initialize(&sDecParam);

    // Load h264 bitstream
    util::sized_buffer bitstream = util::ReadWholeBinaryFileIntoMemory(h264BitstreamFilename);
    assert(!bitstream.has_error);

    u8* pBuf = new u8[bitstream.len + 4];
    memcpy(pBuf, bitstream.bytes, bitstream.len);
    memcpy(pBuf + bitstream.len, &uiStartCode[0], 4);

    uint8_t* pData[3] = { NULL };
    uint8_t* pDst[3] = { NULL };

    DECODING_STATE st;

    i32 iBufPos = 0;
    i32 iFrameCount = 0;
    i32 iSliceSize = 0;
    i32 iSliceIndex = 0;
    i32 iWidth = 0;
    i32 iHeight = 0;

    bool bEndOfStreamFlag = false;

    unsigned long long uiTimeStamp = 0;

    while (true) {
        if (iBufPos >= bitstream.len) {
            bEndOfStreamFlag = true;
            h264Decoder->SetOption(DECODER_OPTION_END_OF_STREAM, (void*)&bEndOfStreamFlag);
            break;
        }

        // Find end of next slice.
        int i = 0;
        for (i = 0; i < bitstream.len; i++) {
            if ((pBuf[iBufPos + i] == 0 && pBuf[iBufPos + i + 1] == 0 && pBuf[iBufPos + i + 2] == 0 && pBuf[iBufPos + i + 3] == 1
                    && i > 0)
                || (pBuf[iBufPos + i] == 0 && pBuf[iBufPos + i + 1] == 0 && pBuf[iBufPos + i + 2] == 1 && i > 0)) {
                break;
            }
        }
        iSliceSize = i;

        if (iSliceSize < 4) {
            iBufPos += iSliceSize;
            continue;
        }

        pData[0] = NULL;
        pData[1] = NULL;
        pData[2] = NULL;

        uiTimeStamp++;

        memset(&sDstBufInfo, 0, sizeof(SBufferInfo));

        sDstBufInfo.uiInBsTimeStamp = uiTimeStamp;

        st = h264Decoder->DecodeFrameNoDelay(pBuf + iBufPos, iSliceSize, pData, &sDstBufInfo);
        logDecodingState(st);

        if (sDstBufInfo.iBufferStatus == 1) {
            ++iFrameCount;

            pDst[0] = sDstBufInfo.pDst[0];
            pDst[1] = sDstBufInfo.pDst[1];
            pDst[2] = sDstBufInfo.pDst[2];

            DEBUG(1, "New frame! %d\n", iFrameCount);

            iWidth = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
            iHeight = sDstBufInfo.UsrData.sSystemBuffer.iHeight;
            int iStride[2];
            iStride[0] = sDstBufInfo.UsrData.sSystemBuffer.iStride[0];
            iStride[1] = sDstBufInfo.UsrData.sSystemBuffer.iStride[1];

            FILE* decodedFrame = fopen(yuvOutputFilename, "wb");
            if (!decodedFrame) XERROR(errno, "opening %s", yuvOutputFilename);

            Write2File(decodedFrame, (unsigned char**)pDst, iStride, iWidth, iHeight);

            fclose(decodedFrame);
        }

        iBufPos += iSliceSize;
        ++iSliceIndex;
    }

    if (h264Decoder) {
        h264Decoder->Uninitialize();
        WelsDestroyDecoder(h264Decoder);
    }

    util::FreeSizedBuffer(&bitstream);
    return 0;

#else
    (void)args; (void)numArgs;
    return 0;
#endif
}
