#ifndef CAMERA_UTILITIES_H
#define CAMERA_UTILITIES_H

#include <QMediaDevices>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QGuiApplication>
#include <QVideoFrameFormat>

#include <CameraSpecification.h>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

enum class CodecID {
    H264 = AV_CODEC_ID_H264,
    H265 = AV_CODEC_ID_H265,
    AV1 = AV_CODEC_ID_AV1,
};

std::vector<CameraSpecification> FetchCamerasSpecification();
const AVCodec* GetDecoderCodec(CodecID codecID);
const AVCodec* GetEncoderCodec(CodecID codecID);

namespace CameraUtilitiesLC {
    AVPixelFormat ToAVPixelFormat(QVideoFrameFormat::PixelFormat format);
    bool CodecSupportsPixelFormat(const AVCodec* codec, AVPixelFormat format);
    AVPixelFormat PickEncoderPixelFormat(const AVCodec* codec, AVPixelFormat inputFormat);
    bool CanOpenEncoderWithFormat(const AVCodec* codec, int width, int height, int fps, AVPixelFormat encoderPixelFormat);
    bool IsCameraFormatSupportedByCodec(const AVCodec* codec, const QCameraFormat& cameraFormat, int requestedFps);

    struct NalSpan {
        const uint8_t* data;
        size_t size;
    };

    bool SplitAvcc(const uint8_t* data, size_t size, uint8_t nalLengthSize, std::vector<NalSpan>& out);
    bool IsAnnexB(const uint8_t* data, size_t size);
    void SplitAnnexB(const uint8_t* data, size_t size, std::vector<NalSpan>& out);
    bool SplitAvccAuto(const uint8_t* data, size_t size, uint8_t preferredNalLengthSize, std::vector<NalSpan>& out);
    bool UpdateH264ParameterSetsFromNalSpans(std::vector<std::vector<uint8_t>>& parameterSets, const std::vector<NalSpan>& nals);
    bool UpdateH264ParameterSetsFromExtradata(std::vector<std::vector<uint8_t>>& parameterSets, uint8_t& nalLengthSize, const uint8_t* data, size_t size);
    bool IsH264ParameterSetNal(uint8_t nalHeader);
    bool IsSupportedH264NalType(uint8_t nalHeader);
    size_t FindStart(const uint8_t* data, size_t size, size_t from);
    std::vector<std::vector<uint8_t>> ParseAnnexBH264ParameterSets(const uint8_t* data, size_t size);
    std::vector<std::vector<uint8_t>> ParseAvccH264ParameterSets(const uint8_t* data, size_t size);
    std::vector<std::vector<uint8_t>> ExtractH264ParameterSets(const AVCodecContext* codecContext);
    uint8_t ExtractAvccNalLengthSize(const AVCodecContext* codecContext);
    bool UpsertH264ParameterSet(std::vector<std::vector<uint8_t>>& parameterSets, const uint8_t* data, size_t size);
    bool EnsureReusableFrameBuffer(AVFrame*& frame, AVPixelFormat format, int width, int height);
}

#endif //CAMERA_UTILITIES_H
