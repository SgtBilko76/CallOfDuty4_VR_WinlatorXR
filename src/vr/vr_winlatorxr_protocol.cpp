#include "vr/vr_winlatorxr_protocol.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

namespace kisak::vr::winlatorxr
{
namespace
{

bool IsSpace(const char character)
{
    return character == ' ' ||
        character == '\t' ||
        character == '\r' ||
        character == '\n';
}

std::vector<std::string_view> Tokenize(const std::string_view text)
{
    std::vector<std::string_view> tokens;
    std::size_t index = 0u;

    while (index < text.size())
    {
        while (index < text.size() && IsSpace(text[index]))
        {
            ++index;
        }

        const std::size_t start = index;
        while (index < text.size() && !IsSpace(text[index]))
        {
            ++index;
        }

        if (index > start)
        {
            tokens.push_back(text.substr(start, index - start));
        }
    }

    return tokens;
}

bool ParseFloat(const std::string_view token, float* value)
{
    if (token.empty())
    {
        return false;
    }

    const char* first = token.data();
    const char* const last = token.data() + token.size();

    // from_chars rejects a leading '+'; tolerate it from other senders.
    if (*first == '+')
    {
        ++first;
    }

    float parsed = 0.0f;
    const std::from_chars_result result =
        std::from_chars(first, last, parsed);

    if (result.ec != std::errc() ||
        result.ptr != last ||
        !std::isfinite(parsed))
    {
        return false;
    }

    *value = parsed;
    return true;
}

bool IsBooleanToken(const std::string_view token)
{
    return !token.empty() &&
        std::all_of(
            token.begin(),
            token.end(),
            [](const char character)
            {
                return character == 'T' || character == 'F';
            });
}

void CopyFloats(
    const float* source,
    float* destination,
    const std::size_t count)
{
    std::copy(source, source + count, destination);
}

void AppendFixed(std::string* output, float value)
{
    if (!std::isfinite(value))
    {
        value = 0.0f;
    }

    if (value < 0.0f)
    {
        output->push_back('-');
        value = -value;
    }

    const long long scaled =
        std::llround(static_cast<double>(value) * 1000.0);
    output->append(std::to_string(scaled / 1000));
    output->push_back('.');

    const std::string fraction =
        std::to_string(scaled % 1000);
    output->append(3u - fraction.size(), '0');
    output->append(fraction);
}

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && IsSpace(value.front()))
    {
        value.remove_prefix(1u);
    }

    while (!value.empty() && IsSpace(value.back()))
    {
        value.remove_suffix(1u);
    }

    return value;
}

std::string Upper(const std::string_view value)
{
    std::string result(value);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(
                character >= 'a' && character <= 'z'
                    ? character - ('a' - 'A')
                    : character);
        });
    return result;
}

const HandInput* HandForSource(
    const Hands& hands,
    const input::Source source)
{
    const input::Hand hand =
        input::GetSourceDefinition(source).hand;

    if (hand == input::Hand::None)
    {
        return nullptr;
    }

    const HandInput& state =
        hands[hand == input::Hand::Right ? 1u : 0u];

    return state.valid ? &state : nullptr;
}

} // namespace

