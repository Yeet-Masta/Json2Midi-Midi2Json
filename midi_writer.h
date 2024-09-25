#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <cstdint>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <functional>
#include <random>

using json = nlohmann::json;

struct MidiEvent {
    int64_t delta = 0;
    std::string type;
    json data;

    // Add equality operator
    bool operator==(const MidiEvent& other) const {
        return delta == other.delta && type == other.type && data == other.data;
    }
};

struct TempoChange {
    uint32_t deltaTime;
    uint32_t microsecondsPerQuarter;
};

struct VelocityCurve {
    std::vector<uint8_t> velocities;
    size_t currentIndex = 0;

    uint8_t getNextVelocity() {
        uint8_t velocity = velocities[currentIndex];
        currentIndex = (currentIndex + 1) % velocities.size();
        return velocity;
    }
};  

struct GrooveTemplate {
    std::vector<std::pair<int, int>> timingAndVelocityOffsets; // <timing offset, velocity offset>
    size_t currentIndex = 0;

    std::pair<int, int> getNextGrooveStep() {
        auto step = timingAndVelocityOffsets[currentIndex];
        currentIndex = (currentIndex + 1) % timingAndVelocityOffsets.size();
        return step;
    }
};

struct ArticulationPattern {
    std::vector<std::pair<float, float>> noteLengthMultipliers; // <duration multiplier, velocity multiplier>
    size_t currentIndex = 0;

    std::pair<float, float> getNextArticulation() {
        auto articulation = noteLengthMultipliers[currentIndex];
        currentIndex = (currentIndex + 1) % noteLengthMultipliers.size();
        return articulation;
    }
};

struct Scale {
    std::vector<int> intervals;
    int rootNote;

    bool isNoteInScale(int note) const {
        int normalizedNote = (note - rootNote + 120) % 12;
        return std::find(intervals.begin(), intervals.end(), normalizedNote) != intervals.end();
    }

    int getNearestScaleNote(int note) const {
        if (isNoteInScale(note)) return note;

        int lower = note;
        int upper = note;
        while (true) {
            lower--;
            if (isNoteInScale(lower)) return lower;
            upper++;
            if (isNoteInScale(upper)) return upper;
        }
    }
};

struct ParameterAutomation {
    std::vector<std::pair<uint32_t, int>> points; // <delta time, value>
    int controllerNumber;

    int getValueAtTime(uint32_t time) const {
        auto it = std::lower_bound(points.begin(), points.end(), time,
            [](const auto& point, uint32_t t) { return point.first < t; });

        if (it == points.begin()) return it->second;
        if (it == points.end()) return (it - 1)->second;

        auto prev = it - 1;
        float t = static_cast<float>(time - prev->first) / (it->first - prev->first);
        return static_cast<int>(std::lerp(static_cast<float>(prev->second), static_cast<float>(it->second), t));
    }
};

struct Polyrhythm {
    std::vector<int> rhythms; // Lengths of each rhythm in the polyrhythm
    std::vector<std::vector<MidiEvent>> patterns; // MIDI patterns for each rhythm
};

struct PhraseWithVariation {
    std::vector<MidiEvent> basePhrase;
    std::function<std::vector<MidiEvent>(const std::vector<MidiEvent>&)> variationFunction;
    int repetitions;
};

struct Arpeggiator {
    enum class Mode { Up, Down, UpDown, Random };
    Mode mode;
    int octaveRange;
    int noteDuration;
};

struct TempoMap {
    struct TempoPoint {
        uint32_t tick;
        uint32_t microsecondsPerQuarter;
    };
    std::vector<TempoPoint> points;

    uint32_t getTempoAtTick(uint32_t tick) const {
        auto it = std::lower_bound(points.begin(), points.end(), tick,
            [](const TempoPoint& point, uint32_t t) { return point.tick < t; });
        return it != points.end() ? it->microsecondsPerQuarter : 500000; // Default to 120 BPM
    }
};

struct RandomizationParams {
    int velocityRange = 10;
    int timingRange = 5;
    int pitchRange = 2;
    float noteProbability = 1.0f;
};

struct ChordProgression {
    struct Chord {
        std::vector<int> notes; // Relative to root
        int duration; // In ticks
    };
    std::vector<Chord> chords;
    int rootNote;
};

struct HarmonizationRule {
    std::vector<int> scaleIntervals;
    std::vector<std::vector<int>> harmonizationIntervals;
};

struct MidiEffect {
    enum class Type { Echo, Chord_Splitter };
    Type type;
    json parameters;
};

struct EventProbability {
    float probability;
    json modification;
};

class MidiWriter {

public:

    MidiWriter(const std::string& filename);

    void writeHeader(uint16_t format, uint16_t numTracks, uint16_t division);

    void writeTrack(const std::vector<MidiEvent>& events);

    

private:

    std::ofstream outFile;

    std::ofstream debugLog;



    void writeInt16(uint16_t value);

    void writeInt32(uint32_t value);

    void writeChars(const char* str);

    void writeVarLen(uint32_t value);

    void writeEvent(const MidiEvent& event);

};

class PatternManager {

public:

    void addPattern(const std::string& name, const std::vector<MidiEvent>& events);

    std::vector<MidiEvent> getPattern(const std::string& name, int repetitions = 1) const;



private:

    std::map<std::string, std::vector<MidiEvent>> patterns;

};

struct MidiContext {

    std::map<int, int> noteCounts;

    std::vector<int> noteSequence;  // New member to store the sequence of notes



    int64_t totalDeltaTime = 0;

    std::vector<int> deltaTimeSequence;

    int currentPolyphony = 0;

    int minVelocity = 127;

    int maxVelocity = 0;

    std::map<int, int> controllerValues;



    void incrementNoteCount(int noteNumber);

