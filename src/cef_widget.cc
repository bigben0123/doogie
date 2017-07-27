#include "cef_widget.h"
#include <string>

namespace doogie {

CefWidget::CefWidget(Cef* cef, const QString& url, QWidget* parent)
    : CefBaseWidget(cef, parent) {
  handler_ = CefRefPtr<CefHandler>(new CefHandler);
  ForwardKeyboardEventsFrom(handler_);
  connect(handler_, &CefHandler::PreContextMenu,
          this, &CefWidget::PreContextMenu);
  connect(handler_, &CefHandler::ContextMenuCommand,
          this, &CefWidget::ContextMenuCommand);
  connect(handler_, &CefHandler::UrlChanged,
          this, &CefWidget::UrlChanged);
  connect(handler_, &CefHandler::TitleChanged,
          this, &CefWidget::TitleChanged);
  connect(handler_, &CefHandler::StatusChanged,
          this, &CefWidget::StatusChanged);
  connect(handler_, &CefHandler::Closed,
          this, &CefWidget::Closed);
  connect(handler_, &CefHandler::FaviconUrlChanged,
          [this](const QString& url) {
    static const auto favicon_sig =
        QMetaMethod::fromSignal(&CefWidget::FaviconChanged);
    if (this->isSignalConnected(favicon_sig) && browser_) {
      // Download the favicon
      browser_->GetHost()->DownloadImage(
            CefString(url.toStdString()),
            true,
            16,
            false,
            new CefWidget::FaviconDownloadCallback(this));
    }
  });
  connect(handler_, &CefHandler::FocusObtained, [this]() {
    setFocus();
  });
  connect(handler_, &CefHandler::LoadStateChanged,
          this, &CefWidget::LoadStateChanged);
  connect(handler_, &CefHandler::PageOpen,
          this, &CefWidget::PageOpen);
  connect(handler_, &CefHandler::FindResult,
          [this](int, int count, const CefRect&,
          int active_match_ordinal, bool) {
    FindResult(count, active_match_ordinal);
  });
  connect(handler_, &CefHandler::ShowBeforeUnloadDialog,
          this, &CefWidget::ShowBeforeUnloadDialog);

  InitBrowser(url);
}

CefWidget::~CefWidget() {
  if (browser_) {
    browser_->GetHost()->CloseBrowser(true);
  }
}

QPointer<QWidget> CefWidget::OverrideWidget() {
  return override_widget_;
}

void CefWidget::LoadUrl(const QString& url) {
  if (browser_) {
    browser_->GetMainFrame()->LoadURL(CefString(url.toStdString()));
  }
}

QString CefWidget::CurrentUrl() {
  if (browser_) {
    return QString::fromStdString(
          browser_->GetMainFrame()->GetURL().ToString());
  }
  return "";
}

void CefWidget::TryClose() {
  if (browser_) {
    browser_->GetHost()->CloseBrowser(true);
  }
}

void CefWidget::Go(int num) {
  if (browser_) {
    browser_->GetMainFrame()->ExecuteJavaScript(
        CefString(std::string("history.go(") + std::to_string(num) + ")"),
        CefString("<doogie>"),
        0);
  }
}

void CefWidget::Refresh(bool ignore_cache) {
  if (browser_) {
    if (ignore_cache) {
      browser_->ReloadIgnoreCache();
    } else {
      browser_->Reload();
    }
  }
}

void CefWidget::Stop() {
  if (browser_) {
    browser_->StopLoad();
  }
}

void CefWidget::Print() {
  if (browser_) {
    browser_->GetHost()->Print();
  }
}

void CefWidget::Find(const QString& text,
                     bool forward,
                     bool match_case,
                     bool continued) {
  if (browser_) {
    browser_->GetHost()->Find(0, CefString(text.toStdString()),
                              forward, match_case, continued);
  }
}

void CefWidget::CancelFind(bool clear_selection) {
  if (browser_) {
    browser_->GetHost()->StopFinding(clear_selection);
  }
}

void CefWidget::ExecJs(const QString &js) {
  if (browser_) {
    browser_->GetMainFrame()->ExecuteJavaScript(
          CefString(js.toStdString()), "<doogie>", 0);
  }
}

void CefWidget::ShowDevTools(CefBaseWidget* widg,
                             const QPoint& inspect_at) {
  if (browser_) {
    CefBrowserSettings settings;
    if (!dev_tools_handler_) {
      dev_tools_handler_ = CefRefPtr<CefHandler>(new CefHandler);
      widg->ForwardKeyboardEventsFrom(dev_tools_handler_);
      connect(dev_tools_handler_, &CefHandler::AfterCreated,
              [this](CefRefPtr<CefBrowser> b) {
        dev_tools_browser_ = b;
      });
      connect(dev_tools_handler_, &CefHandler::LoadEnd,
              [this](CefRefPtr<CefFrame> frame, int) {
        if (frame->IsMain()) emit DevToolsLoadComplete();
      });
      connect(dev_tools_handler_, &CefHandler::Closed, [this]() {
        CloseDevTools();
      });
    }
    browser_->GetHost()->ShowDevTools(
          widg->WindowInfo(),
          dev_tools_handler_,
          settings,
          CefPoint(inspect_at.x(), inspect_at.y()));
  }
}

void CefWidget::ExecDevToolsJs(const QString& js) {
  if (dev_tools_browser_) {
    dev_tools_browser_->GetMainFrame()->ExecuteJavaScript(
          CefString(js.toStdString()), "<doogie>", 0);
  }
}

void CefWidget::CloseDevTools() {
  // We have to nullify the dev tools browser to make this reentrant
  // Sadly, HasDevTools is not false after CloseDevTools
  if (dev_tools_browser_) {
    dev_tools_browser_->GetHost()->CloseDevTools();
    dev_tools_browser_ = nullptr;
    emit DevToolsClosed();
  }
}

double CefWidget::GetZoomLevel() {
  if (browser_) {
    return browser_->GetHost()->GetZoomLevel();
  }
  return 0.0;
}

void CefWidget::SetZoomLevel(double level) {
  if (browser_) {
    browser_->GetHost()->SetZoomLevel(level);
  }
}

void CefWidget::SetJsDialogCallback(CefHandler::JsDialogCallback callback) {
  handler_->SetJsDialogCallback(callback);
}

std::vector<CefWidget::NavEntry> CefWidget::NavEntries() {
  CefRefPtr<CefWidget::NavEntryVisitor> visitor =
      new CefWidget::NavEntryVisitor;
  if (browser_) {
    browser_->GetHost()->GetNavigationEntries(visitor, false);
  }
  return visitor->Entries();
}

void CefWidget::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  if (browser_) {
    browser_->GetHost()->SendFocusEvent(true);
  }
}

void CefWidget::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  if (browser_) {
    browser_->GetHost()->SendFocusEvent(false);
  }
}

void CefWidget::FaviconDownloadCallback::OnDownloadImageFinished(
    const CefString&, int, CefRefPtr<CefImage> image) {
  // TODO(cretz): should I somehow check if the page has changed before
  //  this came back?
  QIcon icon;
  if (image) {
    // TODO(cretz): This would be quicker if we used GetAsBitmap
    int width, height;
    auto png = image->GetAsPNG(1.0f, true, width, height);
    if (!png) {
      qDebug("Unable to get icon as PNG");
    } else {
      auto size = png->GetSize();
      std::vector<uchar> data(size);
      png->GetData(&data[0], size, 0);
      QPixmap pixmap;
      if (!pixmap.loadFromData(&data[0], (uint)size, "PNG")) {
        qDebug("Unable to convert icon as PNG");
      } else {
        icon.addPixmap(pixmap);
      }
    }
  }
  emit cef_widg_->FaviconChanged(icon);
}

}  // namespace doogie
