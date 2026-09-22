// Rebuild icon assets from the SVG master using Qt's renderer.
// Compile with Qt Core, Gui and Svg; no font or external image tools are needed.
#include <QGuiApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QDataStream>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) return 1;
    QDir directory(QString::fromLocal8Bit(argv[1]));
    QSvgRenderer svg(directory.filePath("hellojson.svg"));
    if (!svg.isValid()) return 2;
    const QList<int> sizes{16,20,24,32,40,48,64,128,256,512,1024};
    QList<QByteArray> layers;
    for (int size : sizes) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        svg.render(&painter);
        painter.end();
        if (!image.save(directory.filePath(QString("hellojson-%1.png").arg(size)))) return 3;
        if (size <= 256) {
            QByteArray png;
            QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "PNG")) return 4;
            layers.append(png);
        }
    }
    QFile ico(directory.filePath("hellojson.ico"));
    if (!ico.open(QIODevice::WriteOnly)) return 5;
    QDataStream out(&ico); out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(layers.size());
    quint32 offset = 6 + 16 * layers.size();
    for (int i = 0; i < layers.size(); ++i) {
        quint8 side = sizes[i] == 256 ? 0 : sizes[i];
        out << side << side << quint8(0) << quint8(0) << quint16(1) << quint16(32)
            << quint32(layers[i].size()) << offset;
        offset += layers[i].size();
    }
    for (const auto &png : layers) out.writeRawData(png.constData(), png.size());
    if (out.status() != QDataStream::Ok) return 6;
    // ICNS stores PNG representations in big-endian, length-prefixed chunks.
    QByteArray chunks;
    QDataStream chunkStream(&chunks, QIODevice::WriteOnly);
    chunkStream.setByteOrder(QDataStream::BigEndian);
    const QList<int> macSizes{128, 256, 512, 1024};
    const QList<QByteArray> macTypes{"ic07", "ic08", "ic09", "ic10"};
    for (int i = 0; i < macSizes.size(); ++i) {
        QFile pngFile(directory.filePath(QString("hellojson-%1.png").arg(macSizes[i])));
        if (!pngFile.open(QIODevice::ReadOnly)) return 7;
        const QByteArray png = pngFile.readAll();
        chunkStream.writeRawData(macTypes[i].constData(), 4);
        chunkStream << quint32(8 + png.size());
        chunkStream.writeRawData(png.constData(), png.size());
    }
    QFile icns(directory.filePath("hellojson.icns"));
    if (!icns.open(QIODevice::WriteOnly)) return 8;
    QDataStream macOut(&icns);
    macOut.setByteOrder(QDataStream::BigEndian);
    macOut.writeRawData("icns", 4);
    macOut << quint32(8 + chunks.size());
    macOut.writeRawData(chunks.constData(), chunks.size());
    return macOut.status() == QDataStream::Ok ? 0 : 9;
}
