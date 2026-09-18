#pragma once

#include "vr/vr_winlatorxr_protocol.h"

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

// The OpenXR and OpenVR compositors show menus as a mono quad sampled from
// the one eye the UI was drawn into (left for frontend menus and modals,
// right for the active pause menu). Direct presentation has no compositor,
// so the eye is copied into a centered 4:3 panel in both window halves before
// Present(). -1 disables the copy.
void SetMenuSourceEye(int eyeIndex);

} // namespace kisak::vr::winlatorxr

// Called by the D3D9 capture hook immediately before Present() while direct
// presentation is enabled. Stamps the frame's HMD_SYNC pixel.
void VR_WinlatorXrBeforePresent(
    IDirect3DDevice9* device,
    std::uint64_t renderFrameId);
