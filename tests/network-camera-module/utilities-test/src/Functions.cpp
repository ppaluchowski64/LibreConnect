#include <Functions.h>


void Functions::FetchCamerasSpecificationCall() {
    const std::vector<CameraSpecification> specifications =
        FetchCamerasSpecification();

    for (const auto& specification : specifications) {
        Debug::Log(specification);
    }
}