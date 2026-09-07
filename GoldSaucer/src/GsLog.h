#pragma once

#include <QString>

// Persistent run log.
//
// GoldSaucer_GUI is built WIN32 (GUI subsystem), so it has no console: every
// qDebug()/qWarning() in the codebase — and there are hundreds — was written to
// a stream nobody could read. A user reporting "Field pickup randomization
// failed" had no way to tell us WHICH of the ~45 failure paths they hit.
//
// GsLog installs a Qt message handler that tees all of it to a file, so the
// existing diagnostics become reportable without touching the code that emits
// them. Every line is flushed as it is written: a hard crash still leaves the
// lines that led up to it.
namespace GsLog {

// Install the message handler and open the log. Call once, first thing in
// main(), before any window is constructed.
void init();

// Absolute path of the log file, or an empty string if none could be opened
// (a read-only AppData and a read-only temp dir, which should not happen).
QString path();

// Write a line verbatim, bypassing the qDebug category prefix. Used to tee the
// GUI's own console pane into the log so the user-facing narrative and the
// internal diagnostics interleave in one file.
void note(const QString& line);

// Blank line + heading, to separate runs within a session.
void banner(const QString& title);

}  // namespace GsLog