    int getNoteCount(int noteNumber) const;

    void addNoteToSequence(int noteNumber);  // New function to add a note to the sequence

    std::vector<TempoChange> tempoChanges;
    std::map<std::string, VelocityCurve> velocityCurves;
    std::default_random_engine randomEngine;
    std::map<std::string, ArticulationPattern> articulationPatterns{
        {"legato", {.noteLengthMultipliers = {{1.0f, 0.9f}}}},
        {"staccato", {.noteLengthMultipliers = {{0.5f, 1.1f}}}},
        {"punchyBass", {.noteLengthMultipliers = {{0.8f, 1.2f}, {0.6f, 1.1f}, {0.7f, 1.15f}, {0.5f, 1.25f}}}},
        {"bouncy", {.noteLengthMultipliers = {{0.7f, 1.1f}, {0.5f, 1.2f}, {0.6f, 1.15f}, {0.4f, 1.25f}}}},
        {"smoothJazz", {.noteLengthMultipliers = {{0.95f, 0.9f}, {1.0f, 0.85f}, {0.9f, 0.95f}}}},
        {"aggressiveGuitar", {.noteLengthMultipliers = {{0.6f, 1.3f}, {0.5f, 1.4f}, {0.55f, 1.35f}, {0.45f, 1.45f}}}}
    };
    std::map<std::string, ChordProgression> chordProgressions;
    std::map<std::string, GrooveTemplate> grooveTemplates{
        {"standard", {.timingAndVelocityOffsets = {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}},
        {"swingyRock", {.timingAndVelocityOffsets = {{0, 10}, {20, -5}, {-10, 5}, {15, -10}}}},
        {"funkySixteenth", {.timingAndVelocityOffsets = {{-5, 5}, {10, -10}, {0, 15}, {5, -5}}}},
        {"shuffleFeel", {.timingAndVelocityOffsets = {{0, 10}, {30, -5}, {0, 5}, {20, -10}}}},
        {"bossaNova", {.timingAndVelocityOffsets = {{0, 5}, {-10, -5}, {5, 10}, {-5, -5}}}},
        {"hiphopPocket", {.timingAndVelocityOffsets = {{5, 10}, {-5, -5}, {10, 5}, {-10, -10}}}}
    };
    std::map<std::string, Scale> scales;
    std::map<std::string, bool> trackMuteStatus;
    std::vector<ParameterAutomation> parameterAutomations;
    TempoMap tempoMap;
    RandomizationParams randomization;
    std::map<std::string, HarmonizationRule> harmonizationRules;
    std::vector<MidiEffect> midiEffects;
    std::map<std::string, EventProbability> eventProbabilities;
    std::map<std::string, Polyrhythm> polyrhythms;
    std::map<std::string, PhraseWithVariation> phrasesWithVariation;
    std::map<std::string, Arpeggiator> arpeggiators;
};

using ConditionFunction = std::function<bool(const MidiContext&, const json&)>;

class ConditionEvaluator {

public:

    ConditionEvaluator();

    bool evaluate(const std::string& type, const MidiContext& context, const json& params) const;



private:

    std::map<std::string, ConditionFunction> conditions;

};

std::vector<MidiEvent> parseJsonToEvents(const json& j, PatternManager& patternManager, MidiContext& context, const ConditionEvaluator& evaluator, int loopCount = 1);

std::vector<std::vector<MidiEvent>> parseJson(const json& j, PatternManager& patternManager, MidiContext& context, const ConditionEvaluator& evaluator);

void createMidiFile(const std::vector<std::vector<MidiEvent>>& tracks, uint16_t format, uint16_t division, const std::string& filename);

void applyVelocityCurve(std::vector<MidiEvent>& events, VelocityCurve& curve);

void applyControlledRandomization(std::vector<MidiEvent>& events, MidiContext& context, int velocityRange, int timingRange);

void applyArticulationPattern(std::vector<MidiEvent>& events, ArticulationPattern& pattern);

//std::vector<MidiEvent> expandChordProgression(const ChordProgression& progression, int rootNote, int octaveRange, int noteDuration);

std::vector<MidiEvent> expandChordProgression(const ChordProgression& progression, bool arpeggiateChords = false);

void applyGrooveTemplate(std::vector<MidiEvent>& events, GrooveTemplate& groove);

std::vector<MidiEvent> generateScaleBasedMelody(const Scale& scale, int length, int minNote, int maxNote);

void applyParameterAutomation(std::vector<MidiEvent>& events, const std::vector<ParameterAutomation>& automations);

std::vector<MidiEvent> generateAdaptiveHarmony(const std::vector<MidiEvent>& melody, const Scale& scale, int harmonizationInterval);

std::vector<MidiEvent> generatePolyrhythm(const Polyrhythm& poly, int measures);

std::vector<MidiEvent> generatePhraseWithVariation(const PhraseWithVariation& phrase);

std::vector<MidiEvent> applyArpeggiator(const std::vector<MidiEvent>& chordEvents, const Arpeggiator& arp);

void applyTempoMap(std::vector<MidiEvent>& events, const TempoMap& tempoMap);

void applyRandomization(std::vector<MidiEvent>& events, const RandomizationParams& params, std::mt19937& gen);

std::vector<MidiEvent> applyAdaptiveHarmonization(const std::vector<MidiEvent>& melody, const HarmonizationRule& rule, int rootNote);

std::vector<MidiEvent> applyMidiEffect(const std::vector<MidiEvent>& events, const MidiEffect& effect);

void applyEventProbabilities(std::vector<MidiEvent>& events, const std::map<std::string, EventProbability>& probabilities, std::mt19937& gen);

void applyConditionalTrackMuting(std::vector<std::vector<MidiEvent>>& tracks, const std::map<std::string, bool>& muteStatus);