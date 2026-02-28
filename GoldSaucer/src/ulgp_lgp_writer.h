#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>
#include <memory>
#include <vector>
#include <string>

namespace GoldSaucer {
namespace Ulgp {

// Forward declarations for ulgp types
class File {
public:
    virtual ~File();
    virtual std::vector<char> data() const = 0;
};

using File_p = std::unique_ptr<File>;
using Archive = std::unordered_map<std::string, File_p>;

// Ulgp LGP Writer class
class UlgpLgpWriter {
public:
    UlgpLgpWriter();
    ~UlgpLgpWriter();
    
    // Write LGP archive from file data
    bool writeLgp(const QString& outputPath, const QMap<QString, QByteArray>& files);
    
    // Get last error message
    QString errorString() const;
    
private:
    QString m_errorString;
    
    // Helper methods for Qt/ulgp conversion
    std::vector<char> qByteArrayToStdVector(const QByteArray& qba);
    std::string qStringToStdString(const QString& qs);
    
    // ulgp core functions (extracted from ulgp source)
    Archive createArchive();
    File_p makeFile(const std::string& path, const std::vector<char>& data);
    void writeArchive(const std::string& path, const Archive& archive);
};

// Simple File implementation for ulgp
class VectorFile : public File {
public:
    VectorFile(const std::vector<char>& data);
    std::vector<char> data() const override;
    
private:
    std::vector<char> m_data;
};

} // namespace Ulgp
} // namespace GoldSaucer
