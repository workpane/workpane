#include "platform/macos/MacWebView.h"

#include "platform/DownloadTarget.h"
#include "platform/macos/MacTextHelper.h"

#import <QuartzCore/QuartzCore.h>
#import <WebKit/WebKit.h>
#include <objc/runtime.h>

#include <utility>

namespace workpane::platform {

MacWebView::MacWebView(WebViewTracker& tracker, NSView* host, WKWebView* webView, std::unique_ptr<WebViewEngine> engine, NSWindow* holder, std::filesystem::path downloads) : TrackedWebView(tracker), m_holder(holder), m_host(host), m_webView(webView), m_engine(std::move(engine)), m_downloads(std::move(downloads)) {
    objc_setAssociatedObject(m_webView, &viewKey, [NSValue valueWithPointer:this], OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    if (m_holder != nil) {
        m_holder.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
    }

    // Every page reads the user agent of the installed Safari and may show an element in full screen, such as a video, which WebKit leaves off in an application.
    m_webView.customUserAgent = browserUserAgent();
    m_webView.configuration.preferences.elementFullscreenEnabled = YES;

    // The web view sits in a frame of its own, which masks the windows the product draws over it and lets the pointer through them.
    m_frame = [[frameClass() alloc] initWithFrame:NSZeroRect];
    m_frame.wantsLayer = YES;
    m_frame.hidden = YES;
    m_webView.frame = m_frame.bounds;
    m_webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [m_frame addSubview:m_webView];
    [m_host addSubview:m_frame];

    // The web view announces every change of its address, title, loading and history to one observer, which reads the whole state again.
    m_observer = [[observerClass() alloc] init];

    for (const char* key : observedKeys) {
        [m_webView addObserver:m_observer forKeyPath:[NSString stringWithUTF8String:key] options:NSKeyValueObservingOptionNew context:this];
    }

    // The delegate opens the windows pages ask for and hands everything else to the delegate of the engine, which a window a page opened receives a copy of.
    id inner = m_webView.UIDelegate != nil ? m_webView.UIDelegate : [[objc_lookUpClass(libraryDelegateName) alloc] init];
    m_delegate = [[delegateClass() alloc] init];
    objc_setAssociatedObject(m_delegate, &innerKey, inner, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    m_webView.UIDelegate = m_delegate;
    m_navigator = [[navigatorClass() alloc] init];
    m_webView.navigationDelegate = m_navigator;

    // A window a page opened shares the content controller of its opener, which already carries the messenger and the page script.
    if (m_engine == nullptr) {
        return;
    }

    // The engine answers posts of its own name with an unchecked conversion that a page could crash, and the product never binds a function, so its scripts go.
    // The messenger and the page script live in a script world of their own, which no script of the page reaches.
    WKUserContentController* controller = m_webView.configuration.userContentController;
    WKContentWorld* world = [WKContentWorld worldWithName:[NSString stringWithUTF8String:scriptWorld]];
    [controller removeScriptMessageHandlerForName:[NSString stringWithUTF8String:libraryHandlerName]];
    [controller removeAllUserScripts];
    [controller addScriptMessageHandler:[[messengerClass() alloc] init] contentWorld:world name:[NSString stringWithUTF8String:handlerName]];
    [controller addUserScript:[[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:pageScript] injectionTime:WKUserScriptInjectionTimeAtDocumentEnd forMainFrameOnly:YES inContentWorld:world]];
}

MacWebView::~MacWebView() {
    for (const char* key : observedKeys) {
        [m_webView removeObserver:m_observer forKeyPath:[NSString stringWithUTF8String:key] context:this];
    }

    objc_setAssociatedObject(m_webView, &viewKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    m_webView.navigationDelegate = nil;
    // The engine releases the delegate it created by asking the web view for it, so a built view hands that delegate back before the engine goes.
    m_webView.UIDelegate = m_engine != nullptr ? objc_getAssociatedObject(m_delegate, &innerKey) : nil;
    [m_webView removeFromSuperview];
    [m_frame removeFromSuperview];
    m_engine.reset();
    [m_holder close];
}

Result<void> MacWebView::navigate(std::string_view url) {
    NSString* address = MacTextHelper::string(url);
    NSURL* location = address != nil ? [NSURL URLWithString:address] : nil;

    if (location == nil) {
        return Result<void>::failure({"webview_navigate_failed", "The web view refused the address", std::string(url)});
    }

    [m_webView loadRequest:[NSURLRequest requestWithURL:location]];

    return Result<void>::success();
}

Result<void> MacWebView::setHtml(std::string_view html) {
    NSString* document = MacTextHelper::string(html);

    if (document == nil) {
        return Result<void>::failure({"webview_html_failed", "The web view refused the document", {}});
    }

    [m_webView loadHTMLString:document baseURL:nil];

    return Result<void>::success();
}

Result<void> MacWebView::evaluate(std::string_view script) {
    NSString* source = MacTextHelper::string(script);

    if (source == nil) {
        return Result<void>::failure({"webview_evaluate_failed", "The web view refused the script", {}});
    }

    [m_webView evaluateJavaScript:source completionHandler:nil];

    return Result<void>::success();
}

void MacWebView::reload() {
    [m_webView reload];
}

void MacWebView::back() {
    [m_webView goBack];
}

void MacWebView::forward() {
    [m_webView goForward];
}

void MacWebView::stop() {
    [m_webView stopLoading];
}

// The product lays out from the top left while the content view of the window counts from the bottom, so the rectangle is flipped here.
void MacWebView::apply(const ImRect& bounds, bool visible) {
    m_frame.frame = NSMakeRect(bounds.Min.x, m_host.bounds.size.height - bounds.Max.y, bounds.GetWidth(), bounds.GetHeight());
    m_webView.frame = m_frame.bounds;
    m_frame.hidden = !visible;
}

// The holes count from the top left of the view while the frame counts from its bottom, so each one is flipped into the list the frame tests the pointer against.
// The mask covers what remains once every hole is taken out, so holes that overlap stay holes where they meet.
void MacWebView::cut(ImVec2 size, const std::vector<ImRect>& holes) {
    NSMutableArray<NSValue*>* rectangles = [NSMutableArray array];
    std::vector<CGRect> kept{CGRectMake(0.0, 0.0, size.x, size.y)};

    for (const ImRect& hole : holes) {
        const CGRect removed = CGRectMake(hole.Min.x, size.y - hole.Max.y, hole.GetWidth(), hole.GetHeight());
        [rectangles addObject:[NSValue valueWithRect:NSRectFromCGRect(removed)]];
        kept = subtract(kept, removed);
    }

    objc_setAssociatedObject(m_frame, &holesKey, rectangles, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    if (holes.empty()) {
        m_frame.layer.mask = nil;
        return;
    }

    CGMutablePathRef path = CGPathCreateMutable();

    for (const CGRect& piece : kept) {
        CGPathAddRect(path, nullptr, piece);
    }

    CAShapeLayer* mask = [CAShapeLayer layer];
    mask.frame = CGRectMake(0.0, 0.0, size.x, size.y);
    mask.path = path;
    m_frame.layer.mask = mask;
    CGPathRelease(path);
}

// Each piece loses the part a hole covers and keeps the bands below and above it across its width and beside it along its height.
std::vector<CGRect> MacWebView::subtract(const std::vector<CGRect>& pieces, CGRect removed) {
    std::vector<CGRect> remaining;

    for (const CGRect& piece : pieces) {
        const CGRect overlap = CGRectIntersection(piece, removed);

        if (CGRectIsEmpty(overlap)) {
            remaining.push_back(piece);
            continue;
        }

        const std::array<CGRect, 4> bands{CGRectMake(piece.origin.x, piece.origin.y, piece.size.width, CGRectGetMinY(overlap) - piece.origin.y), CGRectMake(piece.origin.x, CGRectGetMaxY(overlap), piece.size.width, CGRectGetMaxY(piece) - CGRectGetMaxY(overlap)), CGRectMake(piece.origin.x, overlap.origin.y, CGRectGetMinX(overlap) - piece.origin.x, overlap.size.height), CGRectMake(CGRectGetMaxX(overlap), overlap.origin.y, CGRectGetMaxX(piece) - CGRectGetMaxX(overlap), overlap.size.height)};

        for (const CGRect& band : bands) {
            if (band.size.width > 0.0 && band.size.height > 0.0) {
                remaining.push_back(band);
            }
        }
    }

    return remaining;
}

// WebKit leaves the name and the version of the browser out of the user agent of an application, and sites refuse such an agent as an outdated browser, so the agent names the Safari installed with the platform and engine parts WebKit keeps the same on every Mac.
NSString* MacWebView::browserUserAgent() {
    NSURL* safari = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.apple.Safari"];
    NSString* version = safari != nil ? [NSBundle bundleWithURL:safari].infoDictionary[@"CFBundleShortVersionString"] : nil;

    return version != nil ? [NSString stringWithFormat:@"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/%@ Safari/605.1.15", version] : nil;
}

// A frame answers no pointer inside a hole, so the press reaches the product window beneath it.
Class MacWebView::frameClass() {
    if (Class existing = objc_lookUpClass(frameName); existing != nil) {
        return existing;
    }

    Class created = objc_allocateClassPair([NSView class], frameName, 0);
    IMP hit = imp_implementationWithBlock(^NSView*(NSView* frame, NSPoint point) {
      const NSPoint local = [frame convertPoint:point fromView:frame.superview];

      if (frame.hidden || !NSPointInRect(local, frame.bounds)) {
          return nil;
      }

      NSArray<NSValue*>* holes = objc_getAssociatedObject(frame, &holesKey);

      for (NSValue* hole in holes) {
          if (NSPointInRect(local, hole.rectValue)) {
              return nil;
          }
      }

      NSView* found = [frame.subviews.firstObject hitTest:local];
      return found != nil ? found : frame;
    });

    class_addMethod(created, @selector(hitTest:), hit, method_getTypeEncoding(class_getInstanceMethod([NSView class], @selector(hitTest:))));
    objc_registerClassPair(created);

    return created;
}

// Key value observing calls an object rather than a function, so one class whose instances forward every change to their view is registered once.
Class MacWebView::observerClass() {
    if (Class existing = objc_lookUpClass(observerName); existing != nil) {
        return existing;
    }

    Class created = objc_allocateClassPair([NSObject class], observerName, 0);
    IMP observe = imp_implementationWithBlock(^(id, NSString*, id, NSDictionary*, void* context) { static_cast<MacWebView*>(context)->refresh(); });
    class_addMethod(created, @selector(observeValueForKeyPath:ofObject:change:context:), observe, "v@:@@@^v");
    objc_registerClassPair(created);

    return created;
}

// The delegate answers a page asking for a window and a page asking to close, and forwards every other question, such as a file chooser, to the delegate of the engine.
Class MacWebView::delegateClass() {
    if (Class existing = objc_lookUpClass(delegateName); existing != nil) {
        return existing;
    }

    Class created = objc_allocateClassPair([NSObject class], delegateName, 0);
    class_addProtocol(created, @protocol(WKUIDelegate));
    IMP open = imp_implementationWithBlock(^WKWebView*(id, WKWebView* webView, WKWebViewConfiguration* configuration, WKNavigationAction*, WKWindowFeatures*) {
      MacWebView* opener = owner(webView);
      return opener != nullptr && opener->opensPopups() ? opener->openPopup(configuration) : nil;
    });

    IMP close = imp_implementationWithBlock(^(id, WKWebView* webView) {
      if (MacWebView* view = owner(webView); view != nullptr) {
          view->closeRequested();
      }
    });

    IMP forward = imp_implementationWithBlock(^id(id delegate, SEL) { return objc_getAssociatedObject(delegate, &innerKey); });
    IMP responds = imp_implementationWithBlock(^BOOL(id delegate, SEL selector) { return class_respondsToSelector(object_getClass(delegate), selector) || [objc_getAssociatedObject(delegate, &innerKey) respondsToSelector:selector]; });
    class_addMethod(created, @selector(webView:createWebViewWithConfiguration:forNavigationAction:windowFeatures:), open, "@@:@@@@");
    class_addMethod(created, @selector(webViewDidClose:), close, "v@:@");
    class_addMethod(created, @selector(forwardingTargetForSelector:), forward, "@@::");
    class_addMethod(created, @selector(respondsToSelector:), responds, "c@::");
    objc_registerClassPair(created);

    return created;
}

Class MacWebView::messengerClass() {
    if (Class existing = objc_lookUpClass(messengerName); existing != nil) {
        return existing;
    }

    Class created = objc_allocateClassPair([NSObject class], messengerName, 0);
    class_addProtocol(created, @protocol(WKScriptMessageHandler));
    IMP received = imp_implementationWithBlock(^(id, WKUserContentController*, WKScriptMessage* message) {
      MacWebView* view = owner(message.webView);

      if (view == nullptr || ![NSJSONSerialization isValidJSONObject:message.body]) {
          return;
      }

      NSData* json = [NSJSONSerialization dataWithJSONObject:message.body options:0 error:nil];
      view->receive(std::string_view(static_cast<const char*>(json.bytes), json.length));
    });

    class_addMethod(created, @selector(userContentController:didReceiveScriptMessage:), received, "v@:@@");
    objc_registerClassPair(created);

    return created;
}

// The navigator turns a link marked for download and a response the page cannot show or sends as an attachment into a download, and saves each download in the downloads folder of the view that started it.
Class MacWebView::navigatorClass() {
    if (Class existing = objc_lookUpClass(navigatorName); existing != nil) {
        return existing;
    }

    Class created = objc_allocateClassPair([NSObject class], navigatorName, 0);
    class_addProtocol(created, @protocol(WKNavigationDelegate));
    class_addProtocol(created, @protocol(WKDownloadDelegate));
    IMP action = imp_implementationWithBlock(^(id, WKWebView*, WKNavigationAction* navigation, void (^decide)(WKNavigationActionPolicy)) { decide(navigation.shouldPerformDownload ? WKNavigationActionPolicyDownload : WKNavigationActionPolicyAllow); });
    IMP response = imp_implementationWithBlock(^(id, WKWebView*, WKNavigationResponse* navigation, void (^decide)(WKNavigationResponsePolicy)) {
      NSHTTPURLResponse* http = [navigation.response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse*)navigation.response : nil;
      NSString* disposition = [http valueForHTTPHeaderField:@"Content-Disposition"].lowercaseString;
      decide([disposition hasPrefix:@"attachment"] || !navigation.canShowMIMEType ? WKNavigationResponsePolicyDownload : WKNavigationResponsePolicyAllow);
    });

    IMP became = imp_implementationWithBlock(^(id navigator, WKWebView*, id, WKDownload* download) { download.delegate = navigator; });
    IMP destination = imp_implementationWithBlock(^(id, WKDownload* download, NSURLResponse*, NSString* suggested, void (^decide)(NSURL*)) {
      MacWebView* view = owner(download.webView);

      if (view == nullptr) {
          decide(nil);
          return;
      }

      NSString* path = MacTextHelper::string(DownloadTarget::choose(view->m_downloads, MacTextHelper::utf8(suggested)).string());
      objc_setAssociatedObject(download, &pathKey, path, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      decide([NSURL fileURLWithPath:path]);
    });

    IMP finished = imp_implementationWithBlock(^(id, WKDownload* download) { finishDownload(download, true, nil); });
    IMP failed = imp_implementationWithBlock(^(id, WKDownload* download, NSError* error, NSData*) { finishDownload(download, false, error.localizedDescription); });
    class_addMethod(created, @selector(webView:decidePolicyForNavigationAction:decisionHandler:), action, "v@:@@@?");
    class_addMethod(created, @selector(webView:decidePolicyForNavigationResponse:decisionHandler:), response, "v@:@@@?");
    class_addMethod(created, @selector(webView:navigationAction:didBecomeDownload:), became, "v@:@@@");
    class_addMethod(created, @selector(webView:navigationResponse:didBecomeDownload:), became, "v@:@@@");
    class_addMethod(created, @selector(download:decideDestinationUsingResponse:suggestedFilename:completionHandler:), destination, "v@:@@@@?");
    class_addMethod(created, @selector(downloadDidFinish:), finished, "v@:@");
    class_addMethod(created, @selector(download:didFailWithError:resumeData:), failed, "v@:@@@");
    objc_registerClassPair(created);

    return created;
}

// A download that ends tells the view of its page where the file is, and one whose page is gone has nobody left to tell.
void MacWebView::finishDownload(WKDownload* download, bool finished, NSString* message) {
    MacWebView* view = owner(download.webView);
    NSString* path = objc_getAssociatedObject(download, &pathKey);

    if (view == nullptr || path == nil) {
        return;
    }

    view->downloaded({std::filesystem::path(MacTextHelper::utf8(path)), finished, finished ? std::string() : MacTextHelper::utf8(message)});
}

MacWebView* MacWebView::owner(WKWebView* webView) {
    NSValue* value = webView != nil ? objc_getAssociatedObject(webView, &viewKey) : nil;
    return value != nil ? static_cast<MacWebView*>(value.pointerValue) : nullptr;
}

// WebKit builds the window of a page from the configuration it hands over, and the window joins the product window like any other view.
WKWebView* MacWebView::openPopup(WKWebViewConfiguration* configuration) {
    WKWebView* popup = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
    offer(std::make_unique<MacWebView>(tracker(), m_host, popup, nullptr, nil, m_downloads));

    return popup;
}

void MacWebView::refresh() {
    report({MacTextHelper::utf8(m_webView.URL.absoluteString), MacTextHelper::utf8(m_webView.title), {}, m_webView.loading == YES, m_webView.canGoBack == YES, m_webView.canGoForward == YES});
}

} // namespace workpane::platform
