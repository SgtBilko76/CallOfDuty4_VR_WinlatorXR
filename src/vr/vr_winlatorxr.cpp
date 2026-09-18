// winsock2.h must precede any header that pulls in Windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "vr/vr_winlatorxr.h"

#include "qcommon/qcommon.h"

#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace kisak::vr::winlatorxr
{
namespace
{

constexpr const char* kExchangeDirectory = "Z:\\tmp\\xr";
constexpr const char* kVersionPath = "Z:\\tmp\\xr\\version";
constexpr const char* kVrMarkerPath = "Z:\\tmp\\xr\\vr";
constexpr const char* kSystemPath = "Z:\\tmp\\xr\\system";
constexpr const char* kDefaultApiVersion = "0.5";

constexpr std::size_t kRenderFrameSyncHistoryCount = 16u;

struct RenderFrameSync
{
    bool valid = false;
    std::uint32_t renderFrameId = 0u;
    int sync = 0;
};

std::atomic<bool> g_running{false};
std::atomic<bool> g_stopRequested{false};
std::atomic<bool> g_wsaStarted{false};
std::thread g_receiveThread;
SOCKET g_receiveSocket = INVALID_SOCKET;
SOCKET g_sendSocket = INVALID_SOCKET;
std::uint16_t g_boundPort = 0u;

std::mutex g_packetMutex;
std::condition_variable g_packetCondition;
Packet g_latestPacket;
bool g_latestPacketValid = false;
std::uint64_t g_packetCount = 0u;
std::uint64_t g_rejectedPacketCount = 0u;
std::string g_lastRejectedPacket;

std::mutex g_sendMutex;
SystemInfo g_systemInfo;

std::mutex g_syncMutex;
std::array<RenderFrameSync, kRenderFrameSyncHistoryCount>
    g_renderFrameSyncs = {};
std::size_t g_renderFrameSyncWriteIndex = 0u;

bool g_loggedStampFailure = false;
bool g_loggedFirstStamp = false;
bool g_loggedMenuFailure = false;
bool g_loggedFirstMenu = false;

std::atomic<int> g_menuSourceEye{-1};

// Fraction of each eye's width used by the mono menu panel.
constexpr float kMenuPanelWidthFraction = 0.8f;
constexpr float kMenuPanelAspect = 4.0f / 3.0f;

bool DirectoryExists(const char* path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
}

bool WriteTextFile(const char* path, const char* text)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr)
    {
        return false;
    }

    const std::size_t length = std::strlen(text);
    const bool written =
        std::fwrite(text, 1u, length, file) == length;
    std::fclose(file);
    return written;
}

std::string ReadTextFile(const char* path)
{
    std::string text;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr)
    {
        return text;
    }

    std::array<char, 512> buffer = {};
    std::size_t read = 0u;
    while ((read = std::fread(buffer.data(), 1u, buffer.size(), file)) > 0u &&
           text.size() < 4096u)
    {
        text.append(buffer.data(), read);
    }

    std::fclose(file);
    return text;
}

const char* RequestedApiVersion()
{
    const char* requested = std::getenv("KISAK_VR_WINLATORXR_API_VERSION");
    return requested != nullptr && requested[0] != '\0'
        ? requested
        : kDefaultApiVersion;
}

bool BindReceiveSocket(const std::uint16_t port)
{
    SOCKET candidate = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (candidate == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (bind(
            candidate,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(candidate);
        return false;
    }

    // A short timeout lets Stop() join the receiver promptly.
    const DWORD timeoutMilliseconds = 250u;
    setsockopt(
        candidate,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeoutMilliseconds),
        sizeof(timeoutMilliseconds));

    g_receiveSocket = candidate;
    g_boundPort = port;
    return true;
}

void ReceiveLoop()
{
    // Runs outside the game's threads; it must not call Com_Printf.
    std::array<char, 2048> buffer = {};

    while (!g_stopRequested.load(std::memory_order_acquire))
    {
        const int received = recvfrom(
            g_receiveSocket,
            buffer.data(),
            static_cast<int>(buffer.size() - 1u),
            0,
            nullptr,
            nullptr);

        if (received <= 0)
        {
            continue;
        }

        const std::string_view text(
            buffer.data(),
            static_cast<std::size_t>(received));

        Packet packet;
        if (!ParsePacket(text, &packet))
        {
            std::lock_guard<std::mutex> lock(g_packetMutex);
            ++g_rejectedPacketCount;
            g_lastRejectedPacket.assign(
                text.substr(0u, (std::min)(text.size(), std::size_t{200u})));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_packetMutex);
            g_latestPacket = std::move(packet);
            g_latestPacketValid = true;
            ++g_packetCount;
        }

        g_packetCondition.notify_all();
    }
}

