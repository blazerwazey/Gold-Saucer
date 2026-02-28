#include "UserFeedback.h"
#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include <QDebug>

UserFeedback* UserFeedback::s_instance = nullptr;

// Error suggestions database
const QMap<QString, QStringList> UserFeedback::ERROR_SUGGESTIONS = {
    {"KERNEL.BIN not found", {
        "Check that FF7 is properly installed",
        "Verify the FF7 installation path in settings",
        "Make sure you're using the correct FF7 version (1998/Steam)",
        "Try running FF7 once to verify installation"
    }},
    {"Invalid KERNEL.BIN format", {
        "The KERNEL.BIN file may be corrupted",
        "Try verifying FF7 installation integrity",
        "Check if you have any other mods that modify KERNEL.BIN",
        "Restore from backup if available"
    }},
    {"Text replacement failed", {
        "Check that text replacement is enabled in settings",
        "Verify the selected categories are enabled",
        "Try using simpler naming style",
        "Check available disk space"
    }},
    {"Failed to create backup", {
        "Check write permissions in FF7 directory",
        "Ensure sufficient disk space is available",
        "Try running as administrator",
        "Check antivirus software isn't blocking file access"
    }},
    {"Validation failed", {
        "The modified file may be corrupted",
        "Try restoring from backup",
        "Check if any other programs are using the file",
        "Restart the application and try again"
    }}
};

// Guidance topics
const QMap<QString, QString> UserFeedback::GUIDANCE_TOPICS = {
    {"Text Replacement", 
        "Text replacement hides randomization results by changing item names to generic alternatives.\n\n"
        "• Enable text replacement in the main settings\n"
        "• Choose which item categories to replace\n"
        "• Select a naming style (Descriptive recommended)\n"
        "• Use the preview to see sample names\n"
        "• Click 'Generate Preview' to test different styles"},
        
    {"Backup System",
        "The system automatically creates backups before modifying files.\n\n"
        "• Backups are stored in the 'backups' folder\n"
        "• Up to 5 recent backups are kept\n"
        "• Backups are automatically restored on errors\n"
        "• Manual restore is possible through the backup system"},
        
    {"Naming Styles",
        "Different naming styles provide different experiences:\n\n"
        "• Generic: 'Random Weapon 1' - Simple and clear\n"
        "• Descriptive: 'Mystic Blade' - More immersive\n"
        "• Lore-Based: 'Blade of the Ancients' - Story-focused\n"
        "• Tier-Based: 'Common Blade' - Shows rarity\n"
        "• Custom: User-defined templates"}
};

// Troubleshooting tips
const QMap<QString, QStringList> UserFeedback::TROUBLESHOOTING_TIPS = {
    {"Text replacement not working", {
        "1. Verify text replacement is enabled in main settings",
        "2. Check that specific item categories are enabled",
        "3. Ensure KERNEL.BIN validation passes",
        "4. Try running randomization again",
        "5. Check the console for detailed error messages"
    }},
    {"Generated names look wrong", {
        "1. Try different naming styles in preview",
        "2. Check if intelligent naming is enabled",
        "3. Adjust complexity slider",
        "4. Verify color coding settings",
        "5. Reset to default naming templates"
    }},
    {"Randomization fails", {
        "1. Check FF7 installation path is correct",
        "2. Verify write permissions in FF7 directory",
        "3. Ensure sufficient disk space",
        "4. Close any other programs using FF7 files",
        "5. Try running as administrator"
    }},
    {"Backup issues", {
        "1. Check available disk space",
        "2. Verify write permissions",
        "3. Check antivirus software settings",
        "4. Ensure FF7 directory isn't read-only",
        "5. Try creating backup manually"
    }}
};

UserFeedback& UserFeedback::instance()
{
    if (!s_instance) {
        s_instance = new UserFeedback();
    }
    return *s_instance;
}

UserFeedback::UserFeedback()
    : m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_verboseMode(false)
    , m_technicalDetailsMode(false)
    , m_autoShowErrors(true)
{
}

void UserFeedback::showMessage(const UserMessage& message)
{
    logMessage(message);
    
    if (m_autoShowErrors || message.type != Error) {
        displayMessageBox(message);
    }
}

void UserFeedback::showInfo(const QString& title, const QString& content, const QString& technical)
{
    showMessage(UserMessage(Info, title, content, Low, technical));
}

void UserFeedback::showWarning(const QString& title, const QString& content, const QString& technical)
{
    showMessage(UserMessage(Warning, title, content, Medium, technical));
}

