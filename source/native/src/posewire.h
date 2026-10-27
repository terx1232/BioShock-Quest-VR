#pragma once
#include "xrapi.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

// BioShock Android bridge, little endian, independent of XrAPI's text rounding.
// Four-byte magic, the 28 legacy axes, sync/buttons, altitude and two grip quats.
namespace posewire {
constexpr unsigned kPort = 7874;
constexpr size_t kBytes = 160;
constexpr size_t kExactBytes = 168;
inline bool Parse(const void* data, size_t bytes, xrapi::State& result) {
    bool exact=bytes==kExactBytes&&!memcmp(data,"BSV\2",4);
    if (!exact&&(bytes != kBytes || memcmp(data, "BSV\1", 4))) return false;
    const auto* p = static_cast<const unsigned char*>(data);
    float f[28], grip[9]; uint32_t sync, buttons;
    memcpy(f, p + 4, sizeof(f));
    memcpy(&sync, p + 116, 4); memcpy(&buttons, p + 120, 4);
    memcpy(grip, p + 124, sizeof(grip));
    if (sync > 252 || sync % 12) return false;
    for (float v : f) if (!isfinite(v)) return false;
    for (float v : grip) if (!isfinite(v)) return false;
    float norm = f[18]*f[18]+f[19]*f[19]+f[20]*f[20]+f[21]*f[21];
    if (norm < .5f || norm > 1.5f) return false;
    result.left = {{f[0],f[1],f[2],f[3]}, {grip[1],grip[2],grip[3],grip[4]}, {f[6],f[7],f[8]}, f[4],f[5]};
    result.right = {{f[9],f[10],f[11],f[12]}, {grip[5],grip[6],grip[7],grip[8]}, {f[15],f[16],f[17]}, f[13],f[14]};
    result.hmdOrientation = {f[18],f[19],f[20],f[21]};
    result.hmdPosition = {f[22],f[23],f[24]};
    result.ipd=f[25]; result.fovX=f[26]; result.fovY=f[27];
    result.sync=static_cast<int>(sync); result.altitude=grip[0];
    result.frameId=0;if(exact){memcpy(&result.frameId,p+160,8);if(!result.frameId)return false;}
    for (int i=0;i<xrapi::BUTTON_COUNT;++i) result.buttons[i]=(buttons & (1u<<i))!=0;
    return true;
}
}
