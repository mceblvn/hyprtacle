/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * A D-Bus client that talks to the screenshot service the same way Spectacle
 * does: it hands over a pipe and reads the raw pixels from it.
 */

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QImage>
#include <QTextStream>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{const auto s_serviceName = u"org.kde.KWin.ScreenShot2"_s;
const auto s_objectPath = u"/org/kde/KWin/ScreenShot2"_s;
const auto s_interfaceName = u"org.kde.KWin.ScreenShot2"_s;

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprtacle-capture"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Takes a screenshot through the hyprtacle D-Bus service."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("mode"), QStringLiteral("screen, active, area or window."));
    parser.addPositionalArgument(QStringLiteral("output"), QStringLiteral("Where to write the PNG file."));
    parser.addPositionalArgument(QStringLiteral("extra"), QStringLiteral("Monitor name for the screen mode, geometry for the area mode."));
    parser.process(application);

    const auto arguments = parser.positionalArguments();
    if (arguments.size() < 2) {
        err() << "Usage: hyprtacle-capture <screen|active|area|window> <output.png> [monitor|x,y WxH]" << Qt::endl;
        return 1;
    }

    const QString mode = arguments.at(0);
    const QString outputPath = arguments.at(1);

    QString method;
    QVariantList methodArguments;

    if (mode == u"screen"_s) {
        method = u"CaptureScreen"_s;
        methodArguments << (arguments.size() > 2 ? arguments.at(2) : QString());
    } else if (mode == u"active"_s) {
        method = u"CaptureActiveScreen"_s;
    } else if (mode == u"window"_s) {
        method = u"CaptureActiveWindow"_s;
    } else if (mode == u"interactive-window"_s) {
        method = u"CaptureInteractive"_s;
        methodArguments << static_cast<uint>(0);
    } else if (mode == u"interactive-screen"_s) {
        method = u"CaptureInteractive"_s;
        methodArguments << static_cast<uint>(1);
    } else if (mode == u"area"_s) {
        if (arguments.size() < 3) {
            err() << "The area mode needs a geometry, for example: 100,100 400x300" << Qt::endl;
            return 1;
        }
        const auto geometry = arguments.at(2).split(QChar(u' '));
        const auto position = geometry.value(0).split(QChar(u','));
        const auto size = geometry.value(1).split(QChar(u'x'));
        if (position.size() != 2 || size.size() != 2) {
            err() << "Could not parse the geometry" << arguments.at(2) << Qt::endl;
            return 1;
        }
        method = u"CaptureArea"_s;
        // The width and the height are unsigned in the interface.
        methodArguments << position.at(0).toInt() //
                        << position.at(1).toInt() //
                        << static_cast<uint>(size.at(0).toInt()) //
                        << static_cast<uint>(size.at(1).toInt());
    } else {
        err() << "Unknown mode" << mode << Qt::endl;
        return 1;
    }

    int pipeFds[2] = {-1, -1};
    if (::pipe(pipeFds) == -1) {
        err() << "Failed to create a pipe" << Qt::endl;
        return 1;
    }

    auto message = QDBusMessage::createMethodCall(s_serviceName, s_objectPath, s_interfaceName, method);
    methodArguments << QVariantMap();
    methodArguments << QVariant::fromValue(QDBusUnixFileDescriptor(pipeFds[1]));
    message.setArguments(methodArguments);
    ::close(pipeFds[1]);

    const QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 30000);
    if (reply.error().isValid()) {
        err() << "The screenshot request failed:" << reply.error().message() << Qt::endl;
        ::close(pipeFds[0]);
        return 1;
    }

    const auto metadata = reply.value();
    const int width = metadata.value(u"width"_s).toInt();
    const int height = metadata.value(u"height"_s).toInt();
    const int format = metadata.value(u"format"_s).toInt();
    const double scale = metadata.value(u"scale"_s).toDouble();

    if (width <= 0 || height <= 0) {
        err() << "The service returned an empty image" << Qt::endl;
        ::close(pipeFds[0]);
        return 1;
    }

    QImage image(width, height, static_cast<QImage::Format>(format));
    if (image.isNull()) {
        err() << "Unsupported image format" << format << Qt::endl;
        ::close(pipeFds[0]);
        return 1;
    }

    QFile file;
    if (!file.open(pipeFds[0], QIODevice::ReadOnly)) {
        err() << "Failed to open the pipe" << Qt::endl;
        return 1;
    }

    // Test hook: behave like a client that walks away right after the reply.
    if (!qEnvironmentVariableIsEmpty("HYPRTACLE_TEST_CLOSE_EARLY")) {
        file.close();
        ::close(pipeFds[0]);
        out() << "Closed the pipe early on purpose" << Qt::endl;
        return 0;
    }

    qint64 read = 0;
    while (read < image.sizeInBytes()) {
        const qint64 bytes = file.read(reinterpret_cast<char *>(image.bits()) + read, image.sizeInBytes() - read);
        if (bytes <= 0) {
            break;
        }
        read += bytes;
    }
    file.close();

    if (read != image.sizeInBytes()) {
        err() << "Expected" << image.sizeInBytes() << "bytes but got" << read << Qt::endl;
        return 1;
    }

    image.setDevicePixelRatio(scale);

    if (!image.save(outputPath, "PNG")) {
        err() << "Failed to write" << outputPath << Qt::endl;
        return 1;
    }

    out() << "Wrote" << outputPath << "(" << width << "x" << height << ", scale" << scale << ")" << Qt::endl;

    return 0;
}
