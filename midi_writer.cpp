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

void applyDynamicTempoChanges(std::vector<MidiEvent>& events, const std::vector<TempoChange>& tempoChanges) {
    if (tempoChanges.empty()) return;

    std::vector<MidiEvent> newEvents;
    size_t tempoIndex = 0;
    uint32_t currentDeltaTime = 0;

    for (const auto& event : events) {
        currentDeltaTime += event.delta;

        while (tempoIndex < tempoChanges.size() && tempoChanges[tempoIndex].deltaTime <= currentDeltaTime) {
            MidiEvent tempoEvent;
            tempoEvent.type = "setTempo";
            tempoEvent.data["microsecondsPerQuarter"] = tempoChanges[tempoIndex].microsecondsPerQuarter;
            tempoEvent.delta = tempoChanges[tempoIndex].deltaTime - (currentDeltaTime - event.delta);

            newEvents.push_back(tempoEvent);
            tempoIndex++;
        }

        newEvents.push_back(event);
    }

    events = std::move(newEvents);
}

void applyVelocityCurve(std::vector<MidiEvent>& events, VelocityCurve& curve) {
    for (auto& event : events) {
        if (event.type == "noteOn" && event.data.contains("velocity")) {
            event.data["velocity"] = curve.getNextVelocity();
        }
    }
}

void applyControlledRandomization(std::vector<MidiEvent>& events, MidiContext& context, int velocityRange, int timingRange) {
    std::uniform_int_distribution<> velocityDist(-velocityRange, velocityRange);
    std::uniform_int_distribution<> timingDist(-timingRange, timingRange);

    for (auto& event : events) {
        if (event.type == "noteOn" && event.data.contains("velocity")) {
            int newVelocity = event.data["velocity"].get<int>() + velocityDist(context.randomEngine);
            event.data["velocity"] = std::clamp(newVelocity, 1, 127);
        }

        int64_t newDelta = static_cast<int64_t>(event.delta) + timingDist(context.randomEngine);
        event.delta = std::max(static_cast<int64_t>(0), newDelta);
    }
}

void applyArticulationPattern(std::vector<MidiEvent>& events, ArticulationPattern& pattern) {
    for (size_t i = 0; i < events.size(); ++i) {
        if (events[i].type == "noteOn" && i + 1 < events.size() && events[i + 1].type == "noteOff") {
            auto [durationMult, velocityMult] = pattern.getNextArticulation();

            // Adjust note velocity
            if (events[i].data.contains("velocity")) {
                int newVelocity = static_cast<int>(events[i].data["velocity"].get<int>() * velocityMult);
                events[i].data["velocity"] = std::clamp(newVelocity, 1, 127);
            }

            // Adjust note duration
            int64_t noteDuration = events[i + 1].delta;
            int64_t newDuration = static_cast<int64_t>(noteDuration * durationMult);
            events[i + 1].delta = newDuration;
        }
    }
}

std::vector<MidiEvent> expandChordProgression(const ChordProgression& progression, bool arpeggiateChords) {
    std::vector<MidiEvent> events;
    uint32_t currentTick = 0;

    for (const auto& chord : progression.chords) {
        if (arpeggiateChords) {
            // Implement arpeggio
            int arpDuration = chord.duration / chord.notes.size();
            for (size_t i = 0; i < chord.notes.size(); ++i) {
                int note = progression.rootNote + chord.notes[i];
                MidiEvent noteOn;
                noteOn.type = "noteOn";
                noteOn.data["noteNumber"] = note;
                noteOn.data["velocity"] = 100;
                noteOn.delta = currentTick + i * arpDuration - (events.empty() ? 0 : events.back().delta);
                events.push_back(noteOn);

                MidiEvent noteOff;
                noteOff.type = "noteOff";
                noteOff.data["noteNumber"] = note;
                noteOff.data["velocity"] = 0;
                noteOff.delta = currentTick + (i + 1) * arpDuration - events.back().delta;
                events.push_back(noteOff);
            }
        }
        else {
            // Play chord as block
            for (int note : chord.notes) {
                MidiEvent noteOn;
                noteOn.type = "noteOn";
                noteOn.data["noteNumber"] = progression.rootNote + note;
                noteOn.data["velocity"] = 100;
                noteOn.delta = currentTick - (events.empty() ? 0 : events.back().delta);
                events.push_back(noteOn);
            }

            for (int note : chord.notes) {
                MidiEvent noteOff;
                noteOff.type = "noteOff";
                noteOff.data["noteNumber"] = progression.rootNote + note;
                noteOff.data["velocity"] = 0;
                noteOff.delta = currentTick + chord.duration - events.back().delta;
                events.push_back(noteOff);
            }
        }

        currentTick += chord.duration;
    }

    return events;
}

