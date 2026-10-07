# Lets the product keep the cookies, storage and cache of its web views under its data directory, which the library leaves where the platform puts them for every program.
# WebKitGTK keeps them in folders under that directory, and macOS 14 and later keeps them in a store named after it, since WebKit takes no folder there.
# The configure step fails when a passage this patch replaces is missing, so a new pin of the library cannot skip it silently, and a passage already replaced is left as it is.
function(replace_passage anchor replacement)
    file(READ "${SOURCE}" content)
    string(FIND "${content}" "${replacement}" patched)
    string(FIND "${content}" "${anchor}" found)

    if(NOT patched EQUAL -1)
        return()
    endif()

    if(found EQUAL -1)
        message(FATAL_ERROR "The web view library no longer holds the passage the data directory patch replaces: ${anchor}")
    endif()

    string(REPLACE "${anchor}" "${replacement}" content "${content}")
    file(WRITE "${SOURCE}" "${content}")
endfunction()

replace_passage([=[
WEBVIEW_API webview_t webview_create(int debug, void *window);
]=] [=[
WEBVIEW_API webview_t webview_create(int debug, void *window);

/**
 * Keeps the cookies, storage and cache of every web view created afterwards under a directory.
 */
WEBVIEW_API void webview_set_data_directory(const char *path);
]=])

replace_passage([=[
namespace webview {
namespace detail {

class bad_access : public std::exception {};
]=] [=[
namespace webview {
namespace detail {

// The directory the web views keep their data under, empty while the platform decides.
inline std::string &data_directory() {
  static std::string directory;
  return directory;
}

class bad_access : public std::exception {};
]=])

replace_passage([=[
WEBVIEW_API webview_t webview_create(int debug, void *wnd) {
]=] [=[
WEBVIEW_API void webview_set_data_directory(const char *path) {
  webview::detail::data_directory() = path != nullptr ? path : "";
}

WEBVIEW_API webview_t webview_create(int debug, void *wnd) {
]=])

replace_passage([=[
class gtk_webkit_engine : public engine_base {
public:
]=] [=[
class gtk_webkit_engine : public engine_base {
public:
  // Every web view shares one context, whose data, cache and cookies live under the data directory.
  static WebKitWebContext *data_context() {
    static WebKitWebContext *context = [] {
      const std::string base = data_directory();
      WebKitWebsiteDataManager *manager = webkit_website_data_manager_new(
          "base-data-directory", (base + "/data").c_str(),
          "base-cache-directory", (base + "/cache").c_str(), nullptr);
      WebKitWebContext *created =
          webkit_web_context_new_with_website_data_manager(manager);
      webkit_cookie_manager_set_persistent_storage(
          webkit_web_context_get_cookie_manager(created),
          (base + "/cookies.sqlite").c_str(),
          WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
      g_object_unref(manager);
      return created;
    }();
    return context;
  }

]=])

replace_passage([=[
    m_webview = webkit_web_view_new();
]=] [=[
    m_webview = data_directory().empty()
                    ? webkit_web_view_new()
                    : webkit_web_view_new_with_context(data_context());
]=])

replace_passage([=[
    auto config = objc::autoreleased(
        objc::msg_send<id>("WKWebViewConfiguration"_cls, "new"_sel));
]=] [=[
    auto config = objc::autoreleased(
        objc::msg_send<id>("WKWebViewConfiguration"_cls, "new"_sel));

    // WebKit takes no folder for the data of a page, so the data directory names a store of its own.
    if (!data_directory().empty()) {
      if (__builtin_available(macOS 14.0, *)) {
        unsigned char bytes[16]{};
        const std::string &directory = data_directory();
        for (std::size_t index = 0; index < directory.size(); ++index) {
          bytes[index % 16] = static_cast<unsigned char>(
              bytes[index % 16] * 31U +
              static_cast<unsigned char>(directory[index]) + index / 16U);
        }
        auto identifier = objc::autoreleased(objc::msg_send<id>(
            objc::msg_send<id>("NSUUID"_cls, "alloc"_sel),
            "initWithUUIDBytes:"_sel, bytes));
        objc::msg_send<void>(
            config, "setWebsiteDataStore:"_sel,
            objc::msg_send<id>("WKWebsiteDataStore"_cls,
                               "dataStoreForIdentifier:"_sel, identifier));
      }
    }
]=])
