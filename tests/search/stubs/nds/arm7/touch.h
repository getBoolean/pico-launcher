#pragma once
#include <nds/touch.h>
struct touchRawArray { u16 rawX[5], rawY[5], z1[5], z2[5]; };
void touchInit();
bool touchPenDown();
bool touchReadData(touchRawArray* data);
void touchApplyCalibration(u16 x, u16 y, u16* px, u16* py);
