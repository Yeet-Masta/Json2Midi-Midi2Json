#pragma once



#include <iostream>

#include <fstream>

#include <vector>

#include <map>

#include <cstdint>

#include <algorithm>

#include <nlohmann/json.hpp>

#include <functional>



using json = nlohmann::json;



struct MidiEvent {

    int64_t delta = 0;

    std::string type;

    json data;

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