bool ParsePacket(const std::string_view text, Packet* const packet)
{
    if (packet == nullptr)
    {
        return false;
    }

    const std::vector<std::string_view> tokens = Tokenize(text);
    std::size_t index = 0u;
    Packet parsed;

    float probe = 0.0f;
    if (index < tokens.size() && !ParseFloat(tokens[index], &probe))
    {
        parsed.client = std::string(tokens[index]);
        ++index;
    }

    if (tokens.size() < index + kTrackingFloatCount + 2u)
    {
        return false;
    }

    std::array<float, kTrackingFloatCount> tracking = {};
    for (float& value : tracking)
    {
        if (!ParseFloat(tokens[index], &value))
        {
            return false;
        }
        ++index;
    }

    float syncValue = 0.0f;
    if (!ParseFloat(tokens[index], &syncValue))
    {
        return false;
    }
    ++index;

    const std::string_view buttonToken = tokens[index];
    if (!IsBooleanToken(buttonToken) ||
        buttonToken.size() < kButtonCount)
    {
        return false;
    }
    ++index;

    CopyFloats(&tracking[0], parsed.leftOrientation.data(), 4u);
    CopyFloats(&tracking[4], parsed.leftThumbstick.data(), 2u);
    CopyFloats(&tracking[6], parsed.leftPosition.data(), 3u);
    CopyFloats(&tracking[9], parsed.rightOrientation.data(), 4u);
    CopyFloats(&tracking[13], parsed.rightThumbstick.data(), 2u);
    CopyFloats(&tracking[15], parsed.rightPosition.data(), 3u);
    CopyFloats(&tracking[18], parsed.headOrientation.data(), 4u);
    CopyFloats(&tracking[22], parsed.headPosition.data(), 3u);

    parsed.ipdMeters = tracking[25];
    // Tolerate a sender that reports millimeters.
    if (parsed.ipdMeters > 1.0f)
    {
        parsed.ipdMeters *= 0.001f;
    }

    parsed.fovXDegrees = tracking[26];
    parsed.fovYDegrees = tracking[27];
    parsed.sync = static_cast<int>(std::lround(syncValue));

    for (std::size_t button = 0u; button < kButtonCount; ++button)
    {
        parsed.buttons[button] = buttonToken[button] == 'T';
    }

    // Some builds append the Immersive/SBS flags to the button token.
    if (buttonToken.size() >= kButtonCount + 2u)
    {
        parsed.modeFlagsValid = true;
        parsed.immersive = buttonToken[kButtonCount] == 'T';
        parsed.sideBySide = buttonToken[kButtonCount + 1u] == 'T';
    }

    if (tokens.size() >= index + kExtendedFloatCount)
    {
        std::array<float, kExtendedFloatCount> extended = {};
        bool extendedValid = true;

        for (std::size_t offset = 0u;
             offset < kExtendedFloatCount;
             ++offset)
        {
            if (!ParseFloat(tokens[index + offset], &extended[offset]))
            {
                extendedValid = false;
                break;
            }
        }

        if (extendedValid)
        {
            parsed.extendedValid = true;
            parsed.headAltitudeMeters = extended[0];
            CopyFloats(&extended[1], parsed.leftGripOrientation.data(), 4u);
            CopyFloats(&extended[5], parsed.rightGripOrientation.data(), 4u);
            index += kExtendedFloatCount;
        }
    }

    if (index < tokens.size() &&
        IsBooleanToken(tokens[index]) &&
        tokens[index].size() >= 2u)
    {
        parsed.modeFlagsValid = true;
        parsed.immersive = tokens[index][0] == 'T';
        parsed.sideBySide = tokens[index][1] == 'T';
    }

    *packet = std::move(parsed);
    return true;
}

std::string FormatStatePacket(const StatePacket& state)
{
    std::string output;
    output.reserve(48u);

    AppendFixed(&output, state.leftHapticFrames);
    output.push_back(' ');
    AppendFixed(&output, state.rightHapticFrames);
    output.push_back(' ');
    output.append(std::to_string(static_cast<int>(state.vrMode)));
    output.push_back(' ');
    output.append(std::to_string(static_cast<int>(state.stereoMode)));
    output.push_back(' ');
    AppendFixed(&output, state.fovXDegrees);
    output.push_back(' ');
    AppendFixed(&output, state.fovYDegrees);

    return output;
}

SystemInfo ParseSystemInfo(const std::string_view text)
{
    SystemInfo info;
    std::vector<std::string_view> lines;
    std::size_t start = 0u;

    while (start <= text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }

        const std::string_view line =
            Trim(text.substr(start, end - start));
        if (!line.empty())
        {
            lines.push_back(line);
        }

        start = end + 1u;
    }

    if (!lines.empty())
    {
        info.manufacturer = std::string(lines[0]);
    }

    if (lines.size() > 1u)
    {
        info.product = std::string(lines[1]);
    }

    // The resolution line format is undocumented; accept "1920x1080",
    // "1920 1080", or "1920,1080" on the last line.
    if (lines.size() >= 5u)
    {
        const std::string_view last = lines.back();
        std::array<int, 2> dimensions = {};
        std::size_t found = 0u;
        std::size_t cursor = 0u;

        while (cursor < last.size() && found < dimensions.size())
        {
            if (last[cursor] < '0' || last[cursor] > '9')
            {
                ++cursor;
                continue;
            }

            int value = 0;
            const std::from_chars_result result = std::from_chars(
                last.data() + cursor,
                last.data() + last.size(),
                value);
            dimensions[found++] = value;
            cursor = static_cast<std::size_t>(result.ptr - last.data());
        }

        if (found == 2u && dimensions[0] > 0 && dimensions[1] > 0)
        {
            info.screenWidth = dimensions[0];
            info.screenHeight = dimensions[1];
        }
    }

    return info;
}

bool DefaultControllerRollFlip(const SystemInfo& info)
{
    const std::string manufacturer = Upper(info.manufacturer);
    const std::string product = Upper(info.product);

    if (manufacturer.find("PICO") != std::string::npos ||
        manufacturer.find("PLAY FOR DREAM") != std::string::npos)
    {
        return true;
    }

    // Quest 2 (HOLLYWOOD) and Quest Pro (SEACLIFF) need the flip; Quest 3
    // and 3S (EUREKA, PANTHER) do not.
    return product.find("HOLLYWOOD") != std::string::npos ||
        product.find("SEACLIFF") != std::string::npos ||
        product.find("QUEST 2") != std::string::npos ||
        product.find("QUEST PRO") != std::string::npos;
}

