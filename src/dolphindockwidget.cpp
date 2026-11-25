/*
 * SPDX-FileCopyrightText: 2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphindockwidget.h"
#include <QMainWindow>
#include <KSharedConfig>

#include "dolphinplacesmodelsingleton.h"
#include "kitemviews/kitemlistwidget.h"

#include <QStyleOption>

#include <KFilePlacesModel>
#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

#include <memory>

#include <KConfig>
#include <KConfigGroup>
#include <KWindowEffects>
#include <KIO/Global>
#include <KLocalizedString>
#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>

#include <QDir>
#include <QElapsedTimer>
#include <QtMath>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QHash>
#include <QImageReader>
#include <QMenu>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

namespace
{
QList<DolphinDockWidget::NgDisk> s_ngDisks;
QElapsedTimer s_ngDisksAge;
bool s_ngDisksDirty = true;
}

namespace
{
bool ngGlassPlain()
{
    return DolphinDockWidget::ngGlassPlain();
}
bool ngGlassImageMode()
{
    return DolphinDockWidget::ngGlassImageMode();
}
// dolphin-ng glass settings. DOLPHIN_NG_GLASS=<0-100> fixes the transparency; without it the
// transparency follows the backdrop: DOLPHIN_NG_GLASS_DARK (default 90) over black,
// DOLPHIN_NG_GLASS_LIGHT (default 10) over white, linear in between.
int ngEnvInt(const char *name, int fallback)
{
    return qEnvironmentVariableIsSet(name) ? qBound(0, qEnvironmentVariableIntValue(name), 100) : fallback;
}

// DOLPHIN_NG_GLASS_MODE=image (default): the card shows a static, blurred copy of the wallpaper, placed by the
// window position on screen. DOLPHIN_NG_GLASS_MODE=live: the card is a hole showing the compositor's real blur.

// The image behind the glass: DOLPHIN_NG_GLASS_IMAGE, else the Plasma wallpaper of that screen.
QString ngBackdropPath(const QScreen *screen)
{
    const QString custom = ngSetting("GLASS_IMAGE");
    if (!custom.isEmpty()) {
        return custom;
    }
    // Plasma numbers its screens with the primary one first
    int screenId = 0;
    if (screen != QGuiApplication::primaryScreen()) {
        screenId = 1;
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (const QScreen *other : screens) {
            if (other == screen) {
                break;
            }
            if (other != QGuiApplication::primaryScreen()) {
                ++screenId;
            }
        }
    }
    KConfig config(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"), KConfig::SimpleConfig);
    const KConfigGroup containments(&config, QStringLiteral("Containments"));
    QString fallback;
    const QStringList names = containments.groupList();
    for (const QString &name : names) {
        const KConfigGroup containment = containments.group(name);
        // the image is stored under the name of the wallpaper plugin in use (org.kde.image, or a third-party one)
        const QString plugin = containment.readEntry("wallpaperplugin", QStringLiteral("org.kde.image"));
        QString image = containment.group(QStringLiteral("Wallpaper")).group(plugin).group(QStringLiteral("General")).readEntry("Image");
        if (image.isEmpty()) {
            continue;
        }
        if (image.startsWith(QLatin1String("file://"))) {
            image = QUrl(image).toLocalFile();
        }
        if (containment.readEntry("lastScreen", -1) == screenId) {
            return image;
        }
        if (fallback.isEmpty()) {
            fallback = image;
        }
    }
    return fallback;
}

// GLASS_MODE lava: is the card an animated "lava lamp"? Read every time, so the settings page applies at once.
bool ngGlassLavaMode()
{
    return ngSetting("GLASS_MODE").toLower() == QLatin1String("lava");
}

// One frame of the lava lamp, drawn small and painted scaled up, which blurs it. The blobs live in screen
// coordinates, as if a slowly moving picture lay behind the whole desktop: `region` is the part of the screen
// behind the card, so moving the window slides the blobs under it. `seconds` is the animation time.
QImage ngLavaFrame(const QSize &size, const QList<QColor> &colours, const QColor &base, qreal seconds, const QRectF &region, const QSizeF &screen)
{
    // LAVA_SIZE 10-100: how large the blobs are (default 55)
    const qreal sizeFactor = 0.30 + qBound(10, ngSettingInt("LAVA_SIZE", 55), 100) / 100.0 * 0.60;
    QImage frame(size, QImage::Format_ARGB32_Premultiplied);
    frame.fill(base);
    if (colours.isEmpty() || region.isEmpty() || screen.isEmpty()) {
        return frame;
    }
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    // map screen coordinates to the frame
    painter.scale(size.width() / region.width(), size.height() / region.height());
    painter.translate(-region.topLeft());
    // The blobs sit on a grid that covers the whole screen (and a margin around it), each one drifting around its
    // own grid point. The cells are small compared with the panel, so wherever the window is there are always
    // several blobs behind it and never an empty patch. Only the blobs that reach the region are drawn.
    const qreal cell = screen.height() * 0.24;
    const qreal radiusBase = cell * 1.25 * (sizeFactor / 0.63);
    const int firstColumn = qFloor((region.left() - radiusBase * 1.3) / cell), lastColumn = qCeil((region.right() + radiusBase * 1.3) / cell);
    const int firstRow = qFloor((region.top() - radiusBase * 1.3) / cell), lastRow = qCeil((region.bottom() + radiusBase * 1.3) / cell);
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const int i = row * 7 + column * 13; // a different phase, period and colour for every grid point
            const int k = ((i % 1000) + 1000) % 1000;
            const qreal periodX = 70.0 + 23.0 * (k % 5), periodY = 95.0 + 31.0 * ((k * 3) % 5), periodR = 60.0 + 17.0 * (k % 4);
            const QPointF centre(cell * (column + 0.5 + 0.42 * qSin(2 * M_PI * seconds / periodX + k * 1.7)),
                                 cell * (row + 0.5 + 0.42 * qCos(2 * M_PI * seconds / periodY + k * 2.3)));
            const qreal radius = radiusBase * (1.0 + 0.18 * qSin(2 * M_PI * seconds / periodR + k));
            QColor inner = colours.at(((row * 2 + column) % colours.size() + colours.size()) % colours.size()), mid = inner, outer = inner;
            inner.setAlphaF(0.88);
            mid.setAlphaF(0.40);
            outer.setAlphaF(0.0);
            QRadialGradient blob(centre, radius);
            blob.setColorAt(0.0, inner);
            blob.setColorAt(0.55, mid);
            blob.setColorAt(1.0, outer);
            painter.setBrush(blob);
            painter.drawEllipse(centre, radius, radius);
        }
    }
    return frame;
}

// The other animated backdrop (LAVA_STYLE=flow, the default): a gradient that flows. Every point takes a colour
// between the palette colours from a few very slow waves crossing the screen, so the panel is always completely
// filled, wherever the window is. Like the blobs it lives in screen coordinates and is drawn small.
QImage ngFlowFrame(const QSize &size, const QList<QColor> &colours, qreal seconds, const QRectF &region, const QSizeF &screen)
{
    QImage frame(size, QImage::Format_RGB32);
    if (colours.isEmpty() || region.isEmpty() || screen.isEmpty()) {
        frame.fill(Qt::black);
        return frame;
    }
    // LAVA_SIZE 10-100: how broad the colour fields are (default 55)
    const qreal scale = 0.63 / (0.30 + qBound(10, ngSettingInt("LAVA_SIZE", 55), 100) / 100.0 * 0.60);
    const qreal unit = screen.height();
    const int last = colours.size() - 1;
    for (int y = 0; y < size.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
        const qreal v = (region.top() + (y + 0.5) / size.height() * region.height()) / unit * scale;
        for (int x = 0; x < size.width(); ++x) {
            const qreal u = (region.left() + (x + 0.5) / size.width() * region.width()) / unit * scale;
            // four waves with different directions and periods of one and a half to three minutes
            const qreal wave = 0.30 * qSin(2 * M_PI * (u * 0.9 + seconds / 97.0)) + 0.28 * qSin(2 * M_PI * (v * 0.7 - seconds / 131.0) + 1.3)
                + 0.24 * qSin(2 * M_PI * ((u + v) * 0.6 + seconds / 173.0)) + 0.18 * qSin(2 * M_PI * ((u - v) * 1.1 - seconds / 113.0) + 0.7);
            const qreal position = qBound(0.0, 0.5 + 0.5 * wave / 0.72, 1.0) * last;
            const int index = qMin(int(position), qMax(0, last - 1));
            const qreal t = last > 0 ? position - index : 0.0;
            const QColor &from = colours.at(index), &to = colours.at(qMin(index + 1, last));
            line[x] = qRgb(qRound(from.red() + (to.red() - from.red()) * t), qRound(from.green() + (to.green() - from.green()) * t),
                           qRound(from.blue() + (to.blue() - from.blue()) * t));
        }
    }
    return frame;
}

// The colours of the lava. LAVA_COLORS is empty for the default set (the teal, amber and indigo glows of the
// design mockup), "wallpaper" to take them from the blurred backdrop picture, or a comma-separated list of
// colours such as "#2f6f8f,#8a5a2c,#3c4a78".
QList<QColor> ngLavaColours(const QPixmap &backdrop, QColor *base)
{
    *base = QColor(16, 18, 22);
    const QString spec = ngSetting("LAVA_COLORS").trimmed().toLower();
    if (spec == QLatin1String("wallpaper") && !backdrop.isNull()) {
        QList<QColor> colours;
        const QImage image = backdrop.toImage();
        static const QPointF spots[] = {{0.15, 0.2}, {0.8, 0.25}, {0.5, 0.55}, {0.2, 0.85}, {0.85, 0.8}};
        qreal red = 0, green = 0, blue = 0;
        for (const QPointF &spot : spots) {
            const QColor pixel = image.pixelColor(qRound(spot.x() * (image.width() - 1)), qRound(spot.y() * (image.height() - 1)));
            red += pixel.redF();
            green += pixel.greenF();
            blue += pixel.blueF();
            float hue, saturation, value;
            pixel.getHsvF(&hue, &saturation, &value);
            colours.append(QColor::fromHsvF(hue < 0 ? 0 : hue, qMin(1.0f, saturation * 1.3f + 0.08f), qBound(0.45f, value * 1.1f, 0.92f)));
        }
        *base = QColor::fromRgbF(red / 5 * 0.45, green / 5 * 0.45, blue / 5 * 0.45);
        return colours;
    }
    QList<QColor> colours;
    const QStringList names = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &name : names) {
        const QColor colour(name.trimmed());
        if (colour.isValid()) {
            colours.append(colour);
        }
    }
    if (colours.isEmpty()) {
        colours = {QColor(0x2f, 0x6f, 0x8f), QColor(0x8a, 0x5a, 0x2c), QColor(0x3c, 0x4a, 0x78)};
    }
    // the ground under the blobs: a dark blend of their colours, so that no gap ever shows a flat grey
    qreal red = 0, green = 0, blue = 0;
    for (const QColor &colour : std::as_const(colours)) {
        red += colour.redF();
        green += colour.greenF();
        blue += colour.blueF();
    }
    *base = QColor::fromRgbF(red / colours.size() * 0.55, green / colours.size() * 0.55, blue / colours.size() * 0.55);
    return colours;
}

// A blurred copy of the backdrop for `screen`, at a quarter of the screen resolution (it is drawn scaled up).
// Null if there is no usable image.
QPixmap ngBackdrop(const QScreen *screen)
{
    static QHash<QString, QPixmap> cache;
    const QSize screenSize = screen->geometry().size();
    const int blur = qBound(0, ngSettingInt("GLASS_BLUR", 45), 100); // how soft the backdrop is
    const QString path = ngBackdropPath(screen);
    const QString key = screen->name() + QLatin1Char(':') + QString::number(screenSize.width()) + QLatin1Char('x') + QString::number(screenSize.height())
        + QLatin1Char(':') + QString::number(blur) + QLatin1Char(':') + path;
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd()) {
        return *cached;
    }

    QPixmap result;
    if (!path.isEmpty()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // the blur comes from resampling through a very small image: the stronger the blur, the smaller that image
        const QSize tiny = (screenSize / (12 + blur * 2)).expandedTo(QSize(4, 4));
        const QSize original = reader.size();
        if (original.isValid() && !tiny.isEmpty()) {
            reader.setScaledSize(original.scaled(tiny, Qt::KeepAspectRatioByExpanding)); // fill the screen, crop the excess
        }
        QImage image = reader.read();
        if (!image.isNull() && !tiny.isEmpty()) {
            QRect crop(QPoint(0, 0), tiny);
            crop.moveCenter(image.rect().center());
            image = image.copy(crop.intersected(image.rect()));
            image = image.scaled(tiny * 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            image = image.scaled(screenSize / 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            result = QPixmap::fromImage(image);
        }
    }
    cache.insert(key, result);
    return result;
}
}
#include <QStyle>

namespace
{
// Disable the 'Floatable' feature, i.e., the possibility to drag the
// dock widget out of the main window. This works around problems like
// https://bugs.kde.org/show_bug.cgi?id=288629
// https://bugs.kde.org/show_bug.cgi?id=322299
const QDockWidget::DockWidgetFeatures DefaultDockWidgetFeatures = QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable;
}

// Empty titlebar for the dock widgets when "Lock Layout" has been activated.
class DolphinDockTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit DolphinDockTitleBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }
    ~DolphinDockTitleBar() override = default;

    QSize minimumSizeHint() const override
    {
        return QSize(0, 0);
    }

    QSize sizeHint() const override
    {
        return minimumSizeHint();
    }
};

DolphinDockWidget::DolphinDockWidget(const QString &title, QWidget *parent, Qt::WindowFlags flags)
    : QDockWidget(title, parent, flags)
    , m_locked(false)
    , m_dockTitleBar(nullptr)
{
    setFeatures(DefaultDockWidgetFeatures);

    startTimer(30000); // disk usage box
    {
        KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
        auto repaint = [this] {
            s_ngDisksDirty = true;
            update();
        };
        connect(places, &QAbstractItemModel::rowsInserted, this, repaint);
        connect(places, &QAbstractItemModel::rowsRemoved, this, repaint);
        connect(places, &QAbstractItemModel::dataChanged, this, repaint);
        connect(places, &KFilePlacesModel::reloaded, this, repaint);
    }
    ngApplySettings();
    connect(NgSettings::instance(), &NgSettings::changed, this, &DolphinDockWidget::ngApplySettings);
}

void DolphinDockWidget::ngApplySettings()
{
    // dolphin-ng: in live mode the transparency follows the backdrop, unless a fixed one was requested
    const bool pollBackdrop = !ngGlassPlain() && !ngGlassImageMode() && !qEnvironmentVariableIsSet("DOLPHIN_NG_GLASS") && KWindowSystem::isPlatformX11();
    if (ngGlassImageMode()) {
        m_glass = ngEnvInt("DOLPHIN_NG_GLASS", 55); // how much of the image shows through the window tint
    } else if (qEnvironmentVariableIsSet("DOLPHIN_NG_GLASS")) {
        m_glass = ngEnvInt("DOLPHIN_NG_GLASS", 75);
    } else {
        // the desktop seen through the card: three quarters of it under a dark scheme; under a light one less, or a
        // dark wallpaper turns the card a muddy grey under black text (Wayland gives no way to look at the backdrop)
        m_glass = ngIsDark(palette()) ? 75 : 30;
    }
    if (pollBackdrop && !m_glassTimer) {
        m_glassTimer = new QTimer(this);
        // DOLPHIN_NG_GLASS_MS: polling interval of the backdrop, milliseconds (default 150)
        m_glassTimer->setInterval(qEnvironmentVariableIsSet("DOLPHIN_NG_GLASS_MS") ? qMax(50, qEnvironmentVariableIntValue("DOLPHIN_NG_GLASS_MS")) : 900);
        connect(m_glassTimer, &QTimer::timeout, this, &DolphinDockWidget::updateGlass);
    }
    if (m_glassTimer) {
        if (pollBackdrop) {
            m_glassTimer->start();
        } else {
            m_glassTimer->stop();
        }
    }
    if (m_lavaTimer) {
        m_lavaTimer->stop(); // paintNgGlass() restarts it when the backdrop still animates
    }
    update();
}

qreal DolphinDockWidget::backdropLuminance() const
{
    const QWidget *top = window();
    QScreen *screen = top ? top->screen() : nullptr;
    if (!screen) {
        return -1;
    }
    const WId self = top->winId();
    const QList<WId> stack = KX11Extras::stackingOrder(); // bottom to top
    const int selfIndex = stack.indexOf(self);
    if (selfIndex <= 0) {
        return -1;
    }

    // the glass card in native screen pixels
    const qreal dpr = devicePixelRatioF();
    const QRect card = QRect(mapToGlobal(QPoint(6, 6)) * dpr, (size() - QSize(12, 12)) * dpr);
    if (card.width() < 24 || card.height() < 24) {
        return -1;
    }

    // windows below ours that are visible and overlap the card, topmost first
    struct Below {
        WId id;
        QRect geometry;
    };
    QList<Below> below;
    for (int i = selfIndex - 1; i >= 0; --i) {
        const KWindowInfo info(stack.at(i), NET::WMGeometry | NET::WMDesktop | NET::WMState | NET::XAWMState);
        if (!info.valid() || info.isMinimized() || !info.isOnCurrentDesktop()) {
            continue;
        }
        if (info.geometry().intersects(card)) {
            below.append({stack.at(i), info.geometry()});
        }
    }
    if (below.isEmpty()) {
        return -1;
    }

    // sample a 2 x 4 grid of small patches; each patch comes from the topmost window below ours at that point
    // (every patch is a round trip to the X server, on the thread that also draws the files)
    const int patch = 6;
    qreal sum = 0;
    int count = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 2; ++col) {
            const QPoint point(card.left() + card.width() * (2 * col + 1) / 4, card.top() + card.height() * (2 * row + 1) / 8);
            for (const Below &window : std::as_const(below)) {
                if (!window.geometry.adjusted(0, 0, -patch, -patch).contains(point)) {
                    continue;
                }
                const QImage image = screen->grabWindow(window.id, point.x() - window.geometry.x(), point.y() - window.geometry.y(), patch, patch)
                                         .toImage()
                                         .convertToFormat(QImage::Format_RGB32);
                if (!image.isNull()) {
                    qreal luminance = 0;
                    for (int y = 0; y < image.height(); ++y) {
                        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                        for (int x = 0; x < image.width(); ++x) {
                            luminance += 0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) + 0.0722 * qBlue(line[x]);
                        }
                    }
                    sum += luminance / (255.0 * image.width() * image.height());
                    ++count;
                }
                break;
            }
        }
    }
    return count > 0 ? sum / count : -1;
}

void DolphinDockWidget::updateGlass()
{
    if (!isVisible() || isFloating() || !window()->isVisible() || window()->isMinimized()) {
        return;
    }
    const qreal luminance = backdropLuminance();
    if (luminance < 0) {
        return;
    }
    static const int dark = ngEnvInt("DOLPHIN_NG_GLASS_DARK", 90);
    static const int light = ngEnvInt("DOLPHIN_NG_GLASS_LIGHT", 10);
    const qreal target = dark + (light - dark) * luminance;
    const qreal next = qAbs(target - m_glass) < 1.5 ? target : m_glass + (target - m_glass) * 0.45; // ease towards the target
    static const bool debug = qEnvironmentVariableIsSet("DOLPHIN_NG_GLASS_DEBUG");
    if (debug) {
        qWarning("dolphin-ng glass [%s]: backdrop luminance %.2f -> target %.0f%%, now %.0f%%", qPrintable(windowTitle()), luminance, target, next);
    }
    if (!qFuzzyCompare(next, m_glass)) {
        m_glass = next;
        update();
    }
    // look often only while the transparency is on its way to a new value; otherwise about once a second
    if (m_glassTimer && !qEnvironmentVariableIsSet("DOLPHIN_NG_GLASS_MS")) {
        const int wanted = qFuzzyCompare(next, target) ? 900 : 120;
        if (m_glassTimer->interval() != wanted) {
            m_glassTimer->setInterval(wanted);
        }
    }
}

DolphinDockWidget::~DolphinDockWidget() = default;

void DolphinDockWidget::ngPaintWindowGround(QPainter &painter, QWidget *from, const QRect &rect)
{
    QWidget *top = from->window();
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    if (ngAurora()) {
        painter.translate(-from->mapTo(top, QPoint(0, 0)));
        QStyleOption option;
        option.initFrom(top);
        top->style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, top);
    } else if (!ngWidgetStyleIsNg()) {
        // (the window's gradient, laid where it is in the window)
        painter.translate(-from->mapTo(top, QPoint(0, 0)));
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        ngPaintWindowGradient(&painter, QRectF(top->rect()), top->palette());
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect, top->palette().color(QPalette::Window));
    }
    painter.restore();
}

bool DolphinDockWidget::ngGlassPlain()
{
    const bool plain = ngSetting("SIDEBAR").toLower() == QLatin1String("plain");
    return plain;
}

bool DolphinDockWidget::ngGlassImageMode()
{
    const bool live = ngSetting("GLASS_MODE").toLower() == QLatin1String("live");
    // no blur from the compositor (its effect off): the live card was a hole showing the desktop sharp behind the
    // window; the blurred picture of the wallpaper instead. (Set by the main window, or by Breeze NG, a moment after
    // start: KWin announces its blur only once the application runs)
    return !live || ngBareGlass();
}

bool DolphinDockWidget::ngBareGlass()
{
    return qApp->property("_breeze_ng_bare_glass").toBool();
}

void DolphinDockWidget::setLocked(bool lock)
{
    if (lock != m_locked) {
        m_locked = lock;

        if (lock) {
            if (!m_dockTitleBar) {
                m_dockTitleBar = new DolphinDockTitleBar(this);
            }
            setTitleBarWidget(m_dockTitleBar);
            setFeatures(QDockWidget::DockWidgetClosable);
        } else if (!m_ngBare) {
            setTitleBarWidget(nullptr);
            setFeatures(DefaultDockWidgetFeatures);
        }
    }
}

void DolphinDockWidget::ngSetBare()
{
    // the stock title bar of an unlocked panel puts its float and close buttons over the glass card, where a close
    // was easy to hit by mistake: none here; the toolbar's sidebar button (and F9) show and hide the panel
    m_ngBare = true;
    if (!m_dockTitleBar) {
        m_dockTitleBar = new DolphinDockTitleBar(this);
    }
    setTitleBarWidget(m_dockTitleBar);
    setFeatures(QDockWidget::DockWidgetClosable);
}

bool DolphinDockWidget::isLocked() const
{
    return m_locked;
}

// dolphin-ng: draw the docked panel as a floating "glass" card.
// DOLPHIN_NG_SIDEBAR=plain restores the stock look.
void DolphinDockWidget::paintEvent(QPaintEvent *event)
{
    QDockWidget::paintEvent(event);
    QPainter painter(this);
    paintNgGlass(painter);
}

QRectF DolphinDockWidget::ngCardRect() const
{
    const QRectF inset = QRectF(rect()).adjusted(6, 6, -6, -6);
    auto *window = qobject_cast<QMainWindow *>(parentWidget());
    if (!ngPanelsFlush() || !window || isFloating()) {
        return inset;
    }
    // past the edges by more than the corners' radius: the round corners there fall outside the panel, the card meets
    // the window's side and foot (the window's own rounded corner cuts the last one)
    const qreal past = 16;
    switch (window->dockWidgetArea(const_cast<DolphinDockWidget *>(this))) {
    case Qt::LeftDockWidgetArea:
        return QRectF(rect()).adjusted(-past, 6, -6, past);
    case Qt::RightDockWidgetArea:
        return QRectF(rect()).adjusted(6, 6, past, past);
    default:
        return inset;
    }
}

bool DolphinDockWidget::paintNgGlass(QPainter &painter)
{
    if (ngGlassPlain() || isFloating()) {
        return false;
    }
    bool opaque = true;
    bool frosted = false; // the card shows the wallpaper (a picture of it, or the live blur)
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = ngCardRect();
    const qreal radius = 12;

    painter.setPen(Qt::NoPen);
    // with another widget style than Breeze NG the panel has no card: it stands on the window's gradient (painted by
    // the window, and by the panels where they paint their ground again); only the disks are drawn here
    const bool noCard = !ngWidgetStyleIsNg();
    const QScreen *currentScreen = noCard ? nullptr : screen();
    const QPixmap backdrop = (ngGlassImageMode() && currentScreen) ? ngBackdrop(currentScreen) : QPixmap();
    const bool lava = !noCard && ngGlassImageMode() && ngGlassLavaMode();
    if (!noCard) { // (the card: closed before the disks)
    if (lava) {
        // animated backdrop: about twelve small frames a second, only while this window is the active one
        if (!m_lavaTimer) {
            m_lavaTimer = new QTimer(this);
            m_lavaTimer->setInterval(80);
            connect(m_lavaTimer, &QTimer::timeout, this, [this] {
                if (isVisible() && window()->isActiveWindow()) {
                    update();
                }
            });
        }
        if (!m_lavaTimer->isActive()) {
            m_lavaTimer->start();
        }
        static QElapsedTimer clock;
        if (!clock.isValid()) {
            clock.start();
        }
        const qreal speed = qBound(0, ngSettingInt("LAVA_SPEED", 30), 100) / 30.0;
        QColor base;
        const QList<QColor> colours = ngLavaColours(backdrop, &base);
        if (!ngIsDark(palette())) {
            base = base.lighter(520); // a pale ground under the blobs on a light scheme
        }
        const QRectF onScreen = currentScreen ? QRectF(QPointF(mapToGlobal(card.topLeft().toPoint()) - currentScreen->geometry().topLeft()), card.size()) : card;
        const QSizeF screenSize = currentScreen ? QSizeF(currentScreen->geometry().size()) : card.size();
        const QSize frameSize = (card.size() / 8).toSize().expandedTo(QSize(8, 8));
        const qreal seconds = clock.elapsed() / 1000.0 * speed;
        const QImage frame = ngSetting("LAVA_STYLE").toLower() == QLatin1String("blobs") ? ngLavaFrame(frameSize, colours, base, seconds, onScreen, screenSize)
                                                                                         : ngFlowFrame(frameSize, colours, seconds, onScreen, screenSize);
        QPainterPath clip;
        clip.addRoundedRect(card, radius, radius);
        painter.save();
        painter.setClipPath(clip, Qt::IntersectClip); // keep a clip set by the caller (the Places titles)
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(card, frame);
        frosted = true;
        QColor tint = palette().color(QPalette::Window);
        tint.setAlphaF(1.0 - qBound(0, ngSettingInt("GLASS", 55), 100) / 100.0);
        painter.fillRect(card, tint);
        painter.restore();
    } else if (m_lavaTimer && m_lavaTimer->isActive() && ngSetting("GLASS_MOTION").toLower() != QLatin1String("drift")) {
        m_lavaTimer->stop();
    }
    if (lava) {
        // drawn above
    } else if (!backdrop.isNull()) {
        // static image: the part of the blurred wallpaper that lies behind the card on screen, under a window-coloured tint
        QPainterPath clip;
        clip.addRoundedRect(card, radius, radius);
        painter.save();
        painter.setClipPath(clip, Qt::IntersectClip); // keep a clip set by the caller (the Places titles)
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPointF onScreen = QPointF(mapToGlobal(card.topLeft().toPoint()) - currentScreen->geometry().topLeft());
        const qreal factor = qreal(backdrop.width()) / currentScreen->geometry().width();
        QRectF source(onScreen * factor, card.size() * factor);
        if (ngSetting("GLASS_MOTION").toLower() == QLatin1String("drift")) {
            // GLASS_MOTION=drift: the picture breathes (zooms in and out a little) and drifts, very slowly
            if (!m_lavaTimer) {
                m_lavaTimer = new QTimer(this);
                m_lavaTimer->setInterval(80);
                connect(m_lavaTimer, &QTimer::timeout, this, [this] {
                    if (isVisible() && !window()->isMinimized()) {
                        update();
                    }
                });
            }
            if (!m_lavaTimer->isActive()) {
                m_lavaTimer->start();
            }
            static QElapsedTimer motionClock;
            if (!motionClock.isValid()) {
                motionClock.start();
            }
            // LAVA_SPEED (the "animation speed" of the settings) scales the clock: at 30 a full breath takes about a minute
            const qreal seconds = motionClock.elapsed() / 1000.0 * (qBound(5, ngSettingInt("LAVA_SPEED", 30), 100) / 30.0);
            const qreal zoom = 1.10 + 0.08 * qSin(2 * M_PI * seconds / 60.0); // between 1.02 and 1.18
            const QPointF drift(backdrop.width() * 0.04 * qSin(2 * M_PI * seconds / 83.0), backdrop.height() * 0.04 * qCos(2 * M_PI * seconds / 101.0));
            const QPointF centre = source.center() + drift;
            source = QRectF(QPointF(), source.size() / zoom);
            source.moveCenter(centre);
            // stay inside the picture
            source.moveLeft(qBound(0.0, source.left(), backdrop.width() - source.width()));
            source.moveTop(qBound(0.0, source.top(), backdrop.height() - source.height()));
        } else if (m_lavaTimer && m_lavaTimer->isActive() && !lava) {
            m_lavaTimer->stop();
        }
        painter.drawPixmap(card, backdrop, source);
        frosted = true;
        QColor tint = palette().color(QPalette::Window);
        tint.setAlphaF(1.0 - qBound(0, ngSettingInt("GLASS", 55), 100) / 100.0); // how much of the picture shows
        painter.fillRect(card, tint);
        painter.restore();
    } else if (ngSetting("SIDEBAR").toLower() == QLatin1String("card")
               || KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString()) == QLatin1String("Breeze NG Soft")) {
        // an opaque look (Breeze NG Soft, with any colours; or SIDEBAR=card): the card is opaque too, not a hole
        // onto the blurred desktop in a window that is otherwise solid. The colour of fields (white on a light
        // scheme, a tone lighter than the window on a dark one) on the window's tone
        const QColor window = palette().color(QPalette::Window);
        painter.setBrush(ngIsDark(palette()) ? window.lighter(118) : palette().color(QPalette::Base).lightnessF() > window.lightnessF() + 0.03 ? palette().color(QPalette::Base) : QColor(253, 253, 254));
        painter.drawRoundedRect(card, radius, radius);
    } else if (ngAurora()) {
        // Breeze NG Aurora: the whole window is already glass; the card is a faint veil of ink on it, not a hole
        opaque = false;
        // the sidebar's own opacity (Breeze NG's setting, -1: the window's): the window's glass under the card is
        // thinned to it (or thickened, with the window's colour) before the veil
        {
            const KConfigGroup common = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common"));
            const int side = common.readEntry("AuroraSidebarOpacity", -1);
            if (side >= 0) {
                const QString theme = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString());
                const bool lightGlass = theme.endsWith(QLatin1String("Light"), Qt::CaseInsensitive) && ngIsDark(palette());
                const qreal window = qBound(10, common.readEntry("AuroraBodyOpacity", 60) - (lightGlass ? 22 : 0), 100) / 100.0;
                const qreal wanted = qBound(0, side, 100) / 100.0;
                painter.save();
                if (wanted < window) {
                    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                    painter.setBrush(QColor::fromRgbF(0, 0, 0, 1.0 - wanted / window));
                } else {
                    QColor thicker = palette().color(QPalette::Window);
                    thicker.setAlphaF((wanted - window) / (1.0 - window + 1e-6));
                    painter.setBrush(thicker);
                }
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(card, radius, radius);
                painter.restore();
            }
        }
        painter.setBrush(ngAuroraVeil(ngIsDark(palette())));
        if (!ngIsDark(palette())) {
            // as the cards of Breeze NG's windows (Helper::renderAuroraCard): the frost a little denser at the top and
            // thinner at the bottom
            const QColor frost = ngAuroraVeil(false);
            QColor top = frost, bottom = frost;
            top.setAlphaF(qMin(1.0, frost.alphaF() * 1.18));
            bottom.setAlphaF(frost.alphaF() * 0.70);
            QLinearGradient sheet(card.topLeft(), card.bottomLeft());
            sheet.setColorAt(0.0, top);
            sheet.setColorAt(1.0, bottom);
            painter.setBrush(sheet);
            // (one hairline, as those cards have: Helper::auroraCardEdge. The double edge seen before was Breeze NG's own
            // card under this one, now left out for Dolphin NG's panels)
            painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
            painter.drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            painter.setPen(Qt::NoPen);
        } else {
            painter.drawRoundedRect(card, radius, radius);
        }
    } else if (KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind)) {
        // live blur: thin out the window tint under the card, so the compositor's blur shows through
        opaque = false;
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.setBrush(QColor(0, 0, 0, qRound(m_glass * 255 / 100)));
        painter.drawRoundedRect(card, radius, radius);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        frosted = true;
    } else {
        // live blur without a blur effect in the compositor: a hole would show the windows behind sharp, text over
        // text. The card stays a tinted panel of the window's colour until a blur is there
        QColor tint = palette().color(QPalette::Window);
        tint = ngIsDark(palette()) ? tint.lighter(118) : tint.darker(104);
        painter.setBrush(tint);
        painter.drawRoundedRect(card, radius, radius);
    }

    if (frosted && !ngAurora() && !ngIsDark(palette())) {
        // a light scheme: frost over the wallpaper, so the card is a light glass about as bright as the window, not a
        // grey slab under black text (a dark wallpaper, as Kubuntu's, greyed it)
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(card, radius, radius);
        painter.setClipPath(clip, Qt::IntersectClip);
        painter.fillRect(card, QColor(255, 255, 255, qRound(ngEnvInt("DOLPHIN_NG_FROST", 55) * 255 / 100.0)));
        painter.restore();
    }
    if (frosted && !ngAurora() && ngIsDark(palette())) {
        // a dark scheme: smoke of the window's colour over the wallpaper, as the frost on a light one, so that light
        // text reads on the glass over a light wallpaper or window behind (it was white on a pale blue; issue #9).
        // DOLPHIN_NG_SMOKE: how dense, in percent
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(card, radius, radius);
        painter.setClipPath(clip, Qt::IntersectClip);
        QColor smoke = palette().color(QPalette::Window);
        smoke.setAlphaF(qBound(0, ngEnvInt("DOLPHIN_NG_SMOKE", 45), 100) / 100.0);
        painter.fillRect(card, smoke);
        painter.restore();
    }
    if (!ngAurora()) {
        // light veil, a little stronger at the top
        QLinearGradient veil(card.topLeft(), card.bottomLeft());
        veil.setColorAt(0.0, QColor(255, 255, 255, 24));
        veil.setColorAt(0.2, QColor(255, 255, 255, 10));
        veil.setColorAt(1.0, QColor(255, 255, 255, 6));
        painter.setBrush(veil);
        painter.drawRoundedRect(card, radius, radius);
    }

    // rim around the card. GLASS_RIM: none (default) | diagonal | soft | bevel | glow | top
    // (Breeze NG Aurora: glass without rims, the card has none whatever the setting)
    const QString rimStyle = ngAurora() ? QString() : ngSetting("GLASS_RIM").toLower() == QLatin1String("show") ? QStringLiteral("diagonal") : ngSetting("GLASS_RIM").toLower();
    const bool dark = ngIsDark(palette());
    const QRectF edge = card.adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(Qt::NoBrush);
    if (rimStyle == QLatin1String("diagonal")) {
        // bright towards the top-left, fading along the diagonal, a faint return at the bottom-right
        QLinearGradient rim(card.topLeft(), card.bottomRight());
        if (dark) {
            rim.setColorAt(0.0, QColor(255, 255, 255, 105));
            rim.setColorAt(0.45, QColor(255, 255, 255, 22));
            rim.setColorAt(1.0, QColor(255, 255, 255, 55));
        } else {
            rim.setColorAt(0.0, QColor(255, 255, 255, 230));
            rim.setColorAt(0.45, QColor(0, 0, 0, 26));
            rim.setColorAt(1.0, QColor(0, 0, 0, 48));
        }
        painter.setPen(QPen(QBrush(rim), 1));
        painter.drawRoundedRect(edge, radius, radius);
    } else if (rimStyle == QLatin1String("soft")) {
        // one even, faint hairline
        painter.setPen(QPen(dark ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 34), 1));
        painter.drawRoundedRect(edge, radius, radius);
    } else if (rimStyle == QLatin1String("bevel")) {
        // glass edge: a dark outer line with a light line just inside it
        painter.setPen(QPen(QColor(0, 0, 0, dark ? 70 : 40), 1));
        painter.drawRoundedRect(edge, radius, radius);
        painter.setPen(QPen(QColor(255, 255, 255, dark ? 48 : 150), 1));
        painter.drawRoundedRect(edge.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
    } else if (rimStyle == QLatin1String("glow")) {
        // a soft halo fading inwards over a few pixels, no hard line
        for (int i = 0; i < 4; ++i) {
            const int alpha = dark ? 34 - i * 8 : 30 - i * 7;
            painter.setPen(QPen(dark ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha), 1));
            painter.drawRoundedRect(edge.adjusted(i, i, -i, -i), radius - i, radius - i);
        }
    } else if (rimStyle == QLatin1String("top")) {
        // light from above: a highlight on the top edge that dies out down the sides
        QLinearGradient rim(card.topLeft(), card.bottomLeft());
        rim.setColorAt(0.0, QColor(255, 255, 255, dark ? 120 : 240));
        rim.setColorAt(0.35, QColor(255, 255, 255, dark ? 20 : 60));
        rim.setColorAt(1.0, Qt::transparent);
        painter.setPen(QPen(QBrush(rim), 1));
        painter.drawRoundedRect(edge, radius, radius);
    }

    } // !noCard

    // the disks at the foot of the Places card (PlacesPanel leaves the room for them): the home file system, then the
    // other disks and partitions, each a box with its usage; an unmounted one is a dashed box
    m_ngDiskBoxes.clear();
    if (objectName() == QLatin1String("placesDock")) {
        // (laid out on the card as seen: with PANELS_EDGE=flush the card runs past the window's side and foot, and the
        // boxes stood against the window's edge)
        const QRectF seen = card.intersected(QRectF(rect()).adjusted(ngPanelsFlush() ? 6 : 0, 0, 0, ngPanelsFlush() ? -6 : 0));
        const QList<NgDisk> disks = ngSidebarDisks();
        const int room = ngDiskRoom();
        if (room > 0 && !disks.isEmpty() && seen.height() > 200 + room - 62) {
            const bool compact = disks.size() > 3;
            const bool alone = disks.size() == 1;
            const bool darkGlass = ngIsDark(palette());
            // on glass the names and the free space get the text shadow of the rest of the sidebar (Breeze NG's
            // "Shadow around text on glass"): they are painted here, outside the panel that carries it
            const bool halo = ngSidebarHalo();
            auto drawText = [&painter, halo](const QRectF &rect, int flags, const QString &text) {
                if (halo) {
                    ngPaintTextHalo(&painter, painter.pen().color(), [&](const QPointF &offset) {
                        painter.drawText(rect.translated(offset), flags, text);
                    });
                }
                painter.drawText(rect, flags, text);
            };
            QFont small = font();
            small.setPointSizeF(small.pointSizeF() * (compact ? 0.82 : 0.86));
            painter.setFont(small);
            const QFontMetrics metrics(small);
            // where the panel gave them room (its sections' order), in this widget's coordinates
            const QVariant disksY = widget() ? widget()->property("ngDisksY") : QVariant();
            qreal top = disksY.isValid() && disksY.toInt() >= 0 ? widget()->mapTo(this, QPoint(0, disksY.toInt())).y() : seen.bottom() - room;
            for (const NgDisk &disk : disks) {
                const bool device = !disk.url.isEmpty(); // not a disk: one line, its kind on the right, no usage
                const bool mounted = !disk.root.isEmpty() || device;
                const qreal height = !mounted || device ? 32 : compact ? 40 : 54;
                const QRectF box(seen.left() + 8, top, seen.width() - 16, height);
                top += height + 6;
                // a mounted USB stick or card: an eject button at the right, as in KDE's own list
                QRectF eject;
                if (mounted && disk.removable) {
                    const qreal side = metrics.height() + 4;
                    eject = QRectF(box.right() - 8 - side, (!compact ? box.top() + 8 : box.top() + 5) + (metrics.height() - side) / 2.0, side, side);
                }
                m_ngDiskBoxes.append({box, disk.root, disk.udi, disk.removable, eject, disk.url});

                if (mounted) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(ngAurora() ? ngAuroraVeil(darkGlass) : darkGlass ? QColor(0, 0, 0, 62) : QColor(255, 255, 255, 130));
                } else {
                    QColor line = palette().color(QPalette::WindowText);
                    line.setAlpha(70);
                    painter.setPen(QPen(line, 1, Qt::DashLine));
                    painter.setBrush(Qt::NoBrush);
                }
                painter.drawRoundedRect(mounted ? box : box.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

                // the name (or, for the home disk alone, its size) on the left, the free space on the right
                QRectF textRect = !mounted || device ? box.adjusted(10, 0, -10, 0) : compact ? box.adjusted(10, 5, -10, -17) : box.adjusted(10, 8, -10, -24);
                if (!eject.isNull()) {
                    textRect.setRight(eject.left() - 6);
                    // the glyph as tall as the text beside it (it was drawn over the whole hit area, larger than the text);
                    // the area to click stays the larger square
                    const int glyph = qMax(10, int(metrics.ascent()) + 1);
                    const QRect glyphRect(qRound(eject.center().x() - glyph / 2.0), qRound(eject.center().y() - glyph / 2.0), glyph, glyph);
                    QIcon::fromTheme(QStringLiteral("media-eject")).paint(&painter, glyphRect);
                }
                QString left;
                if (disk.home) {
                    left = alone ? KIO::convertSize(disk.total) : ngTr("Home", "Home") + QStringLiteral(" · ") + KIO::convertSize(disk.total);
                } else {
                    left = disk.fs.isEmpty() ? disk.name : disk.name + QStringLiteral(" · ") + disk.fs;
                }
                const QString right = device ? disk.kind
                    : mounted               ? i18nc("@info:status Free disk space", "%1 free", KIO::convertSize(disk.free))
                                            : ngTr("not mounted", "non montato");
                QColor text = palette().color(QPalette::WindowText);
                const int rightWidth = metrics.horizontalAdvance(right);
                const bool both = rightWidth + 12 + qMin(metrics.horizontalAdvance(left), 40) <= textRect.width();
                if (both) {
                    QFont bold = small;
                    bold.setWeight(disk.home && alone ? QFont::Normal : QFont::DemiBold);
                    painter.setFont(bold);
                    painter.setPen(text);
                    drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(bold).elidedText(left, Qt::ElideRight, int(textRect.width() - rightWidth - 12)));
                    painter.setFont(small);
                }
                if (!mounted || device) {
                    text.setAlpha(150);
                }
                painter.setPen(text);
                drawText(textRect, (both ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, metrics.elidedText(right, Qt::ElideRight, int(textRect.width())));

                if (mounted && !device && disk.total > 0) {
                    const qreal used = 1.0 - qreal(disk.free) / qreal(disk.total);
                    const qreal barHeight = compact ? 4 : 6;
                    const QRectF groove(box.left() + 10, box.bottom() - (compact ? 11 : 16), box.width() - 20, barHeight);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(darkGlass ? QColor(255, 255, 255, 44) : QColor(0, 0, 0, 36));
                    painter.drawRoundedRect(groove, barHeight / 2, barHeight / 2);
                    painter.setBrush(used > 0.9 ? QColor(224, 145, 79) : palette().color(QPalette::Highlight));
                    painter.drawRoundedRect(QRectF(groove.topLeft(), QSizeF(qMax(barHeight, groove.width() * used), groove.height())), barHeight / 2, barHeight / 2);
                }
            }
        }
    }
    return opaque;
}

namespace
{

QString ngFsName(const QString &fs)
{
    const QString lower = fs.toLower();
    if (lower.startsWith(QLatin1String("ntfs"))) {
        return QStringLiteral("NTFS");
    }
    if (lower == QLatin1String("vfat")) {
        return QStringLiteral("FAT");
    }
    if (lower == QLatin1String("exfat")) {
        return QStringLiteral("exFAT");
    }
    return lower;
}
}

QList<DolphinDockWidget::NgDisk> DolphinDockWidget::ngSidebarDisks()
{
    static bool watching = false;
    if (!watching) {
        watching = true;
        KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
        auto dirty = [] {
            s_ngDisksDirty = true;
        };
        QObject::connect(places, &QAbstractItemModel::rowsInserted, places, dirty);
        QObject::connect(places, &QAbstractItemModel::rowsRemoved, places, dirty);
        QObject::connect(places, &QAbstractItemModel::dataChanged, places, dirty);
        QObject::connect(places, &QAbstractItemModel::modelReset, places, dirty);
        QObject::connect(places, &KFilePlacesModel::reloaded, places, dirty);
        QObject::connect(NgSettings::instance(), &NgSettings::changed, places, dirty);
    }
    // the free space is read again when it is older than 15 s
    if (!s_ngDisksDirty && s_ngDisksAge.isValid() && s_ngDisksAge.elapsed() < 15000) {
        return s_ngDisks;
    }
    s_ngDisksDirty = false;
    s_ngDisksAge.restart();
    s_ngDisks.clear();

    const QStorageInfo home(QDir::homePath());
    if (home.isValid() && home.bytesTotal() > 0) {
        NgDisk disk;
        disk.home = true;
        disk.root = home.rootPath();
        disk.total = home.bytesTotal();
        disk.free = home.bytesAvailable();
        s_ngDisks.append(disk);
    }
    const QString mode = ngSetting("DEVICES").toLower();
    if (!mode.isEmpty() || ngGlassPlain()) {
        return s_ngDisks; // the devices are a section of the list (classic), or hidden
    }
    // for screenshots: DOLPHIN_NG_DEMO_DISKS="Data|btrfs|310|500;Archive|NTFS" (name|fs|free GiB|total GiB; no sizes:
    // not mounted) stands for the disks of the machine
    if (const QString demo = qEnvironmentVariable("DOLPHIN_NG_DEMO_DISKS"); !demo.isEmpty()) {
        for (const QString &entry : demo.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QStringList parts = entry.split(QLatin1Char('|'));
            NgDisk disk;
            disk.udi = QStringLiteral("demo:") + parts.value(0);
            disk.name = parts.value(0);
            disk.fs = parts.value(1);
            if (parts.value(1) == QLatin1String("kdeconnect")) { // a phone: "Pixel|kdeconnect"
                disk.fs.clear();
                disk.url = QUrl(QStringLiteral("kdeconnect://demo"));
                disk.kind = QStringLiteral("KDE Connect");
            } else if (parts.size() >= 4) {
                disk.root = QDir::homePath();
                disk.free = qint64(parts.value(2).toDouble() * 1024 * 1024 * 1024);
                disk.total = qint64(parts.value(3).toDouble() * 1024 * 1024 * 1024);
            }
            s_ngDisks.append(disk);
        }
        return s_ngDisks;
    }
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        const auto group = places->groupType(index);
        if ((group != KFilePlacesModel::DevicesType && group != KFilePlacesModel::RemovableDevicesType) || places->isHidden(index)) {
            continue;
        }
        const Solid::Device device = places->deviceForIndex(index);
        const auto *access = device.as<Solid::StorageAccess>();
        if (!access) {
            // not a disk: a phone through KDE Connect, a media player... Its row is hidden with the others, so it gets
            // a box too, without usage, that opens it (it vanished from the sidebar before)
            const QUrl url = places->url(index);
            if (url.isEmpty()) {
                continue;
            }
            NgDisk other;
            other.udi = device.udi();
            other.name = places->text(index);
            other.url = url;
            other.kind = url.scheme() == QLatin1String("kdeconnect") ? QStringLiteral("KDE Connect") : ngTr("Device", "Dispositivo");
            s_ngDisks.append(other);
            continue;
        }
        NgDisk disk;
        disk.udi = device.udi();
        disk.name = places->text(index);
        disk.removable = group == KFilePlacesModel::RemovableDevicesType;
        if (const auto *volume = device.as<Solid::StorageVolume>()) {
            disk.fs = ngFsName(volume->fsType());
        }
        if (access->isAccessible() && !access->filePath().isEmpty()) {
            const QStorageInfo storage(access->filePath());
            if (home.isValid() && storage.device() == home.device()) {
                continue; // the home disk is already the first box
            }
            const QString mount = access->filePath();
            if (mount == QLatin1String("/") || mount.startsWith(QLatin1String("/boot")) || mount.startsWith(QLatin1String("/efi"))) {
                continue; // the system's own partitions (the boot loader's): nothing to open there
            }
            disk.root = access->filePath();
            disk.total = storage.bytesTotal();
            disk.free = storage.bytesAvailable();
        }
        s_ngDisks.append(disk);
    }
    return s_ngDisks;
}

int DolphinDockWidget::ngDiskRoom()
{
    if (ngSetting("SIDEBAR_DISKS").toLower() == QLatin1String("hide")) {
        return 0; // the disks section switched off
    }
    const QList<NgDisk> disks = ngSidebarDisks();
    if (disks.isEmpty()) {
        return 62;
    }
    const bool compact = disks.size() > 3;
    int room = 8;
    for (const NgDisk &disk : disks) {
        room += (disk.root.isEmpty() || !disk.url.isEmpty() ? 32 : compact ? 40 : 54) + 6;
    }
    return room - 6;
}

void DolphinDockWidget::timerEvent(QTimerEvent *event)
{
    QDockWidget::timerEvent(event);
    if (objectName() == QLatin1String("placesDock") && isVisible()) {
        update(); // the disk usage box re-reads the free space when it is older than 15 s
    }
}

bool DolphinDockWidget::eventFilter(QObject *watched, QEvent *event)
{
    // dolphin-ng: the static backdrop depends on where the window is on screen
    if (watched == window() && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        update();
    }
    if (watched == widget() && ngDiskBoxEvent(event)) {
        return true;
    }
    return QDockWidget::eventFilter(watched, event);
}

bool DolphinDockWidget::ngDiskBoxEvent(QEvent *event)
{
    // a click on a disk box opens the root of that file system (an unmounted one is mounted first); the hand cursor
    // announces it
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseMove) {
        return false;
    }
    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPointF at = mapFromGlobal(mouse->globalPosition().toPoint());
    int hit = -1;
    for (int i = 0; i < m_ngDiskBoxes.size(); ++i) {
        if (m_ngDiskBoxes.at(i).rect.contains(at)) {
            hit = i;
            break;
        }
    }
    const bool inside = hit >= 0;
    if (event->type() == QEvent::MouseMove) {
        if (inside != m_ngDiskHover) {
            m_ngDiskHover = inside;
            if (inside) {
                widget()->setCursor(Qt::PointingHandCursor);
            } else {
                widget()->unsetCursor();
            }
        }
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        if (inside && mouse->button() == Qt::RightButton) {
            const NgDiskHit target = m_ngDiskBoxes.at(hit);
            ngDiskMenu(target, mouse->globalPosition().toPoint());
            return true;
        }
        m_ngDiskPressed = inside && mouse->button() == Qt::LeftButton ? hit : -1;
        return m_ngDiskPressed >= 0;
    }
    if (m_ngDiskPressed < 0) {
        return false;
    }
    const bool same = hit == m_ngDiskPressed;
    m_ngDiskPressed = -1;
    if (!same) {
        return true;
    }
    const NgDiskHit target = m_ngDiskBoxes.at(hit);
    if (!target.eject.isNull() && target.eject.contains(at)) {
        ngDiskUnmount(target.udi); // the eject button
        return true;
    }
    if (!target.url.isEmpty()) {
        Q_EMIT ngDiskRootRequested(target.url); // a device that is not a disk: its own address
        return true;
    }
    if (!target.root.isEmpty()) {
        Q_EMIT ngDiskRootRequested(QUrl::fromLocalFile(target.root));
        return true;
    }
    ngDiskMount(target.udi);
    return true;
}

void DolphinDockWidget::ngDiskMenu(const NgDiskHit &target, const QPoint &globalPos)
{
    QMenu menu(this);
    if (!target.url.isEmpty()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), ngTr("Open", "Apri"), this, [this, url = target.url] {
            Q_EMIT ngDiskRootRequested(url);
        });
    } else if (!target.root.isEmpty()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), ngTr("Open", "Apri"), this, [this, root = target.root] {
            Q_EMIT ngDiskRootRequested(QUrl::fromLocalFile(root));
        });
        if (!target.udi.isEmpty()) { // not the home disk
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(target.removable ? QStringLiteral("media-eject") : QStringLiteral("media-mount")),
                           target.removable ? ngTr("Safely Remove", "Rimuovi in sicurezza") : ngTr("Unmount", "Smonta"), this, [this, udi = target.udi] {
                               ngDiskUnmount(udi);
                           });
        }
    } else if (!target.udi.isEmpty()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("media-mount")), ngTr("Mount and Open", "Monta e apri"), this, [this, udi = target.udi] {
            ngDiskMount(udi);
        });
    }
    if (!menu.isEmpty()) {
        menu.exec(globalPos);
    }
}

void DolphinDockWidget::ngDiskUnmount(const QString &udi)
{
    // the places model unmounts it (a removable device also powered down where it can be); errors reach the places panel
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        if (places->deviceForIndex(index).udi() == udi) {
            auto connection = std::make_shared<QMetaObject::Connection>();
            *connection = connect(places, &KFilePlacesModel::teardownDone, this, [this, places, udi, connection](const QModelIndex &done) {
                if (places->deviceForIndex(done).udi() != udi) {
                    return;
                }
                disconnect(*connection);
                s_ngDisksDirty = true;
                update();
            });
            places->requestTeardown(index);
            return;
        }
    }
}

void DolphinDockWidget::ngDiskMount(const QString &udi)
{
    // not mounted: the places model mounts it (asking for a password if needed; errors reach the places panel)
    const NgDiskHit target{QRectF(), QString(), udi, false, QRectF()};
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        if (places->deviceForIndex(index).udi() != target.udi) {
            continue;
        }
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(places, &KFilePlacesModel::setupDone, this, [this, places, udi = target.udi, connection](const QModelIndex &done, bool success) {
            if (places->deviceForIndex(done).udi() != udi) {
                return;
            }
            disconnect(*connection);
            s_ngDisksDirty = true;
            update();
            if (success) {
                Q_EMIT ngDiskRootRequested(places->url(done));
            }
        });
        places->requestSetup(index);
        break;
    }
}

void DolphinDockWidget::mousePressEvent(QMouseEvent *event)
{
    if (!ngDiskBoxEvent(event)) {
        QDockWidget::mousePressEvent(event);
    }
}

void DolphinDockWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!ngDiskBoxEvent(event)) {
        QDockWidget::mouseReleaseEvent(event);
    }
}

bool DolphinDockWidget::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        ngApplySettings(); // a light or a dark colour scheme: the card lets more or less of the desktop through
        update();
    }
    // dolphin-ng: the compositor blurs only behind the glass cards, so the window tells it where they are
    if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        if (QWidget *top = window(); top && top != this && top->metaObject()->indexOfSlot("ngUpdateBlurRegion()") >= 0) {
            QMetaObject::invokeMethod(top, "ngUpdateBlurRegion", Qt::QueuedConnection);
        }
    }
    if ((event->type() == QEvent::Show || event->type() == QEvent::ParentChange) && !ngGlassPlain() && window() != this) {
        window()->removeEventFilter(this);
        window()->installEventFilter(this);
        if (widget()) {
            widget()->removeEventFilter(this);
            widget()->installEventFilter(this);
            widget()->setMouseTracking(true);
        }
    }
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
        if (event->spontaneous()) {
            // The Dolphin window has been minimized or restored. We do not want this to be interpreted like a user was toggling the visibility of this widget.
            // We return here so no QDockWidget::visibilityChanged() signal is emitted. This does not seem to happen either way on Wayland.
            return true;
        }
        [[fallthrough]];
    default:
        return QDockWidget::event(event);
    }
}

#include "dolphindockwidget.moc"
#include "moc_dolphindockwidget.cpp"
