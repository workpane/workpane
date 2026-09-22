#include "DonateView.h"

#include "ui/Icons.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace workpane::plugins::donate {

const QUrl githubSponsorsUrl(QStringLiteral("https://github.com/sponsors/paulocoutinhox"));
const QUrl kofiUrl(QStringLiteral("https://ko-fi.com/A0A412XEV"));
constexpr int profileSize = 176;

DonateView::DonateView(PluginHost& host, QWidget* parent) : QWidget(parent), m_host(host) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 32, 32, 32);
    root->addStretch(1);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("donateCard"));
    card->setMaximumWidth(640);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(48, 42, 48, 40);
    cardLayout->setSpacing(14);

    auto* profile = new QLabel(card);
    profile->setObjectName(QStringLiteral("donateProfile"));
    profile->setFixedSize(profileSize, profileSize);
    profile->setPixmap(DonateAssets::profilePixmap(profileSize, profile->devicePixelRatioF()));
    profile->setAccessibleName(m_host.translate(QStringLiteral("donate.view.name")));
    cardLayout->addWidget(profile, 0, Qt::AlignHCenter);

    auto* name = new QLabel(m_host.translate(QStringLiteral("donate.view.name")), card);
    name->setObjectName(QStringLiteral("donateName"));
    name->setAlignment(Qt::AlignHCenter);
    cardLayout->addWidget(name);

    auto* title = new QLabel(m_host.translate(QStringLiteral("donate.view.title")), card);
    title->setObjectName(QStringLiteral("donateTitle"));
    title->setAlignment(Qt::AlignHCenter);
    cardLayout->addWidget(title);

    auto* description = new QLabel(m_host.translate(QStringLiteral("donate.view.description")), card);
    description->setObjectName(QStringLiteral("donateDescription"));
    description->setAlignment(Qt::AlignHCenter);
    description->setWordWrap(true);
    cardLayout->addWidget(description);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 8, 0, 0);
    actions->setSpacing(10);
    auto* github = new QPushButton(ui::IconCatalog::primaryIcon(ui::IconName::Donate, m_host.theme()), m_host.translate(QStringLiteral("donate.view.github-sponsors")), card);
    github->setObjectName(QStringLiteral("primaryButton"));
    auto* kofi = new QPushButton(ui::IconCatalog::icon(ui::IconName::Donate, m_host.theme()), m_host.translate(QStringLiteral("donate.view.kofi")), card);
    actions->addStretch(1);
    actions->addWidget(github);
    actions->addWidget(kofi);
    actions->addStretch(1);
    cardLayout->addLayout(actions);

    auto* note = new QLabel(m_host.translate(QStringLiteral("donate.view.external-note")), card);
    note->setObjectName(QStringLiteral("mutedLabel"));
    note->setAlignment(Qt::AlignHCenter);
    note->setWordWrap(true);
    cardLayout->addWidget(note);

    root->addWidget(card, 0, Qt::AlignHCenter);
    root->addStretch(1);

    // clang-format off
    connect(github, &QPushButton::clicked, this, [this]() { openDonationPage(githubSponsorsUrl); });
    connect(kofi, &QPushButton::clicked, this, [this]() { openDonationPage(kofiUrl); });
    // clang-format on
}

void DonateView::openDonationPage(const QUrl& url) {
    if (QDesktopServices::openUrl(url)) {
        return;
    }

    m_host.notify(m_host.translate(QStringLiteral("donate.error.open-title")), m_host.translate(QStringLiteral("donate.error.open-message")), AlertSeverity::Error);
}

QPixmap DonateAssets::profilePixmap(int logicalSize, qreal devicePixelRatio) {
    if (logicalSize <= 0 || !qIsFinite(devicePixelRatio) || devicePixelRatio <= 0.0) {
        return {};
    }

    const QImage source(QStringLiteral(":/workpane/donate/assets/profile.png"));

    if (source.isNull()) {
        return {};
    }

    const int pixelSize = qRound(static_cast<qreal>(logicalSize) * devicePixelRatio);
    QPixmap result(pixelSize, pixelSize);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0.0, 0.0, pixelSize, pixelSize));
    painter.setClipPath(clip);
    painter.drawImage(QRect(0, 0, pixelSize, pixelSize), source);
    painter.end();
    result.setDevicePixelRatio(devicePixelRatio);
    return result;
}

} // namespace workpane::plugins::donate
