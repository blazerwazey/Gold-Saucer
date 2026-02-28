#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMessageBox>
#include <QProgressBar>
#include <QLabel>

// User-friendly error handling and feedback system
class UserFeedback
{
public:
    enum MessageType {
        Info,
        Warning,
        Error,
        Success,
        Progress
    };
    
    enum ErrorSeverity {
        Low,      // Minor issues, can continue
        Medium,   // Important but recoverable
        High,     // Critical, may need user action
        Critical  // Fatal, cannot continue
    };
    
    struct UserMessage {
        MessageType type;
        QString title;
        QString content;
        QString technicalDetails;
        ErrorSeverity severity;
        QDateTime timestamp;
        QStringList suggestedActions;
        
        UserMessage() : type(Info), severity(Low), timestamp(QDateTime::currentDateTime()) {}
        UserMessage(MessageType t, const QString& title_, const QString& content_, 
                   ErrorSeverity sev = Low, const QString& tech = QString())
            : type(t), title(title_), content(content_), technicalDetails(tech), 
              severity(sev), timestamp(QDateTime::currentDateTime()) {}
    };
    
    static UserFeedback& instance();
    
    // Message display
    void showMessage(const UserMessage& message);
    void showInfo(const QString& title, const QString& content, const QString& technical = QString());
    void showWarning(const QString& title, const QString& content, const QString& technical = QString());
    void showError(const QString& title, const QString& content, const QString& technical = QString());
    void showSuccess(const QString& title, const QString& content);
    
    // Error handling with suggestions
    void handleError(const QString& error, ErrorSeverity severity, const QString& context = QString());
    void handleTextReplacementError(const QString& operation, const QString& error, const QString& itemId = QString());
    void handleKernelBinError(const QString& operation, const QString& error, const QString& filePath = QString());
    
    // Progress feedback
    void setProgressWidget(QProgressBar* progressBar, QLabel* statusLabel = nullptr);
    void updateProgress(int value, const QString& status = QString());
    void setProgressRange(int minimum, int maximum);
    void finishProgress(const QString& completionMessage = QString());
    void resetProgress();
    
    // Message history
    QStringList getRecentMessages(int count = 10) const;
    void clearHistory();
    bool hasUnreadErrors() const;
    int getErrorCount() const;
    
    // User guidance
    void showGuidance(const QString& topic);
    void showTroubleshootingTips(const QString& issue);
    void showValidationResults(const QStringList& errors, const QStringList& warnings);
    
    // Configuration
    void setVerboseMode(bool enabled);
    void setTechnicalDetailsMode(bool enabled);
    void setAutoShowErrors(bool enabled);
    
private:
    UserFeedback();
    ~UserFeedback() = default;
    
    // Prevent copying
    UserFeedback(const UserFeedback&) = delete;
    UserFeedback& operator=(const UserFeedback&) = delete;
    
    // Internal methods
    QString formatMessageForUser(const UserMessage& message) const;
    QStringList generateSuggestion(const QString& error, ErrorSeverity severity) const;
    void logMessage(const UserMessage& message);
    void displayMessageBox(const UserMessage& message);
    
    // Member variables
    QVector<UserMessage> m_messageHistory;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    bool m_verboseMode;
    bool m_technicalDetailsMode;
    bool m_autoShowErrors;
    
    static UserFeedback* s_instance;
    
    // Common error messages and suggestions
    static const QMap<QString, QStringList> ERROR_SUGGESTIONS;
    static const QMap<QString, QString> GUIDANCE_TOPICS;
    static const QMap<QString, QStringList> TROUBLESHOOTING_TIPS;
};