Hands HandsFromPacket(const Packet& packet)
{
    Hands hands = {};

    HandInput& left = hands[0];
    left.valid = true;
    left.thumbstickX = packet.leftThumbstick[0];
    left.thumbstickY = packet.leftThumbstick[1];
    left.trigger = packet.Pressed(Button::LeftTrigger);
    left.squeeze = packet.Pressed(Button::LeftGrip);
    left.primary = packet.Pressed(Button::LeftX);
    left.secondary = packet.Pressed(Button::LeftY);
    left.menu = packet.Pressed(Button::LeftMenu);
    left.thumbstickClick = packet.Pressed(Button::LeftThumbstickPress);

    HandInput& right = hands[1];
    right.valid = true;
    right.thumbstickX = packet.rightThumbstick[0];
    right.thumbstickY = packet.rightThumbstick[1];
    right.trigger = packet.Pressed(Button::RightTrigger);
    right.squeeze = packet.Pressed(Button::RightGrip);
    right.primary = packet.Pressed(Button::RightA);
    right.secondary = packet.Pressed(Button::RightB);
    right.thumbstickClick = packet.Pressed(Button::RightThumbstickPress);

    return hands;
}

bool GetBooleanSourceState(
    const Hands& hands,
    const input::Source source,
    bool* const active)
{
    if (active == nullptr)
    {
        return false;
    }

    *active = false;

    const HandInput* const hand = HandForSource(hands, source);
    if (hand == nullptr)
    {
        return false;
    }

    using input::Source;

    switch (source)
    {
        case Source::LeftPrimary:
        case Source::RightPrimary:
            *active = true;
            return hand->primary;

        case Source::LeftSecondary:
        case Source::RightSecondary:
            *active = true;
            return hand->secondary;

        case Source::LeftMenu:
            *active = true;
            return hand->menu;

        case Source::LeftTrigger:
        case Source::RightTrigger:
            *active = true;
            return hand->trigger;

        case Source::LeftSqueeze:
        case Source::RightSqueeze:
            *active = true;
            return hand->squeeze;

        case Source::LeftThumbstickClick:
        case Source::RightThumbstickClick:
            *active = true;
            return hand->thumbstickClick;

        case Source::LeftPrimaryAxisUp:
        case Source::LeftPrimaryAxisDown:
        case Source::LeftPrimaryAxisLeft:
        case Source::LeftPrimaryAxisRight:
        case Source::RightPrimaryAxisUp:
        case Source::RightPrimaryAxisDown:
        case Source::RightPrimaryAxisLeft:
        case Source::RightPrimaryAxisRight:
        {
            bool vectorActive = false;
            const input::OpenVrVector2 value =
                GetVector2SourceState(
                    hands,
                    input::PhysicalSource(source),
                    &vectorActive);
            *active = vectorActive;
            return vectorActive &&
                input::DirectionalSourcePressed(
                    source,
                    value.x,
                    value.y);
        }

        // The right Touch menu button is the Oculus system button, and
        // XrAPI has no trackpad, thumbrest, or auxiliary control.
        case Source::RightMenu:
        case Source::LeftAuxiliary:
        case Source::RightAuxiliary:
        case Source::LeftTrackpadClick:
        case Source::RightTrackpadClick:
        case Source::LeftThumbrestTouch:
        case Source::RightThumbrestTouch:
        case Source::LeftTrackpadTouch:
        case Source::RightTrackpadTouch:
        case Source::Unbound:
        case Source::LeftPrimaryAxis:
        case Source::RightPrimaryAxis:
        case Source::LeftThumbstick:
        case Source::LeftTrackpad:
        case Source::RightThumbstick:
        case Source::RightTrackpad:
        case Source::Count:
            return false;
    }

    return false;
}

input::OpenVrVector2 GetVector2SourceState(
    const Hands& hands,
    const input::Source source,
    bool* const active)
{
    input::OpenVrVector2 value;
    if (active == nullptr)
    {
        return value;
    }

    *active = false;

    const HandInput* const hand = HandForSource(hands, source);
    if (hand == nullptr)
    {
        return value;
    }

    switch (source)
    {
        case input::Source::LeftPrimaryAxis:
        case input::Source::RightPrimaryAxis:
        case input::Source::LeftThumbstick:
        case input::Source::RightThumbstick:
            break;
        default:
            return value;
    }

    if (!std::isfinite(hand->thumbstickX) ||
        !std::isfinite(hand->thumbstickY))
    {
        return value;
    }

    *active = true;
    value.x = hand->thumbstickX;
    value.y = hand->thumbstickY;
    return value;
}

} // namespace kisak::vr::winlatorxr
