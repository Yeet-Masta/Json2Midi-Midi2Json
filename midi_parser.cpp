#include "midi_writer.h"

std::vector<MidiEvent> parseJsonToEvents(const json& j, PatternManager& patternManager, MidiContext& context, const ConditionEvaluator& evaluator, int loopCount) {
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
                else if (element.contains("midiChannelPrefix")) {
                    event.type = "midiChannelPrefix";
                    event.data["channel"] = element["midiChannelPrefix"];
                }
                else if (element.contains("timeSignature") && element["timeSignature"].is_object()) {
                    const auto& timeSignature = element["timeSignature"];
                    event.type = "timeSignature";
                    event.data["numerator"] = timeSignature.value("numerator", 4);
                    event.data["denominator"] = timeSignature.value("denominator", 4);
                    event.data["metronome"] = timeSignature.value("metronome", 24);
                    event.data["thirtySeconds"] = timeSignature.value("thirtyseconds", 8);
                }
                else if (element.contains("polyphonicKeyPressure") && element["polyphonicKeyPressure"].is_object()) {
                    const auto& polyphonicKeyPressure = element["polyphonicKeyPressure"];
                    event.type = "polyphonicKeyPressure";
                    event.data["noteNumber"] = polyphonicKeyPressure.value("noteNumber", 0);
                    event.data["pressure"] = polyphonicKeyPressure.value("pressure", 0);
                    event.data["channel"] = element.value("channel", 0);
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