#include <QApplication>
#include <QDir>
#include <QDebug>
#include "GUI/SimpleMainWindow.h"
#include "GsLog.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Gold Saucer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Gold Saucer Team");

    // Before anything else: this build is WIN32 (no console), so without a
    // message handler every qDebug() in the program is discarded and a failed
    // run leaves the user with nothing to send us.
    GsLog::init();
    
    // Set application style
    app.setStyle("Fusion");
    
    // Dark theme palette
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::AlternateBase, QColor(60, 60, 60));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(60, 60, 60));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    
    app.setPalette(darkPalette);
    
    // Create and show main window
    SimpleMainWindow window;
    window.show();
    
    qDebug() << "Gold Saucer FF7 Randomizer GUI started";
    qDebug() << "Version" << app.applicationVersion();
    
    return app.exec();
}
