#ifndef APSEEDFILE_H
#define APSEEDFILE_H

#include <QString>
#include <QByteArray>

/**
 * ApSeedFile — reads the .apff7 seed JSON in either format:
 *  - legacy: the file IS the JSON payload;
 *  - container (apworld 2026-07-10+): an APPlayerContainer zip holding
 *    archipelago.json (manifest) + ff7_seed.json (the payload).
 * The container form exists so archipelago.gg rooms offer the per-slot
 * download (the WebHost requires a zip with an archipelago.json manifest).
 *
 * readJson() sniffs the PK zip magic and returns the payload bytes, or an
 * empty array if the file cannot be opened/parsed.
 */
class ApSeedFile
{
public:
    static QByteArray readJson(const QString& path);
};

#endif // APSEEDFILE_H
