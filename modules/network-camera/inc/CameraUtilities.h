#ifndef CAMERA_UTILITIES_H
#define CAMERA_UTILITIES_H

#include <QMediaDevices>
#include <QCameraDevice>
#include <QGuiApplication>

#include <CameraSpecification.h>
#include <vector>

std::vector<CameraSpecification> FetchCamerasSpecification();

#endif //CAMERA_UTILITIES_H
