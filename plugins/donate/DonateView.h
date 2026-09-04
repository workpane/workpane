#pragma once

#include "plugins/PluginInterface.h"

#include <QPixmap>
#include <QUrl>
#include <QWidget>

namespace workpane::plugins::donate {

class DonateView final : public QWidget {
    Q_OBJECT

  public:
    explicit DonateView(PluginHost& host, QWidget* parent = nullptr);

  private:
    void openDonationPage(const QUrl& url);

    PluginHost& m_host;
};

class DonateAssets final {
  public:
    [[nodiscard]] static QPixmap profilePixmap(int logicalSize, qreal devicePixelRatio = 1.0);
};

} // namespace workpane::plugins::donate
