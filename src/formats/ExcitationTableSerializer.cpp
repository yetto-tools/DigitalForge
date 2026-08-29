#include "ExcitationTableSerializer.hpp"

#include <QString>

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "editor/ExcitationTableDocument.hpp"

namespace digitalforge::formats {

using editor::ExcitationTableDocument;
using editor::FlipFlopType;
using editor::KarnaughCellValue;

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

std::string flipFlopTypeToString(FlipFlopType type) {
    switch (type) {
        case FlipFlopType::D:
            return "D";
        case FlipFlopType::T:
            return "T";
        case FlipFlopType::JK:
            return "JK";
        case FlipFlopType::SR:
            return "SR";
    }
    return "D";
}

FlipFlopType flipFlopTypeFromString(const std::string& text) {
    if (text == "T") return FlipFlopType::T;
    if (text == "JK") return FlipFlopType::JK;
    if (text == "SR") return FlipFlopType::SR;
    return FlipFlopType::D;
}

} // namespace

nlohmann::json serializeExcitationTableDocument(const ExcitationTableDocument& document) {
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["kind"] = "excitationtable";
    json["stateBitCount"] = document.stateBitCount();

    nlohmann::json bits = nlohmann::json::array();
    for (int i = 0; i < document.stateBitCount(); ++i) {
        nlohmann::json bitJson;
        bitJson["name"] = document.stateBitName(i).toStdString();
        bitJson["flipFlopType"] = flipFlopTypeToString(document.flipFlopType(i));
        nlohmann::json presentState = nlohmann::json::array();
        nlohmann::json nextState = nlohmann::json::array();
        const int cellCount = 1 << document.stateBitCount();
        for (int m = 0; m < cellCount; ++m) {
            presentState.push_back(std::string(1, cellValueToChar(document.presentState(i, m))));
            nextState.push_back(std::string(1, cellValueToChar(document.nextState(i, m))));
        }
        bitJson["presentState"] = std::move(presentState);
        bitJson["nextState"] = std::move(nextState);
        bits.push_back(std::move(bitJson));
    }
    json["bits"] = std::move(bits);
    return json;
}

void loadExcitationTableDocument(ExcitationTableDocument& document, const nlohmann::json& json) {
    if (!json.contains("schemaVersion") || json.at("schemaVersion").get<int>() != 1) {
        throw std::invalid_argument("loadExcitationTableDocument: unsupported or missing schemaVersion");
    }
    const int stateBitCount = json.at("stateBitCount").get<int>();
    if (stateBitCount < 2 || stateBitCount > 4) {
        throw std::invalid_argument("loadExcitationTableDocument: stateBitCount must be between 2 and 4");
    }
    if (!json.contains("bits") || json.at("bits").size() != static_cast<std::size_t>(stateBitCount)) {
        throw std::invalid_argument("loadExcitationTableDocument: bits must have stateBitCount entries");
    }

    const std::size_t expectedCellCount = std::size_t{1} << stateBitCount;
    const nlohmann::json& bitsJson = json.at("bits");
    for (const nlohmann::json& bitJson : bitsJson) {
        if (bitJson.at("presentState").size() != expectedCellCount ||
            bitJson.at("nextState").size() != expectedCellCount) {
            throw std::invalid_argument("loadExcitationTableDocument: presentState/nextState array size does not "
                                         "match 2^stateBitCount for some bit");
        }
    }

    document.reset(stateBitCount);
    for (std::size_t i = 0; i < bitsJson.size(); ++i) {
        const nlohmann::json& bitJson = bitsJson.at(i);
        const int bitIndex = static_cast<int>(i);
        document.setStateBitName(bitIndex, QString::fromStdString(bitJson.at("name").get<std::string>()));
        document.setFlipFlopType(bitIndex, flipFlopTypeFromString(bitJson.value("flipFlopType", std::string("D"))));
        const nlohmann::json& presentStateJson = bitJson.at("presentState");
        const nlohmann::json& nextStateJson = bitJson.at("nextState");
        for (std::size_t m = 0; m < expectedCellCount; ++m) {
            const std::string presentStr = presentStateJson.at(m).get<std::string>();
            const char presentChar = presentStr.empty() ? '0' : presentStr.front();
            document.setPresentState(bitIndex, static_cast<int>(m), charToCellValue(presentChar));
            const std::string nextStr = nextStateJson.at(m).get<std::string>();
            const char nextChar = nextStr.empty() ? '0' : nextStr.front();
            document.setNextState(bitIndex, static_cast<int>(m), charToCellValue(nextChar));
        }
    }
    document.markClean();
}

void saveExcitationTableDocumentToFile(const ExcitationTableDocument& document, const std::string& path) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("saveExcitationTableDocumentToFile: cannot open '" + path + "' for writing");
    }
    file << serializeExcitationTableDocument(document).dump(2);
    if (!file.good()) {
        throw std::runtime_error("saveExcitationTableDocumentToFile: write failed for '" + path + "'");
    }
}

void loadExcitationTableDocumentFromFile(ExcitationTableDocument& document, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("loadExcitationTableDocumentFromFile: cannot open '" + path + "' for reading");
    }
    nlohmann::json json;
    file >> json;
    loadExcitationTableDocument(document, json);
}

} // namespace digitalforge::formats
