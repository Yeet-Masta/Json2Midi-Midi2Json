#include "midi_writer.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <set>

void PatternManager::addPattern(const std::string& name, const std::vector<MidiEvent>& events) {
    patterns[name] = events;
}

std::vector<MidiEvent> PatternManager::getPattern(const std::string& name, int repetitions) const {
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

void MidiContext::incrementNoteCount(int noteNumber) {
    noteCounts[noteNumber]++;
    addNoteToSequence(noteNumber);  // Add the note to the sequence when incrementing count
}

int MidiContext::getNoteCount(int noteNumber) const {
    auto it = noteCounts.find(noteNumber);
    return it != noteCounts.end() ? it->second : 0;
}

void MidiContext::addNoteToSequence(int noteNumber) {
    noteSequence.push_back(noteNumber);
}

ConditionEvaluator::ConditionEvaluator() {
    conditions["noteCount"] = [](const MidiContext& context, const json& params) {
        int noteNumber = params.value("noteNumber", 0);
        int count = params.value("count", 0);
        return context.getNoteCount(noteNumber) >= count;
        };

    conditions["totalNoteCount"] = [](const MidiContext& context, const json& params) {
        int count = params.value("count", 0);
        int total = std::accumulate(context.noteCounts.begin(), context.noteCounts.end(), 0,
            [](int sum, const std::pair<int, int>& p) { return sum + p.second; });
        return total >= count;
        };

    conditions["noteInRange"] = [](const MidiContext& context, const json& params) {
        int minNote = params.value("minNote", 0);
        int maxNote = params.value("maxNote", 127);
        return std::any_of(context.noteCounts.begin(), context.noteCounts.end(),
            [minNote, maxNote](const std::pair<int, int>& p) {
                return p.first >= minNote && p.first <= maxNote && p.second > 0;
            });
        };

    conditions["noteCountInRange"] = [](const MidiContext& context, const json& params) {
        int minNote = params.value("minNote", 0);
        int maxNote = params.value("maxNote", 127);
        int minCount = params.value("minCount", 1);
        return std::count_if(context.noteCounts.begin(), context.noteCounts.end(),
            [minNote, maxNote, minCount](const std::pair<int, int>& p) {
                return p.first >= minNote && p.first <= maxNote && p.second >= minCount;
            }) > 0;
        };

    conditions["specificNoteSequence"] = [](const MidiContext& context, const json& params) {
        std::vector<int> sequence = params.value("sequence", std::vector<int>());
        return std::equal(sequence.begin(), sequence.end(), context.noteSequence.end() - sequence.size(),
            context.noteSequence.end());
        };

    conditions["noteVariety"] = [](const MidiContext& context, const json& params) {
        int minVariety = params.value("minVariety", 1);
        return context.noteCounts.size() >= static_cast<size_t>(minVariety);
        };

    conditions["intervalBetweenNotes"] = [](const MidiContext& context, const json& params) {
        int interval = params.value("interval", 0);
        if (context.noteSequence.size() < 2) return false;
        return std::abs(context.noteSequence.back() - context.noteSequence[context.noteSequence.size() - 2]) == interval;
        };

    conditions["noteRepetition"] = [](const MidiContext& context, const json& params) {
        int repetitions = params.value("repetitions", 2);
        if (context.noteSequence.size() < static_cast<size_t>(repetitions)) return false;
        int lastNote = context.noteSequence.back();
        return std::all_of(context.noteSequence.end() - repetitions, context.noteSequence.end(),
            [lastNote](int note) { return note == lastNote; });
        };

    conditions["noteProgression"] = [](const MidiContext& context, const json& params) {
        std::string direction = params.value("direction", "ascending");
        int length = params.value("length", 2);
        if (context.noteSequence.size() < static_cast<size_t>(length)) return false;
        auto start = context.noteSequence.end() - length;
        return (direction == "ascending" && std::is_sorted(start, context.noteSequence.end())) ||
            (direction == "descending" && std::is_sorted(start, context.noteSequence.end(), std::greater<>()));
        };

    conditions["chordPresence"] = [](const MidiContext& context, const json& params) {
        std::vector<int> chord = params.value("chord", std::vector<int>());
        return std::all_of(chord.begin(), chord.end(),
            [&context](int note) { return context.noteCounts.find(note) != context.noteCounts.end(); });
        };
    conditions["timeElapsed"] = [](const MidiContext& context, const json& params) {
        int64_t time = params.value("time", 0);
        return context.totalDeltaTime >= time;
        };

    conditions["noteRange"] = [](const MidiContext& context, const json& params) {
        int minNote = params.value("minNote", 0);
        int maxNote = params.value("maxNote", 127);
        auto minmax = std::minmax_element(context.noteCounts.begin(), context.noteCounts.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        return minmax.first->first >= minNote && minmax.second->first <= maxNote;
        };

    conditions["rhythmicPattern"] = [](const MidiContext& context, const json& params) {
        std::vector<int> pattern = params.value("pattern", std::vector<int>());
        if (context.deltaTimeSequence.size() < pattern.size()) return false;
        return std::equal(pattern.begin(), pattern.end(),
            context.deltaTimeSequence.end() - pattern.size(),
            [](int a, int b) { return std::abs(a - b) <= 5; }); // Allow small timing differences
        };

    conditions["polyphony"] = [](const MidiContext& context, const json& params) {
        int minVoices = params.value("minVoices", 1);
        int maxVoices = params.value("maxVoices", 127);
        return context.currentPolyphony >= minVoices && context.currentPolyphony <= maxVoices;
        };

    conditions["velocityRange"] = [](const MidiContext& context, const json& params) {
        int minVelocity = params.value("minVelocity", 0);
        int maxVelocity = params.value("maxVelocity", 127);
        return context.minVelocity >= minVelocity && context.maxVelocity <= maxVelocity;
        };

    conditions["scaleAdherence"] = [](const MidiContext& context, const json& params) {
        std::vector<int> scale = params.value("scale", std::vector<int>{0, 2, 4, 5, 7, 9, 11}); // Default to major scale
        int rootNote = params.value("rootNote", 0);
        std::set<int> scaleSet;
        for (int note : scale) {
            scaleSet.insert((rootNote + note) % 12);
        }
        return std::all_of(context.noteCounts.begin(), context.noteCounts.end(),
            [&scaleSet](const auto& pair) { return scaleSet.count(pair.first % 12) > 0; });
        };

    conditions["uniqueNoteCount"] = [](const MidiContext& context, const json& params) {
        int minUnique = params.value("minUnique", 1);
        int maxUnique = params.value("maxUnique", 127);
        int uniqueCount = context.noteCounts.size();
        return uniqueCount >= minUnique && uniqueCount <= maxUnique;
        };

    conditions["noteRatio"] = [](const MidiContext& context, const json& params) {
        int note1 = params.value("note1", 60);
        int note2 = params.value("note2", 64);
        float ratio = params.value("ratio", 1.0f);
        float epsilon = params.value("epsilon", 0.1f);
        int count1 = context.getNoteCount(note1);
        int count2 = context.getNoteCount(note2);
        if (count2 == 0) return false;
        float actualRatio = static_cast<float>(count1) / count2;
        return std::abs(actualRatio - ratio) <= epsilon;
        };

    conditions["controllerValue"] = [](const MidiContext& context, const json& params) {
        int controller = params.value("controller", 0);
        int minValue = params.value("minValue", 0);
        int maxValue = params.value("maxValue", 127);
        auto it = context.controllerValues.find(controller);
        if (it == context.controllerValues.end()) return false;
        return it->second >= minValue && it->second <= maxValue;
        };
}

bool ConditionEvaluator::evaluate(const std::string& type, const MidiContext& context, const json& params) const {
    auto it = conditions.find(type);
    if (it == conditions.end()) {
        throw std::runtime_error("Unknown condition type: " + type);
    }
    return it->second(context, params);
}