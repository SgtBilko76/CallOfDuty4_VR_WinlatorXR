#pragma once

#include "vr/vr_input_bindings.h"
#include "vr/vr_openvr_input.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// KISAK_SP_VR_WINLATORXR_XRAPI_V1
// WinlatorXR runs Windows games inside a Wine/Box64 container on standalone
// Quest and Pico headsets. It does not expose an OpenXR or OpenVR runtime to
// the container. Instead its XrAPI sends plain-text 6DoF packets over UDP and
// displays the game's own window as side-by-side stereo. This module contains
// the platform-independent protocol pieces so the game and the unit tests use
// one parser: https://winlatorxr.github.io/xrapi.html
namespace kisak::vr::winlatorxr
{

constexpr std::uint16_t kIncomingPort = 7872u;
constexpr std::uint16_t kIncomingFallbackPort = 7873u;
constexpr std::uint16_t kOutgoingStatePort = 7278u;

constexpr std::size_t kTrackingFloatCount = 28u;
constexpr std::size_t kButtonCount = 19u;
constexpr std::size_t kExtendedFloatCount = 9u;

// Order of the first T/F token, exactly as transmitted by XrAPI.
enum class Button : std::uint8_t
{
    LeftGrip,
    LeftMenu,
    LeftThumbstickPress,
    LeftThumbstickLeft,
    LeftThumbstickRight,
    LeftThumbstickUp,
    LeftThumbstickDown,
    LeftTrigger,
    LeftX,
    LeftY,
    RightA,
    RightB,
    RightGrip,
    RightThumbstickPress,
    RightThumbstickLeft,
    RightThumbstickRight,
    RightThumbstickUp,
    RightThumbstickDown,
    RightTrigger,
};

// Poses use the headset runtime's OpenXR convention: +X right, +Y up, -Z
// forward, meters, relative to the tracking origin WinlatorXR captured when
// the container started.
struct Packet
{
    std::string client;

    std::array<float, 4> leftOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 2> leftThumbstick = {};
    std::array<float, 3> leftPosition = {};

    std::array<float, 4> rightOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 2> rightThumbstick = {};
    std::array<float, 3> rightPosition = {};

    std::array<float, 4> headOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> headPosition = {};

    float ipdMeters = 0.0f;
    float fovXDegrees = 0.0f;
    float fovYDegrees = 0.0f;
    int sync = 0;

    std::array<bool, kButtonCount> buttons = {};

    // XrAPI 0.5 appends the floor-to-start altitude and upward-oriented grip
    // poses. Older WinlatorXR builds omit them.
    bool extendedValid = false;
    float headAltitudeMeters = 0.0f;
    std::array<float, 4> leftGripOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> rightGripOrientation = {0.0f, 0.0f, 0.0f, 1.0f};

    bool modeFlagsValid = false;
    bool immersive = false;
    bool sideBySide = false;

    bool Pressed(const Button button) const
    {
        return buttons[static_cast<std::size_t>(button)];
    }
};

// Accepts the documented field order, with or without the leading client
// label that current WinlatorXR builds prepend. Returns false for truncated
// or non-finite tracking data.
bool ParsePacket(std::string_view text, Packet* packet);

enum class VrMode : int
{
    Disabled = 0,
    Immersive = 1,
    Screen = 2,
    ScreenHeadTracked = 3,
};

enum class StereoMode : int
{
    UserControlled = -1,
    Flat = 0,
    SideBySide = 1,
    AlternateEye = 2,
};

struct StatePacket
{
    float leftHapticFrames = 0.0f;
    float rightHapticFrames = 0.0f;
    VrMode vrMode = VrMode::Immersive;
    StereoMode stereoMode = StereoMode::SideBySide;
    float fovXDegrees = 0.0f;
    float fovYDegrees = 0.0f;
};

// Locale-independent "L R MODE_VR MODE_3D FOVX FOVY".
std::string FormatStatePacket(const StatePacket& state);

// The Windows app stamps HMD_SYNC as the red channel of the top-left pixel so
// WinlatorXR can match each displayed image to the pose that rendered it.
constexpr std::uint8_t SyncPixelRed(const int sync)
{
    return static_cast<std::uint8_t>(
        static_cast<unsigned int>(sync) & 0xFFu);
}

struct SystemInfo
{
    std::string manufacturer;
    std::string product;
    int screenWidth = 0;
    int screenHeight = 0;
};

// Z:\tmp\xr\system: manufacturer, product, Android version, security patch,
// and virtual screen resolution, one per line.
SystemInfo ParseSystemInfo(std::string_view text);

// HWXR observed upside-down controller orientation on Quest 2 and Pico
// class devices; Quest 3 reports the expected basis.
bool DefaultControllerRollFlip(const SystemInfo& info);

struct HandInput
{
    bool valid = false;
    float thumbstickX = 0.0f;
    float thumbstickY = 0.0f;
    bool trigger = false;
    bool squeeze = false;
    bool primary = false;
    bool secondary = false;
    bool menu = false;
    bool thumbstickClick = false;
};

using Hands = std::array<HandInput, 2>;

Hands HandsFromPacket(const Packet& packet);

// XrAPI maps to Touch-style semantics: primary=A/X, secondary=B/Y,
// menu=left menu only. There is no trackpad, thumbrest, or auxiliary control.
bool GetBooleanSourceState(
    const Hands& hands,
    input::Source source,
    bool* active);

input::OpenVrVector2 GetVector2SourceState(
    const Hands& hands,
    input::Source source,
    bool* active);

} // namespace kisak::vr::winlatorxr
