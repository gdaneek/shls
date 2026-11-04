#include <drogon/drogon.h>

#include <functional>

int main() {
  drogon::app().registerHandler(
      "/hello",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        Json::Value body;
        body["message"] = "hello-world";
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
      },
      {drogon::Get});

  drogon::app()
      .addListener("0.0.0.0", 8000)
      .setThreadNum(1)
      .setClientMaxBodySize(1024)
      // Drogon creates its upload directories at startup; the container
      // root filesystem is read-only, so keep them on the /tmp tmpfs.
      .setUploadPath("/tmp/game-api-uploads")
      .run();
  return 0;
}
