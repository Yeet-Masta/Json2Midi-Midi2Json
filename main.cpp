#include "midi_writer.h"
#include "midi_reader.h"
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <mode> <input_file> <output_file>" << std::endl;
        std::cerr << "Modes: json2midi, midi2json" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    std::string inputFile = argv[2];
    std::string outputFile = argv[3];

    try {
        if (mode == "json2midi") {
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
        else if (mode == "midi2json") {
            json midiJson = midiFileToJson(inputFile);

            std::ofstream outFile(outputFile);
            if (!outFile) {
                throw std::runtime_error("Unable to open output file: " + outputFile);
            }

            outFile << midiJson.dump(2);  // Use 2 spaces for indentation
            std::cout << "JSON file created successfully." << std::endl;
        }
        else {
            throw std::runtime_error("Invalid mode. Use 'json2midi' or 'midi2json'.");
        }
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