// Copies the eye that holds the menu into a centered 4:3 panel in both
// halves. The temporary render target is released immediately so it never
// blocks a legacy IDirect3DDevice9::Reset().
HRESULT PresentMonoMenu(
    IDirect3DDevice9* const device,
    IDirect3DSurface9* const backBuffer,
    const int sourceEye)
{
    D3DSURFACE_DESC description = {};
    HRESULT hr = backBuffer->GetDesc(&description);
    if (FAILED(hr))
    {
        return hr;
    }

    const LONG eyeWidth = static_cast<LONG>(description.Width / 2u);
    const LONG eyeHeight = static_cast<LONG>(description.Height);
    if (eyeWidth < 16 || eyeHeight < 16)
    {
        return S_OK;
    }

    IDirect3DSurface9* menuCopy = nullptr;
    hr = device->CreateRenderTarget(
        static_cast<UINT>(eyeWidth),
        static_cast<UINT>(eyeHeight),
        description.Format,
        D3DMULTISAMPLE_NONE,
        0u,
        FALSE,
        &menuCopy,
        nullptr);
    if (FAILED(hr))
    {
        return hr;
    }

    const RECT sourceRect = {
        sourceEye == 1 ? eyeWidth : 0,
        0,
        sourceEye == 1 ? 2 * eyeWidth : eyeWidth,
        eyeHeight,
    };

    hr = device->StretchRect(
        backBuffer,
        &sourceRect,
        menuCopy,
        nullptr,
        D3DTEXF_NONE);

    LONG panelWidth = static_cast<LONG>(
        static_cast<float>(eyeWidth) * kMenuPanelWidthFraction);
    LONG panelHeight = static_cast<LONG>(
        static_cast<float>(panelWidth) / kMenuPanelAspect);
    const LONG maximumPanelHeight = static_cast<LONG>(
        static_cast<float>(eyeHeight) * kMenuPanelWidthFraction);
    if (panelHeight > maximumPanelHeight)
    {
        panelHeight = maximumPanelHeight;
        panelWidth = static_cast<LONG>(
            static_cast<float>(panelHeight) * kMenuPanelAspect);
    }

    const LONG panelLeft = (eyeWidth - panelWidth) / 2;
    const LONG panelTop = (eyeHeight - panelHeight) / 2;

    if (SUCCEEDED(hr))
    {
        hr = device->ColorFill(
            backBuffer,
            nullptr,
            D3DCOLOR_XRGB(0, 0, 0));
    }

    for (LONG eye = 0; eye < 2 && SUCCEEDED(hr); ++eye)
    {
        const RECT panelRect = {
            eye * eyeWidth + panelLeft,
            panelTop,
            eye * eyeWidth + panelLeft + panelWidth,
            panelTop + panelHeight,
        };

        hr = device->StretchRect(
            menuCopy,
            nullptr,
            backBuffer,
            &panelRect,
            D3DTEXF_LINEAR);
    }

    menuCopy->Release();
    return hr;
}

} // namespace

