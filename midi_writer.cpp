#include "midi_writer.h"

MidiWriter::MidiWriter(const std::string& filename) : outFile(filename, std::ios::binary), debugLog("out.log") {
    if (!outFile) {
        throw std::runtime_error("Unable to open output file: " + filename);
    }
    if (!debugLog) {
        throw std::runtime_error("Unable to open debug log file: out.log");
    }
}

void MidiWriter::writeHeader(uint16_t format, uint16_t numTracks, uint16_t division) {
    writeChars("MThd");
    writeInt32(6);  // Header length
    writeInt16(format);
    writeInt16(numTracks);
    writeInt16(division);
}

void MidiWriter::writeTrack(const std::vector<MidiEvent>& events) {
    writeChars("MTrk");
    std::streampos trackLengthPos = outFile.tellp();
    writeInt32(0);  // Placeholder for track length

    for (const auto& event : events) {
        writeVarLen(static_cast<uint32_t>(event.delta));
        writeEvent(event);
    }

    // Write actual track length
    std::streampos endPos = outFile.tellp();
    outFile.seekp(trackLengthPos);
    writeInt32(static_cast<int32_t>(endPos - trackLengthPos - 4));
    outFile.seekp(endPos);
}

void MidiWriter::writeInt16(uint16_t value) {
    outFile.put(static_cast<char>((value >> 8) & 0xFF));
    outFile.put(static_cast<char>(value & 0xFF));
}

void MidiWriter::writeInt32(uint32_t value) {
    outFile.put(static_cast<char>((value >> 24) & 0xFF));
    outFile.put(static_cast<char>((value >> 16) & 0xFF));
    outFile.put(static_cast<char>((value >> 8) & 0xFF));
    outFile.put(static_cast<char>(value & 0xFF));
}

void MidiWriter::writeChars(const char* str) {
    outFile.write(str, 4);
}

void MidiWriter::writeVarLen(uint32_t value) {
    uint8_t buffer[4];
    int count = 0;

    buffer[count++] = value & 0x7F;
    while (value >>= 7) {
        buffer[count++] = (value & 0x7F) | 0x80;
    }

    while (count--) {
        outFile.put(static_cast<char>(buffer[count]));
    }
}

