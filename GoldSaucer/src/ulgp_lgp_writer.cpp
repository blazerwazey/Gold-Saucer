#include "ulgp_lgp_writer.h"
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <array>
#include <algorithm>

namespace GoldSaucer {
namespace Ulgp {

// File destructor implementation
File::~File() {
}

// VectorFile implementation
VectorFile::VectorFile(const std::vector<char>& data) : m_data(data) {
}

std::vector<char> VectorFile::data() const {
    return m_data;
}

// UlgpLgpWriter implementation
UlgpLgpWriter::UlgpLgpWriter() {
}

UlgpLgpWriter::~UlgpLgpWriter() {
}

QString UlgpLgpWriter::errorString() const {
    return m_errorString;
}

std::vector<char> UlgpLgpWriter::qByteArrayToStdVector(const QByteArray& qba) {
    return std::vector<char>(qba.begin(), qba.end());
}

std::string UlgpLgpWriter::qStringToStdString(const QString& qs) {
    return qs.toStdString();
}

// Simple hash function for ulgp archive
std::size_t string_hash(const std::string& str) {
    std::size_t hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Create empty archive
Archive UlgpLgpWriter::createArchive() {
    return Archive(); // Use default constructor
}

// Create file from data
File_p UlgpLgpWriter::makeFile(const std::string& path, const std::vector<char>& data) {
    return std::make_unique<VectorFile>(data);
}

// LGP Header structure (extracted from ulgp)
#pragma pack(push, 1)
struct Header {
    Header() = default;
    Header(const Archive& ar) : size(static_cast<uint16_t>(ar.size())) {}
    
    uint16_t null1{};
    std::array<char, 10> magic = {'S','Q','U','A','R','E','S','O','F','T'};
    uint16_t size{};
    uint16_t null2{};
};
#pragma pack(pop)

// File info structure (extracted from ulgp)
struct FileInfo {
    FileInfo() = default;
    FileInfo(const std::string& name) {
        std::fill(this->name.begin(), this->name.end(), 0);
        std::copy(name.begin(), name.end(), this->name.begin());
    }
    
    std::array<char, 20> name{};
    uint32_t offset{};
    uint8_t type{0xE};
    uint16_t path{};
};

// File header structure (extracted from ulgp)
struct FileHeader {
    FileInfo info;
    uint32_t dataSize;
};

// Write data to stream (simple implementation)
template<typename T>
void writeData(std::ostream& out, const T& data) {
    out.write(reinterpret_cast<const char*>(&data), sizeof(T));
}

// Write archive to LGP file (extracted from ulgp)
void UlgpLgpWriter::writeArchive(const std::string& path, const Archive& archive) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        m_errorString = QString("Could not open %1 for writing").arg(QString::fromStdString(path));
        return;
    }
    
    try {
        // Create header
        Header header(archive);
        
        // Prepare file info and data
        std::vector<FileInfo> fileInfos;
        std::vector<std::vector<char>> fileData;
        std::vector<std::string> paths; // Simple path table
        
        // Collect file information
        for (const auto& file : archive) {
            FileInfo info(file.first);
            fileInfos.push_back(info);
            
            const auto& data = file.second->data();
            fileData.push_back(data);
            
            // Add to paths (simple implementation - no subdirectories)
            paths.push_back(""); // Root directory
        }
        
        // Calculate offsets (start after header + file count + file info + path table)
        uint32_t currentOffset = sizeof(Header) + 2 + (static_cast<uint32_t>(fileInfos.size()) * sizeof(FileInfo)) + 2 + (static_cast<uint32_t>(paths.size()) * 2);
        
        // Update file offsets
        for (size_t i = 0; i < fileInfos.size(); ++i) {
            fileInfos[i].offset = currentOffset;
            currentOffset += static_cast<uint32_t>(fileData[i].size());
        }
        
        // Write header
        writeData(out, header);
        
        // Write file count
        uint16_t fileCount = static_cast<uint16_t>(archive.size());
        writeData(out, fileCount);
        
        // Write file info
        for (const auto& info : fileInfos) {
            writeData(out, info);
        }
        
        // Write path table
        writeData(out, static_cast<uint16_t>(paths.size()));
        for (const auto& path : paths) {
            writeData(out, static_cast<uint16_t>(path.size()));
            out.write(path.c_str(), path.size());
        }
        
        // Write file data
        for (size_t i = 0; i < fileData.size(); ++i) {
            // Write file header
            FileHeader fileHeader;
            fileHeader.info = fileInfos[i];
            fileHeader.dataSize = static_cast<uint32_t>(fileData[i].size());
            writeData(out, fileHeader);
            
            // Write file data
            out.write(fileData[i].data(), fileData[i].size());
        }
        
        // Write footer
        out << "FINAL FANTASY7";
        
        // Update header with file count (LGP format requirement)
        out.seekp(sizeof(Header));
        writeData(out, fileCount);
        
    } catch (const std::exception& e) {
        m_errorString = QString("Error writing LGP: %1").arg(e.what());
        return;
    }
}

// Main write function
bool UlgpLgpWriter::writeLgp(const QString& outputPath, const QMap<QString, QByteArray>& files) {
    m_errorString.clear();
    
    if (files.isEmpty()) {
        m_errorString = "No files to write";
        return false;
    }
    
    try {
        // Create archive
        Archive archive = createArchive();
        
        // Add files to archive
        for (auto it = files.begin(); it != files.end(); ++it) {
            std::string fileName = qStringToStdString(it.key());
            std::vector<char> fileData = qByteArrayToStdVector(it.value());
            
            if (fileData.empty()) {
                m_errorString = QString("Empty file data for %1").arg(it.key());
                return false;
            }
            
            archive[fileName] = makeFile(fileName, fileData);
        }
        
        // Write archive to file
        std::string outputPathStr = qStringToStdString(outputPath);
        writeArchive(outputPathStr, archive);
        
        return m_errorString.isEmpty();
        
    } catch (const std::exception& e) {
        m_errorString = QString("Exception: %1").arg(e.what());
        return false;
    }
}

} // namespace Ulgp
} // namespace GoldSaucer