void UserFeedback::showError(const QString& title, const QString& content, const QString& technical)
{
    showMessage(UserMessage(Error, title, content, High, technical));
}

void UserFeedback::showSuccess(const QString& title, const QString& content)
{
    showMessage(UserMessage(Success, title, content, Low));
}

void UserFeedback::handleError(const QString& error, ErrorSeverity severity, const QString& context)
{
    QString title = "Error";
    QString content = error;
    
    if (!context.isEmpty()) {
        content = QString("%1\n\nContext: %2").arg(error, context);
    }
    
    // Add suggestions based on error type
    QStringList suggestions;
    for (auto it = ERROR_SUGGESTIONS.begin(); it != ERROR_SUGGESTIONS.end(); ++it) {
        if (error.contains(it.key(), Qt::CaseInsensitive)) {
            suggestions = it.value();
            break;
        }
    }
    
    if (suggestions.isEmpty()) {
        suggestions = generateSuggestion(error, severity);
    }
    
    UserMessage message(Error, title, content, severity, QString());
    message.suggestedActions = suggestions;
    
    showMessage(message);
}

void UserFeedback::handleTextReplacementError(const QString& operation, const QString& error, const QString& itemId)
{
    QString title = "Text Replacement Error";
    QString content = QString("Failed to %1").arg(operation);
    
    if (!itemId.isEmpty()) {
        content += QString(" for item %1").arg(itemId);
    }
    
    content += QString(":\n\n%1").arg(error);
    
    QStringList suggestions = {
        "Check text replacement settings",
        "Verify the item category is enabled",
        "Try a simpler naming style",
        "Check KERNEL.BIN file integrity"
    };
    
    UserMessage message(Error, title, content, Medium, QString());
    message.suggestedActions = suggestions;
    
    showMessage(message);
}

void UserFeedback::handleKernelBinError(const QString& operation, const QString& error, const QString& filePath)
{
    QString title = "KERNEL.BIN Error";
    QString content = QString("Failed to %1 KERNEL.BIN").arg(operation);
    
    if (!filePath.isEmpty()) {
        content += QString(":\n%1").arg(filePath);
    }
    
    content += QString(":\n\n%1").arg(error);
    
    QStringList suggestions = {
        "Check FF7 installation path",
        "Verify file permissions",
        "Ensure KERNEL.BIN isn't corrupted",
        "Try restoring from backup"
    };
    
    UserMessage message(Error, title, content, High, QString());
    message.suggestedActions = suggestions;
    
    showMessage(message);
}

void UserFeedback::setProgressWidget(QProgressBar* progressBar, QLabel* statusLabel)
{
    m_progressBar = progressBar;
    m_statusLabel = statusLabel;
}

void UserFeedback::updateProgress(int value, const QString& status)
{
    if (m_progressBar) {
        m_progressBar->setValue(value);
    }
    
    if (m_statusLabel) {
        m_statusLabel->setText(status);
    }
}

void UserFeedback::setProgressRange(int minimum, int maximum)
{
    if (m_progressBar) {
        m_progressBar->setRange(minimum, maximum);
    }
}

void UserFeedback::finishProgress(const QString& completionMessage)
{
    if (m_progressBar) {
        m_progressBar->setValue(m_progressBar->maximum());
    }
    
    if (m_statusLabel) {
        m_statusLabel->setText(completionMessage);
    }
    
    if (!completionMessage.isEmpty()) {
        showSuccess("Complete", completionMessage);
    }
}

void UserFeedback::resetProgress()
{
    if (m_progressBar) {
        m_progressBar->setValue(0);
    }
    
    if (m_statusLabel) {
        m_statusLabel->setText("Ready");
    }
}

QStringList UserFeedback::getRecentMessages(int count) const
{
    QStringList recent;
    int start = qMax(0, m_messageHistory.size() - count);
    
    for (int i = start; i < m_messageHistory.size(); i++) {
        const UserMessage& msg = m_messageHistory[i];
        QString formatted = QString("[%1] %2: %3")
                           .arg(msg.timestamp.toString("hh:mm:ss"))
                           .arg(msg.title)
                           .arg(msg.content);
        recent.append(formatted);
    }
    
    return recent;
}

void UserFeedback::clearHistory()
{
    m_messageHistory.clear();
}

bool UserFeedback::hasUnreadErrors() const
{
    for (const UserMessage& msg : m_messageHistory) {
        if (msg.type == Error) {
            return true;
        }
    }
    return false;
}