void MidiWriter::writeEvent(const MidiEvent& event) {
    try {
        std::string debugMessage = "Writing event: " + event.type + " with data: " + event.data.dump();
        debugLog << debugMessage << std::endl;

        if (event.type == "noteOn" || event.type == "noteOff") {
            uint8_t status = (event.type == "noteOn") ? 0x90 : 0x80;
            status |= event.data.value("channel", 0);
            outFile.put(static_cast<char>(status));
            outFile.put(static_cast<char>(event.data.value("noteNumber", 0)));
            outFile.put(static_cast<char>(event.data.value("velocity", 0)));
        }
        else if (event.type == "controlChange") {
            outFile.put(static_cast<char>(0xB0 | event.data.value("channel", 0)));
            outFile.put(static_cast<char>(event.data.value("controlNumber", 0)));
            outFile.put(static_cast<char>(event.data.value("value", 0)));
        }
        else if (event.type == "midiChannelPrefix") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x20));
            outFile.put(static_cast<char>(0x01));
            outFile.put(static_cast<char>(event.data.value("channel", 0)));
        }
        else if (event.type == "marker") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x06));  // Marker event type
            std::string text = event.data.value("text", "");
            writeVarLen(static_cast<uint32_t>(text.length()));
            outFile.write(text.c_str(), text.length());
        }
        else if (event.type == "programChange") {
            outFile.put(static_cast<char>(0xC0 | event.data.value("channel", 0)));
            outFile.put(static_cast<char>(event.data.value("programNumber", 0)));
        }
        else if (event.type == "pitchBend") {
            outFile.put(static_cast<char>(0xE0 | event.data.value("channel", 0)));
            int16_t value = event.data.value("value", 0);
            // Pitch bend range is 14 bits, from 0 to 16383, with 8192 being the center (no bend)
            uint16_t adjustedValue = static_cast<uint16_t>(value + 8192);
            adjustedValue = std::min(std::max(adjustedValue, static_cast<uint16_t>(0)), static_cast<uint16_t>(16383));
            uint8_t lsb = adjustedValue & 0x7F;        // Least significant 7 bits
            uint8_t msb = (adjustedValue >> 7) & 0x7F; // Most significant 7 bits
            outFile.put(static_cast<char>(lsb));
            outFile.put(static_cast<char>(msb));
        }
        else if (event.type == "setTempo") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x51));
            outFile.put(static_cast<char>(0x03));
            uint32_t tempo = event.data.value("microsecondsPerQuarter", 500000);
            outFile.put(static_cast<char>((tempo >> 16) & 0xFF));
            outFile.put(static_cast<char>((tempo >> 8) & 0xFF));
            outFile.put(static_cast<char>(tempo & 0xFF));
        }
        else if (event.type == "timeSignature") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x58));
            outFile.put(static_cast<char>(0x04));
            outFile.put(static_cast<char>(event.data.value("numerator", 4)));
            outFile.put(static_cast<char>(event.data.value("denominator", 4)));
            outFile.put(static_cast<char>(event.data.value("metronome", 24)));
            outFile.put(static_cast<char>(event.data.value("thirtySeconds", 8)));
        }
        else if (event.type == "sequencerSpecificData") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x7F));
            std::vector<uint8_t> data = event.data.value("data", std::vector<uint8_t> {});
            writeVarLen(static_cast<uint32_t>(data.size()));  // Cast to avoid possible loss of data
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        else if (event.type == "midiPort") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x21));
            outFile.put(static_cast<char>(0x01));
            outFile.put(static_cast<char>(event.data.value("port", 0)));
        }
        // idk if this should be here...
        else if (event.type == "sysex") {
            std::vector<uint8_t> data = event.data.value("data", std::vector<uint8_t> {});
            outFile.put(static_cast<char>(0xF0));
            writeVarLen(static_cast<uint32_t>(data.size()));  // Cast to avoid possible loss of data
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
            outFile.put(static_cast<char>(0xF7));
        }
        else if (event.type == "sysex") {
            std::vector<uint8_t> data = event.data.value("data", std::vector<uint8_t> {});
            outFile.put(static_cast<char>(0xF0));
            writeVarLen(static_cast<uint32_t>(data.size()));  // Cast to avoid possible loss of data
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
            outFile.put(static_cast<char>(0xF7));  // End of SysEx
        }
        else if (event.type == "channelPrefix") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x20));
            outFile.put(static_cast<char>(0x01));
            outFile.put(static_cast<char>(event.data.value("channel", 0)));
        }
        else if (event.type == "endOfTrack") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x2F));
            outFile.put(static_cast<char>(0x00));
        }
        else if (event.type == "trackName") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x03));  // Track name event type
            std::string text = event.data.value("text", "");
            writeVarLen(static_cast<uint32_t>(text.length()));  // Cast to avoid possible loss of data
            outFile.write(text.c_str(), text.length());
        }
        else if (event.type == "channelPressure") {
            outFile.put(static_cast<char>(0xD0 | event.data.value("channel", 0)));
            outFile.put(static_cast<char>(event.data.value("pressure", 0)));
        }
        else if (event.type == "metaText") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(event.data.value("subtype", 0x01))); // Default to text event
            std::string text = event.data.value("text", "");
            writeVarLen(static_cast<uint32_t>(text.length()));  // Cast to avoid possible loss of data
            outFile.write(text.c_str(), text.length());
        }
        else if (event.type == "sequencerSpecific") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x7F));
            std::vector<uint8_t> data = event.data.value("data", std::vector<uint8_t> {});
            writeVarLen(static_cast<uint32_t>(data.size()));  // Cast to avoid possible loss of data
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        else if (event.type == "smpteOffset") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x54));
            outFile.put(static_cast<char>(0x05));
            outFile.put(static_cast<char>(event.data.value("hour", 0)));
            outFile.put(static_cast<char>(event.data.value("minute", 0)));
            outFile.put(static_cast<char>(event.data.value("second", 0)));
            outFile.put(static_cast<char>(event.data.value("frame", 0)));
            outFile.put(static_cast<char>(event.data.value("subFrame", 0)));
        }
        else if (event.type == "cuePoint") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x07));
            std::string text = event.data.value("text", "");
            writeVarLen(static_cast<uint32_t>(text.length()));  // Cast to avoid possible loss of data
            outFile.write(text.c_str(), text.length());
        }
        else if (event.type == "deviceName") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x09));
            std::string text = event.data.value("text", "");
            writeVarLen(static_cast<uint32_t>(text.length()));  // Cast to avoid possible loss of data
            outFile.write(text.c_str(), text.length());
        }
        else if (event.type == "channelAftertouch") {
            outFile.put(static_cast<char>(0xA0 | event.data.value("channel", 0)));
            outFile.put(static_cast<char>(event.data.value("noteNumber", 0)));
            outFile.put(static_cast<char>(event.data.value("pressure", 0)));
        }
        else if (event.type == "songPositionPointer") {
            outFile.put(static_cast<char>(0xF2));
            uint16_t position = event.data.value("position", 0);
            outFile.put(static_cast<char>(position & 0x7F));
            outFile.put(static_cast<char>((position >> 7) & 0x7F));
        }
        else if (event.type == "songSelect") {
            outFile.put(static_cast<char>(0xF3));
            outFile.put(static_cast<char>(event.data.value("songNumber", 0)));
        }
        else if (event.type == "tuneRequest") {
            outFile.put(static_cast<char>(0xF6));
        }
        else if (event.type == "timingClock") {
            outFile.put(static_cast<char>(0xF8));
        }
        else if (event.type == "start") {
            outFile.put(static_cast<char>(0xFA));
        }
        else if (event.type == "continue") {
            outFile.put(static_cast<char>(0xFB));
        }
        else if (event.type == "stop") {
            outFile.put(static_cast<char>(0xFC));
        }
        else if (event.type == "activeSensing") {
            outFile.put(static_cast<char>(0xFE));
        }
        else if (event.type == "systemReset") {
            outFile.put(static_cast<char>(0xFF));
        }
        else if (event.type == "polyphonicKeyPressure") {
            outFile.put(static_cast<char>(0xA0 | event.data.value("channel", 0)));
            outFile.put(static_cast<char>(event.data.value("noteNumber", 0)));
            outFile.put(static_cast<char>(event.data.value("pressure", 0)));
        }
        else if (event.type == "keySignature") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x59));
            outFile.put(static_cast<char>(0x02));
            outFile.put(static_cast<char>(event.data.value("key", 0)));

            int scale;
            if (event.data["scale"].is_string()) {
                std::string scaleStr = event.data["scale"];
                scale = (scaleStr == "minor") ? 1 : 0;
            }
            else if (event.data["scale"].is_number()) {
                scale = event.data["scale"].get<int>();
            }
            else {
                scale = 0; // Default to major if scale is neither string nor number
            }
            outFile.put(static_cast<char>(scale));
        }
        else if (event.type == "sequencerSpecific") {
            outFile.put(static_cast<char>(0xFF));
            outFile.put(static_cast<char>(0x7F));
            std::vector<uint8_t> data;
            if (event.data.is_string()) {
                std::string dataStr = event.data.get<std::string>();
                data = std::vector<uint8_t>(dataStr.begin(), dataStr.end());
            }
            else if (event.data.is_array()) {
                data = event.data.get<std::vector<uint8_t>>();
            }
            writeVarLen(static_cast<uint32_t>(data.size()));
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        else {
            std::cerr << "Warning: Unknown MIDI event type: " << event.type << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error writing event: " << e.what() << std::endl;
    }
}