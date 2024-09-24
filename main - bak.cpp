#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <functional>

using json = nlohmann::json;

struct MidiEvent {
    int64_t delta = 0;  // Initialize delta to avoid uninitialized variable warning
    std::string type;
    json data;
};

class MidiWriter {
public:
    std::ofstream debugLog;

    MidiWriter(const std::string& filename) : outFile(filename, std::ios::binary), debugLog("out.log") {
        if (!outFile) {
            throw std::runtime_error("Unable to open output file: " + filename);
        }
        if (!debugLog) {
            throw std::runtime_error("Unable to open debug log file: out.log");
        }
    }

    void writeHeader(uint16_t format, uint16_t numTracks, uint16_t division) {
        writeChars("MThd");
        writeInt32(6);  // Header length
        writeInt16(format);
        writeInt16(numTracks);
        writeInt16(division);
    }

    void writeTrack(const std::vector<MidiEvent>& events) {
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

private:
    std::ofstream outFile;

    void writeInt16(uint16_t value) {
        outFile.put(static_cast<char>((value >> 8) & 0xFF));
        outFile.put(static_cast<char>(value & 0xFF));
    }

    void writeInt32(uint32_t value) {
        outFile.put(static_cast<char>((value >> 24) & 0xFF));
        outFile.put(static_cast<char>((value >> 16) & 0xFF));
        outFile.put(static_cast<char>((value >> 8) & 0xFF));
        outFile.put(static_cast<char>(value & 0xFF));
    }

    void writeChars(const char* str) {
        outFile.write(str, 4);
    }

    void writeVarLen(uint32_t value) {
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

    void writeEvent(const MidiEvent& event) {
        try {
            std::string debugMessage = "Writing event: " + event.type + " with data: " + event.data.dump();
            //std::cout << debugMessage << std::endl;
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
                uint8_t lsb = value & 0x7F;  // Least significant 7 bits
                uint8_t msb = (value >> 7) & 0x7F;  // Most significant 7 bits
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
            else if (event.type == "keySignature") {
                outFile.put(static_cast<char>(0xFF));
                outFile.put(static_cast<char>(0x59));
                outFile.put(static_cast<char>(0x02));
                outFile.put(static_cast<char>(event.data.value("key", 0)));
                outFile.put(static_cast<char>(event.data.value("scale", 0)));
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
            else {
                std::cerr << "Warning: Unknown MIDI event type: " << event.type << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error writing event: " << e.what() << std::endl;
        }
    }


};

class PatternManager {
private:
    std::map<std::string, std::vector<MidiEvent>> patterns;

public:
    void addPattern(const std::string& name, const std::vector<MidiEvent>& events) {
        patterns[name] = events;
    }

    std::vector<MidiEvent> getPattern(const std::string& name, int repetitions = 1) const {
        auto it = patterns.find(name);
        if (it == patterns.end()) {
            throw std::runtime_error("Pattern not found: " + name);
        }
        std::vector<MidiEvent> result;
        for (int i = 0; i < repetitions; ++i) {
            result.insert(result.end(), it->second.begin(), it->second.end());
        }
        return result;
    }
};

// New MidiContext class
struct MidiContext {
    std::map<int, int> noteCounts;  // noteNumber -> count

    void incrementNoteCount(int noteNumber) {
        noteCounts[noteNumber]++;
    }

    int getNoteCount(int noteNumber) const {
        auto it = noteCounts.find(noteNumber);
        return it != noteCounts.end() ? it->second : 0;
    }
};

// New ConditionEvaluator class
using ConditionFunction = std::function<bool(const MidiContext&, const json&)>;

class ConditionEvaluator {
private:
    std::map<std::string, ConditionFunction> conditions;

public:
    ConditionEvaluator() {
        conditions["noteCount"] = [](const MidiContext& context, const json& params) {
            int noteNumber = params.value("noteNumber", 0);
            int count = params.value("count", 0);
            return context.getNoteCount(noteNumber) >= count;
            };
        // Add more condition types as needed
    }

    bool evaluate(const std::string& type, const MidiContext& context, const json& params) const {
        auto it = conditions.find(type);
        if (it == conditions.end()) {
            throw std::runtime_error("Unknown condition type: " + type);
        }
        return it->second(context, params);
    }
};

// Revised parseJsonToEvents to handle all MIDI event types
std::vector<MidiEvent> parseJsonToEvents(const json& j, PatternManager& patternManager, MidiContext& context, const ConditionEvaluator& evaluator, int loopCount = 1) {
    std::vector<MidiEvent> events;

    for (const auto& element : j) {
        if (element.contains("definePattern") && element["definePattern"].is_object()) {
            const auto& patternDef = element["definePattern"];
            std::string patternName = patternDef["name"];
            auto patternEvents = parseJsonToEvents(patternDef["events"], patternManager, context, evaluator);
            patternManager.addPattern(patternName, patternEvents);
        }
        else if (element.contains("usePattern") && element["usePattern"].is_object()) {
            const auto& patternUse = element["usePattern"];
            std::string patternName = patternUse["name"];
            int repetitions = patternUse.value("repetitions", 1);
            auto patternEvents = patternManager.getPattern(patternName, repetitions);
            events.insert(events.end(), patternEvents.begin(), patternEvents.end());
        }
        else if (element.contains("conditional") && element["conditional"].is_object()) {
            const auto& conditional = element["conditional"];
            std::string conditionType = conditional["condition"]["type"];
            json conditionParams = conditional["condition"]["parameters"];

            if (evaluator.evaluate(conditionType, context, conditionParams)) {
                auto trueEvents = parseJsonToEvents(conditional["ifTrue"], patternManager, context, evaluator);
                events.insert(events.end(), trueEvents.begin(), trueEvents.end());
            }
            else if (conditional.contains("ifFalse")) {
                auto falseEvents = parseJsonToEvents(conditional["ifFalse"], patternManager, context, evaluator);
                events.insert(events.end(), falseEvents.begin(), falseEvents.end());
            }
        }
        else if (element.contains("loop") && element["loop"].is_object()) {
            const auto& loop = element["loop"];
            int count = loop.value("count", 1);
            const auto& loopEvents = loop["events"];
            for (int i = 0; i < count; ++i) {
                auto loopedEvents = parseJsonToEvents(loopEvents, patternManager, context, evaluator);
                events.insert(events.end(), loopedEvents.begin(), loopedEvents.end());
            }
        }
        else {
            MidiEvent event;
            event.delta = element.value("delta", 0);

            try {
                if (element.contains("noteOn") && element["noteOn"].is_object()) {
                    const auto& noteOn = element["noteOn"];
                    event.type = "noteOn";
                    event.data["noteNumber"] = noteOn.value("noteNumber", 0);
                    event.data["velocity"] = noteOn.value("velocity", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("noteOff") && element["noteOff"].is_object()) {
                    const auto& noteOff = element["noteOff"];
                    event.type = "noteOff";
                    event.data["noteNumber"] = noteOff.value("noteNumber", 0);
                    event.data["velocity"] = noteOff.value("velocity", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("marker") && element["marker"].is_object()) {
                    const auto& marker = element["marker"];
                    event.type = "marker";
                    event.data["text"] = marker.value("text", "");
                }
                else if (element.contains("controlChange") && element["controlChange"].is_object()) {
                    const auto& controlChange = element["controlChange"];
                    event.type = "controlChange";
                    event.data["controlNumber"] = controlChange.value("type", 0);
                    event.data["value"] = controlChange.value("value", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("programChange") && element["programChange"].is_object()) {
                    const auto& programChange = element["programChange"];
                    event.type = "programChange";
                    event.data["programNumber"] = programChange.value("programNumber", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("setTempo") && element["setTempo"].is_object()) {
                    const auto& setTempo = element["setTempo"];
                    event.type = "setTempo";
                    event.data["microsecondsPerQuarter"] = setTempo.value("microsecondsPerQuarter", 500000);
                }
                else if (element.contains("timeSignature") && element["timeSignature"].is_object()) {
                    const auto& timeSignature = element["timeSignature"];
                    event.type = "timeSignature";
                    event.data["numerator"] = timeSignature.value("numerator", 4);
                    event.data["denominator"] = timeSignature.value("denominator", 4);
                    event.data["metronome"] = timeSignature.value("metronome", 24);
                    event.data["thirtySeconds"] = timeSignature.value("thirtyseconds", 8);
                }
                else if (element.contains("keySignature") && element["keySignature"].is_object()) {
                    const auto& keySignature = element["keySignature"];
                    event.type = "keySignature";
                    event.data["key"] = keySignature.value("key", 0);
                    event.data["scale"] = keySignature.value("scale", 0);
                }
                //idk if this should be here...
                else if (element.contains("sysex") && element["sysex"].is_object()) {
                    const auto& sysex = element["sysex"];
                    event.type = "sysex";
                    event.data["data"] = sysex.value("data", std::vector<uint8_t> {});
                }
                else if (element.contains("sysex") && element["sysex"].is_string()) {
                    event.type = "sysex";
                    std::string dataStr = element["sysex"];
                    std::vector<uint8_t> data;
                    for (size_t i = 0; i < dataStr.length(); i += 2) {
                        uint8_t byte = std::stoi(dataStr.substr(i, 2), nullptr, 16);
                        data.push_back(byte);
                    }
                    event.data["data"] = data;
                }
                else if (element.contains("channelPrefix") && element["channelPrefix"].is_number()) {
                    event.type = "channelPrefix";
                    event.data["channel"] = element["channelPrefix"];
                }
                else if (element.contains("midiPort") && element["midiPort"].is_number()) {
                    event.type = "midiPort";
                    event.data["port"] = element["midiPort"];
                }
                else if (element.contains("endOfTrack")) {
                    event.type = "endOfTrack";
                }
                else if (element.contains("pitchBend")) {
                    event.type = "pitchBend";
                    event.data["value"] = element.value("pitchBend", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("pitchBend") && element["pitchBend"].is_object()) {
                    const auto& pitchBend = element["pitchBend"];
                    event.type = "pitchBend";
                    event.data["value"] = pitchBend.value("value", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("trackName")) {
                    event.type = "trackName";
                    event.data["text"] = element.value("trackName", "");
                }
                else if (element.contains("channelPressure") && element["channelPressure"].is_object()) {
                    const auto& channelPressure = element["channelPressure"];
                    event.type = "channelPressure";
                    event.data["pressure"] = channelPressure.value("pressure", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("metaText") && element["metaText"].is_object()) {
                    const auto& metaText = element["metaText"];
                    event.type = "metaText";
                    event.data["subtype"] = metaText.value("subtype", 0x01); // Default to text event
                    event.data["text"] = metaText.value("text", "");
                }
                else if (element.contains("sequencerSpecific") && element["sequencerSpecific"].is_object()) {
                    const auto& sequencerSpecific = element["sequencerSpecific"];
                    event.type = "sequencerSpecific";
                    event.data["data"] = sequencerSpecific.value("data", std::vector<uint8_t> {});
                }
                else if (element.contains("smpteOffset") && element["smpteOffset"].is_object()) {
                    const auto& smpteOffset = element["smpteOffset"];
                    event.type = "smpteOffset";
                    event.data["hour"] = smpteOffset.value("hour", 0);
                    event.data["minute"] = smpteOffset.value("minute", 0);
                    event.data["second"] = smpteOffset.value("second", 0);
                    event.data["frame"] = smpteOffset.value("frame", 0);
                    event.data["subFrame"] = smpteOffset.value("subFrame", 0);
                }
                else if (element.contains("cuePoint") && element["cuePoint"].is_object()) {
                    const auto& cuePoint = element["cuePoint"];
                    event.type = "cuePoint";
                    event.data["text"] = cuePoint.value("text", "");
                }
                else if (element.contains("deviceName") && element["deviceName"].is_object()) {
                    const auto& deviceName = element["deviceName"];
                    event.type = "deviceName";
                    event.data["text"] = deviceName.value("text", "");
                }
                else if (element.contains("channelAftertouch") && element["channelAftertouch"].is_object()) {
                    const auto& channelAftertouch = element["channelAftertouch"];
                    event.type = "channelAftertouch";
                    event.data["noteNumber"] = channelAftertouch.value("noteNumber", 0);
                    event.data["pressure"] = channelAftertouch.value("pressure", 0);
                    event.data["channel"] = element.value("channel", 0);
                }
                else if (element.contains("songPositionPointer")) {
                    event.type = "songPositionPointer";
                    event.data["position"] = element.value("songPositionPointer", 0);
                }
                else if (element.contains("sequencerSpecificData") && element["sequencerSpecificData"].is_string()) {
                    event.type = "sequencerSpecificData";
                    std::string dataStr = element["sequencerSpecificData"];
                    std::vector<uint8_t> data(dataStr.begin(), dataStr.end());
                    event.data["data"] = data;
                }
                else if (element.contains("songSelect")) {
                    event.type = "songSelect";
                    event.data["songNumber"] = element.value("songSelect", 0);
                }
                else if (element.contains("tuneRequest")) {
                    event.type = "tuneRequest";
                }
                else if (element.contains("timingClock")) {
                    event.type = "timingClock";
                }
                else if (element.contains("start")) {
                    event.type = "start";
                }
                else if (element.contains("continue")) {
                    event.type = "continue";
                }
                else if (element.contains("stop")) {
                    event.type = "stop";
                }
                else if (element.contains("activeSensing")) {
                    event.type = "activeSensing";
                }
                else if (element.contains("systemReset")) {
                    event.type = "systemReset";
                }
                else {
                    std::cerr << "Warning: Unknown or unexpected MIDI event type or format. Element: " << element.dump() << std::endl;
                    continue;
                }
                events.push_back(event);
            }
            catch (const json::exception& e) {
                std::cerr << "Error parsing MIDI event: " << e.what() << " Element: " << element.dump() << std::endl;
            }
        }
    }

    // Adjust delta times for loops
    if (loopCount > 1) {
        int64_t totalDelta = 0;
        for (auto& event : events) {
            totalDelta += event.delta;
            event.delta = 0;
        }
        if (!events.empty()) {
            events.front().delta = totalDelta;
        }
    }

    return events;
}

std::vector<std::vector<MidiEvent>> parseJson(const json& j, PatternManager& patternManager, MidiContext& context, const ConditionEvaluator& evaluator) {
    std::vector<std::vector<MidiEvent>> tracks;

    try {
        if (j.contains("tracks") && j["tracks"].is_array()) {
            for (const auto& trackJson : j["tracks"]) {
                tracks.push_back(parseJsonToEvents(trackJson, patternManager, context, evaluator));
            }
        }
        else {
            tracks.push_back(parseJsonToEvents(j, patternManager, context, evaluator));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: Error parsing JSON structure: " << e.what() << std::endl;
        std::cerr << "Attempting to parse as a single track..." << std::endl;
        try {
            tracks.push_back(parseJsonToEvents(j, patternManager, context, evaluator));
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing as single track: " << e.what() << std::endl;
        }
    }

    return tracks;
}

void createMidiFile(const std::vector<std::vector<MidiEvent>>& tracks, uint16_t format, uint16_t division, const std::string& filename) {
    MidiWriter writer(filename);
    writer.writeHeader(format, tracks.size(), division);

    for (const auto& track : tracks) {
        writer.writeTrack(track);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_json> <output_midi>" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];

    try {
        std::ifstream file(inputFile);
        if (!file) {
            throw std::runtime_error("Unable to open input file: " + inputFile);
        }

        json j;
        file >> j;

        uint16_t format = j.value("format", 1);
        uint16_t division = j.value("division", 480);

        PatternManager patternManager;
        MidiContext context;
        ConditionEvaluator evaluator;

        std::vector<std::vector<MidiEvent>> tracks = parseJson(j, patternManager, context, evaluator);

        if (tracks.empty()) {
            std::cerr << "No valid MIDI events found. MIDI file will not be created." << std::endl;
            return 1;
        }

        createMidiFile(tracks, format, division, outputFile);
        std::cout << "MIDI file created successfully." << std::endl;
    }
    catch (const json::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}