int UserFeedback::getErrorCount() const
{
    int count = 0;
    for (const UserMessage& msg : m_messageHistory) {
        if (msg.type == Error) {
            count++;
        }
    }
    return count;
}

void UserFeedback::showGuidance(const QString& topic)
{
    QString content = GUIDANCE_TOPICS.value(topic, "No guidance available for this topic.");
    showInfo(topic, content);
}

void UserFeedback::showTroubleshootingTips(const QString& issue)
{
    QStringList tips = TROUBLESHOOTING_TIPS.value(issue, {"No specific tips available for this issue."});
    
    QString content = tips.join("\n");
    showInfo("Troubleshooting Tips", content);
}

void UserFeedback::showValidationResults(const QStringList& errors, const QStringList& warnings)
{
    QString content;
    
    if (!errors.isEmpty()) {
        content += "Errors:\n" + errors.join("\n") + "\n\n";
    }
    
    if (!warnings.isEmpty()) {
        content += "Warnings:\n" + warnings.join("\n");
    }
    
    if (errors.isEmpty() && warnings.isEmpty()) {
        content = "Validation passed successfully!";
        showSuccess("Validation Results", content);
    } else {
        QString title = errors.isEmpty() ? "Validation Warnings" : "Validation Errors";
        MessageType type = errors.isEmpty() ? Warning : Error;
        showMessage(UserMessage(type, title, content, errors.isEmpty() ? Medium : High));
    }
}

void UserFeedback::setVerboseMode(bool enabled)
{
    m_verboseMode = enabled;
}

void UserFeedback::setTechnicalDetailsMode(bool enabled)
{
    m_technicalDetailsMode = enabled;
}

void UserFeedback::setAutoShowErrors(bool enabled)
{
    m_autoShowErrors = enabled;
}

QString UserFeedback::formatMessageForUser(const UserMessage& message) const
{
    QString formatted = message.content;
    
    if (m_technicalDetailsMode && !message.technicalDetails.isEmpty()) {
        formatted += "\n\nTechnical Details:\n" + message.technicalDetails;
    }
    
    if (!message.suggestedActions.isEmpty()) {
        formatted += "\n\nSuggested Actions:\n";
        for (int i = 0; i < message.suggestedActions.size(); i++) {
            formatted += QString("%1. %2\n").arg(i + 1).arg(message.suggestedActions[i]);
        }
    }
    
    return formatted;
}

QStringList UserFeedback::generateSuggestion(const QString& error, ErrorSeverity severity) const
{
    QStringList suggestions;
    
    if (error.contains("permission", Qt::CaseInsensitive)) {
        suggestions << "Check file permissions" << "Try running as administrator";
    } else if (error.contains("corrupt", Qt::CaseInsensitive)) {
        suggestions << "Restore from backup" << "Verify file integrity";
    } else if (error.contains("space", Qt::CaseInsensitive)) {
        suggestions << "Check available disk space" << "Clean up temporary files";
    } else {
        suggestions << "Check the troubleshooting guide" << "Restart the application";
    }
    
    return suggestions;
}

void UserFeedback::logMessage(const UserMessage& message)
{
    m_messageHistory.append(message);
    
    // Keep only recent messages (last 100)
    if (m_messageHistory.size() > 100) {
        m_messageHistory.removeFirst();
    }
    
    // Log to console in verbose mode
    if (m_verboseMode) {
        QString logLevel;
        switch (message.type) {
            case Info: logLevel = "INFO"; break;
            case Warning: logLevel = "WARN"; break;
            case Error: logLevel = "ERROR"; break;
            case Success: logLevel = "SUCCESS"; break;
            case Progress: logLevel = "PROGRESS"; break;
        }
        
        qDebug() << QString("[%1] [%2] %3: %4")
                    .arg(message.timestamp.toString("hh:mm:ss"))
                    .arg(logLevel)
                    .arg(message.title)
                    .arg(message.content);
    }
}

void UserFeedback::displayMessageBox(const UserMessage& message)
{
    QMessageBox::Icon icon;
    switch (message.type) {
        case Info: icon = QMessageBox::Information; break;
        case Warning: icon = QMessageBox::Warning; break;
        case Error: icon = QMessageBox::Critical; break;
        case Success: icon = QMessageBox::Information; break;
        case Progress: return; // Don't show progress as popup
    }
    
    QString content = formatMessageForUser(message);
    
    QMessageBox msgBox(icon, message.title, content, QMessageBox::Ok);
    msgBox.exec();
}
