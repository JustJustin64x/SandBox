#ifndef CAMERA_H
#define CAMERA_H

#include "common.h"

Camera2D Camera_Create(int screenWidth, int screenHeight, int worldPixelWidth, int worldPixelHeight);
void Camera_Update(Camera2D* camera, float deltaTime);

#endif // CAMERA_H