bool IsContainerDetected()
{
    // WinlatorXR writes the system file itself; a bare folder is not enough.
    const DWORD attributes = GetFileAttributesA(kSystemPath);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

bool Start(std::string* const error)
{
    if (g_running.load(std::memory_order_acquire))
    {
        return true;
    }

    const auto fail = [error](const char* message)
    {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    if (!DirectoryExists(kExchangeDirectory) &&
        !CreateDirectoryA(kExchangeDirectory, nullptr))
    {
        return fail(
            "Z:\\tmp\\xr is unavailable. Start the game from a "
            "WinlatorXR container with the XR API enabled.");
    }

    g_systemInfo = ParseSystemInfo(ReadTextFile(kSystemPath));

    // Winsock 2.2. q_shared.h undefines MAKEWORD.
    WSADATA wsaData = {};
    if (WSAStartup(0x0202u, &wsaData) != 0)
    {
        return fail("WSAStartup failed.");
    }
    g_wsaStarted.store(true, std::memory_order_release);

    if (!BindReceiveSocket(kIncomingPort) &&
        !BindReceiveSocket(kIncomingFallbackPort))
    {
        Stop();
        return fail(
            "Could not bind UDP 127.0.0.1:7872 or :7873 for XrAPI "
            "tracking data.");
    }

    g_sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sendSocket == INVALID_SOCKET)
    {
        Stop();
        return fail("Could not create the XrAPI state socket.");
    }

    const char* apiVersion = RequestedApiVersion();
    const bool wroteVersion = WriteTextFile(kVersionPath, apiVersion);
    const bool wroteMarker = WriteTextFile(kVrMarkerPath, "VR");

    if (!wroteVersion)
    {
        Stop();
        return fail("Could not write Z:\\tmp\\xr\\version.");
    }

    {
        std::lock_guard<std::mutex> lock(g_packetMutex);
        g_latestPacket = {};
        g_latestPacketValid = false;
        g_packetCount = 0u;
        g_rejectedPacketCount = 0u;
        g_lastRejectedPacket.clear();
    }

    {
        std::lock_guard<std::mutex> lock(g_syncMutex);
        g_renderFrameSyncs = {};
        g_renderFrameSyncWriteIndex = 0u;
    }

    g_loggedStampFailure = false;
    g_loggedFirstStamp = false;
    g_loggedMenuFailure = false;
    g_loggedFirstMenu = false;
    g_menuSourceEye.store(-1, std::memory_order_release);
    g_stopRequested.store(false, std::memory_order_release);
    g_receiveThread = std::thread(ReceiveLoop);
    g_running.store(true, std::memory_order_release);

    Com_Printf(
        0,
        "[VR][WINLATORXR] XrAPI %s advertised%s; listening on "
        "127.0.0.1:%u. Headset '%s' '%s', virtual screen %dx%d.\n",
        apiVersion,
        wroteMarker ? "" : " (vr marker not written)",
        static_cast<unsigned int>(g_boundPort),
        g_systemInfo.manufacturer.c_str(),
        g_systemInfo.product.c_str(),
        g_systemInfo.screenWidth,
        g_systemInfo.screenHeight);

    return true;
}

void Stop()
{
    const bool wasRunning =
        g_running.exchange(false, std::memory_order_acq_rel);

    g_stopRequested.store(true, std::memory_order_release);

    if (g_receiveThread.joinable())
    {
        g_receiveThread.join();
    }

    if (wasRunning)
    {
        // Return WinlatorXR to its normal flat presentation.
        StatePacket state;
        state.vrMode = VrMode::Disabled;
        state.stereoMode = StereoMode::Flat;
        SendState(state);
        DeleteFileA(kVrMarkerPath);
    }

    if (g_receiveSocket != INVALID_SOCKET)
    {
        closesocket(g_receiveSocket);
        g_receiveSocket = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(g_sendMutex);
        if (g_sendSocket != INVALID_SOCKET)
        {
            closesocket(g_sendSocket);
            g_sendSocket = INVALID_SOCKET;
        }
    }

    if (g_wsaStarted.exchange(false, std::memory_order_acq_rel))
    {
        WSACleanup();
    }

    g_boundPort = 0u;
}

bool IsRunning()
{
    return g_running.load(std::memory_order_acquire);
}

const SystemInfo& GetSystemInfo()
{
    return g_systemInfo;
}

bool WaitForPacket(
    const int lastSync,
    const unsigned int timeoutMilliseconds,
    Packet* const packet,
    bool* const fresh)
{
    if (packet == nullptr)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(g_packetMutex);

    const bool arrived = g_packetCondition.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMilliseconds),
        [lastSync]()
        {
            return g_latestPacketValid &&
                g_latestPacket.sync != lastSync;
        });

    if (fresh != nullptr)
    {
        *fresh = arrived;
    }

    if (!g_latestPacketValid)
    {
        return false;
    }

    *packet = g_latestPacket;
    return true;
}

std::uint64_t ReceivedPacketCount()
{
    std::lock_guard<std::mutex> lock(g_packetMutex);
    return g_packetCount;
}

std::uint64_t RejectedPacketCount(std::string* lastRejected)
{
    std::lock_guard<std::mutex> lock(g_packetMutex);
    if (lastRejected != nullptr)
    {
        *lastRejected = g_lastRejectedPacket;
    }
    return g_rejectedPacketCount;
}

void SendState(const StatePacket& state)
{
    const std::string text = FormatStatePacket(state);

    std::lock_guard<std::mutex> lock(g_sendMutex);
    if (g_sendSocket == INVALID_SOCKET)
    {
        return;
    }

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(kOutgoingStatePort);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    sendto(
        g_sendSocket,
        text.data(),
        static_cast<int>(text.size()),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));
}