void applyGrooveTemplate(std::vector<MidiEvent>& events, GrooveTemplate& groove) {
    for (auto& event : events) {
        if (event.type == "noteOn" || event.type == "noteOff") {
            auto [timingOffset, velocityOffset] = groove.getNextGrooveStep();

            // Apply timing offset
            event.delta = std::max(static_cast<int64_t>(0), event.delta + timingOffset);

            // Apply velocity offset for note-on events
            if (event.type == "noteOn" && event.data.contains("velocity")) {
                int newVelocity = event.data["velocity"].get<int>() + velocityOffset;
                event.data["velocity"] = std::clamp(newVelocity, 1, 127);
            }
        }
    }
}

std::vector<MidiEvent> generateScaleBasedMelody(const Scale& scale, int length, int minNote, int maxNote) {
    std::vector<MidiEvent> melody;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> noteDist(minNote, maxNote);
    std::uniform_int_distribution<> durationDist(120, 480); // Quarter to whole note

    uint32_t currentTime = 0;
    for (int i = 0; i < length; ++i) {
        int note = scale.getNearestScaleNote(noteDist(gen));
        int duration = durationDist(gen);

        MidiEvent noteOn;
        noteOn.type = "noteOn";
        noteOn.data["noteNumber"] = note;
        noteOn.data["velocity"] = 100; // Can be randomized for more variety
        noteOn.delta = currentTime;
        melody.push_back(noteOn);

        MidiEvent noteOff;
        noteOff.type = "noteOff";
        noteOff.data["noteNumber"] = note;
        noteOff.data["velocity"] = 0;
        noteOff.delta = duration;
        melody.push_back(noteOff);

        currentTime += duration;
    }

    return melody;
}

void applyParameterAutomation(std::vector<MidiEvent>& events, const std::vector<ParameterAutomation>& automations) {
    uint32_t currentTime = 0;
    for (const auto& automation : automations) {
        std::vector<MidiEvent> automationEvents;
        int lastValue = -1;
        for (const auto& event : events) {
            currentTime += event.delta;
            int currentValue = automation.getValueAtTime(currentTime);
            if (currentValue != lastValue) {
                MidiEvent ccEvent;
                ccEvent.type = "controlChange";
                ccEvent.data["controlNumber"] = automation.controllerNumber;
                ccEvent.data["value"] = currentValue;
                ccEvent.delta = currentTime;
                automationEvents.push_back(ccEvent);
                lastValue = currentValue;
            }
        }
        events.insert(events.end(), automationEvents.begin(), automationEvents.end());
    }
    std::sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.delta < b.delta;
        });
}

std::vector<MidiEvent> generateAdaptiveHarmony(const std::vector<MidiEvent>& melody, const Scale& scale, int harmonizationInterval) {
    std::vector<MidiEvent> harmony;
    for (const auto& event : melody) {
        if (event.type == "noteOn") {
            int melodyNote = event.data["noteNumber"].get<int>();
            int harmonyNote = scale.getNearestScaleNote(melodyNote + harmonizationInterval);

            MidiEvent harmonyNoteOn = event;
            harmonyNoteOn.data["noteNumber"] = harmonyNote;
            harmony.push_back(harmonyNoteOn);
        }
        else if (event.type == "noteOff") {
            int melodyNote = event.data["noteNumber"].get<int>();
            int harmonyNote = scale.getNearestScaleNote(melodyNote + harmonizationInterval);

            MidiEvent harmonyNoteOff = event;
            harmonyNoteOff.data["noteNumber"] = harmonyNote;
            harmony.push_back(harmonyNoteOff);
        }
    }
    return harmony;
}

