#include "TruthTableSerializer.hpp"

#include <QString>

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "editor/TruthTableDocument.hpp"

namespace digitalforge::formats {

using editor::KarnaughCellValue;
using editor::TruthTableDocument;

namespace {

char cellValueToChar(KarnaughCellValue value) {
    switch (value) {
        case KarnaughCellValue::Zero:
            return '0';
        case KarnaughCellValue::One:
            return '1';
        case KarnaughCellValue::DontCare:
            return 'X';
    }
    return '0';
}

KarnaughCellValue charToCellValue(char c) {
    switch (c) {
        case '1':
            return KarnaughCellValue::One;
        case 'X':
        case 'x':
            return KarnaughCellValue::DontCare;
        default:
            return KarnaughCellValue::Zero;
    }
}

} // namespace

nlohmann::json serializeTruthTableDocument(const TruthTableDocument& document) {
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["kind"] = "truthtable";
    json["variableCount"] = document.variableCount();

    nlohmann::json variableNames = nlohmann::json::array();
    for (int i = 0; i < document.variableCount(); ++i) {
        variableNames.push_back(document.variableName(i).toStdString());
    }
    json["variableNames"] = std::move(variableNames);

    nlohmann::json outputs = nlohmann::json::array();
    for (int o = 0; o < document.outputCount(); ++o) {
        nlohmann::json output;
        output["name"] = document.outputName(o).toStdString();
        nlohmann::json cells = nlohmann::json::array();
        for (KarnaughCellValue value : document.outputCells(o)) {
            cells.push_back(std::string(1, cellValueToChar(value)));
        }
        output["cells"] = std::move(cells);
        outputs.push_back(std::move(output));
    }
    json["outputs"] = std::move(outputs);
    return json;
}

void loadTruthTableDocument(TruthTableDocument& document, const nlohmann::json& json) {
    if (!json.contains("schemaVersion") || json.at("schemaVersion").get<int>() != 1) {
        throw std::invalid_argument("loadTruthTableDocument: unsupported or missing schemaVersion");
    }
    const int variableCount = json.at("variableCount").get<int>();
    if (variableCount < 2 || variableCount > 4) {
        throw std::invalid_argument("loadTruthTableDocument: variableCount must be between 2 and 4");
    }
    if (!json.contains("outputs") || json.at("outputs").empty()) {
        throw std::invalid_argument("loadTruthTableDocument: outputs must be a non-empty array");
    }

    const std::size_t expectedCellCount = std::size_t{1} << variableCount;
    const nlohmann::json& outputsJson = json.at("outputs");
    for (const nlohmann::json& outputJson : outputsJson) {
        if (outputJson.at("cells").size() != expectedCellCount) {
            throw std::invalid_argument(
                "loadTruthTableDocument: cells array size does not match 2^variableCount for some output");
        }
    }

    document.reset(variableCount);
    if (json.contains("variableNames")) {
        const nlohmann::json& namesJson = json.at("variableNames");
        for (int i = 0; i < variableCount && i < static_cast<int>(namesJson.size()); ++i) {
            document.setVariableName(i, QString::fromStdString(namesJson.at(static_cast<std::size_t>(i)).get<std::string>()));
        }
    }

    // reset() ya dejo una salida (F1, en Zero); se reutiliza para la primera
    // entrada de "outputs" y se agregan las que falten.
    for (std::size_t o = 0; o < outputsJson.size(); ++o) {
        const nlohmann::json& outputJson = outputsJson.at(o);
        const int outputIndex = o == 0 ? 0 : document.addOutput();
        document.setOutputName(outputIndex, QString::fromStdString(outputJson.at("name").get<std::string>()));
        const nlohmann::json& cellsJson = outputJson.at("cells");
        for (std::size_t m = 0; m < expectedCellCount; ++m) {
            const std::string cellStr = cellsJson.at(m).get<std::string>();
            const char c = cellStr.empty() ? '0' : cellStr.front();
            document.setCellValue(outputIndex, static_cast<int>(m), charToCellValue(c));
        }
    }
    document.markClean();
}

void saveTruthTableDocumentToFile(const TruthTableDocument& document, const std::string& path) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("saveTruthTableDocumentToFile: cannot open '" + path + "' for writing");
    }
    file << serializeTruthTableDocument(document).dump(2);
    if (!file.good()) {
        throw std::runtime_error("saveTruthTableDocumentToFile: write failed for '" + path + "'");
    }
}

void loadTruthTableDocumentFromFile(TruthTableDocument& document, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("loadTruthTableDocumentFromFile: cannot open '" + path + "' for reading");
    }
    nlohmann::json json;
    file >> json;
    loadTruthTableDocument(document, json);
}

} // namespace digitalforge::formats