void SetMenuSourceEye(const int eyeIndex)
{
    g_menuSourceEye.store(
        eyeIndex == 0 || eyeIndex == 1 ? eyeIndex : -1,
        std::memory_order_release);
}

void RecordRenderFrameSync(
    const std::uint32_t renderFrameId,
    const int sync)
{
    std::lock_guard<std::mutex> lock(g_syncMutex);

    RenderFrameSync& entry =
        g_renderFrameSyncs[g_renderFrameSyncWriteIndex];
    entry.valid = true;
    entry.renderFrameId = renderFrameId;
    entry.sync = sync;

    g_renderFrameSyncWriteIndex =
        (g_renderFrameSyncWriteIndex + 1u) %
        g_renderFrameSyncs.size();
}

} // namespace kisak::vr::winlatorxr

void VR_WinlatorXrBeforePresent(
    IDirect3DDevice9* const device,
    const std::uint64_t renderFrameId)
{
    namespace wxr = kisak::vr::winlatorxr;

    if (device == nullptr ||
        !wxr::IsRunning() ||
        FAILED(device->TestCooperativeLevel()))
    {
        return;
    }

    int sync = 0;
    bool matched = false;

    {
        std::lock_guard<std::mutex> lock(wxr::g_syncMutex);
        for (const wxr::RenderFrameSync& entry : wxr::g_renderFrameSyncs)
        {
            if (entry.valid &&
                entry.renderFrameId ==
                    static_cast<std::uint32_t>(renderFrameId))
            {
                sync = entry.sync;
                matched = true;
                break;
            }
        }
    }

    // Menus and loading screens do not record a render pose; stamp the most
    // recent sync so WinlatorXR keeps presenting.
    if (!matched)
    {
        std::lock_guard<std::mutex> lock(wxr::g_packetMutex);
        if (!wxr::g_latestPacketValid)
        {
            return;
        }
        sync = wxr::g_latestPacket.sync;
    }

    IDirect3DSurface9* backBuffer = nullptr;
    HRESULT hr = device->GetBackBuffer(
        0,
        0,
        D3DBACKBUFFER_TYPE_MONO,
        &backBuffer);

    const int menuSourceEye =
        wxr::g_menuSourceEye.load(std::memory_order_acquire);

    if (SUCCEEDED(hr) && backBuffer != nullptr && menuSourceEye >= 0)
    {
        const HRESULT menuResult =
            wxr::PresentMonoMenu(device, backBuffer, menuSourceEye);

        if (FAILED(menuResult) && !wxr::g_loggedMenuFailure)
        {
            Com_PrintWarning(
                0,
                "[VR][WINLATORXR] Could not copy the menu eye into both "
                "window halves (HRESULT 0x%08X); menus may appear in one "
                "eye only.\n",
                static_cast<unsigned int>(menuResult));
            wxr::g_loggedMenuFailure = true;
        }
        else if (SUCCEEDED(menuResult) && !wxr::g_loggedFirstMenu)
        {
            Com_Printf(
                0,
                "[VR][WINLATORXR] Presented the %s-eye menu as a centered "
                "4:3 panel in both eyes.\n",
                menuSourceEye == 1 ? "right" : "left");
            wxr::g_loggedFirstMenu = true;
        }
    }

    if (SUCCEEDED(hr) && backBuffer != nullptr)
    {
        // Stamp last so the menu copy cannot overwrite it. HWXR stamps a
        // 10x10 block so window scaling cannot lose the pixel.
        const RECT syncRect = {0, 0, 10, 10};
        hr = device->ColorFill(
            backBuffer,
            &syncRect,
            D3DCOLOR_XRGB(wxr::SyncPixelRed(sync), 0, 0));
        backBuffer->Release();
    }

    if (FAILED(hr))
    {
        if (!wxr::g_loggedStampFailure)
        {
            Com_PrintWarning(
                0,
                "[VR][WINLATORXR] Could not stamp the HMD_SYNC pixel "
                "(HRESULT 0x%08X). WinlatorXR may present frames with a "
                "mismatched pose.\n",
                static_cast<unsigned int>(hr));
            wxr::g_loggedStampFailure = true;
        }
        return;
    }

    if (!wxr::g_loggedFirstStamp)
    {
        Com_Printf(
            0,
            "[VR][WINLATORXR] Stamped the first HMD_SYNC pixel (%d, %s).\n",
            sync,
            matched ? "matched render pose" : "latest packet");
        wxr::g_loggedFirstStamp = true;
    }
}
