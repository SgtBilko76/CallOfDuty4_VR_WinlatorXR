#pragma once

#include "vr/vr_winlatorxr_protocol.h"

#include <array>
#include <cstdint>
#include <string>

struct IDirect3DDevice9;

// KISAK_SP_VR_WINLATORXR_XRAPI_V1
// Container-side transport for WinlatorXR's XrAPI. The game renders its
// packed side-by-side frame straight into its own window; WinlatorXR shows
// the left half to the left eye and the right half to the right eye.
namespace kisak::vr::winlatorxr
{

// True when WinlatorXR's Z:\tmp\xr\system headset description is visible
// to this process.
bool IsContainerDetected();

// Advertises XrAPI support to WinlatorXR and starts the UDP receiver.
bool Start(std::string* error);

void Stop();

bool IsRunning();

const SystemInfo& GetSystemInfo();

// Waits until a packet whose HMD_SYNC differs from lastSync arrives, or until
// timeoutMilliseconds elapses. Returns false only when no packet has ever
// been received; *fresh reports whether the returned packet is new.
bool WaitForPacket(
    int lastSync,
    unsigned int timeoutMilliseconds,
    Packet* packet,
    bool* fresh);

std::uint64_t ReceivedPacketCount();

// Packets that did not match the documented layout, with the latest one.
std::uint64_t RejectedPacketCount(std::string* lastRejected);

void SendState(const StatePacket& state);

// Remembers which HMD_SYNC value a render frame's cameras were built from so
// the matching value is stamped when that frame is presented.
void RecordRenderFrameSync(
    std::uint32_t renderFrameId,
    int sync);

// A flat image shown on a virtual screen fixed in the room. Direct
// presentation has no compositor layers, so menus (drawn into one eye) and
// cinematics (drawn across the whole window) are copied from the packed
// backbuffer and drawn as a perspective-correct quad in each eye before
// Present().
struct VirtualScreen
{
    bool active = false;

    // Source rectangle in the packed backbuffer, normalized to its size.
    float sourceLeft = 0.0f;
    float sourceTop = 0.0f;
    float sourceRight = 1.0f;
    float sourceBottom = 1.0f;

    // The screen is split into a grid so no vertex lands far outside an eye
    // image, where rasterizers may drop pre-transformed geometry. Per eye,
    // each grid point (row-major from the top-left) holds normalized
    // eye-image coordinates u, v and 1/depth; depth <= 0 marks a point
    // behind the eye.
    static constexpr std::size_t kGridCells = 8u;
    static constexpr std::size_t kGridPoints =
        (kGridCells + 1u) * (kGridCells + 1u);
    std::array<std::array<std::array<float, 3>, kGridPoints>, 2> grid = {};
};

void SetVirtualScreen(const VirtualScreen& screen);

} // namespace kisak::vr::winlatorxr

// Called by the D3D9 capture hook immediately before Present() while direct
// presentation is enabled. Stamps the frame's HMD_SYNC pixel.
void VR_WinlatorXrBeforePresent(
    IDirect3DDevice9* device,
    std::uint64_t renderFrameId);
