#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <nds/arm7/touch.h>
#include "touchScreen.h"

static bool penDown;
static u16 sample = 1000, noise;
void touchInit() { }
bool touchPenDown() { return penDown; }
bool touchReadData(touchRawArray* data)
{
    for (auto values : {data->rawX, data->rawY, data->z1, data->z2})
    {
        values[0] = sample;
        values[1] = noise;
    }
    return true;
}
void touchApplyCalibration(u16 x, u16 y, u16* px, u16* py) { *px = x; *py = y; }
struct Measurement { u16 value, noisiness; };
extern "C" Measurement libnds_touchMeasurementFilter(u16 values[5]) { return {values[0], values[1]}; }

int main()
{
    touchPosition position;
    for (u16 noisiness : {u16(0), u16(1), u16(17), u16(18), u16(19), u16(32), u16(40), u16(50)})
    {
        penDown = false;
        touch_update(position);
        touch_update(position);
        sample = 1000;
        noise = 0;
        penDown = true;
        assert(touch_update(position));
        assert(position.rawx == 1000 && position.rawy == 1000);
        sample = 2000;
        noise = noisiness;
        assert(touch_update(position));
        int weight = noisiness > 18 ? noisiness - 18 : 0;
        u16 expected = (2000 * (32 - weight) + 1000 * weight) / 32;
        assert(position.rawx == expected && position.rawy == expected);
        assert(position.rawx >= 1000 && position.rawx <= 2000);
    }
    puts("Touch filtering stays within the sampled positions across noise thresholds");
}
