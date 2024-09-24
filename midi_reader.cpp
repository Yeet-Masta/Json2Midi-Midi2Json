#include "midi_reader.h"
#include <stdexcept>
#include <iostream>

MidiReader::MidiReader(const std::string& filename) : file(filename, std::ios::binary) {
    if (!file) {
        throw std::runtime_error("Unable to open file: " + filename);
    }
}

json MidiReader::parseToJson() {
    json result;
    result["metadata"] = json::object();
    result["tracks"] = json::array();

    json header = parseHeader();
    result["division"] = header["division"];
    result["format"] = header["format"];

    while (file.peek() != EOF) {
        runningStatus = 0;  // Reset running status at the start of each track
        result["tracks"].push_back(parseTrack());
    }

    return result;
}

uint32_t MidiReader::readInt32() {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        result = (result << 8) | readInt8();
    }
    return result;
}

uint16_t MidiReader::readInt16() {
    uint16_t result = 0;
    for (int i = 0; i < 2; ++i) {
        result = (result << 8) | readInt8();
    }
    return result;
}

uint8_t MidiReader::readInt8() {
    return static_cast<uint8_t>(file.get());
}

uint32_t MidiReader::readVarLen() {
    uint32_t value = 0;
    uint8_t byte;
    do {
        byte = readInt8();
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

std::vector<uint8_t> MidiReader::readBytes(int length) {
    std::vector<uint8_t> result(length);
    file.read(reinterpret_cast<char*>(result.data()), length);
    return result;
}

json MidiReader::parseHeader() {
    json header;
    std::vector<uint8_t> chunkType = readBytes(4);
    if (std::string(chunkType.begin(), chunkType.end()) != "MThd") {
        throw std::runtime_error("Invalid MIDI file: Missing MThd header");
    }

    uint32_t headerLength = readInt32();
    if (headerLength != 6) {
        throw std::runtime_error("Invalid MIDI header length");
    }

    header["format"] = readInt16();
    header["numTracks"] = readInt16();
    header["division"] = readInt16();

    return header;
}

json MidiReader::parseTrack() {
    json track = json::array();
    std::vector<uint8_t> chunkType = readBytes(4);
    if (std::string(chunkType.begin(), chunkType.end()) != "MTrk") {
        throw std::runtime_error("Invalid MIDI file: Missing MTrk header");
    }

    uint32_t trackLength = readInt32();
    uint32_t endPosition = file.tellg() + static_cast<std::streamoff>(trackLength);

    while (file.tellg() < endPosition) {
        uint32_t deltaTime = readVarLen();
        json event = parseEvent(deltaTime);
        if (!event.empty()) {
            track.push_back(event);
        }
    }

    return track;
}

/*
json MidiReader::parseEvent(uint32_t& deltaTime) {
    json event;
    event["delta"] = deltaTime;

    uint8_t status = readInt8();

    if (status < 0x80) {
        // Running status, reuse previous status
        file.seekg(-1, std::ios::cur);
        status = runningStatus;
    }
    else {
        runningStatus = status;
    }

    if (status == 0xFF) {
        // Meta event
        uint8_t type = readInt8();
        uint32_t length = readVarLen();
        std::vector<uint8_t> data = readBytes(length);

        event["type"] = "meta";
        event["metaType"] = type;

        switch (type) {
        case 0x00:
            event["name"] = "Sequence Number";
            event["number"] = (data[0] << 8) | data[1];
            break;
        case 0x01:
            event["name"] = "Text Event";
            event["text"] = safeByteToString(data);
            break;
        case 0x02:
            event["name"] = "Copyright Notice";
            event["text"] = safeByteToString(data);
            break;
        case 0x03:
            event["name"] = "Track Name";
            event["text"] = safeByteToString(data);
            break;
        case 0x04:
            event["name"] = "Instrument Name";
            event["text"] = safeByteToString(data);
            break;
        case 0x05:
            event["name"] = "Lyric";
            event["text"] = safeByteToString(data);
            break;
        case 0x06:
            event["name"] = "Marker";
            event["text"] = safeByteToString(data);
            break;
        case 0x07:
            event["name"] = "Cue Point";
            event["text"] = safeByteToString(data);
            break;
        case 0x08:
            event["name"] = "Program Name";
            event["text"] = safeByteToString(data);
            break;
        case 0x09:
            event["name"] = "Device Name";
            event["text"] = safeByteToString(data);
            break;
        case 0x20:
            event["name"] = "MIDI Channel Prefix";
            event["channel"] = data[0];
            break;
        case 0x21:
            event["name"] = "MIDI Port";
            event["port"] = data[0];
            break;
        case 0x2F:
            event["name"] = "End of Track";
            break;
        case 0x51:
            event["name"] = "Set Tempo";
            event["tempo"] = (data[0] << 16) | (data[1] << 8) | data[2];
            event["bpm"] = 60000000.0 / event["tempo"].get<int>();
            break;
        case 0x54:
            event["name"] = "SMPTE Offset";
            event["hour"] = data[0];
            event["minute"] = data[1];
            event["second"] = data[2];
            event["frame"] = data[3];
            event["fractionalFrame"] = data[4];
            break;
        case 0x58:
            event["name"] = "Time Signature";
            event["numerator"] = data[0];
            event["denominator"] = 1 << data[1];
            event["metronome"] = data[2];
            event["thirtySecondNotes"] = data[3];
            break;
        case 0x59:
            event["name"] = "Key Signature";
            event["key"] = static_cast<int8_t>(data[0]);
            event["scale"] = data[1] ? "minor" : "major";
            break;
        case 0x7F:
            event["name"] = "Sequencer-Specific Meta-event";
            event["data"] = safeByteToString(data);
            break;
        default:
            event["name"] = "Unknown Meta Event";
            event["data"] = safeByteToString(data);
        }
    }
    else if (status == 0xF0 || status == 0xF7) {
        // SysEx event
        event["type"] = "sysex";
        event["name"] = (status == 0xF0) ? "SysEx" : "Escaped SysEx";
        uint32_t length = readVarLen();
        std::vector<uint8_t> data = readBytes(length);
        event["data"] = safeByteToString(data);
        runningStatus = 0;  // Reset running status after SysEx
    }
    else {
        // MIDI event
        event["type"] = "midi";
        event["name"] = getMidiEventName(status);

        uint8_t channel = status & 0x0F;
        event["channel"] = channel;

        switch (status & 0xF0) {
        case 0x80:
            event["noteNumber"] = readInt8();
            event["velocity"] = readInt8();
            break;
        case 0x90:
            event["noteNumber"] = readInt8();
            event["velocity"] = readInt8();
            if (event["velocity"] == 0) {
                event["name"] = "Note Off"; // Note-on with velocity 0 is equivalent to note-off
            }
            break;
        case 0xA0:
            event["noteNumber"] = readInt8();
            event["pressure"] = readInt8();
            break;
        case 0xB0:
        {
            uint8_t controllerNumber = readInt8();
            uint8_t controllerValue = readInt8();
            event["controllerNumber"] = controllerNumber;
            event["controllerValue"] = controllerValue;

            // Add names for common controller numbers
            switch (controllerNumber) {
            case 0: event["controllerName"] = "Bank Select"; break;
            case 1: event["controllerName"] = "Modulation Wheel"; break;
            case 7: event["controllerName"] = "Channel Volume"; break;
            case 10: event["controllerName"] = "Pan"; break;
            case 11: event["controllerName"] = "Expression Controller"; break;
            case 64: event["controllerName"] = "Damper Pedal (Sustain)"; break;
            case 65: event["controllerName"] = "Portamento On/Off"; break;
            case 71: event["controllerName"] = "Resonance"; break;
            case 74: event["controllerName"] = "Brightness"; break;
            case 91: event["controllerName"] = "Reverb Level"; break;
            case 93: event["controllerName"] = "Chorus Level"; break;
            case 120: event["controllerName"] = "All Sound Off"; break;
            case 121: event["controllerName"] = "Reset All Controllers"; break;
            case 123: event["controllerName"] = "All Notes Off"; break;
            }
        }
        break;
        case 0xC0:
            event["programNumber"] = readInt8();
            break;
        case 0xD0:
            event["pressure"] = readInt8();
            break;
        case 0xE0:
        {
            uint8_t lsb = readInt8();
            uint8_t msb = readInt8();
            int16_t pitchBend = ((msb << 7) | lsb) - 8192;
            event["pitchBend"] = pitchBend;
            event["pitchBendNormalized"] = pitchBend / 8192.0;
        }
        break;
        }
    }

    // Handle System Common Messages
    if ((status & 0xF8) == 0xF0 && status != 0xF0 && status != 0xF7) {
        event["type"] = "system";
        switch (status) {
        case 0xF1: // MIDI Time Code Quarter Frame
            event["name"] = "MIDI Time Code Quarter Frame";
            event["data"] = readInt8();
            break;
        case 0xF2: // Song Position Pointer
            event["name"] = "Song Position Pointer";
            event["position"] = (readInt8() << 7) | readInt8();
            break;
        case 0xF3: // Song Select
            event["name"] = "Song Select";
            event["songNumber"] = readInt8();
            break;
        case 0xF6: // Tune Request
            event["name"] = "Tune Request";
            break;
        case 0xF8: // Timing Clock
            event["name"] = "Timing Clock";
            break;
        case 0xFA: // Start
            event["name"] = "Start";
            break;
        case 0xFB: // Continue
            event["name"] = "Continue";
            break;
        case 0xFC: // Stop
            event["name"] = "Stop";
            break;
        case 0xFE: // Active Sensing
            event["name"] = "Active Sensing";
            break;
        case 0xFF: // System Reset
            event["name"] = "System Reset";
            break;
        }
        runningStatus = 0; // Reset running status after system common messages
    }

    return event;
}*/

json MidiReader::parseEvent(uint32_t deltaTime) {
    json event;

    uint8_t status = readInt8();

    if (status < 0x80) {
        // Running status, reuse previous status
        file.seekg(-1, std::ios::cur);
        status = runningStatus;
    }
    else {
        runningStatus = status;
    }

    event["delta"] = deltaTime;

    if (status == 0xFF) {
        // Meta event
        uint8_t type = readInt8();
        uint32_t length = readVarLen();
        std::vector<uint8_t> data = readBytes(length);

        switch (type) {
        case 0x00:
            event["sequenceNumber"] = (data[0] << 8) | data[1];
            break;
        case 0x01:
            event["text"] = safeByteToString(data);
            break;
        case 0x02:
            event["copyrightNotice"] = safeByteToString(data);
            break;
        case 0x03:
            event["trackName"] = safeByteToString(data);
            break;
        case 0x04:
            event["instrumentName"] = safeByteToString(data);
            break;
        case 0x05:
            event["lyric"] = safeByteToString(data);
            break;
        case 0x06:
            event["marker"] = { {"text", safeByteToString(data)} };
            break;
        case 0x07:
            event["cuePoint"] = { {"text", safeByteToString(data)} };
            break;
        case 0x08:
            event["programName"] = safeByteToString(data);
            break;
        case 0x09:
            event["deviceName"] = safeByteToString(data);
            break;
        case 0x20:
            event["midiChannelPrefix"] = data[0];
            break;
        case 0x21:
            event["midiPort"] = data[0];
            break;
        case 0x2F:
            event["endOfTrack"] = true;
            break;
        case 0x51:
            event["setTempo"] = { {"microsecondsPerQuarter", (data[0] << 16) | (data[1] << 8) | data[2]} };
            break;
        case 0x54:
            event["smpteOffset"] = {
                {"hour", data[0]},
                {"minute", data[1]},
                {"second", data[2]},
                {"frame", data[3]},
                {"fractionalFrame", data[4]}
            };
            break;
        case 0x58:
            event["timeSignature"] = {
                {"numerator", data[0]},
                {"denominator", 1 << data[1]},
                {"metronome", data[2]},
                {"thirtyseconds", data[3]}
            };
            break;
        case 0x59:
            event["keySignature"] = {
                {"key", static_cast<int8_t>(data[0])},
                {"scale", data[1] == 0 ? "major" : "minor"}
            };
            break;
        case 0x7F:
            event["sequencerSpecific"] = { {"data", data} };
            break;
        default:
            event["unknownMeta"] = {
                {"type", type},
                {"data", data}
            };
        }
    }
    else if (status == 0xF0 || status == 0xF7) {
        // SysEx event
        uint32_t length = readVarLen();
        std::vector<uint8_t> data = readBytes(length);
        event["sysex"] = {
            {"type", status == 0xF0 ? "normal" : "escaped"},
            {"data", data}
        };
    }
    else {
        // MIDI event
        uint8_t channel = status & 0x0F;
        event["channel"] = channel;

        switch (status & 0xF0) {
        case 0x80:
            event["noteOff"] = {
                {"noteNumber", readInt8()},
                {"velocity", readInt8()}
            };
            break;
        case 0x90: {
            uint8_t noteNumber = readInt8();
            uint8_t velocity = readInt8();
            if (velocity == 0) {
                event["noteOff"] = {
                    {"noteNumber", noteNumber},
                    {"velocity", velocity}
                };
            }
            else {
                event["noteOn"] = {
                    {"noteNumber", noteNumber},
                    {"velocity", velocity}
                };
            }
            break;
        }
        case 0xA0:
            event["polyphonicKeyPressure"] = {
                {"noteNumber", readInt8()},
                {"pressure", readInt8()}
            };
            break;
        case 0xB0:
            event["controlChange"] = {
                {"controllerNumber", readInt8()},
                {"value", readInt8()}
            };
            break;
        case 0xC0:
            event["programChange"] = { {"programNumber", readInt8()} };
            break;
        case 0xD0:
            event["channelPressure"] = { {"pressure", readInt8()} };
            break;
        case 0xE0:
        {
            uint8_t lsb = readInt8();
            uint8_t msb = readInt8();
            int16_t pitchBend = ((msb << 7) | lsb) - 8192;
            event["pitchBend"] = pitchBend;
            event["pitchBendNormalized"] = pitchBend / 8192.0;
        }
        break;
        }
    }

    // Handle System Common Messages
    if ((status & 0xF8) == 0xF0 && status != 0xF0 && status != 0xF7) {
        switch (status) {
        case 0xF1:
            event["midiTimeCodeQuarterFrame"] = { {"data", readInt8()} };
            break;
        case 0xF2: {
            uint8_t lsb = readInt8();
            uint8_t msb = readInt8();
            event["songPositionPointer"] = (msb << 7) | lsb;
            break;
        }
        case 0xF3:
            event["songSelect"] = { {"songNumber", readInt8()} };
            break;
        case 0xF6:
            event["tuneRequest"] = true;
            break;
        case 0xF8:
            event["timingClock"] = true;
            break;
        case 0xFA:
            event["start"] = true;
            break;
        case 0xFB:
            event["continue"] = true;
            break;
        case 0xFC:
            event["stop"] = true;
            break;
        case 0xFE:
            event["activeSensing"] = true;
            break;
        case 0xFF:
            event["systemReset"] = true;
            break;
        }
        runningStatus = 0; // Reset running status after system common messages
    }

    return event;
}

std::string MidiReader::safeByteToString(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    for (uint8_t byte : data) {
        if (byte >= 32 && byte <= 126) {  // Printable ASCII characters
            ss << static_cast<char>(byte);
        }
        else {
            ss << "\\x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(byte);
        }
    }
    return ss.str();
}

std::string MidiReader::getMidiEventName(uint8_t status) {
    switch (status & 0xF0) {
    case 0x80: return "Note Off";
    case 0x90: return "Note On";
    case 0xA0: return "Polyphonic Key Pressure";
    case 0xB0: return "Control Change";
    case 0xC0: return "Program Change";
    case 0xD0: return "Channel Pressure";
    case 0xE0: return "Pitch Bend";
    case 0xF0:
        switch (status) {
        case 0xF0: return "System Exclusive";
        case 0xF1: return "MIDI Time Code Quarter Frame";
        case 0xF2: return "Song Position Pointer";
        case 0xF3: return "Song Select";
        case 0xF6: return "Tune Request";
        case 0xF7: return "End of Exclusive";
        case 0xF8: return "Timing Clock";
        case 0xFA: return "Start";
        case 0xFB: return "Continue";
        case 0xFC: return "Stop";
        case 0xFE: return "Active Sensing";
        case 0xFF: return "System Reset";
        default: return "Unknown System Common Message";
        }
    default: return "Unknown MIDI Event";
    }
}

json midiFileToJson(const std::string& filename) {
    MidiReader reader(filename);
    return reader.parseToJson();
}