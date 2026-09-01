#include "MainWindow.hpp"

#include <QtCore/QCommandLineParser>
#include <QtWidgets/QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PeerDesk");
    app.setOrganizationName("PeerDesk");

    QCommandLineParser parser;
    parser.setApplicationDescription("PeerDesk viewer — connect to an Ubuntu host");
    parser.addHelpOption();
    QCommandLineOption ca({"ca-file", "c"}, "Host TLS certificate (PEM) to trust", "path");
    parser.addOption(ca);
    parser.process(app);
    if (parser.isSet(ca)) {
        qputenv("PEERDESK_CA_FILE", parser.value(ca).toUtf8());
    }

    peerdesk::MainWindow w;
    w.show();
    return app.exec();
}
