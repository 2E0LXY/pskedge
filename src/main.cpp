#include "MainWindow.h"
#include "AppConfig.h"
#include "cat/CatController.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QPixmap>
#include <QSplashScreen>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<DecodeLine>("DecodeLine");
    qRegisterMetaType<CatSnapshot>("CatSnapshot");
    qRegisterMetaType<AppConfig>("AppConfig");

    // Optional splash screen shown while the main window constructs. Skipped
    // silently (not an error) if assets/splash.png hasn't been added yet -
    // see assets/README.md for the expected image. The assets directory is
    // copied next to the executable at build time (see CMakeLists.txt) so
    // this path resolves the same way whether running from the build tree
    // or an installed copy.
    QSplashScreen *splash = nullptr;
    const QString splashPath = QDir(QCoreApplication::applicationDirPath()).filePath("assets/splash.png");
    if (QFile::exists(splashPath)) {
        QPixmap splashPixmap(splashPath);
        if (!splashPixmap.isNull()) {
            // The shipped splash.png is a full marketing-size graphic
            // (1672x941) - shown at native resolution it would dominate
            // most screens. Scale to a conventional splash-screen width,
            // preserving aspect ratio, rather than assuming the source
            // image is already splash-sized.
            constexpr int kMaxSplashWidth = 640;
            if (splashPixmap.width() > kMaxSplashWidth) {
                splashPixmap = splashPixmap.scaledToWidth(kMaxSplashWidth, Qt::SmoothTransformation);
            }
            splash = new QSplashScreen(splashPixmap);
            splash->show();
            app.processEvents();
        }
    }

    MainWindow window;
    window.show();

    if (splash) {
        splash->finish(&window);
        delete splash;
    }

    return app.exec();
}
