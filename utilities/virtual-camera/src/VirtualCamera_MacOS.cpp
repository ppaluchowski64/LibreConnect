#if defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>

#if TARGET_OS_MAC && !TARGET_OS_IPHONE

#include <VirtualCamera.h>
#include <DebugLog.h>

void VirtualCamera::PushFrame(const void* data) const {

}

void VirtualCamera::SetupCamera(const std::string_view name, const FrameFormat format, const int width, const int height, const int fps) {

}

void VirtualCamera::DestroyCamera() {

}

#endif

#endif