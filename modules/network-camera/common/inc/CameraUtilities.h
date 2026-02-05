#ifndef CAMERA_UTILITIES_H
#define CAMERA_UTILITIES_H

#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>

#include <CameraSpecification.h>
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

#endif //CAMERA_UTILITIES_H
