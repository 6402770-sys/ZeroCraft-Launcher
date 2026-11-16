// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of "Keep my game settings" (zero/content/ModpackKeep): the player's files come back after an update, a file the
// instance did not have is not created, and a file that cannot be put back leaves what the update wrote. Temp dirs only.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "zero/content/ModpackKeep.h"

using namespace Zero::Content;

namespace {

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString S(const char* text)
{
    return QString::fromLatin1(text);
}

}  // namespace

class ModpackKeepTest : public QObject {
    Q_OBJECT

   private slots:
    void listIsFixed()
    {
        const QStringList& files = keptGameFiles();
        QVERIFY(files.contains(S("options.txt")));
        QVERIFY(files.contains(S("optionsof.txt")));
        QVERIFY(files.contains(S("optionsshaders.txt")));
        QVERIFY(files.contains(S("servers.dat")));
        QVERIFY(files.contains(S("config/iris.properties")));
        QVERIFY(files.contains(S("config/oculus.properties")));
        QCOMPARE(files.size(), 6);
    }

    void restoresWhatTheUpdateOverwrote()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString game = root.filePath(S("minecraft"));
        QVERIFY(writeFile(game + S("/options.txt"), "mine=1"));
        QVERIFY(writeFile(game + S("/servers.dat"), QByteArray("\x01\x00\x02", 3)));
        QVERIFY(writeFile(game + S("/config/iris.properties"), "shaderPack=Mine"));
        QVERIFY(writeFile(game + S("/mods/other.jar"), "keep-me-alone"));

        KeptSettings kept(root.path());
        QVERIFY(kept.isValid());
        QVERIFY(kept.save(game));
        QCOMPARE(kept.savedFiles().size(), 3);

        // the update: the pack's own files replace them, and it brings one more of the list
        QVERIFY(writeFile(game + S("/options.txt"), "pack=1"));
        QVERIFY(writeFile(game + S("/servers.dat"), "pack-servers"));
        QVERIFY(QFile::remove(game + S("/config/iris.properties")));
        QVERIFY(writeFile(game + S("/optionsof.txt"), "pack-of"));

        QStringList failed;
        QVERIFY(kept.restore(game, &failed));
        QVERIFY(failed.isEmpty());
        QCOMPARE(readFile(game + S("/options.txt")), QByteArray("mine=1"));
        QCOMPARE(readFile(game + S("/servers.dat")), QByteArray("\x01\x00\x02", 3));
        QCOMPARE(readFile(game + S("/config/iris.properties")), QByteArray("shaderPack=Mine"));  // its folder is made again when needed
        // not in the player's copy: the pack's file stays, and what the player added elsewhere is untouched
        QCOMPARE(readFile(game + S("/optionsof.txt")), QByteArray("pack-of"));
        QCOMPARE(readFile(game + S("/mods/other.jar")), QByteArray("keep-me-alone"));
    }

    void absentFilesAreNotCreated()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString game = root.filePath(S("minecraft"));
        QVERIFY(QDir().mkpath(game));
        QVERIFY(writeFile(game + S("/options.txt"), "mine"));

        KeptSettings kept(root.path());
        QVERIFY(kept.save(game));
        QCOMPARE(kept.savedFiles(), QStringList{ S("options.txt") });

        // the update removed options.txt and the pack has no servers.dat
        QVERIFY(QFile::remove(game + S("/options.txt")));
        QVERIFY(kept.restore(game));
        QVERIFY(QFileInfo::exists(game + S("/options.txt")));
        QVERIFY(!QFileInfo::exists(game + S("/servers.dat")));
        QVERIFY(!QFileInfo::exists(game + S("/optionsof.txt")));
        QVERIFY(!QFileInfo::exists(game + S("/config")));
    }

    void nothingToKeep()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString game = root.filePath(S("empty"));  // does not even exist
        KeptSettings kept(root.path());
        QVERIFY(kept.save(game));
        QVERIFY(kept.savedFiles().isEmpty());
        QVERIFY(kept.restore(game));
        QVERIFY(!QFileInfo::exists(game));
    }

    void failureLeavesTheNewFiles()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString game = root.filePath(S("minecraft"));
        QVERIFY(writeFile(game + S("/options.txt"), "mine"));
        QVERIFY(writeFile(game + S("/servers.dat"), "mine-servers"));

        KeptSettings kept(root.path());
        QVERIFY(kept.save(game));

        // the update wrote new files; servers.dat is now a folder, so the player's file cannot go back over it
        QVERIFY(writeFile(game + S("/options.txt"), "pack"));
        QVERIFY(QFile::remove(game + S("/servers.dat")));
        QVERIFY(writeFile(game + S("/servers.dat/inside.txt"), "pack-folder"));

        QStringList failed;
        QVERIFY(!kept.restore(game, &failed));
        QCOMPARE(failed, QStringList{ S("servers.dat") });
        QCOMPARE(readFile(game + S("/options.txt")), QByteArray("mine"));                   // the others still came back
        QCOMPARE(readFile(game + S("/servers.dat/inside.txt")), QByteArray("pack-folder"));  // what the update wrote is untouched
        // and no half-written leftovers next to it
        QCOMPARE(QDir(game).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 1);
    }

    void missingCopyLeavesTheNewFile()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString game = root.filePath(S("minecraft"));
        QVERIFY(writeFile(game + S("/options.txt"), "mine"));
        KeptSettings kept(root.path());
        QVERIFY(kept.save(game));
        QVERIFY(writeFile(game + S("/options.txt"), "pack"));
        kept.discard();  // the temporary copy is gone: nothing can be restored, nothing is touched
        QVERIFY(kept.savedFiles().isEmpty());
        QVERIFY(kept.restore(game));
        QCOMPARE(readFile(game + S("/options.txt")), QByteArray("pack"));
    }

    void tempCopyIsRemoved()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString game = root.filePath(S("minecraft"));
        QVERIFY(writeFile(game + S("/options.txt"), "mine"));
        {
            KeptSettings kept(root.path());
            QVERIFY(kept.save(game));
            QCOMPARE(QDir(root.path()).entryList({ S("zero-keep-*") }, QDir::Dirs).size(), 1);
        }
        QCOMPARE(QDir(root.path()).entryList({ S("zero-keep-*") }, QDir::Dirs).size(), 0);
        QCOMPARE(readFile(game + S("/options.txt")), QByteArray("mine"));
    }
};

QTEST_MAIN(ModpackKeepTest)

#include "ModpackKeep_test.moc"