std::vector<MidiEvent> generatePolyrhythm(const Polyrhythm& poly, int measures) {
    std::vector<MidiEvent> result;
    int lcm = 1;
    for (int rhythm : poly.rhythms) {
        lcm = std::lcm(lcm, rhythm);
    }

    for (int measure = 0; measure < measures; ++measure) {
        for (size_t i = 0; i < poly.rhythms.size(); ++i) {
            int repetitions = lcm / poly.rhythms[i];
            for (int rep = 0; rep < repetitions; ++rep) {
                for (const auto& event : poly.patterns[i]) {
                    MidiEvent newEvent = event;
                    newEvent.delta += measure * lcm * 480 + rep * poly.rhythms[i] * 480 / repetitions;
                    result.push_back(newEvent);
                }
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.delta < b.delta;
        });

    return result;
}

std::vector<MidiEvent> generatePhraseWithVariation(const PhraseWithVariation& phrase) {
    std::vector<MidiEvent> result;
    for (int i = 0; i < phrase.repetitions; ++i) {
        std::vector<MidiEvent> variation = (i == 0) ? phrase.basePhrase : phrase.variationFunction(phrase.basePhrase);
        result.insert(result.end(), variation.begin(), variation.end());
    }
    return result;
}

std::vector<MidiEvent> applyArpeggiator(const std::vector<MidiEvent>& chordEvents, const Arpeggiator& arp) {
    std::vector<MidiEvent> result;
    std::vector<int> notes;

    // Extract notes from chord events
    for (const auto& event : chordEvents) {
        if (event.type == "noteOn") {
            notes.push_back(event.data["noteNumber"].get<int>());
        }
    }

    if (notes.empty()) return result;

    // Sort notes
    std::sort(notes.begin(), notes.end());

    // Generate arpeggiated notes
    int totalDuration = chordEvents.back().delta - chordEvents.front().delta;
    int numNotes = totalDuration / arp.noteDuration;

    for (int i = 0; i < numNotes; ++i) {
        int noteIndex;
        switch (arp.mode) {
        case Arpeggiator::Mode::Up:
            noteIndex = i % notes.size();
            break;
        case Arpeggiator::Mode::Down:
            noteIndex = (notes.size() - 1) - (i % notes.size());
            break;
        case Arpeggiator::Mode::UpDown:
            noteIndex = i % (2 * notes.size() - 2);
            if (noteIndex >= notes.size()) {
                noteIndex = 2 * (notes.size() - 1) - noteIndex;
            }
            break;
        case Arpeggiator::Mode::Random:
            noteIndex = std::rand() % notes.size();
            break;
        }

        int note = notes[noteIndex] + (i / notes.size()) * 12 * arp.octaveRange;

        MidiEvent noteOn;
        noteOn.type = "noteOn";
        noteOn.data["noteNumber"] = note;
        noteOn.data["velocity"] = 100;
        noteOn.delta = i * arp.noteDuration;
        result.push_back(noteOn);

        MidiEvent noteOff;
        noteOff.type = "noteOff";
        noteOff.data["noteNumber"] = note;
        noteOff.data["velocity"] = 0;
        noteOff.delta = (i + 1) * arp.noteDuration;
        result.push_back(noteOff);
    }

    return result;
}

void applyTempoMap(std::vector<MidiEvent>& events, const TempoMap& tempoMap) {
    std::vector<MidiEvent> newEvents;
    uint32_t currentTick = 0;
    uint32_t currentTempo = 500000; // Default to 120 BPM

    for (const auto& event : events) {
        currentTick += event.delta;
        uint32_t newTempo = tempoMap.getTempoAtTick(currentTick);

        if (newTempo != currentTempo) {
            MidiEvent tempoEvent;
            tempoEvent.type = "setTempo";
            tempoEvent.data["microsecondsPerQuarter"] = newTempo;
            tempoEvent.delta = currentTick - (newEvents.empty() ? 0 : newEvents.back().delta);
            newEvents.push_back(tempoEvent);
            currentTempo = newTempo;
        }

        newEvents.push_back(event);
    }

    events = std::move(newEvents);
}

void applyRandomization(std::vector<MidiEvent>& events, const RandomizationParams& params, std::mt19937& gen) {
    std::uniform_int_distribution<> velocityDist(-params.velocityRange, params.velocityRange);
    std::uniform_int_distribution<> timingDist(-params.timingRange, params.timingRange);
    std::uniform_int_distribution<> pitchDist(-params.pitchRange, params.pitchRange);
    std::uniform_real_distribution<> probDist(0.0, 1.0);

    for (auto it = events.begin(); it != events.end(); ) {
        if (it->type == "noteOn" || it->type == "noteOff") {
            if (probDist(gen) > params.noteProbability) {
                // Remove both noteOn and noteOff events
                auto noteNumber = it->data["noteNumber"].get<int>();
                it = events.erase(it);
                it = std::find_if(it, events.end(), [noteNumber](const MidiEvent& e) {
                    return e.type == "noteOff" && e.data["noteNumber"].get<int>() == noteNumber;
                    });
                if (it != events.end()) it = events.erase(it);
                continue;
            }

            if (it->type == "noteOn") {
                it->data["velocity"] = std::clamp(it->data["velocity"].get<int>() + velocityDist(gen), 1, 127);
                it->data["noteNumber"] = std::clamp(it->data["noteNumber"].get<int>() + pitchDist(gen), 0, 127);
            }
        }

        int64_t newDelta = static_cast<int64_t>(it->delta) + timingDist(gen);
        it->delta = std::max(static_cast<int64_t>(0), newDelta);
        ++it;
    }
}

std::vector<MidiEvent> applyAdaptiveHarmonization(const std::vector<MidiEvent>& melody, const HarmonizationRule& rule, int rootNote) {
    std::vector<MidiEvent> harmony;
    std::vector<int> currentChord;
    int melodyPosition = 0;

    for (const auto& event : melody) {
        if (event.type == "noteOn") {
            int melodyNote = event.data["noteNumber"].get<int>();
            int scaleDegree = (melodyNote - rootNote + 120) % 12;
            auto it = std::find(rule.scaleIntervals.begin(), rule.scaleIntervals.end(), scaleDegree);
            if (it != rule.scaleIntervals.end()) {
                int index = std::distance(rule.scaleIntervals.begin(), it);
                currentChord = rule.harmonizationIntervals[index];
                for (int interval : currentChord) {
                    MidiEvent harmonyNote = event;
                    harmonyNote.data["noteNumber"] = melodyNote + interval;
                    harmony.push_back(harmonyNote);
                }
            }
            melodyPosition++;
        }
        else if (event.type == "noteOff") {
            int melodyNote = event.data["noteNumber"].get<int>();
            for (int interval : currentChord) {
                MidiEvent harmonyNoteOff = event;
                harmonyNoteOff.data["noteNumber"] = melodyNote + interval;
                harmony.push_back(harmonyNoteOff);
            }
        }
    }

    return harmony;
}

std::vector<MidiEvent> applyMidiEffect(const std::vector<MidiEvent>& events, const MidiEffect& effect) {
    std::vector<MidiEvent> result = events;

    switch (effect.type) {
    case MidiEffect::Type::Echo: {
        int delay = effect.parameters.value("delay", 240);
        int repetitions = effect.parameters.value("repetitions", 3);
        float decay = effect.parameters.value("decay", 0.7f);

        std::vector<MidiEvent> echoEvents;
        for (int i = 1; i <= repetitions; ++i) {
            for (const auto& event : events) {
                if (event.type == "noteOn" || event.type == "noteOff") {
                    MidiEvent echoEvent = event;
                    echoEvent.delta += delay * i;
                    if (event.type == "noteOn") {
                        echoEvent.data["velocity"] = static_cast<int>(event.data["velocity"].get<int>() * std::pow(decay, i));
                    }
                    echoEvents.push_back(echoEvent);
                }
            }
        }
        result.insert(result.end(), echoEvents.begin(), echoEvents.end());
        std::sort(result.begin(), result.end(), [](const MidiEvent& a, const MidiEvent& b) {
            return a.delta < b.delta;
            });
        break;
    }
    case MidiEffect::Type::Chord_Splitter: {
        int splitInterval = effect.parameters.value("interval", 50);
        std::vector<MidiEvent> splitEvents;
        for (const auto& event : events) {
            if (event.type == "noteOn") {
                MidiEvent splitEvent = event;
                splitEvent.delta += splitInterval;
                splitEvents.push_back(splitEvent);
            }
            else if (event.type == "noteOff") {
                MidiEvent splitEvent = event;
                splitEvent.delta += splitInterval;
                splitEvents.push_back(splitEvent);
            }
        }
        result.insert(result.end(), splitEvents.begin(), splitEvents.end());
        std::sort(result.begin(), result.end(), [](const MidiEvent& a, const MidiEvent& b) {
            return a.delta < b.delta;
            });
        break;
    }
    }

    return result;
}

void applyEventProbabilities(std::vector<MidiEvent>& events, const std::map<std::string, EventProbability>& probabilities, std::mt19937& gen) {
    std::uniform_real_distribution<> dist(0.0, 1.0);

    for (auto& event : events) {
        auto it = probabilities.find(event.type);
        if (it != probabilities.end()) {
            const auto& prob = it->second;
            if (dist(gen) < prob.probability) {
                for (const auto& [key, value] : prob.modification.items()) {
                    event.data[key] = value;
                }
            }
        }
    }
}

void applyConditionalTrackMuting(std::vector<std::vector<MidiEvent>>& tracks, const std::map<std::string, bool>& muteStatus) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        std::string trackName = "Track" + std::to_string(i + 1);
        auto it = muteStatus.find(trackName);
        if (it != muteStatus.end() && it->second) {
            // Mute the track by replacing note on/off events with silent events
            for (auto& event : tracks[i]) {
                if (event.type == "noteOn") {
                    event.type = "silentNoteOn";
                    event.data["velocity"] = 0;
                }
                else if (event.type == "noteOff") {
                    event.type = "silentNoteOff";
                }
            }
        }
    }
}