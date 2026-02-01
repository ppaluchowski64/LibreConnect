#include <Functions.h>

Functions::Functions(QObject* parent) {

}


QString Functions::fetchSpecs() {
    auto specs = FetchCamerasSpecification();
    if (specs.empty()) return "No cameras detected.";

    std::string result;
    for (const auto& spec : specs) {
        // Using the fmt::formatter defined in your CameraSpecification.h
        result += fmt::format("{}\n\n", spec);
    }
    return QString::fromStdString(result);
}

QString Functions::getDecoder(int codecId) {
    // Cast int to your enum class
    CodecID id = static_cast<CodecID>(codecId);
    const AVCodec* codec = GetDecoderCodec(id);

    if (!codec) return "Decoder not found.";

    // Format AVCodec struct data for display
    return QString("ID: %1\nName: %2\nLong Name: %3")
            .arg(codec->id)
            .arg(codec->name)
            .arg(codec->long_name);
}

QString Functions::getEncoder(int codecId) {
    CodecID id = static_cast<CodecID>(codecId);
    const AVCodec* codec = GetEncoderCodec(id);

    if (!codec) return "Encoder not found.";

    return QString("ID: %1\nName: %2\nLong Name: %3")
            .arg(codec->id)
            .arg(codec->name)
            .arg(codec->long_name);
}