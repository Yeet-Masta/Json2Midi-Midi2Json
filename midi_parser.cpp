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
        else if (element.contains("articulationPattern") && element["articulationPattern"].is_object()) {
            std::string patternName = element["articulationPattern"].value("name", "default");
            std::vector<std::pair<float, float>> pattern;
            for (const auto& step : element["articulationPattern"]["pattern"]) {
                pattern.emplace_back(step[0].get<float>(), step[1].get<float>());
            }
            context.articulationPatterns[patternName] = ArticulationPattern{ pattern };
        }
        else if (element.contains("definePolyrhythm") && element["definePolyrhythm"].is_object()) {
            std::string name = element["definePolyrhythm"].value("name", "default");
            Polyrhythm poly;
            poly.rhythms = element["definePolyrhythm"]["rhythms"].get<std::vector<int>>();
            for (const auto& pattern : element["definePolyrhythm"]["patterns"]) {
                poly.patterns.push_back(parseJsonToEvents(pattern, patternManager, context, evaluator));
            }
            context.polyrhythms[name] = poly;
        }
        else if (element.contains("generatePolyrhythm") && element["generatePolyrhythm"].is_object()) {
            std::string name = element["generatePolyrhythm"].value("name", "default");
            int measures = element["generatePolyrhythm"].value("measures", 1);

            auto it = context.polyrhythms.find(name);
            if (it != context.polyrhythms.end()) {
                auto polyEvents = generatePolyrhythm(it->second, measures);
                events.insert(events.end(), polyEvents.begin(), polyEvents.end());
            }
            else {
                std::cerr << "Warning: Polyrhythm '" << name << "' not found." << std::endl;
            }
        }
        else if (element.contains("definePhraseWithVariation") && element["definePhraseWithVariation"].is_object()) {
            std::string name = element["definePhraseWithVariation"].value("name", "default");
            PhraseWithVariation phrase;
            phrase.basePhrase = parseJsonToEvents(element["definePhraseWithVariation"]["basePhrase"], patternManager, context, evaluator);
            phrase.repetitions = element["definePhraseWithVariation"].value("repetitions", 4);

            // Define variation function (this is a simple example, can be expanded)
            phrase.variationFunction = [](const std::vector<MidiEvent>& base) {
                std::vector<MidiEvent> variation = base;
                for (auto& event : variation) {
                    if (event.type == "noteOn" || event.type == "noteOff") {
                        event.data["noteNumber"] = event.data["noteNumber"].get<int>() + (std::rand() % 3 - 1);
                    }
                }
                return variation;
                };

            context.phrasesWithVariation[name] = phrase;
        }
        else if (element.contains("generatePhraseWithVariation") && element["generatePhraseWithVariation"].is_string()) {
            std::string name = element["generatePhraseWithVariation"].get<std::string>();

            auto it = context.phrasesWithVariation.find(name);
            if (it != context.phrasesWithVariation.end()) {
                auto phraseEvents = generatePhraseWithVariation(it->second);
                events.insert(events.end(), phraseEvents.begin(), phraseEvents.end());
            }
            else {
                std::cerr << "Warning: Phrase with variation '" << name << "' not found." << std::endl;
            }
        }
        else if (element.contains("defineArpeggiator") && element["defineArpeggiator"].is_object()) {
            std::string name = element["defineArpeggiator"].value("name", "default");
            Arpeggiator arp;
            std::string modeStr = element["defineArpeggiator"].value("mode", "up");
            if (modeStr == "up") arp.mode = Arpeggiator::Mode::Up;
            else if (modeStr == "down") arp.mode = Arpeggiator::Mode::Down;
            else if (modeStr == "updown") arp.mode = Arpeggiator::Mode::UpDown;
            else if (modeStr == "random") arp.mode = Arpeggiator::Mode::Random;
            arp.octaveRange = element["defineArpeggiator"].value("octaveRange", 1);
            arp.noteDuration = element["defineArpeggiator"].value("noteDuration", 120);
            context.arpeggiators[name] = arp;
        }
        else if (element.contains("applyArpeggiator") && element["applyArpeggiator"].is_object()) {
            std::string name = element["applyArpeggiator"].value("name", "default");
            std::vector<MidiEvent> chordEvents = parseJsonToEvents(element["applyArpeggiator"]["chord"], patternManager, context, evaluator);

            auto it = context.arpeggiators.find(name);
            if (it != context.arpeggiators.end()) {
                auto arpEvents = applyArpeggiator(chordEvents, it->second);
                events.insert(events.end(), arpEvents.begin(), arpEvents.end());
            }
            else {
                std::cerr << "Warning: Arpeggiator '" << name << "' not found." << std::endl;
            }
        }
        else if (element.contains("defineHarmonizationRule") && element["defineHarmonizationRule"].is_object()) {
            std::string name = element["defineHarmonizationRule"].value("name", "default");
            HarmonizationRule rule;
            rule.scaleIntervals = element["defineHarmonizationRule"]["scaleIntervals"].get<std::vector<int>>();
            rule.harmonizationIntervals = element["defineHarmonizationRule"]["harmonizationIntervals"].get<std::vector<std::vector<int>>>();
            context.harmonizationRules[name] = rule;
        }
        else if (element.contains("applyAdaptiveHarmonization") && element["applyAdaptiveHarmonization"].is_object()) {
            std::string ruleName = element["applyAdaptiveHarmonization"].value("rule", "default");
            int rootNote = element["applyAdaptiveHarmonization"].value("rootNote", 60);

            auto it = context.harmonizationRules.find(ruleName);
            if (it != context.harmonizationRules.end()) {
                auto harmonyEvents = applyAdaptiveHarmonization(events, it->second, rootNote);
                events.insert(events.end(), harmonyEvents.begin(), harmonyEvents.end());
            }
            else {
                std::cerr << "Warning: Harmonization rule '" << ruleName << "' not found." << std::endl;
            }
        }
        else if (element.contains("defineMidiEffect") && element["defineMidiEffect"].is_object()) {
            MidiEffect effect;
            std::string typeStr = element["defineMidiEffect"].value("type", "echo");
            if (typeStr == "echo") effect.type = MidiEffect::Type::Echo;
            else if (typeStr == "chord_splitter") effect.type = MidiEffect::Type::Chord_Splitter;
            effect.parameters = element["defineMidiEffect"].value("parameters", json::object());
            context.midiEffects.push_back(effect);
        }
        else if (element.contains("applyMidiEffects") && element["applyMidiEffects"].is_boolean()) {
            if (element["applyMidiEffects"].get<bool>()) {
                for (const auto& effect : context.midiEffects) {
                    events = applyMidiEffect(events, effect);
                }
            }
        }
        else if (element.contains("defineEventProbability") && element["defineEventProbability"].is_object()) {
            const auto& probDef = element["defineEventProbability"];
            std::string eventType = probDef.value("eventType", "noteOn");
            float probability = probDef.value("probability", 1.0f);
            json modification = probDef.value("modification", json::object());

            EventProbability eventProb;
            eventProb.probability = probability;
            eventProb.modification = modification;

            context.eventProbabilities[eventType] = eventProb;
        }
        else if (element.contains("applyEventProbabilities") && element["applyEventProbabilities"].is_boolean()) {
            if (element["applyEventProbabilities"].get<bool>()) {
                applyEventProbabilities(events, context.eventProbabilities, context.randomEngine);
            }
        }
		//i also don't know if this should be here...
        else if (element.contains("applyEventProbabilities") && element["applyEventProbabilities"].is_object()) {
            for (const auto& [eventType, probData] : element["applyEventProbabilities"].items()) {
                EventProbability prob;
                prob.probability = probData.value("probability", 1.0f);
                prob.modification = probData.value("modification", json::object());
                context.eventProbabilities[eventType] = prob;
            }
            applyEventProbabilities(events, context.eventProbabilities, context.randomEngine);
        }
        else if (element.contains("applyArticulationPattern") && element["applyArticulationPattern"].is_string()) {
            std::string patternName = element["applyArticulationPattern"].get<std::string>();
            auto it = context.articulationPatterns.find(patternName);
            if (it != context.articulationPatterns.end()) {
                applyArticulationPattern(events, it->second);
            }
            else {
                std::cerr << "Warning: Articulation pattern '" << patternName << "' not found." << std::endl;
            }
        }
        else if (element.contains("defineScale") && element["defineScale"].is_object()) {
            std::string scaleName = element["defineScale"].value("name", "default");
            std::vector<int> intervals = element["defineScale"].value("intervals", std::vector<int>{0, 2, 4, 5, 7, 9, 11});
            int rootNote = element["defineScale"].value("rootNote", 60);
            context.scales[scaleName] = Scale{ intervals, rootNote };
        }
        else if (element.contains("generateScaleBasedMelody") && element["generateScaleBasedMelody"].is_object()) {
            std::string scaleName = element["generateScaleBasedMelody"].value("scale", "default");
            int length = element["generateScaleBasedMelody"].value("length", 8);
            int minNote = element["generateScaleBasedMelody"].value("minNote", 60);
            int maxNote = element["generateScaleBasedMelody"].value("maxNote", 84);

            auto it = context.scales.find(scaleName);
            if (it != context.scales.end()) {
                auto melodyEvents = generateScaleBasedMelody(it->second, length, minNote, maxNote);
                events.insert(events.end(), melodyEvents.begin(), melodyEvents.end());
            }
            else {
                std::cerr << "Warning: Scale '" << scaleName << "' not found." << std::endl;
            }
        }
        else if (element.contains("setTrackMute") && element["setTrackMute"].is_object()) {
            std::string trackName = element["setTrackMute"].value("track", "");
            bool muteStatus = element["setTrackMute"].value("mute", false);
            context.trackMuteStatus[trackName] = muteStatus;
        }
        else if (element.contains("parameterAutomation") && element["parameterAutomation"].is_object()) {
            ParameterAutomation automation;
            automation.controllerNumber = element["parameterAutomation"].value("controllerNumber", 1);
            for (const auto& point : element["parameterAutomation"]["points"]) {
                automation.points.emplace_back(point[0].get<uint32_t>(), point[1].get<int>());
            }
            context.parameterAutomations.push_back(automation);
        }
        else if (element.contains("applyParameterAutomation") && element["applyParameterAutomation"].is_boolean()) {
            if (element["applyParameterAutomation"].get<bool>()) {
                applyParameterAutomation(events, context.parameterAutomations);
            }
        }
        else if (element.contains("generateAdaptiveHarmony") && element["generateAdaptiveHarmony"].is_object()) {
            std::string scaleName = element["generateAdaptiveHarmony"].value("scale", "default");
            int harmonizationInterval = element["generateAdaptiveHarmony"].value("interval", 4);

            auto scaleIt = context.scales.find(scaleName);
            if (scaleIt != context.scales.end()) {
                auto harmonyEvents = generateAdaptiveHarmony(events, scaleIt->second, harmonizationInterval);
                events.insert(events.end(), harmonyEvents.begin(), harmonyEvents.end());
            }
            else {
                std::cerr << "Warning: Scale '" << scaleName << "' not found for adaptive harmonization." << std::endl;
            }
        }
        else if (element.contains("defineTempoMap") && element["defineTempoMap"].is_array()) {
            for (const auto& point : element["defineTempoMap"]) {
                context.tempoMap.points.push_back({
                    point["tick"].get<uint32_t>(),
                    point["microsecondsPerQuarter"].get<uint32_t>()
                });
            }
            std::sort(context.tempoMap.points.begin(), context.tempoMap.points.end(),
                [](const TempoMap::TempoPoint& a, const TempoMap::TempoPoint& b) {
                    return a.tick < b.tick;
                });
        }
        else if (element.contains("applyTempoMap") && element["applyTempoMap"].is_boolean()) {
            if (element["applyTempoMap"].get<bool>()) {
                applyTempoMap(events, context.tempoMap);
            }
        }
        else if (element.contains("setRandomizationParams") && element["setRandomizationParams"].is_object()) {
            const auto& params = element["setRandomizationParams"];
            context.randomization.velocityRange = params.value("velocityRange", 10);
            context.randomization.timingRange = params.value("timingRange", 5);
            context.randomization.pitchRange = params.value("pitchRange", 2);
            context.randomization.noteProbability = params.value("noteProbability", 1.0f);
        }
        else if (element.contains("applyRandomization") && element["applyRandomization"].is_boolean()) {
            if (element["applyRandomization"].get<bool>()) {
                std::random_device rd;
                std::mt19937 gen(rd());
                applyRandomization(events, context.randomization, gen);
            }
        }
        else if (element.contains("defineChordProgression") && element["defineChordProgression"].is_object()) {
            std::string name = element["defineChordProgression"].value("name", "default");
            ChordProgression progression;
            progression.rootNote = element["defineChordProgression"].value("rootNote", 60);
            for (const auto& chordDef : element["defineChordProgression"]["chords"]) {
                ChordProgression::Chord chord;
                chord.notes = chordDef["notes"].get<std::vector<int>>();
                chord.duration = chordDef["duration"].get<int>();
                progression.chords.push_back(chord);
            }
            context.chordProgressions[name] = progression;
        }
        else if (element.contains("expandChordProgression") && element["expandChordProgression"].is_object()) {
            std::string name = element["expandChordProgression"].value("name", "default");
            bool arpeggiate = element["expandChordProgression"].value("arpeggiate", false);
            
            auto it = context.chordProgressions.find(name);
            if (it != context.chordProgressions.end()) {
                auto chordEvents = expandChordProgression(it->second, arpeggiate);
                events.insert(events.end(), chordEvents.begin(), chordEvents.end());
            } else {
                std::cerr << "Warning: Chord progression '" << name << "' not found." << std::endl;
            }
        }
        else if (element.contains("grooveTemplate") && element["grooveTemplate"].is_object()) {
            std::string templateName = element["grooveTemplate"].value("name", "default");
            std::vector<std::pair<int, int>> groove;
            for (const auto& step : element["grooveTemplate"]["steps"]) {
                groove.emplace_back(step[0].get<int>(), step[1].get<int>());
            }
            context.grooveTemplates[templateName] = GrooveTemplate{ groove };
        }
        else if (element.contains("applyGrooveTemplate") && element["applyGrooveTemplate"].is_string()) {
            std::string templateName = element["applyGrooveTemplate"].get<std::string>();
            auto it = context.grooveTemplates.find(templateName);
            if (it != context.grooveTemplates.end()) {
                applyGrooveTemplate(events, it->second);
            }
            else {
                std::cerr << "Warning: Groove template '" << templateName << "' not found." << std::endl;
            }
        }
        else if (element.contains("usePattern") && element["usePattern"].is_object()) {
            const auto& patternUse = element["usePattern"];
            std::string patternName = patternUse["name"];
            int repetitions = patternUse.value("repetitions", 1);
            auto patternEvents = patternManager.getPattern(patternName, repetitions);
            events.insert(events.end(), patternEvents.begin(), patternEvents.end());
        }
        else if (element.contains("tempoChange") && element["tempoChange"].is_object()) {
            TempoChange tempoChange;
            tempoChange.deltaTime = element["tempoChange"].value("deltaTime", 0u);
            tempoChange.microsecondsPerQuarter = element["tempoChange"].value("microsecondsPerQuarter", 500000u);
            context.tempoChanges.push_back(tempoChange);
        }
        else if (element.contains("velocityCurve") && element["velocityCurve"].is_object()) {
            std::string curveName = element["velocityCurve"].value("name", "default");
            std::vector<uint8_t> velocities = element["velocityCurve"].value("velocities", std::vector<uint8_t>{64, 96, 80, 112});
            context.velocityCurves[curveName] = VelocityCurve{ velocities };
        }
        else if (element.contains("applyVelocityCurve") && element["applyVelocityCurve"].is_string()) {
            std::string curveName = element["applyVelocityCurve"].get<std::string>();
            auto it = context.velocityCurves.find(curveName);
            if (it != context.velocityCurves.end()) {
                applyVelocityCurve(events, it->second);
            }
            else {
                std::cerr << "Warning: Velocity curve '" << curveName << "' not found." << std::endl;
            }
        }
        else if (element.contains("applyRandomization") && element["applyRandomization"].is_object()) {
            int velocityRange = element["applyRandomization"].value("velocityRange", 10);
            int timingRange = element["applyRandomization"].value("timingRange", 5);
            applyControlledRandomization(events, context, velocityRange, timingRange);
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

        // Apply conditional track muting after parsing all tracks
        if (j.contains("trackMuting") && j["trackMuting"].is_object()) {
            std::map<std::string, bool> muteStatus;
            for (const auto& [trackName, muted] : j["trackMuting"].items()) {
                muteStatus[trackName] = muted.get<bool>();
            }
            applyConditionalTrackMuting(tracks, muteStatus);
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