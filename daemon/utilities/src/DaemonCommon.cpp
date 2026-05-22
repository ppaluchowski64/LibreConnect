#include <DaemonCommon.h>
#include <QStandardPaths>
#include <fstream>
#include <nlohmann/json.hpp>

void DaemonUtils::AddDeviceToAutoConnectList(const std::string& uuid) {
    const std::string file = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/daemon/auto-connect-list.JSON").toStdString();
    const std::filesystem::path filePath(file);
    std::filesystem::create_directories(filePath.parent_path());

    nlohmann::json jsonArray = nlohmann::json::array();

    std::ifstream inFile(filePath);
    if (inFile.is_open()) {
        try {
            inFile >> jsonArray;
            if (!jsonArray.is_array()) {
                jsonArray = nlohmann::json::array();
            }
        } catch (const nlohmann::json::parse_error& e) {
            jsonArray = nlohmann::json::array();
        }
        inFile.close();
    }

    if (std::find(jsonArray.begin(), jsonArray.end(), uuid) == jsonArray.end()) {
        jsonArray.push_back(uuid);

        std::ofstream outFile(filePath);
        if (outFile.is_open()) {
            outFile << jsonArray.dump(4);
            outFile.close();
        }
    }
}

void DaemonUtils::RemoveDeviceFromAutoConnectList(const std::string& uuid) {
    const std::string file = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/daemon/auto-connect-list.JSON").toStdString();
    const std::filesystem::path filePath(file);

    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        return;
    }

    nlohmann::json jsonArray;
    try {
        inFile >> jsonArray;
    } catch (const nlohmann::json::parse_error& e) {
        return;
    }
    inFile.close();

    if (!jsonArray.is_array()) {
        return;
    }

    auto it = std::find(jsonArray.begin(), jsonArray.end(), uuid);
    if (it != jsonArray.end()) {
        jsonArray.erase(it);

        std::ofstream outFile(filePath);
        if (outFile.is_open()) {
            outFile << jsonArray.dump(4);
            outFile.close();
        }
    }
}
