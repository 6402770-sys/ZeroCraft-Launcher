// SPDX-License-Identifier: GPL-3.0-only
// zero-setup-pack: appends the payload (a launcher folder) to the setup stub exe.
//
//   zero-setup-pack --stub zero-setup.exe --stage dist/stage/ZEROLauncher --version 0.1.0 --out ZEROLauncher-0.1.0-Windows-Setup.exe
//   zero-setup-pack --info ZEROLauncher-0.1.0-Windows-Setup.exe        (lists the payload and verifies its checksum)
#include "Payload.h"

#include <QCoreApplication>
#include <QTextStream>

using namespace ZeroSetup;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString stub, stage, version, outExe, info;
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        auto val = [&](QString& dst) {
            if (i + 1 >= args.size()) {
                err << args[i] << " needs a value\n";
                return false;
            }
            dst = args[++i];
            return true;
        };
        bool ok = true;
        if (args[i] == "--stub")
            ok = val(stub);
        else if (args[i] == "--stage")
            ok = val(stage);
        else if (args[i] == "--version")
            ok = val(version);
        else if (args[i] == "--out")
            ok = val(outExe);
        else if (args[i] == "--info")
            ok = val(info);
        else {
            err << "unknown argument " << args[i] << "\n";
            return 2;
        }
        if (!ok)
            return 2;
    }

    if (!info.isEmpty()) {
        PayloadReader r;
        QString e;
        const OpenStatus st = r.open(info, &e);
        if (st != OpenStatus::Ok) {
            err << "no valid payload: " << e << "\n";
            return 1;
        }
        out << "version " << r.info().version << ", " << r.info().files.size() << " files, " << r.info().totalSize << " bytes, stub "
            << r.info().stubSize << " bytes, payload " << r.info().payloadSize << " bytes\n";
        if (!r.verifyHash(nullptr, &e)) {
            err << "checksum FAILED: " << e << "\n";
            return 1;
        }
        out << "payload checksum OK\n";
        return 0;
    }

    if (stub.isEmpty() || stage.isEmpty() || version.isEmpty() || outExe.isEmpty()) {
        err << "usage: zero-setup-pack --stub <setup.exe> --stage <folder> --version <X.Y.Z> --out <file.exe>\n"
               "       zero-setup-pack --info <file.exe>\n";
        return 2;
    }
    QString e;
    if (!packPayload(stub, stage, version, outExe, &e, [&](const QString& m) { out << "\r" << m << "      " << Qt::flush; })) {
        err << "\npack failed: " << e << "\n";
        return 1;
    }
    PayloadReader r;
    if (r.open(outExe, &e) != OpenStatus::Ok || !r.verifyHash(nullptr, &e)) {
        err << "\nself-check failed: " << e << "\n";
        return 1;
    }
    out << "\nwrote " << outExe << ": " << r.info().files.size() << " files, " << (r.info().totalSize >> 20) << " MB raw -> "
        << (r.info().payloadSize >> 20) << " MB payload\n";
    return 0;
}
