#include <Functions.h>

void Functions::FetchCamerasSpecificationCall() {
    const std::vector<CameraSpecification> specifications =
        FetchCamerasSpecification();

    for (const auto& specification : specifications) {
        Debug::Log(specification);
    }
}

void Functions::GetCodecs() {
    void* iterator = nullptr;
    const AVCodec* codec = nullptr;

    std::vector<std::string> encoders;
    std::vector<std::string> decoders;

    Debug::Log(avcodec_configuration());

    Debug::Log("--- LISTING ALL AVAILABLE CODECS ---");

    while ((codec = av_codec_iterate(&iterator))) {
        if (av_codec_is_encoder(codec)) {
            if (codec->id == AV_CODEC_ID_AV1 || codec->id == AV_CODEC_ID_H264 || codec->id == AV_CODEC_ID_HEVC)
                encoders.push_back(codec->name);
        } else if (av_codec_is_decoder(codec)) {
            if (codec->id == AV_CODEC_ID_AV1 || codec->id == AV_CODEC_ID_H264 || codec->id == AV_CODEC_ID_HEVC)
                decoders.push_back(codec->name);
        }
    }

    for (const auto& name : encoders) {
        Debug::Log("Found HW Encoder: {}", name);
    }

    for (const auto& name : decoders) {
        Debug::Log("Found HW Decoder: {}", name);
    }
    Debug::Log("------------------------------------");

    const AVCodec* encoderH264 = GetEncoderCodec(CodecID::H264);
    const AVCodec* encoderH265 = GetEncoderCodec(CodecID::H265);
    const AVCodec* encoderAV1 = GetEncoderCodec(CodecID::AV1);

    const AVCodec* decoderH264 = GetDecoderCodec(CodecID::H264);
    const AVCodec* decoderH265 = GetDecoderCodec(CodecID::H265);
    const AVCodec* decoderAV1 = GetDecoderCodec(CodecID::AV1);

    if (encoderH264 == nullptr || encoderH265 == nullptr || encoderAV1 == nullptr || decoderH264 == nullptr || decoderH265 == nullptr || decoderAV1 == nullptr) {
        Debug::Log("Failed to get encoder or decoder info {}, {}, {}, {}, {}, {}", encoderH264 == nullptr, encoderH265 == nullptr, encoderAV1 == nullptr, decoderH264 == nullptr, decoderH265 == nullptr, decoderAV1 == nullptr);
        return;
    }

    Debug::Log("Encoders (\nH264: {}\nH265: {}\nAV1: {}\n)\nDecoders (\nH264: {}\nH265: {}\nAV1: {}\n)", encoderH264->name, encoderH265->name, encoderAV1->name, decoderH264->name, decoderH265->name, decoderAV1->name);

}