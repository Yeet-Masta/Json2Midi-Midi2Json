#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class MidiReader {
public:
    MidiReader(const std::string& filename);
    json parseToJson();
    uint16_t ticksPerQuarterNote;
    double currentTempo;

private:
    std::ifstream file;
    uint8_t runningStatus;

    uint32_t readInt32();
    uint16_t readInt16();
    uint8_t readInt8();
    uint32_t readVarLen();
    std::vector<uint8_t> readBytes(int length);

    json parseHeader();
    json parseTrack();
    json parseEvent(uint32_t deltaTime);
    std::string safeByteToString(const std::vector<uint8_t>& data);
    std::string getMidiEventName(uint8_t status);
};

json midiFileToJson(const std::string& filename);
json analyzeTempo(const json& midiData);